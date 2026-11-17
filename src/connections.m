#include "connections.h"
#import <AppKit/AppKit.h>
#import <CoreWLAN/CoreWLAN.h>
#import <IOBluetooth/IOBluetooth.h>
#import <SystemConfiguration/SystemConfiguration.h>

// Private IOBluetooth functions (used by blueutil as well).
extern int IOBluetoothPreferenceGetControllerPowerState(void);
extern void IOBluetoothPreferenceSetControllerPowerState(int state);

#define NETWORKSETUP "/usr/sbin/networksetup"
// A Bluetooth connect that hasn't reported back after this long failed.
#define BLUETOOTH_TIMEOUT 15.0
#define PASSWORD_SLOTS    8
#define PASSWORD_LENGTH   128

// ------------------------------------------------------------ state

// Main thread only.
static struct connections g_state;
static connections_callback* g_callback = NULL;
// Bumped on start/stop: results of work from an older session are dropped.
static uint64_t g_generation = 0;
// Bumped per connect attempt: only the latest one may clear the notice.
static uint64_t g_attempt = 0;

static dispatch_queue_t g_queue = NULL;
static dispatch_source_t g_refresh_timer = NULL;
static dispatch_source_t g_scan_timer = NULL;
static bool g_scan_running = false;

// Last scan (only kept when macOS showed the names) and the saved networks.
static struct wifi_network g_scanned[CONNECTIONS_MAX_ITEMS];
static int g_scanned_count = 0;
static bool g_scan_had_names = false;
static bool g_scan_done = false;
static char g_saved[CONNECTIONS_MAX_ITEMS * 4][CONNECTIONS_NAME];
static int g_saved_count = 0;

// Passwords typed for networks that then failed to join, kept in memory to
// pre-fill the prompt on the next attempt.
static struct {
  char ssid[CONNECTIONS_NAME];
  char password[PASSWORD_LENGTH];
} g_passwords[PASSWORD_SLOTS];
static int g_password_next = 0;

// The Bluetooth connect waiting for its -connectionComplete:status:.
static uint64_t g_bluetooth_attempt = 0;
static char g_bluetooth_address[24];

static id g_delegate = nil;

static void notify() {
  if (g_callback) g_callback();
}

static void copy_string(char* destination, size_t size, NSString* string) {
  snprintf(destination, size, "%s", string ? [string UTF8String] : "");
}

static CWInterface* wifi_interface() {
  return [[CWWiFiClient sharedWiFiClient] interface];
}

// The network the Mac is on. Without Location access CoreWLAN hides it (nil)
// and configd's SSID_STR is empty, but configd's cached scan record of the
// associated network still carries the name.
static NSString* current_ssid() {
  CWInterface* wifi = wifi_interface();
  if (!wifi || !wifi.powerOn) return nil;
  if (wifi.ssid.length > 0) return wifi.ssid;

  NSString* ssid = nil;
  SCDynamicStoreRef store = SCDynamicStoreCreate(NULL, CFSTR("sketchybar"),
                                                 NULL, NULL               );
  if (!store) return nil;

  NSString* key = [NSString stringWithFormat:
                     @"State:/Network/Interface/%@/AirPort",
                     wifi.interfaceName                    ];
  NSDictionary* state = (NSDictionary*)SCDynamicStoreCopyValue(store,
                                                      (CFStringRef)key);
  if ([state isKindOfClass:[NSDictionary class]]) {
    ssid = state[@"SSID_STR"];
    NSData* record = state[@"CachedScanRecord"];
    if (ssid.length == 0 && [record isKindOfClass:[NSData class]]) {
      NSKeyedUnarchiver* unarchiver
        = [[NSKeyedUnarchiver alloc] initForReadingFromData:record error:NULL];
      unarchiver.requiresSecureCoding = NO;
      id root = [unarchiver decodeObjectForKey:NSKeyedArchiveRootObjectKey];
      [unarchiver finishDecoding];
      ssid = [root isKindOfClass:[NSDictionary class]] ? root[@"SSID_STR"]
                                                       : nil;
      [unarchiver release];
    }
    if (![ssid isKindOfClass:[NSString class]]) ssid = nil;
    [[ssid retain] autorelease];
  }
  if (state) CFRelease((CFDictionaryRef)state);
  CFRelease(store);
  return ssid.length > 0 ? ssid : nil;
}

// Runs a tool and returns its trimmed output (stdout + stderr), nil if it
// couldn't be started.
static NSString* run_tool(const char* path, NSArray* arguments) {
  NSTask* task = [[NSTask alloc] init];
  task.executableURL = [NSURL fileURLWithPath:@(path)];
  task.arguments = arguments;
  NSPipe* pipe = [NSPipe pipe];
  task.standardOutput = pipe;
  task.standardError = pipe;

  NSString* output = nil;
  if ([task launchAndReturnError:NULL]) {
    NSData* data = [[pipe fileHandleForReading] readDataToEndOfFile];
    [task waitUntilExit];
    output = [[[[NSString alloc] initWithData:data
                                     encoding:NSUTF8StringEncoding]
                                                        autorelease]
              stringByTrimmingCharactersInSet:
                [NSCharacterSet whitespaceAndNewlineCharacterSet]];
  }
  [task release];
  return output;
}

static int signal_bars(NSInteger rssi) {
  if (rssi >= -60) return 3;
  if (rssi >= -70) return 2;
  if (rssi >= -80) return 1;
  return 0;
}

// ------------------------------------------------------------ passwords

static int password_slot(const char* ssid) {
  for (int i = 0; i < PASSWORD_SLOTS; i++) {
    if (g_passwords[i].ssid[0] && strcmp(g_passwords[i].ssid, ssid) == 0)
      return i;
  }
  return -1;
}

static void remember_failure(const char* ssid, const char* password) {
  int slot = password_slot(ssid);
  if (slot < 0) {
    slot = g_password_next;
    g_password_next = (g_password_next + 1) % PASSWORD_SLOTS;
  }
  snprintf(g_passwords[slot].ssid, CONNECTIONS_NAME, "%s", ssid);
  snprintf(g_passwords[slot].password, PASSWORD_LENGTH, "%s",
           password ? password : "");
}

static void forget_password(const char* ssid) {
  int slot = password_slot(ssid);
  if (slot >= 0) memset(&g_passwords[slot], 0, sizeof(g_passwords[slot]));
}

const char* connections_wifi_password(const char* ssid) {
  int slot = password_slot(ssid);
  return slot >= 0 && g_passwords[slot].password[0]
         ? g_passwords[slot].password
         : NULL;
}

// Saved networks always join directly (macOS has their password); only new
// secured ones ask.
bool connections_wifi_needs_password(const struct wifi_network* network) {
  return network->secure && !network->known;
}

// ------------------------------------------------------------ wifi list

static bool is_saved(const char* ssid) {
  for (int i = 0; i < g_saved_count; i++) {
    if (strcmp(g_saved[i], ssid) == 0) return true;
  }
  return false;
}

static int compare_networks(const void* a, const void* b) {
  const struct wifi_network* x = a;
  const struct wifi_network* y = b;
  if (x->current != y->current) return x->current ? -1 : 1;
  if (x->known != y->known) return x->known ? -1 : 1;
  if (x->signal != y->signal) return y->signal - x->signal;
  return strcasecmp(x->ssid, y->ssid);
}

// Main thread. Nearby networks if macOS shows their names, otherwise the
// saved ones.
static void rebuild_wifi_list(NSString* current) {
  const char* current_name = current ? [current UTF8String] : "";
  g_state.wifi_names_hidden = g_scan_done && !g_scan_had_names;
  g_state.wifi_count = 0;

  if (g_scan_had_names) {
    for (int i = 0; i < g_scanned_count; i++) {
      struct wifi_network network = g_scanned[i];
      network.known = is_saved(network.ssid);
      network.current = strcmp(network.ssid, current_name) == 0;
      g_state.wifi[g_state.wifi_count++] = network;
    }
  } else if (current_name[0]) {
    // Nearby names are hidden: of the networks in range only the current
    // one is known by name, so that is all the list can offer.
    struct wifi_network network = { .signal = -1,
                                    .secure = true,
                                    .known = true,
                                    .current = true };
    snprintf(network.ssid, sizeof(network.ssid), "%s", current_name);
    g_state.wifi[g_state.wifi_count++] = network;
  }

  if (g_scan_had_names) {
    qsort(g_state.wifi, g_state.wifi_count, sizeof(struct wifi_network),
          compare_networks                                          );
  }

  // Being on a network proves its password works: drop a remembered one.
  if (current_name[0]) forget_password(current_name);
}

// ------------------------------------------------------------ refresh

// Main thread: cheap reads only.
static void refresh_state() {
  struct connections before = g_state;

  @autoreleasepool {
    CWInterface* wifi = wifi_interface();
    g_state.wifi_available = wifi != nil;
    g_state.wifi_on = wifi && wifi.powerOn;
    rebuild_wifi_list(g_state.wifi_on ? current_ssid() : nil);

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
      }
    }
  }

  if (memcmp(&before, &g_state, sizeof(g_state)) != 0) notify();
}

static void read_saved_networks() {
  uint64_t generation = g_generation;
  NSString* interface = [wifi_interface().interfaceName copy];
  if (!interface) return;

  dispatch_async(g_queue, ^{
    @autoreleasepool {
      NSString* output = run_tool(NETWORKSETUP,
                                  @[ @"-listpreferredwirelessnetworks",
                                     interface                        ]);
      NSArray* lines = [output componentsSeparatedByString:@"\n"];

      // Copied into a plain struct for the trip back to the main thread.
      struct { int count; char names[CONNECTIONS_MAX_ITEMS * 4]
                                     [CONNECTIONS_NAME]; } saved;
      saved.count = 0;
      for (NSUInteger i = 1; i < lines.count; i++) {
        NSString* name = [lines[i] stringByTrimmingCharactersInSet:
                           [NSCharacterSet whitespaceCharacterSet]];
        if (name.length == 0 || saved.count >= CONNECTIONS_MAX_ITEMS * 4)
          continue;
        copy_string(saved.names[saved.count++], CONNECTIONS_NAME, name);
      }

      dispatch_async(dispatch_get_main_queue(), ^{
        if (generation != g_generation) return;
        g_saved_count = saved.count;
        memcpy(g_saved, saved.names, sizeof(saved.names));
        refresh_state();
      });
    }
    [interface release];
  });
}

// One scan's networks, one entry per name (its strongest access point).
// Plain data, so it can travel between threads in a block.
struct scan_list {
  int count;
  bool had_names;
  struct wifi_network networks[CONNECTIONS_MAX_ITEMS];
};

static void scan_list_add(struct scan_list* scan,
                          NSString* name,
                          NSInteger rssi,
                          bool secure      ) {
  if (![name isKindOfClass:[NSString class]] || name.length == 0) return;

  char ssid[CONNECTIONS_NAME];
  copy_string(ssid, sizeof(ssid), name);
  int bars = signal_bars(rssi);
  for (int i = 0; i < scan->count; i++) {
    if (strcmp(scan->networks[i].ssid, ssid) != 0) continue;
    scan->networks[i].signal = max(scan->networks[i].signal, bars);
    return;
  }
  if (scan->count >= CONNECTIONS_MAX_ITEMS) return;

  struct wifi_network* entry = &scan->networks[scan->count++];
  memset(entry, 0, sizeof(*entry));
  snprintf(entry->ssid, sizeof(entry->ssid), "%s", ssid);
  entry->signal = bars;
  entry->secure = secure;
}

// Main thread.
static void apply_scan(const struct scan_list* scan) {
  g_scan_running = false;
  g_state.wifi_scanning = false;
  g_scan_done = true;
  g_scan_had_names = scan->had_names;
  g_scanned_count = scan->count;
  memcpy(g_scanned, scan->networks, sizeof(scan->networks));
  refresh_state();
  notify();
}

static void scan_wifi() {
  if (g_scan_running || !g_state.wifi_on) return;
  g_scan_running = true;
  g_state.wifi_scanning = true;
  notify();
  uint64_t generation = g_generation;

  // Names stay hidden unless macOS grants Location access, which this binary
  // doesn't get: then only the current network is listed (see
  // rebuild_wifi_list).
  dispatch_async(g_queue, ^{
    struct scan_list scan;
    memset(&scan, 0, sizeof(scan));
    @autoreleasepool {
      NSSet* networks = [wifi_interface() scanForNetworksWithName:nil
                                                            error:NULL];
      for (CWNetwork* network in networks) {
        if (network.ssid) scan.had_names = true;
        scan_list_add(&scan, network.ssid, network.rssiValue,
                      ![network supportsSecurity:kCWSecurityNone]);
      }
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      if (generation == g_generation) apply_scan(&scan);
    });
  });
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

static uint64_t begin_attempt(const char* target, bool bluetooth,
                              bool disconnect                     ) {
  g_state.status = CONNECTION_CONNECTING;
  g_state.status_bluetooth = bluetooth;
  g_state.status_disconnect = disconnect;
  snprintf(g_state.status_target, sizeof(g_state.status_target), "%s",
           target                                                     );
  notify();
  return ++g_attempt;
}

// ------------------------------------------------------------ delegates

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
  if (!g_queue) {
    g_queue = dispatch_queue_create("sketchybar.connections",
                                    DISPATCH_QUEUE_SERIAL    );
  }
  if (!g_delegate) g_delegate = [[connections_delegate alloc] init];

  g_callback = on_change;
  g_generation++;
  g_scan_running = false;
  g_state.wifi_scanning = false;
  g_state.status = CONNECTION_IDLE;

  refresh_state();
  read_saved_networks();
  scan_wifi();

  g_refresh_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                           dispatch_get_main_queue()       );
  dispatch_source_set_timer(g_refresh_timer,
                            dispatch_time(DISPATCH_TIME_NOW,
                                          CONNECTIONS_REFRESH * NSEC_PER_SEC),
                            CONNECTIONS_REFRESH * NSEC_PER_SEC,
                            0.5 * NSEC_PER_SEC                               );
  dispatch_source_set_event_handler(g_refresh_timer, ^{ refresh_state(); });
  dispatch_resume(g_refresh_timer);

  g_scan_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                        dispatch_get_main_queue()       );
  dispatch_source_set_timer(g_scan_timer,
                            dispatch_time(DISPATCH_TIME_NOW,
                                          CONNECTIONS_SCAN * NSEC_PER_SEC),
                            CONNECTIONS_SCAN * NSEC_PER_SEC,
                            2 * NSEC_PER_SEC                              );
  dispatch_source_set_event_handler(g_scan_timer, ^{ scan_wifi(); });
  dispatch_resume(g_scan_timer);
}

void connections_stop(void) {
  if (!g_callback) return;
  g_callback = NULL;
  g_generation++;

  dispatch_source_cancel(g_refresh_timer);
  dispatch_release(g_refresh_timer);
  g_refresh_timer = NULL;
  dispatch_source_cancel(g_scan_timer);
  dispatch_release(g_scan_timer);
  g_scan_timer = NULL;

  // Keep the saved networks and the last scan: reopening the tab then shows
  // a list right away while the fresh scan runs.
  g_scan_running = false;
  g_state.wifi_scanning = false;
  g_state.status = CONNECTION_IDLE;
}

const struct connections* connections_get(void) {
  return &g_state;
}

void connections_set_wifi_power(bool on) {
  if (!g_callback) return;
  NSString* interface = [wifi_interface().interfaceName copy];
  if (!interface) return;

  uint64_t generation = g_generation;
  dispatch_async(g_queue, ^{
    @autoreleasepool {
      run_tool(NETWORKSETUP, @[ @"-setairportpower", interface,
                                on ? @"on" : @"off"           ]);
    }
    [interface release];
    dispatch_async(dispatch_get_main_queue(), ^{
      if (generation != g_generation) return;
      refresh_state();
      if (on) scan_wifi();
    });
  });
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

void connections_join_wifi(const char* ssid_c, const char* password_c) {
  if (!g_callback || !ssid_c || !ssid_c[0]) return;
  NSString* interface = [wifi_interface().interfaceName copy];
  if (!interface) return;

  uint64_t attempt = begin_attempt(ssid_c, false, false);
  uint64_t generation = g_generation;
  NSString* ssid = [@(ssid_c) copy];
  NSString* password = password_c ? [@(password_c) copy] : nil;

  dispatch_async(g_queue, ^{
    bool joined = false;
    @autoreleasepool {
      NSMutableArray* arguments = [NSMutableArray arrayWithObjects:
                                    @"-setairportnetwork", interface, ssid,
                                    nil                                    ];
      if (password) [arguments addObject:password];

      // networksetup exits with 0 either way; it only prints on failure
      // ("Could not find network …", "Failed to join network …").
      NSString* output = run_tool(NETWORKSETUP, arguments);
      joined = output && output.length == 0;

      // Double check that we are on it now.
      if (joined) {
        NSString* current = current_ssid();
        if (current && ![current isEqualToString:ssid]) joined = false;
      }
    }

    char ssid_copy[CONNECTIONS_NAME];
    char password_copy[PASSWORD_LENGTH];
    copy_string(ssid_copy, sizeof(ssid_copy), ssid);
    copy_string(password_copy, sizeof(password_copy), password);
    bool had_password = password != nil;
    [interface release];
    [ssid release];
    [password release];

    // Plain struct: C arrays can't be captured by a block directly.
    struct { char ssid[CONNECTIONS_NAME]; char password[PASSWORD_LENGTH]; }
      result;
    memcpy(result.ssid, ssid_copy, sizeof(result.ssid));
    memcpy(result.password, password_copy, sizeof(result.password));

    dispatch_async(dispatch_get_main_queue(), ^{
      // Remembered even if the tab closed meanwhile.
      if (joined) forget_password(result.ssid);
      else if (had_password) remember_failure(result.ssid, result.password);

      if (generation != g_generation) return;
      if (joined) read_saved_networks();  // it is saved now
      finish_attempt(attempt, joined);
    });
  });
}

void connections_toggle_bluetooth(const char* address) {
  if (!g_callback || !address) return;

  @autoreleasepool {
    IOBluetoothDevice* device
      = [IOBluetoothDevice deviceWithAddressString:@(address)];
    if (!device) return;

    const char* name = device.name ? [device.name UTF8String] : address;
    if (device.isConnected) {
      uint64_t attempt = begin_attempt(name, true, true);
      finish_attempt(attempt, [device closeConnection] == kIOReturnSuccess);
      return;
    }

    uint64_t attempt = begin_attempt(name, true, false);
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
