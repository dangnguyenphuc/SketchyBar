#include "desktops.h"
#include "display.h"

// IDs of "Switch to Desktop 1..16" in com.apple.symbolichotkeys.
#define HOTKEY_DESKTOP_1 118

// Defaults of those shortcuts: ⌃ + the digit (virtual key codes of 1..9, 0).
static const CGKeyCode g_default_keys[10] = { 18, 19, 20, 21, 23,
                                              22, 26, 28, 25, 29 };

void desktops_read(uint32_t did, struct desktops* desktops) {
  desktops->count = 0;
  desktops->active = -1;

  int count = 0;
  uint64_t* spaces = display_space_list(did, &count);
  if (!spaces) return;

  // Mission Control numbers only the desktops, not full screen apps.
  uint64_t current = display_space_id(did);
  for (int i = 0; i < count && desktops->count < DESKTOPS_MAX; i++) {
    if (SLSSpaceGetType(g_connection, spaces[i]) == 4) continue;
    if (spaces[i] == current) desktops->active = desktops->count;
    desktops->count++;
  }
  free(spaces);
}

// The user's setting for "Switch to Desktop <index + 1>": enabled, key code
// and modifier flags (same bits as CGEventFlags). Missing means the macOS
// default: off, ⌃ + digit.
static bool desktops_shortcut(int index, CGKeyCode* key, CGEventFlags* flags) {
  *key = index < 10 ? g_default_keys[index] : 0;
  *flags = kCGEventFlagMaskControl;
  if (index < 0 || index >= DESKTOPS_MAX) return false;

  CFDictionaryRef hotkeys = CFPreferencesCopyAppValue(
                              CFSTR("AppleSymbolicHotKeys"),
                              CFSTR("com.apple.symbolichotkeys"));
  if (!hotkeys) return false;

  bool enabled = false;
  if (CFGetTypeID(hotkeys) == CFDictionaryGetTypeID()) {
    char id[8];
    snprintf(id, sizeof(id), "%d", HOTKEY_DESKTOP_1 + index);
    CFStringRef name = CFStringCreateWithCString(NULL, id,
                                                 kCFStringEncodingUTF8);
    CFDictionaryRef entry = CFDictionaryGetValue(hotkeys, name);
    CFRelease(name);

    if (entry && CFGetTypeID(entry) == CFDictionaryGetTypeID()) {
      CFTypeRef on = CFDictionaryGetValue(entry, CFSTR("enabled"));
      enabled = on && CFGetTypeID(on) == CFBooleanGetTypeID()
                && CFBooleanGetValue(on);

      // value.parameters = [character, key code, modifiers]
      CFDictionaryRef value = CFDictionaryGetValue(entry, CFSTR("value"));
      CFArrayRef parameters = value
                              && CFGetTypeID(value)
                                 == CFDictionaryGetTypeID()
                              ? CFDictionaryGetValue(value,
                                                     CFSTR("parameters"))
                              : NULL;
      if (parameters && CFGetTypeID(parameters) == CFArrayGetTypeID()
          && CFArrayGetCount(parameters) >= 3) {
        int64_t code = 0, modifiers = 0;
        CFNumberGetValue(CFArrayGetValueAtIndex(parameters, 1),
                         kCFNumberSInt64Type, &code             );
        CFNumberGetValue(CFArrayGetValueAtIndex(parameters, 2),
                         kCFNumberSInt64Type, &modifiers        );
        *key = (CGKeyCode)code;
        *flags = (CGEventFlags)modifiers;
      }
    }
  }
  CFRelease(hotkeys);
  return enabled;
}

bool desktops_shortcut_enabled(int index) {
  CGKeyCode key;
  CGEventFlags flags;
  return desktops_shortcut(index, &key, &flags);
}

bool desktops_can_switch(void) {
  return CGPreflightPostEventAccess();
}

bool desktops_switch(int index) {
  CGKeyCode key;
  CGEventFlags flags;
  if (!desktops_shortcut(index, &key, &flags)) return false;

  // Shows the Accessibility prompt (once) when access is missing.
  if (!CGPreflightPostEventAccess()) {
    CGRequestPostEventAccess();
    return false;
  }

  CGEventSourceRef source
    = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
  CGEventRef down = CGEventCreateKeyboardEvent(source, key, true);
  CGEventRef up = CGEventCreateKeyboardEvent(source, key, false);
  CGEventSetFlags(down, flags);
  CGEventSetFlags(up, flags);
  CGEventPost(kCGHIDEventTap, down);
  CGEventPost(kCGHIDEventTap, up);
  CFRelease(down);
  CFRelease(up);
  if (source) CFRelease(source);
  return true;
}
