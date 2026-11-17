#include "system_info.h"
#import <Foundation/Foundation.h>
#import <CoreWLAN/CoreWLAN.h>
#include <Network/Network.h>
#include <sys/sysctl.h>

static struct system_info g_info;
static system_info_callback* g_callback = NULL;
static nw_path_monitor_t g_monitor = NULL;

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

  if (chip[0] && gigabytes > 0) {
    snprintf(g_info.hardware, sizeof(g_info.hardware), "%s · %d GB",
             chip, gigabytes);
  } else if (chip[0]) {
    snprintf(g_info.hardware, sizeof(g_info.hardware), "%s", chip);
  } else {
    g_info.hardware[0] = '\0';
  }
}

// Main thread (the monitor delivers on the main queue).
static void update_network(nw_path_t path) {
  struct system_info info = g_info;
  info.online = nw_path_get_status(path) == nw_path_status_satisfied;

  if (!info.online) info.network = SYSTEM_NETWORK_NONE;
  else if (nw_path_uses_interface_type(path, nw_interface_type_wifi))
    info.network = SYSTEM_NETWORK_WIFI;
  else if (nw_path_uses_interface_type(path, nw_interface_type_wired))
    info.network = SYSTEM_NETWORK_ETHERNET;
  else info.network = SYSTEM_NETWORK_OTHER;

  switch (info.network) {
    case SYSTEM_NETWORK_WIFI: {
      @autoreleasepool {
        // nil unless SketchyBar may use Location Services (macOS 14+).
        NSString* ssid = [[[CWWiFiClient sharedWiFiClient] interface] ssid];
        snprintf(info.network_name, sizeof(info.network_name), "%s",
                 ssid.length > 0 ? [ssid UTF8String] : "Wi-Fi");
      }
      break;
    }
    case SYSTEM_NETWORK_ETHERNET:
      snprintf(info.network_name, sizeof(info.network_name), "Ethernet");
      break;
    case SYSTEM_NETWORK_OTHER:
      snprintf(info.network_name, sizeof(info.network_name), "Connected");
      break;
    case SYSTEM_NETWORK_NONE:
      snprintf(info.network_name, sizeof(info.network_name), "Offline");
      break;
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
  nw_path_monitor_set_update_handler(g_monitor, ^(nw_path_t path) {
    if (g_callback) update_network(path);
  });
  nw_path_monitor_start(g_monitor);
}

void system_info_stop(void) {
  if (!g_callback) return;
  g_callback = NULL;

  if (g_monitor) {
    nw_path_monitor_cancel(g_monitor);
    nw_release(g_monitor);
    g_monitor = NULL;
  }
}

const struct system_info* system_info_get(void) {
  return &g_info;
}
