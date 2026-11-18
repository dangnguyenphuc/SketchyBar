#pragma once
#include "misc/helpers.h"

// Bluetooth for the island's Bluetooth tab: power, paired devices, connect
// and disconnect. (Wi-Fi lives in the System tab, see system_info.h.)
//
// Only active between connections_start() and connections_stop(), i.e. while
// the Bluetooth tab is visible in an open island: then the state is refreshed
// every CONNECTIONS_REFRESH seconds. Nothing runs otherwise.
//
// Power is switched with the private IOBluetoothPreferenceSetControllerPowerState
// (what blueutil uses). macOS asks once for Bluetooth access.
//
// Battery levels come from where macOS already keeps them, so only devices
// that report one have it: the HID battery level in the IORegistry (Apple
// keyboards/mice/trackpads and other HID devices reporting it) and the levels
// system_profiler lists (Bluetooth LE Battery Service, AirPods). Read off the
// main thread while the tab shows: on start, when devices (dis)connect and
// every CONNECTIONS_BATTERY seconds.

#define CONNECTIONS_MAX_ITEMS  32
#define CONNECTIONS_NAME       64
#define CONNECTIONS_REFRESH    3.0
#define CONNECTIONS_BATTERY    30.0
// How long a connect result stays visible before the notice goes away.
#define CONNECTIONS_RESULT     2.0

struct bluetooth_device {
  char name[CONNECTIONS_NAME];
  char address[24];
  bool connected;
  int battery;  // percent, -1 if the device doesn't report one
};

enum connection_status {
  CONNECTION_IDLE,
  CONNECTION_CONNECTING,
  CONNECTION_SUCCEEDED,
  CONNECTION_FAILED
};

struct connections {
  bool bluetooth_available;
  bool bluetooth_on;
  int bluetooth_count;
  struct bluetooth_device bluetooth[CONNECTIONS_MAX_ITEMS];

  // The last connect attempt, shown as a notice until it is cleared
  // (CONNECTIONS_RESULT seconds after the result).
  enum connection_status status;
  bool status_disconnect;  // the attempt was a disconnect
  char status_target[CONNECTIONS_NAME];
};

// Called on the main thread whenever the state above changed.
typedef void connections_callback(void);

void connections_start(connections_callback* on_change);
void connections_stop(void);
const struct connections* connections_get(void);

void connections_set_bluetooth_power(bool on);
// Connects a paired device, or disconnects it if it is connected.
void connections_toggle_bluetooth(const char* address);
