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
  char hardware[64]; // "Apple M3 Pro · 18 GB"

  bool online;       // a usable route to the internet exists
  enum system_network network;
  // Wi-Fi name if macOS reveals it (needs Location Services access for
  // SketchyBar), otherwise the connection type.
  char network_name[64];
};

// Called on the main thread whenever the network state changed.
typedef void system_info_callback(void);

void system_info_start(system_info_callback* on_change);
void system_info_stop(void);
const struct system_info* system_info_get(void);
