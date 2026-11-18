#include "connections.h"
#import <AppKit/AppKit.h>
#import <IOBluetooth/IOBluetooth.h>
#include <IOKit/IOKitLib.h>

// Private IOBluetooth functions (used by blueutil as well).
extern int IOBluetoothPreferenceGetControllerPowerState(void);
extern void IOBluetoothPreferenceSetControllerPowerState(int state);

// A connect that hasn't reported back after this long failed.
#define BLUETOOTH_TIMEOUT 15.0

// Main thread only.
static struct connections g_state;
static connections_callback* g_callback = NULL;
// Bumped on start/stop: results of work from an older session are dropped.
static uint64_t g_generation = 0;
// Bumped per connect attempt: only the latest one may clear the notice.
static uint64_t g_attempt = 0;
static dispatch_source_t g_refresh_timer = NULL;

// Battery levels by address (normalized: lowercase hex digits only).
#define BATTERY_SLOTS CONNECTIONS_MAX_ITEMS
struct battery_list {
  int count;
  struct { char address[16]; int percent; } entries[BATTERY_SLOTS];
};
static struct battery_list g_batteries;
static dispatch_queue_t g_queue = NULL;
static dispatch_source_t g_battery_timer = NULL;
static bool g_battery_running = false;
static int g_connected_count = -1;

// The connect waiting for its -connectionComplete:status:.
static uint64_t g_bluetooth_attempt = 0;
static char g_bluetooth_address[24];

static id g_delegate = nil;

static void notify() {
  if (g_callback) g_callback();
}

static void copy_string(char* destination, size_t size, NSString* string) {
  snprintf(destination, size, "%s", string ? [string UTF8String] : "");
}

// "B4:EE:25:ED:08:88", "b4-ee-25-ed-08-88" -> "b4ee25ed0888".
static void normalize_address(const char* address, char* out, size_t size) {
  size_t length = 0;
  for (const char* c = address; c && *c && length + 1 < size; c++) {
    if (isxdigit((unsigned char)*c)) out[length++] = tolower(*c);
  }
  out[length] = '\0';
}

static int battery_for(const char* address) {
  char key[16];
  normalize_address(address, key, sizeof(key));
  for (int i = 0; i < g_batteries.count; i++) {
    if (strcmp(g_batteries.entries[i].address, key) == 0)
      return g_batteries.entries[i].percent;
  }
  return -1;
}

static void battery_list_add(struct battery_list* list,
                             const char* address,
                             int percent              ) {
  if (percent < 0 || percent > 100 || list->count >= BATTERY_SLOTS) return;
  char key[16];
  normalize_address(address, key, sizeof(key));
  if (!key[0]) return;
  for (int i = 0; i < list->count; i++) {
    if (strcmp(list->entries[i].address, key) == 0) return;
  }
  snprintf(list->entries[list->count].address, 16, "%s", key);
  list->entries[list->count].percent = percent;
  list->count++;
}

// Background queue. HID devices that report a battery level.
static void read_registry_batteries(struct battery_list* list) {
  io_iterator_t iterator = IO_OBJECT_NULL;
  // MACH_PORT_NULL: the default main port (kIOMainPortDefault is 12.0+).
  if (IOServiceGetMatchingServices(MACH_PORT_NULL,
        IOServiceMatching("AppleDeviceManagementHIDEventService"),
        &iterator) != KERN_SUCCESS) {
    return;
  }

  io_object_t service;
  while ((service = IOIteratorNext(iterator))) {
    CFTypeRef percent = IORegistryEntryCreateCFProperty(service,
                          CFSTR("BatteryPercent"), NULL, 0);
    CFTypeRef address = IORegistryEntryCreateCFProperty(service,
                          CFSTR("DeviceAddress"), NULL, 0);
    if (percent && address
        && CFGetTypeID(percent) == CFNumberGetTypeID()
        && CFGetTypeID(address) == CFStringGetTypeID()) {
      int value = -1;
      char text[32];
      CFNumberGetValue(percent, kCFNumberIntType, &value);
      if (CFStringGetCString(address, text, sizeof(text),
                             kCFStringEncodingUTF8      )) {
        battery_list_add(list, text, value);
      }
    }
    if (percent) CFRelease(percent);
    if (address) CFRelease(address);
    IOObjectRelease(service);
  }
  IOObjectRelease(iterator);
}

// Background queue. The levels system_profiler lists for connected devices
// (device_batteryLevelMain, or Left/Right for earbuds: the lower one).
static void read_profiler_batteries(struct battery_list* list) {
  NSTask* task = [[NSTask alloc] init];
  task.executableURL = [NSURL fileURLWithPath:@"/usr/sbin/system_profiler"];
  task.arguments = @[ @"SPBluetoothDataType", @"-json" ];
  NSPipe* pipe = [NSPipe pipe];
  task.standardOutput = pipe;
  task.standardError = [NSFileHandle fileHandleWithNullDevice];

  NSData* data = nil;
  if ([task launchAndReturnError:NULL]) {
    data = [[pipe fileHandleForReading] readDataToEndOfFile];
    [task waitUntilExit];
  }
  [task release];
  if (!data) return;

  id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
  NSArray* root = [json isKindOfClass:[NSDictionary class]]
                  ? json[@"SPBluetoothDataType"] : nil;
  if (![root isKindOfClass:[NSArray class]]) return;

  for (NSDictionary* controller in root) {
    if (![controller isKindOfClass:[NSDictionary class]]) continue;
    NSArray* connected = controller[@"device_connected"];
    if (![connected isKindOfClass:[NSArray class]]) continue;

    for (NSDictionary* entry in connected) {
      if (![entry isKindOfClass:[NSDictionary class]]) continue;
      for (NSDictionary* device in entry.allValues) {
        if (![device isKindOfClass:[NSDictionary class]]) continue;
        NSString* address = device[@"device_address"];
        if (![address isKindOfClass:[NSString class]]) continue;

        int main = -1, lowest = -1;
        for (NSString* key in device) {
          if (![key hasPrefix:@"device_batteryLevel"]) continue;
          id value = device[key];
          if (![value isKindOfClass:[NSString class]]) continue;
          int percent = [(NSString*)value intValue];  // "85%" -> 85
          if ([key isEqualToString:@"device_batteryLevelMain"]) main = percent;
          else if (![key isEqualToString:@"device_batteryLevelCase"]
                   && (lowest < 0 || percent < lowest)) {
            lowest = percent;
          }
        }
        battery_list_add(list, [address UTF8String],
                         main >= 0 ? main : lowest );
      }
    }
  }
}

static void apply_batteries(void);

static void read_batteries() {
  if (g_battery_running || !g_state.bluetooth_on) return;
  g_battery_running = true;
  uint64_t generation = g_generation;

  dispatch_async(g_queue, ^{
    struct battery_list list;
    memset(&list, 0, sizeof(list));
    @autoreleasepool {
      read_registry_batteries(&list);
      read_profiler_batteries(&list);
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      g_battery_running = false;
      if (generation != g_generation) return;
      g_batteries = list;
      apply_batteries();
    });
  });
}

// Main thread: cheap reads only.
static void refresh_state() {
  struct connections before = g_state;

  @autoreleasepool {
    g_state.bluetooth_available = [IOBluetoothHostController defaultController]
                                  != nil;
    g_state.bluetooth_on = g_state.bluetooth_available
                           && IOBluetoothPreferenceGetControllerPowerState();
    g_state.bluetooth_count = 0;
    if (g_state.bluetooth_on) {
      for (IOBluetoothDevice* device in [IOBluetoothDevice pairedDevices]) {
        if (g_state.bluetooth_count >= CONNECTIONS_MAX_ITEMS) break;
        struct bluetooth_device* entry
          = &g_state.bluetooth[g_state.bluetooth_count++];
        copy_string(entry->name, sizeof(entry->name),
                    device.name ?: device.addressString);
        copy_string(entry->address, sizeof(entry->address),
                    device.addressString                );
        entry->connected = device.isConnected;
        entry->battery = entry->connected ? battery_for(entry->address) : -1;
      }
    }
  }

  // A device (dis)connected: its battery may be known now.
  int connected = 0;
  for (int i = 0; i < g_state.bluetooth_count; i++) {
    if (g_state.bluetooth[i].connected) connected++;
  }
  if (connected != g_connected_count) {
    g_connected_count = connected;
    read_batteries();
  }

  if (memcmp(&before, &g_state, sizeof(g_state)) != 0) notify();
}

static void apply_batteries(void) {
  refresh_state();
}

// ------------------------------------------------------------ results

static void finish_attempt(uint64_t attempt, bool succeeded) {
  if (attempt != g_attempt) return;
  g_state.status = succeeded ? CONNECTION_SUCCEEDED : CONNECTION_FAILED;
  refresh_state();
  notify();

  uint64_t generation = g_generation;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                               CONNECTIONS_RESULT * NSEC_PER_SEC),
                 dispatch_get_main_queue(),
                 ^{
    if (generation != g_generation || attempt != g_attempt) return;
    g_state.status = CONNECTION_IDLE;
    notify();
  });
}

static uint64_t begin_attempt(const char* target, bool disconnect) {
  g_state.status = CONNECTION_CONNECTING;
  g_state.status_disconnect = disconnect;
  snprintf(g_state.status_target, sizeof(g_state.status_target), "%s",
           target                                                     );
  notify();
  return ++g_attempt;
}

@interface connections_delegate : NSObject
@end

@implementation connections_delegate
// IOBluetoothDevice -openConnection: result.
- (void)connectionComplete:(IOBluetoothDevice*)device status:(IOReturn)status {
  if (!g_bluetooth_attempt
      || strcasecmp([device.addressString UTF8String],
                    g_bluetooth_address              ) != 0) {
    return;
  }
  uint64_t attempt = g_bluetooth_attempt;
  g_bluetooth_attempt = 0;
  finish_attempt(attempt, status == kIOReturnSuccess);
}
@end

// ------------------------------------------------------------ public

void connections_start(connections_callback* on_change) {
  if (g_callback) return;
  if (!g_delegate) g_delegate = [[connections_delegate alloc] init];
  if (!g_queue) {
    g_queue = dispatch_queue_create("sketchybar.bluetooth",
                                    DISPATCH_QUEUE_SERIAL  );
  }

  g_callback = on_change;
  g_generation++;
  g_state.status = CONNECTION_IDLE;
  g_connected_count = -1;  // reads the batteries right away
  refresh_state();

  g_battery_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                           dispatch_get_main_queue()       );
  dispatch_source_set_timer(g_battery_timer,
                            dispatch_time(DISPATCH_TIME_NOW,
                                          CONNECTIONS_BATTERY * NSEC_PER_SEC),
                            CONNECTIONS_BATTERY * NSEC_PER_SEC,
                            5 * NSEC_PER_SEC                                 );
  dispatch_source_set_event_handler(g_battery_timer, ^{ read_batteries(); });
  dispatch_resume(g_battery_timer);

  g_refresh_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                           dispatch_get_main_queue()       );
  dispatch_source_set_timer(g_refresh_timer,
                            dispatch_time(DISPATCH_TIME_NOW,
                                          CONNECTIONS_REFRESH * NSEC_PER_SEC),
                            CONNECTIONS_REFRESH * NSEC_PER_SEC,
                            0.5 * NSEC_PER_SEC                               );
  dispatch_source_set_event_handler(g_refresh_timer, ^{ refresh_state(); });
  dispatch_resume(g_refresh_timer);
}

void connections_stop(void) {
  if (!g_callback) return;
  g_callback = NULL;
  g_generation++;

  dispatch_source_cancel(g_refresh_timer);
  dispatch_release(g_refresh_timer);
  g_refresh_timer = NULL;
  dispatch_source_cancel(g_battery_timer);
  dispatch_release(g_battery_timer);
  g_battery_timer = NULL;
  g_battery_running = false;
  g_state.status = CONNECTION_IDLE;
}

const struct connections* connections_get(void) {
  return &g_state;
}

void connections_set_bluetooth_power(bool on) {
  if (!g_callback) return;
  IOBluetoothPreferenceSetControllerPowerState(on ? 1 : 0);

  // The controller takes a moment to switch.
  uint64_t generation = g_generation;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1.5 * NSEC_PER_SEC),
                 dispatch_get_main_queue(),
                 ^{ if (generation == g_generation) refresh_state(); });
}

void connections_toggle_bluetooth(const char* address) {
  if (!g_callback || !address) return;

  @autoreleasepool {
    IOBluetoothDevice* device
      = [IOBluetoothDevice deviceWithAddressString:@(address)];
    if (!device) return;

    const char* name = device.name ? [device.name UTF8String] : address;
    if (device.isConnected) {
      uint64_t attempt = begin_attempt(name, true);
      finish_attempt(attempt, [device closeConnection] == kIOReturnSuccess);
      return;
    }

    uint64_t attempt = begin_attempt(name, false);
    g_bluetooth_attempt = attempt;
    snprintf(g_bluetooth_address, sizeof(g_bluetooth_address), "%s",
             [device.addressString UTF8String]                       );
    if ([device openConnection:g_delegate] != kIOReturnSuccess) {
      g_bluetooth_attempt = 0;
      finish_attempt(attempt, false);
      return;
    }

    uint64_t generation = g_generation;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 BLUETOOTH_TIMEOUT * NSEC_PER_SEC),
                   dispatch_get_main_queue(),
                   ^{
      if (generation == g_generation
          && attempt == g_attempt
          && g_state.status == CONNECTION_CONNECTING) {
        finish_attempt(attempt, false);
      }
    });
  }
}
