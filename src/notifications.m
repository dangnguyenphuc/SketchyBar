#include "notifications.h"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <sqlite3.h>
#include <sys/stat.h>

#define NOTIFICATION_CENTER_ID @"com.apple.notificationcenterui"
#define BANNER_SUBROLE         @"AXNotificationCenterBanner"
// How deep to look for banners in Notification Center's windows.
#define SCAN_DEPTH             6
// Identifiers remembered (banners captured, notifications marked read).
#define REMEMBER_MAX           100
// Notification Center's database: what it currently stores.
#ifndef DATABASE_PATH
#define DATABASE_PATH \
  "~/Library/Group Containers/group.com.apple.usernoted/db2/db"
#endif
#define DATABASE_ROWS          100
// Without access to it, look again this often (seconds).
#define DATABASE_RETRY         15.0
// A banner and a stored notification with the same app and texts within
// this many seconds are the same notification.
#define SAME_WINDOW            150.0
// The apps that may send notifications (System Settings > Notifications).
#define NCPREFS_PATH "~/Library/Preferences/com.apple.ncprefs.plist"

extern char g_config_file[4096];

static struct notification_list g_list;
static notifications_callback* g_callback = NULL;

// Captured banners (newest first) and what the database stores.
static struct notification_item g_banners[NOTIFICATIONS_MAX];
static int g_banner_count = 0;
static struct notification_item g_stored[DATABASE_ROWS];
static int g_stored_count = 0;

static NSMutableOrderedSet* g_seen = nil;  // banners already captured
static NSMutableOrderedSet* g_read = nil;  // opened / read all

static AXUIElementRef g_center = NULL;
static pid_t g_center_pid = 0;
static dispatch_source_t g_timer = NULL;

static bool g_capture = true;
static bool g_prompted = false;
static double g_scan_interval = NOTIFICATIONS_SCAN_DEFAULT;
static int g_per_app = NOTIFICATIONS_PER_APP_DEFAULT;

// Apps: from ncprefs (re-read when it changed) and seen in notifications;
// the ones switched off (loaded from the apps file once the config directory
// is known, re-read when it changed).
static struct notification_apps g_apps;
static NSMutableSet* g_ncprefs_apps = nil;
static NSMutableSet* g_seen_apps = nil;
static NSMutableDictionary* g_ncprefs_names = nil;  // bundle -> name / NSNull
static struct timespec g_ncprefs_stamp;
static NSMutableSet* g_disabled = nil;
static bool g_disabled_loaded = false;
static struct timespec g_disabled_stamp;

static dispatch_queue_t g_database_queue = NULL;
static bool g_database_reading = false;
static struct timespec g_database_stamp[2];
static CFAbsoluteTime g_database_retry = 0;
static NSMutableDictionary* g_app_names = nil;

static void notify() {
  if (g_callback) g_callback();
}

static void copy_string(char* destination, size_t size, NSString* string) {
  snprintf(destination, size, "%s", string ? [string UTF8String] : "");
}

static void remember(NSMutableOrderedSet* set, NSString* identifier) {
  [set addObject:identifier];
  while (set.count > REMEMBER_MAX) [set removeObjectAtIndex:0];
}

static struct timespec file_stamp(NSString* path) {
  struct stat info;
  struct timespec stamp = { 0, 0 };
  if (path && stat([path fileSystemRepresentation], &info) == 0)
    stamp = info.st_mtimespec;
  return stamp;
}

static bool same_stamp(struct timespec a, struct timespec b) {
  return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

// "/Applications/Zalo.app" -> "Zalo", as Finder (and banners) name it.
static NSString* display_name(NSString* path) {
  NSString* name = [[NSFileManager defaultManager] displayNameAtPath:path];
  if ([name hasSuffix:@".app"]) name = [name stringByDeletingPathExtension];
  return name;
}

// ------------------------------------------------------------ apps

static bool app_disabled(const char* app) {
  if (!g_disabled || !app[0]) return false;
  return [g_disabled containsObject:@(app)];
}

// The apps file: next to the config file. nil until that is known (the
// config runs after capturing started).
static NSString* apps_file_path() {
  NSString* directory = nil;
  if (g_config_file[0]) {
    directory = [@(g_config_file) stringByDeletingLastPathComponent];
  } else {
    const char* config_dir = getenv("CONFIG_DIR");
    if (config_dir && config_dir[0]) directory = @(config_dir);
  }
  return [directory stringByAppendingPathComponent:@NOTIFICATIONS_APPS_FILE];
}

// Reads the apps switched off (again when the file changed, e.g. edited by
// hand). Returns whether that changed anything.
static bool load_disabled() {
  NSString* path = apps_file_path();
  if (!path) return false;
  struct timespec stamp = file_stamp(path);
  if (g_disabled_loaded && same_stamp(stamp, g_disabled_stamp)) return false;
  g_disabled_loaded = true;
  g_disabled_stamp = stamp;

  NSMutableSet* disabled = [NSMutableSet set];
  NSString* text = [NSString stringWithContentsOfFile:path
                                             encoding:NSUTF8StringEncoding
                                                error:NULL                 ];
  for (NSString* line in [text componentsSeparatedByCharactersInSet:
                            [NSCharacterSet newlineCharacterSet]]) {
    NSString* name = [line stringByTrimmingCharactersInSet:
                        [NSCharacterSet whitespaceCharacterSet]];
    if (name.length > 0 && ![name hasPrefix:@"#"]) [disabled addObject:name];
  }
  if ([disabled isEqualToSet:g_disabled]) return false;
  [g_disabled setSet:disabled];
  return true;
}

static void save_disabled() {
  NSString* path = apps_file_path();
  if (!path) return;
  NSMutableString* text = [NSMutableString stringWithString:
    @"# Apps whose notifications SketchyBar doesn't capture, one per line.\n"
     "# Written by the island's Apps tab; can be edited by hand.\n"];
  NSArray* names = [[g_disabled allObjects]
    sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
  for (NSString* name in names) [text appendFormat:@"%@\n", name];
  [text writeToFile:path atomically:YES encoding:NSUTF8StringEncoding
              error:NULL                                             ];
  g_disabled_stamp = file_stamp(path);
}

// The app behind an ncprefs entry, by its name. nil for what can't be picked
// as an app (system services, helpers inside another app).
static NSString* ncprefs_app_name(NSDictionary* entry) {
  NSString* bundle = entry[@"bundle-id"];
  if (![bundle isKindOfClass:[NSString class]] || bundle.length == 0
      || [bundle hasPrefix:@"_"]) {
    return nil;
  }
  id cached = g_ncprefs_names[bundle];
  if (cached) return cached == [NSNull null] ? nil : cached;

  NSString* path = [entry[@"path"] isKindOfClass:[NSString class]]
                   ? entry[@"path"] : nil;
  // Notification bundles of system apps (Calendar, Reminders, ...): the app.
  if (![path hasSuffix:@".app"]) {
    path = [[NSWorkspace sharedWorkspace]
             URLForApplicationWithBundleIdentifier:bundle].path;
  }
  NSString* name = nil;
  if ([path hasSuffix:@".app"] && ![path containsString:@".app/"])
    name = display_name(path);
  g_ncprefs_names[bundle] = name.length > 0 ? name : [NSNull null];
  return name.length > 0 ? name : nil;
}

// Re-reads ncprefs when it changed. Returns whether the apps changed.
static bool load_ncprefs() {
  NSString* path = [@NCPREFS_PATH stringByExpandingTildeInPath];
  struct timespec stamp = file_stamp(path);
  if (same_stamp(stamp, g_ncprefs_stamp) && g_ncprefs_apps.count > 0)
    return false;
  g_ncprefs_stamp = stamp;

  NSMutableSet* apps = [NSMutableSet set];
  NSDictionary* prefs = [NSDictionary dictionaryWithContentsOfFile:path];
  NSArray* entries = [prefs isKindOfClass:[NSDictionary class]]
                     ? prefs[@"apps"] : nil;
  if ([entries isKindOfClass:[NSArray class]]) {
    for (NSDictionary* entry in entries) {
      if (![entry isKindOfClass:[NSDictionary class]]) continue;
      NSString* name = ncprefs_app_name(entry);
      if (name) [apps addObject:name];
    }
  }
  if ([apps isEqualToSet:g_ncprefs_apps]) return false;
  [g_ncprefs_apps setSet:apps];
  return true;
}

static void see_app(const char* app) {
  if (app[0]) [g_seen_apps addObject:@(app)];
}

// Rebuilds g_apps (sorted, with the switches). Returns whether it changed.
static bool refresh_apps() {
  struct notification_apps apps;
  memset(&apps, 0, sizeof(apps));
  @autoreleasepool {
    NSMutableSet* all = [NSMutableSet setWithSet:g_ncprefs_apps];
    [all unionSet:g_seen_apps];
    NSArray* names = [[all allObjects]
      sortedArrayUsingSelector:@selector(localizedCaseInsensitiveCompare:)];
    for (NSString* name in names) {
      if (apps.count >= NOTIFICATIONS_APPS_MAX) break;
      struct notification_app* app = &apps.items[apps.count++];
      copy_string(app->name, sizeof(app->name), name);
      app->enabled = ![g_disabled containsObject:name];
    }
  }
  if (memcmp(&apps, &g_apps, sizeof(apps)) == 0) return false;
  g_apps = apps;
  return true;
}

// ------------------------------------------------------------ the list

static int compare_newest(const void* a, const void* b) {
  CFAbsoluteTime x = ((const struct notification_item*)a)->time;
  CFAbsoluteTime y = ((const struct notification_item*)b)->time;
  return x < y ? 1 : (x > y ? -1 : 0);
}

static bool same_notification(const struct notification_item* a,
                              const struct notification_item* b) {
  if (strcmp(a->id, b->id) == 0) return true;
  return strcmp(a->app, b->app) == 0
         && strcmp(a->title, b->title) == 0
         && strcmp(a->body, b->body) == 0
         && fabs(a->time - b->time) < SAME_WINDOW;
}

// The unread list: stored notifications plus banners that aren't stored,
// minus what was marked read and apps switched off; newest first, at most
// g_per_app per app and NOTIFICATIONS_MAX in all. Also notifies when the apps
// changed.
static void rebuild() {
  bool apps_changed = refresh_apps();
  static struct notification_item all[DATABASE_ROWS + NOTIFICATIONS_MAX];
  int count = 0;

  @autoreleasepool {
    for (int i = 0; i < g_stored_count; i++) {
      if (app_disabled(g_stored[i].app)) continue;
      if ([g_read containsObject:@(g_stored[i].id)]) continue;
      all[count++] = g_stored[i];
    }
    int stored = count;
    for (int i = 0; i < g_banner_count; i++) {
      if (app_disabled(g_banners[i].app)) continue;
      if ([g_read containsObject:@(g_banners[i].id)]) continue;
      bool duplicate = false;
      for (int j = 0; j < stored && !duplicate; j++)
        duplicate = same_notification(&g_banners[i], &all[j]);
      if (!duplicate) all[count++] = g_banners[i];
    }
  }
  qsort(all, count, sizeof(struct notification_item), compare_newest);

  struct notification_list list;
  memset(&list, 0, sizeof(list));
  list.trusted = g_list.trusted;
  list.history = g_list.history;
  for (int i = 0; i < count && list.count < NOTIFICATIONS_MAX; i++) {
    int same = 0;
    for (int j = 0; j < list.count; j++) {
      if (strcmp(list.items[j].app, all[i].app) == 0) same++;
    }
    if (same >= g_per_app) continue;
    list.items[list.count++] = all[i];
  }

  if (memcmp(&list, &g_list, sizeof(list)) == 0) {
    if (apps_changed) notify();
    return;
  }
  g_list = list;
  notify();
}

// ------------------------------------------------------------ banners

static NSString* ax_string(AXUIElementRef element, CFStringRef attribute) {
  CFTypeRef value = NULL;
  if (AXUIElementCopyAttributeValue(element, attribute, &value)
        != kAXErrorSuccess || !value) {
    return nil;
  }
  if (CFGetTypeID(value) != CFStringGetTypeID()) {
    CFRelease(value);
    return nil;
  }
  return [(NSString*)value autorelease];
}

static NSArray* ax_children(AXUIElementRef element) {
  CFTypeRef value = NULL;
  if (AXUIElementCopyAttributeValue(element, kAXChildrenAttribute, &value)
        != kAXErrorSuccess || !value) {
    return nil;
  }
  if (CFGetTypeID(value) != CFArrayGetTypeID()) {
    CFRelease(value);
    return nil;
  }
  return [(NSArray*)value autorelease];
}

// One banner: "Zalo, Dang P, Hello" + #title "Dang P" + #body "Hello".
static void capture_banner(AXUIElementRef banner, NSMutableArray* added) {
  NSString* description = ax_string(banner, kAXDescriptionAttribute);
  NSString* identifier = ax_string(banner, kAXIdentifierAttribute);
  if (identifier.length == 0) identifier = description;
  if (identifier.length == 0 || [g_seen containsObject:identifier]) return;

  NSString* title = nil;
  NSString* subtitle = nil;
  NSString* body = nil;
  for (id child in ax_children(banner)) {
    AXUIElementRef element = (AXUIElementRef)child;
    NSString* name = ax_string(element, kAXIdentifierAttribute);
    NSString* value = ax_string(element, kAXValueAttribute);
    if ([name isEqualToString:@"title"]) title = value;
    else if ([name isEqualToString:@"subtitle"]) subtitle = value;
    else if ([name isEqualToString:@"body"]) body = value;
  }

  // The app is the description's first part (the rest repeats the texts).
  NSArray* parts = [description componentsSeparatedByString:@", "];
  NSString* app = parts.firstObject;
  if (!title && !body) {
    if (parts.count > 1) title = parts[1];
    if (parts.count > 2) {
      body = [[parts subarrayWithRange:NSMakeRange(2, parts.count - 2)]
               componentsJoinedByString:@", "];
    }
  }
  if (subtitle.length > 0) {
    body = body.length > 0 ? [NSString stringWithFormat:@"%@ · %@",
                                                        subtitle, body]
                           : subtitle;
  }

  remember(g_seen, identifier);
  [added addObject:@{ @"id": identifier,
                      @"app": app ?: @"",
                      @"title": title ?: @"",
                      @"body": body ?: @"" }];
}

static void find_banners(AXUIElementRef element,
                         int depth,
                         NSMutableArray* added) {
  if (depth > SCAN_DEPTH) return;
  if ([ax_string(element, kAXSubroleAttribute) isEqualToString:BANNER_SUBROLE]) {
    capture_banner(element, added);
    return;
  }
  for (id child in ax_children(element)) {
    find_banners((AXUIElementRef)child, depth + 1, added);
  }
}

// Banners (and Notification Center's list while it is open) on screen now.
static void scan_banners() {
  if (!g_center) return;

  @autoreleasepool {
    CFTypeRef windows = NULL;
    if (AXUIElementCopyAttributeValue(g_center, kAXWindowsAttribute, &windows)
          != kAXErrorSuccess || !windows) {
      return;
    }
    NSMutableArray* added = [NSMutableArray array];
    if (CFGetTypeID(windows) == CFArrayGetTypeID()) {
      for (id window in (NSArray*)windows) {
        find_banners((AXUIElementRef)window, 0, added);
      }
    }
    CFRelease(windows);
    if (added.count == 0) return;

    // Listed newest first: insert from the oldest so the newest ends first.
    // Apps switched off aren't captured (they stay seen, so switching one on
    // again doesn't bring them back).
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    for (NSDictionary* entry in [added reverseObjectEnumerator]) {
      see_app([entry[@"app"] UTF8String]);
      if (app_disabled([entry[@"app"] UTF8String])) continue;
      int keep = min(g_banner_count, NOTIFICATIONS_MAX - 1);
      memmove(&g_banners[1], &g_banners[0],
              keep * sizeof(struct notification_item));
      g_banner_count = keep + 1;

      struct notification_item* item = &g_banners[0];
      memset(item, 0, sizeof(*item));
      copy_string(item->id, sizeof(item->id), entry[@"id"]);
      copy_string(item->app, sizeof(item->app), entry[@"app"]);
      copy_string(item->title, sizeof(item->title), entry[@"title"]);
      copy_string(item->body, sizeof(item->body), entry[@"body"]);
      item->time = now;
    }
  }
  rebuild();
}

// ------------------------------------------------------------ database

// "com.vng.zalo" -> "Zalo" (the installed app's name), cached. Apps that
// aren't on this Mac (iPhone-mirrored) fall back to the identifier's last
// part.
static NSString* app_name(NSString* bundle) {
  if (bundle.length == 0) return @"";
  NSString* name = g_app_names[bundle];
  if (name) return name;

  NSURL* url = [[NSWorkspace sharedWorkspace]
                 URLForApplicationWithBundleIdentifier:bundle];
  if (url) name = display_name(url.path);
  if (name.length == 0) {
    name = [[bundle componentsSeparatedByString:@"."] lastObject];
    if (name.length > 0) {
      name = [[[name substringToIndex:1] uppercaseString]
               stringByAppendingString:[name substringFromIndex:1]];
    }
  }
  g_app_names[bundle] = name ?: bundle;
  return g_app_names[bundle];
}

// Modification times of the database and its write-ahead log: re-read only
// when one changed.
static bool database_changed() {
  NSString* path = [@DATABASE_PATH stringByExpandingTildeInPath];
  NSString* files[2] = { path, [path stringByAppendingString:@"-wal"] };
  bool changed = false;
  for (int i = 0; i < 2; i++) {
    struct stat info;
    struct timespec stamp = { 0, 0 };
    if (stat([files[i] fileSystemRepresentation], &info) == 0)
      stamp = info.st_mtimespec;
    if (stamp.tv_sec != g_database_stamp[i].tv_sec
        || stamp.tv_nsec != g_database_stamp[i].tv_nsec) {
      g_database_stamp[i] = stamp;
      changed = true;
    }
  }
  return changed;
}

static NSString* uuid_string(const void* bytes, int length) {
  if (length != 16) return nil;
  const uint8_t* b = bytes;
  return [NSString stringWithFormat:
           @"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-"
            "%02X%02X%02X%02X%02X%02X",
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9],
           b[10], b[11], b[12], b[13], b[14], b[15]];
}

// Background queue. False without access (Full Disk Access is missing).
static bool read_database(struct notification_item* out, int* count) {
  *count = 0;
  NSString* path = [@DATABASE_PATH stringByExpandingTildeInPath];
  sqlite3* database = NULL;
  if (sqlite3_open_v2([path fileSystemRepresentation], &database,
                      SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
    if (database) sqlite3_close(database);
    return false;
  }

  // Each record: its uuid, the app's bundle id, the notification as a binary
  // plist (req: titl, subt, body) and when it was delivered.
  sqlite3_stmt* statement = NULL;
  const char* query =
    "SELECT r.uuid, a.identifier, r.data, "
    "       COALESCE(r.delivered_date, r.request_date) "
    "FROM record r LEFT JOIN app a ON a.app_id = r.app_id "
    "ORDER BY 4 DESC LIMIT 200";  // DATABASE_ROWS
  if (sqlite3_prepare_v2(database, query, -1, &statement, NULL) != SQLITE_OK) {
    sqlite3_close(database);
    return false;
  }

  while (sqlite3_step(statement) == SQLITE_ROW && *count < DATABASE_ROWS) {
    @autoreleasepool {
      const void* data = sqlite3_column_blob(statement, 2);
      int length = sqlite3_column_bytes(statement, 2);
      if (!data || length <= 0) continue;

      NSDictionary* plist = [NSPropertyListSerialization
        propertyListWithData:[NSData dataWithBytes:data length:length]
                     options:NSPropertyListImmutable
                      format:NULL
                       error:NULL                                      ];
      if (![plist isKindOfClass:[NSDictionary class]]) continue;
      NSDictionary* request = plist[@"req"];
      if (![request isKindOfClass:[NSDictionary class]]) continue;

      NSString* bundle = nil;
      const unsigned char* identifier = sqlite3_column_text(statement, 1);
      if (identifier) bundle = @((const char*)identifier);
      if (bundle.length == 0 && [plist[@"app"] isKindOfClass:[NSString class]])
        bundle = plist[@"app"];

      NSString* title = [request[@"titl"] isKindOfClass:[NSString class]]
                        ? request[@"titl"] : nil;
      NSString* subtitle = [request[@"subt"] isKindOfClass:[NSString class]]
                           ? request[@"subt"] : nil;
      NSString* body = [request[@"body"] isKindOfClass:[NSString class]]
                       ? request[@"body"] : nil;
      if (subtitle.length > 0) {
        body = body.length > 0 ? [NSString stringWithFormat:@"%@ · %@",
                                                            subtitle, body]
                               : subtitle;
      }
      if (title.length == 0 && body.length == 0) continue;

      NSString* uuid = uuid_string(sqlite3_column_blob(statement, 0),
                                   sqlite3_column_bytes(statement, 0));
      struct notification_item* item = &out[(*count)++];
      memset(item, 0, sizeof(*item));
      copy_string(item->id, sizeof(item->id),
                  uuid ?: [NSString stringWithFormat:@"%@-%f", bundle,
                           sqlite3_column_double(statement, 3)]);
      copy_string(item->app, sizeof(item->app), app_name(bundle));
      copy_string(item->title, sizeof(item->title), title);
      copy_string(item->body, sizeof(item->body), body);
      // Stored as seconds since 2001 (Mac absolute time), like CFAbsoluteTime.
      item->time = sqlite3_column_double(statement, 3);
    }
  }
  sqlite3_finalize(statement);
  sqlite3_close(database);
  return true;
}

static void scan_database() {
  CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  if (g_database_reading) return;
  if (!g_list.history && now < g_database_retry) return;
  if (g_list.history && !database_changed()) return;

  g_database_reading = true;
  dispatch_async(g_database_queue, ^{
    static struct notification_item rows[DATABASE_ROWS];
    int count = 0;
    bool access = read_database(rows, &count);

    dispatch_async(dispatch_get_main_queue(), ^{
      g_database_reading = false;
      if (!g_capture) return;  // switched off meanwhile
      if (!access) {
        g_database_retry = CFAbsoluteTimeGetCurrent() + DATABASE_RETRY;
        g_stored_count = 0;
      } else {
        memcpy(g_stored, rows, count * sizeof(struct notification_item));
        g_stored_count = count;
        for (int i = 0; i < count; i++) see_app(g_stored[i].app);
      }
      if (access && !g_list.history) database_changed();  // take the stamps
      g_list.history = access;
      rebuild();
    });
  });
}

// ------------------------------------------------------------ attaching

static void detach() {
  if (g_center) {
    CFRelease(g_center);
    g_center = NULL;
  }
  g_center_pid = 0;
}

// The running Notification Center process (again after it relaunched).
static void attach() {
  if (!g_list.trusted) return;

  NSRunningApplication* center = [[NSRunningApplication
    runningApplicationsWithBundleIdentifier:NOTIFICATION_CENTER_ID]
                                                     firstObject];
  if (!center) {
    detach();
    return;
  }
  if (center.processIdentifier == g_center_pid && g_center) return;

  detach();
  g_center_pid = center.processIdentifier;
  g_center = AXUIElementCreateApplication(g_center_pid);
}

// Every scan interval: banners on screen (Accessibility), Notification
// Center's database if it changed, and the apps (ncprefs, apps file) if they
// changed.
static void tick() {
  if (!g_capture) return;
  // Asks for Accessibility once, the first time capturing actually runs.
  if (!g_prompted) {
    g_prompted = true;
    NSDictionary* options = @{ (id)kAXTrustedCheckOptionPrompt: @YES };
    g_list.trusted = AXIsProcessTrustedWithOptions((CFDictionaryRef)options);
  }

  bool apps = load_ncprefs();
  if (load_disabled()) apps = true;
  if (apps) rebuild();

  bool trusted = AXIsProcessTrusted();
  if (trusted != g_list.trusted) {
    g_list.trusted = trusted;
    if (!trusted) detach();
    notify();
  }
  if (trusted) {
    attach();
    scan_banners();
  }
  scan_database();
}

// ------------------------------------------------------------ public

static void schedule_timer() {
  if (!g_timer) return;
  uint64_t interval = g_scan_interval * NSEC_PER_SEC;
  dispatch_source_set_timer(g_timer,
                            dispatch_time(DISPATCH_TIME_NOW, interval),
                            interval,
                            interval / 4                               );
}

void notifications_begin(notifications_callback* on_change) {
  if (g_seen) return;
  g_callback = on_change;
  g_seen = [[NSMutableOrderedSet alloc] init];
  g_read = [[NSMutableOrderedSet alloc] init];
  g_app_names = [[NSMutableDictionary alloc] init];
  g_ncprefs_apps = [[NSMutableSet alloc] init];
  g_seen_apps = [[NSMutableSet alloc] init];
  g_ncprefs_names = [[NSMutableDictionary alloc] init];
  g_disabled = [[NSMutableSet alloc] init];
  g_database_queue = dispatch_queue_create("sketchybar.notifications",
                                           DISPATCH_QUEUE_SERIAL      );

  g_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                   dispatch_get_main_queue()       );
  schedule_timer();
  dispatch_source_set_event_handler(g_timer, ^{ @autoreleasepool { tick(); } });
  dispatch_resume(g_timer);
  // The first scan runs a moment later: the callback must not run while the
  // bar is still being built, and the config (which may switch capturing
  // off) runs right after startup.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC),
                 dispatch_get_main_queue(),
                 ^{ @autoreleasepool { tick(); } });
}

void notifications_set_capture(bool on) {
  if (on == g_capture) return;
  g_capture = on;
  if (on) {
    if (g_seen) @autoreleasepool { tick(); }
    return;
  }
  // Drop what was captured; added (Claude) notifications stay.
  int kept = 0;
  for (int i = 0; i < g_banner_count; i++) {
    if (g_banners[i].claude) g_banners[kept++] = g_banners[i];
  }
  g_banner_count = kept;
  g_stored_count = 0;
  g_list.history = false;
  detach();
  if (g_seen) rebuild();
}

bool notifications_capture(void) {
  return g_capture;
}

const struct notification_list* notifications_get(void) {
  return &g_list;
}

// The app a notification came from, by its name: a running app, else one in
// the usual application folders. nil for apps that aren't on this Mac (e.g.
// iPhone-mirrored notifications).
static NSURL* app_url(NSString* name) {
  if (name.length == 0) return nil;
  for (NSRunningApplication* app in
         [[NSWorkspace sharedWorkspace] runningApplications]) {
    if ([app.localizedName isEqualToString:name] && app.bundleURL)
      return app.bundleURL;
  }

  NSArray* folders = @[ @"/Applications",
                        @"/System/Applications",
                        @"/Applications/Utilities",
                        @"/System/Applications/Utilities",
                        [@"~/Applications" stringByExpandingTildeInPath] ];
  NSString* bundle = [name stringByAppendingString:@".app"];
  for (NSString* folder in folders) {
    NSString* path = [folder stringByAppendingPathComponent:bundle];
    if ([[NSFileManager defaultManager] fileExistsAtPath:path])
      return [NSURL fileURLWithPath:path];
  }
  return nil;
}

void notifications_open(int index) {
  if (index < 0 || index >= g_list.count) return;

  @autoreleasepool {
    NSURL* url = app_url(@(g_list.items[index].app));
    if (url) {
      NSWorkspaceOpenConfiguration* configuration
        = [NSWorkspaceOpenConfiguration configuration];
      configuration.activates = YES;
      [[NSWorkspace sharedWorkspace] openApplicationAtURL:url
                                            configuration:configuration
                                        completionHandler:nil          ];
    }
    remember(g_read, @(g_list.items[index].id));
  }
  rebuild();
}

void notifications_dismiss(int index) {
  if (index < 0 || index >= g_list.count) return;
  @autoreleasepool {
    remember(g_read, @(g_list.items[index].id));
  }
  rebuild();
}

void notifications_add(const struct notification_item* item) {
  if (!g_seen) return;
  see_app(item->app);
  if (!app_disabled(item->app)) {
    int keep = min(g_banner_count, NOTIFICATIONS_MAX - 1);
    memmove(&g_banners[1], &g_banners[0],
            keep * sizeof(struct notification_item));
    g_banner_count = keep + 1;
    g_banners[0] = *item;
  }
  rebuild();
}

static struct notification_item* find_banner(const char* id) {
  for (int i = 0; i < g_banner_count; i++) {
    if (strcmp(g_banners[i].id, id) == 0) return &g_banners[i];
  }
  return NULL;
}

bool notifications_set_suggestion(const char* id,
                                  const char* suggestion,
                                  CFAbsoluteTime until,
                                  struct notification_item* out) {
  struct notification_item* item = find_banner(id);
  if (!item) return false;
  snprintf(item->suggestion, sizeof(item->suggestion), "%s", suggestion);
  item->suggest_until = until;
  @autoreleasepool {
    [g_read removeObject:@(id)];
  }
  if (out) *out = *item;
  rebuild();
  return true;
}

void notifications_clear_suggestion(const char* id) {
  struct notification_item* item = find_banner(id);
  if (!item || !item->suggestion[0]) return;
  item->suggestion[0] = '\0';
  item->suggest_until = 0;
  rebuild();
}

void notifications_dismiss_id(const char* id) {
  @autoreleasepool {
    remember(g_read, @(id));
  }
  rebuild();
}

// Everything known, not just what the list shows (it keeps
// g_per_app per app): older ones mustn't move up.
void notifications_read_all(void) {
  if (g_list.count == 0) return;
  @autoreleasepool {
    for (int i = 0; i < g_stored_count; i++)
      remember(g_read, @(g_stored[i].id));
    for (int i = 0; i < g_banner_count; i++)
      remember(g_read, @(g_banners[i].id));
  }
  rebuild();
}

const struct notification_apps* notifications_apps(void) {
  return &g_apps;
}

void notifications_set_app_enabled(int index, bool enabled) {
  if (index < 0 || index >= g_apps.count) return;
  if (g_apps.items[index].enabled == enabled) return;
  @autoreleasepool {
    load_disabled();
    NSString* name = @(g_apps.items[index].name);
    if (enabled) [g_disabled removeObject:name];
    else [g_disabled addObject:name];
    save_disabled();
  }
  rebuild();
}

void notifications_set_scan_interval(double seconds) {
  seconds = max(min(seconds, NOTIFICATIONS_SCAN_MAX), NOTIFICATIONS_SCAN_MIN);
  if (seconds == g_scan_interval) return;
  g_scan_interval = seconds;
  schedule_timer();
}

double notifications_scan_interval(void) {
  return g_scan_interval;
}

void notifications_set_per_app(int count) {
  count = max(min(count, NOTIFICATIONS_MAX), 1);
  if (count == g_per_app) return;
  g_per_app = count;
  if (g_seen) rebuild();
}

int notifications_per_app(void) {
  return g_per_app;
}
