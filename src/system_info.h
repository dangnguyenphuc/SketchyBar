#pragma once
#include "misc/helpers.h"

// Mac and network info for the island's System tab. The Mac info is read once
// per open; the network is watched (no polling) between system_info_start()
// and system_info_stop() only.

enum system_network {
  SYSTEM_NETWORK_NONE,
  SYSTEM_NETWORK_WIFI,
  SYSTEM_NETWORK_ETHERNET,
  SYSTEM_NETWORK_OTHER
};

struct system_info {
  char os[64];       // "macOS Tahoe 26.0.1"
  char hardware[64]; // "M3 Pro · 18 GB"

  bool online;       // a usable route to the internet exists
  enum system_network network;
  // What the info card shows for the connection: the Wi-Fi network's name,
  // "Ethernet", "Not connected", "Wi-Fi off", ...
  char network_name[64];

  // Wi-Fi (the interface, not necessarily the route in use).
  bool wifi_available;
  bool wifi_on;
  char wifi_ssid[64];  // the joined network, "" if none
};

// Called on the main thread whenever the network state changed.
typedef void system_info_callback(void);

void system_info_start(system_info_callback* on_change);
void system_info_stop(void);
const struct system_info* system_info_get(void);
// Turns Wi-Fi on or off (networksetup, off the main thread).
void system_info_set_wifi_power(bool on);
