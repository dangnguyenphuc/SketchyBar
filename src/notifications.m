#include "notifications.h"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <sqlite3.h>
#include <sys/stat.h>

#define NOTIFICATION_CENTER_ID @"com.apple.notificationcenterui"
#define BANNER_SUBROLE         @"AXNotificationCenterBanner"
// How deep to look for banners in Notification Center's windows.
#define SCAN_DEPTH             12
// Identifiers remembered (banners captured, notifications marked read).
#define REMEMBER_MAX           500
// Notification Center's database: what it currently stores.
#ifndef DATABASE_PATH
#define DATABASE_PATH \
  "~/Library/Group Containers/group.com.apple.usernoted/db2/db"
#endif
#define DATABASE_ROWS          200
// Without access to it, look again this often (seconds).
#define DATABASE_RETRY         30.0
// A banner and a stored notification with the same app and texts within
// this many seconds are the same notification.
#define SAME_WINDOW            300.0

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
// minus what was marked read; newest first, at most NOTIFICATIONS_PER_APP
// per app and NOTIFICATIONS_MAX in all.
static void rebuild() {
  static struct notification_item all[DATABASE_ROWS + NOTIFICATIONS_MAX];
  int count = 0;

  @autoreleasepool {
    for (int i = 0; i < g_stored_count; i++) {
      if ([g_read containsObject:@(g_stored[i].id)]) continue;
      all[count++] = g_stored[i];
    }
    int stored = count;
    for (int i = 0; i < g_banner_count; i++) {
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
    if (same >= NOTIFICATIONS_PER_APP) continue;
    list.items[list.count++] = all[i];
  }

  if (memcmp(&list, &g_list, sizeof(list)) == 0) return;
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
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    for (NSDictionary* entry in [added reverseObjectEnumerator]) {
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
  if (url) {
    name = [[[NSFileManager defaultManager] displayNameAtPath:url.path]
             stringByDeletingPathExtension];
  }
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
      if (!access) {
        g_database_retry = CFAbsoluteTimeGetCurrent() + DATABASE_RETRY;
        g_stored_count = 0;
      } else {
        memcpy(g_stored, rows, count * sizeof(struct notification_item));
        g_stored_count = count;
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

// Every NOTIFICATIONS_SCAN seconds: banners on screen (Accessibility), and
// Notification Center's database if it changed.
static void tick() {
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

void notifications_begin(notifications_callback* on_change) {
  if (g_seen) return;
  g_callback = on_change;
  g_seen = [[NSMutableOrderedSet alloc] init];
  g_read = [[NSMutableOrderedSet alloc] init];
  g_app_names = [[NSMutableDictionary alloc] init];
  g_database_queue = dispatch_queue_create("sketchybar.notifications",
                                           DISPATCH_QUEUE_SERIAL      );

  // Shows the Accessibility prompt once if access is missing.
  NSDictionary* options = @{ (id)kAXTrustedCheckOptionPrompt: @YES };
  g_list.trusted = AXIsProcessTrustedWithOptions((CFDictionaryRef)options);

  g_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                   dispatch_get_main_queue()       );
  dispatch_source_set_timer(g_timer,
                            dispatch_time(DISPATCH_TIME_NOW,
                                          NOTIFICATIONS_SCAN * NSEC_PER_SEC),
                            NOTIFICATIONS_SCAN * NSEC_PER_SEC,
                            0.5 * NSEC_PER_SEC                              );
  dispatch_source_set_event_handler(g_timer, ^{ tick(); });
  dispatch_resume(g_timer);
  tick();
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

// Everything known, not just what the list shows (it keeps
// NOTIFICATIONS_PER_APP per app): older ones mustn't move up.
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
