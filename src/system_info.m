#include "system_info.h"
#import <Foundation/Foundation.h>
#import <CoreWLAN/CoreWLAN.h>
#import <SystemConfiguration/SystemConfiguration.h>
#include <Network/Network.h>
#include <sys/sysctl.h>

static struct system_info g_info;
static system_info_callback* g_callback = NULL;
static nw_path_monitor_t g_monitor = NULL;
static nw_path_t g_path = NULL;
static uint64_t g_generation = 0;

static const char* macos_name(int major) {
  switch (major) {
    case 26: return "Tahoe";
    case 15: return "Sequoia";
    case 14: return "Sonoma";
    case 13: return "Ventura";
    case 12: return "Monterey";
    case 11: return "Big Sur";
    default: return NULL;
  }
}

static void read_mac_info() {
  NSOperatingSystemVersion version
    = [[NSProcessInfo processInfo] operatingSystemVersion];
  const char* name = macos_name((int)version.majorVersion);

  char number[24];
  if (version.patchVersion > 0) {
    snprintf(number, sizeof(number), "%ld.%ld.%ld",
             (long)version.majorVersion,
             (long)version.minorVersion,
             (long)version.patchVersion);
  } else {
    snprintf(number, sizeof(number), "%ld.%ld",
             (long)version.majorVersion,
             (long)version.minorVersion);
  }
  snprintf(g_info.os, sizeof(g_info.os), "macOS %s%s%s",
           name ? name : "", name ? " " : "", number);

  char chip[48] = "";
  size_t length = sizeof(chip);
  if (sysctlbyname("machdep.cpu.brand_string", chip, &length, NULL, 0) != 0)
    chip[0] = '\0';

  uint64_t memory = 0;
  length = sizeof(memory);
  sysctlbyname("hw.memsize", &memory, &length, NULL, 0);
  int gigabytes = (int)round(memory / (1024.0 * 1024.0 * 1024.0));

  // "Apple M4" -> "M4": the info card's first line is short.
  if (strncmp(chip, "Apple ", 6) == 0) memmove(chip, chip + 6, strlen(chip) - 5);

  if (chip[0] && gigabytes > 0) {
    snprintf(g_info.hardware, sizeof(g_info.hardware), "%s · %d GB",
             chip, gigabytes);
  } else if (chip[0]) {
    snprintf(g_info.hardware, sizeof(g_info.hardware), "%s", chip);
  } else {
    g_info.hardware[0] = '\0';
  }
}

// The network the Mac is on. Without Location access CoreWLAN hides it (nil)
// and configd's SSID_STR is empty, but configd's cached scan record of the
// associated network still carries the name.
static NSString* current_ssid(CWInterface* wifi) {
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

// Main thread (the monitor delivers on the main queue).
static void update_network(nw_path_t path) {
  struct system_info info = g_info;
  info.online = nw_path_get_status(path) == nw_path_status_satisfied;

  @autoreleasepool {
    CWInterface* wifi = [[CWWiFiClient sharedWiFiClient] interface];
    info.wifi_available = wifi != nil;
    info.wifi_on = wifi && wifi.powerOn;
    NSString* ssid = info.wifi_on ? current_ssid(wifi) : nil;
    snprintf(info.wifi_ssid, sizeof(info.wifi_ssid), "%s",
             ssid ? [ssid UTF8String] : "");
  }

  if (!info.online) info.network = SYSTEM_NETWORK_NONE;
  else if (nw_path_uses_interface_type(path, nw_interface_type_wifi))
    info.network = SYSTEM_NETWORK_WIFI;
  else if (nw_path_uses_interface_type(path, nw_interface_type_wired))
    info.network = SYSTEM_NETWORK_ETHERNET;
  else info.network = SYSTEM_NETWORK_OTHER;

  if (info.wifi_ssid[0]) {
    snprintf(info.network_name, sizeof(info.network_name), "%s",
             info.wifi_ssid                                     );
  } else if (info.network == SYSTEM_NETWORK_ETHERNET) {
    snprintf(info.network_name, sizeof(info.network_name), "Ethernet");
  } else if (info.wifi_available && !info.wifi_on) {
    snprintf(info.network_name, sizeof(info.network_name), "Wi-Fi off");
  } else if (info.network == SYSTEM_NETWORK_OTHER) {
    snprintf(info.network_name, sizeof(info.network_name), "Connected");
  } else if (info.network == SYSTEM_NETWORK_WIFI) {
    snprintf(info.network_name, sizeof(info.network_name), "Wi-Fi");
  } else {
    snprintf(info.network_name, sizeof(info.network_name), "Not connected");
  }

  if (memcmp(&info, &g_info, sizeof(info)) == 0) return;
  g_info = info;
  if (g_callback) g_callback();
}

void system_info_start(system_info_callback* on_change) {
  if (g_callback) return;
  g_callback = on_change;

  memset(&g_info, 0, sizeof(g_info));
  read_mac_info();
  snprintf(g_info.network_name, sizeof(g_info.network_name), "Checking…");

  g_monitor = nw_path_monitor_create();
  nw_path_monitor_set_queue(g_monitor, dispatch_get_main_queue());
  g_generation++;
  nw_path_monitor_set_update_handler(g_monitor, ^(nw_path_t path) {
    if (!g_callback) return;
    // Kept to re-read Wi-Fi after switching it (the path may not change).
    if (g_path) nw_release(g_path);
    g_path = nw_retain(path);
    update_network(path);
  });
  nw_path_monitor_start(g_monitor);
}

void system_info_stop(void) {
  if (!g_callback) return;
  g_callback = NULL;

  g_generation++;
  if (g_monitor) {
    nw_path_monitor_cancel(g_monitor);
    nw_release(g_monitor);
    g_monitor = NULL;
  }
  if (g_path) {
    nw_release(g_path);
    g_path = NULL;
  }
}

void system_info_set_wifi_power(bool on) {
  if (!g_callback) return;
  NSString* interface = [[[[CWWiFiClient sharedWiFiClient] interface]
                                                interfaceName] copy];
  if (!interface) return;

  uint64_t generation = g_generation;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    @autoreleasepool {
      NSTask* task = [[NSTask alloc] init];
      task.executableURL = [NSURL fileURLWithPath:@"/usr/sbin/networksetup"];
      task.arguments = @[ @"-setairportpower", interface,
                          on ? @"on" : @"off"           ];
      task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
      task.standardError = [NSFileHandle fileHandleWithNullDevice];
      if ([task launchAndReturnError:NULL]) [task waitUntilExit];
      [task release];
    }
    [interface release];

    // Re-read once the radio settled; the path monitor reports the
    // connection itself when it changes.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1.0 * NSEC_PER_SEC),
                   dispatch_get_main_queue(),
                   ^{
      if (generation == g_generation && g_callback && g_path)
        update_network(g_path);
    });
  });
}

const struct system_info* system_info_get(void) {
  return &g_info;
}
