#pragma once
#include "misc/helpers.h"

// Wi-Fi and Bluetooth for the island's Connections tab.
//
// Only active between connections_start() and connections_stop(), i.e. while
// the Connections tab is visible in an open island: then Bluetooth/Wi-Fi state
// is refreshed every CONNECTIONS_REFRESH seconds and Wi-Fi is scanned every
// CONNECTIONS_SCAN seconds (off the main thread). Nothing runs otherwise.
//
// Wi-Fi names: macOS hides nearby network names unless the scanning app has
// Location access, which a plain binary like sketchybar can't get. While they
// are hidden (`wifi_names_hidden`) the list only shows the current network,
// whose name is read from configd. Joining uses
// `networksetup -setairportnetwork`.
//
// Bluetooth: paired devices only. Power is switched with the private
// IOBluetoothPreferenceSetControllerPowerState (what blueutil uses).

#define CONNECTIONS_MAX_ITEMS  32
#define CONNECTIONS_NAME       64
#define CONNECTIONS_REFRESH    3.0
#define CONNECTIONS_SCAN       15.0
// How long a connect result stays visible before the notice goes away.
#define CONNECTIONS_RESULT     2.0

struct wifi_network {
  char ssid[CONNECTIONS_NAME];
  int signal;     // 0..3 bars, -1 unknown (saved network, not scanned)
  bool secure;
  bool known;     // in the saved (preferred) networks
  bool current;
};

struct bluetooth_device {
  char name[CONNECTIONS_NAME];
  char address[24];
  bool connected;
};

enum connection_status {
  CONNECTION_IDLE,
  CONNECTION_CONNECTING,
  CONNECTION_SUCCEEDED,
  CONNECTION_FAILED
};

struct connections {
  bool wifi_available;
  bool wifi_on;
  bool wifi_names_hidden;
  bool wifi_scanning;
  int wifi_count;
  struct wifi_network wifi[CONNECTIONS_MAX_ITEMS];

  bool bluetooth_available;
  bool bluetooth_on;
  int bluetooth_count;
  struct bluetooth_device bluetooth[CONNECTIONS_MAX_ITEMS];

  // The last connect attempt, shown as a notice until it is cleared
  // (CONNECTIONS_RESULT seconds after the result).
  enum connection_status status;
  bool status_bluetooth;
  bool status_disconnect;  // Bluetooth: the attempt was a disconnect
  char status_target[CONNECTIONS_NAME];
};

// Called on the main thread whenever the state above changed.
typedef void connections_callback(void);

void connections_start(connections_callback* on_change);
void connections_stop(void);
const struct connections* connections_get(void);

void connections_set_wifi_power(bool on);
void connections_set_bluetooth_power(bool on);

// Joins a Wi-Fi network; `password` NULL for open or saved networks. A
// password that didn't work is kept (in memory) for the next attempt.
void connections_join_wifi(const char* ssid, const char* password);
// True if joining `network` should ask for a password first: a secured
// network that isn't saved (saved ones always join directly).
bool connections_wifi_needs_password(const struct wifi_network* network);
// The password last tried for `ssid` (NULL if none).
const char* connections_wifi_password(const char* ssid);

// Connects a paired device, or disconnects it if it is connected.
void connections_toggle_bluetooth(const char* address);
