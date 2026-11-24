#pragma once
#include "misc/helpers.h"

// Unread notifications for the island's Alerts tab, from two sources, since
// each app chooses where its notifications go (System Settings >
// Notifications > app: Desktop, Notification Center):
//
// - Notification Center's database: what it currently stores (apps with
//   "Notification Center" on). Needs Full Disk Access (`history` false
//   without). Re-read only when the file changed.
// - Banners on screen (apps with "Desktop" on, also ones that are never
//   stored): through Accessibility each is an AXNotificationCenterBanner with
//   a unique identifier, its app in the description and #title / #body
//   texts. Needs Accessibility (`trusted`).
//
// Both are checked every scan interval (notifications_set_scan_interval; a
// banner shows for ~5s, so none is missed), also while the island is
// closed. A notification found
// in both is listed once; one dismissed in Notification Center leaves the
// list. Apps with neither on can't be seen at all.
//
// The list lives in memory only (no history across restarts), at most
// `per_app` per app (the newest). Opening a notification or "read all"
// removes it; it isn't captured again.
//
// Apps: every app macOS lets send notifications (the apps in
// ~/Library/Preferences/com.apple.ncprefs.plist, i.e. System Settings >
// Notifications), plus any other app a notification was seen from (e.g.
// iPhone-mirrored ones). Each can be switched off: its banners aren't
// captured and its stored notifications aren't listed. The apps switched off
// are kept, one name per line, in NOTIFICATIONS_APPS_FILE next to the config
// file (sketchybarrc), so the choice survives restarts and can be edited.
//
// Accessibility is asked for once; Full Disk Access has to be granted in
// Privacy & Security > Full Disk Access (add the sketchybar binary).

#define NOTIFICATIONS_MAX   50
#define NOTIFICATIONS_TEXT  256
#define NOTIFICATIONS_APPS_MAX 160
#define NOTIFICATIONS_APPS_FILE "notification_apps"

// Defaults of the settings below (sketchybarrc: --bar notification_*).
#define NOTIFICATIONS_PER_APP_DEFAULT 5
#define NOTIFICATIONS_SCAN_DEFAULT    1.0
#define NOTIFICATIONS_SCAN_MIN        0.2
#define NOTIFICATIONS_SCAN_MAX        60.0

struct notification_item {
  char id[64];
  char app[64];
  char title[128];
  char body[NOTIFICATIONS_TEXT];
  CFAbsoluteTime time;  // delivered (stored ones) or captured (banners)
};

struct notification_list {
  bool trusted;   // Accessibility: banners
  bool history;   // Full Disk Access: Notification Center's database
  int count;
  struct notification_item items[NOTIFICATIONS_MAX];  // newest first
};

struct notification_app {
  char name[64];
  bool enabled;
};

// Sorted by name, case-insensitive.
struct notification_apps {
  int count;
  struct notification_app items[NOTIFICATIONS_APPS_MAX];
};

// Called on the main thread whenever the list (or the apps) changed.
typedef void notifications_callback(void);

// Starts capturing (for the life of the process).
void notifications_begin(notifications_callback* on_change);
const struct notification_list* notifications_get(void);

// Opens the notification's app and marks it read (removes it).
void notifications_open(int index);
// Marks it read without opening anything (the card's close button).
void notifications_dismiss(int index);
void notifications_read_all(void);

const struct notification_apps* notifications_apps(void);
// Switches capturing an app's notifications on or off, and saves it.
void notifications_set_app_enabled(int index, bool enabled);

// Settings. How often banners and Notification Center's database are checked
// (seconds, NOTIFICATIONS_SCAN_MIN..MAX; banners stay ~5s, so keep it below
// that), and how many notifications are kept per app (1..NOTIFICATIONS_MAX).
void notifications_set_scan_interval(double seconds);
double notifications_scan_interval(void);
void notifications_set_per_app(int count);
int notifications_per_app(void);
