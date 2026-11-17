#pragma once
#include "misc/helpers.h"

// Desktops (Mission Control spaces) for the island's Screens tab.
//
// macOS has no public API to switch spaces, so switching presses the user's
// own "Switch to Desktop N" shortcut (Keyboard > Keyboard Shortcuts > Mission
// Control, ⌃1… by default, off by default): the key and modifiers are read
// from the user's settings. Pressing keys needs Accessibility access
// (Privacy & Security > Accessibility), requested on the first switch.

// Shortcuts exist for the first 16 desktops.
#define DESKTOPS_MAX 16

struct desktops {
  int count;   // desktops (not full screen apps) on the display
  int active;  // index of the current one, -1 if a full screen app is shown
};

void desktops_read(uint32_t did, struct desktops* desktops);

// Whether the "Switch to Desktop <index + 1>" shortcut is turned on.
bool desktops_shortcut_enabled(int index);
// Whether SketchyBar may press keys (Accessibility).
bool desktops_can_switch(void);
// Presses the shortcut; asks for Accessibility access first if needed.
// False if it couldn't (shortcut off, or no access yet).
bool desktops_switch(int index);
