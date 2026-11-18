#include "notifications.h"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#define NOTIFICATION_CENTER_ID @"com.apple.notificationcenterui"
#define BANNER_SUBROLE         @"AXNotificationCenterBanner"
// How deep to look for banners in Notification Center's windows.
#define SCAN_DEPTH             12
// Identifiers remembered after a notification left the list, so it isn't
// captured again while Notification Center still shows it.
#define SEEN_MAX               500

static struct notification_list g_list;
static notifications_callback* g_callback = NULL;

static AXUIElementRef g_center = NULL;
static pid_t g_center_pid = 0;
static NSMutableOrderedSet* g_seen = nil;
static dispatch_source_t g_timer = NULL;

static void notify() {
  if (g_callback) g_callback();
}

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

static void copy_string(char* destination, size_t size, NSString* string) {
  snprintf(destination, size, "%s", string ? [string UTF8String] : "");
}

static void remember(NSString* identifier) {
  [g_seen addObject:identifier];
  while (g_seen.count > SEEN_MAX) [g_seen removeObjectAtIndex:0];
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
  NSString* app = [[description componentsSeparatedByString:@", "]
                     firstObject];
  if (!title && !body) {
    // No labelled texts: fall back to the description's remaining parts.
    NSArray* parts = [description componentsSeparatedByString:@", "];
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

  remember(identifier);
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

static void scan() {
  if (!g_center) return;

  @autoreleasepool {
    CFTypeRef windows = NULL;
    if (AXUIElementCopyAttributeValue(g_center, kAXWindowsAttribute, &windows)
          != kAXErrorSuccess || !windows) {
      return;
    }

    // Banners are listed top (newest) to bottom; insert oldest first so the
    // newest ends up at the front.
    NSMutableArray* added = [NSMutableArray array];
    if (CFGetTypeID(windows) == CFArrayGetTypeID()) {
      for (id window in (NSArray*)windows) {
        find_banners((AXUIElementRef)window, 0, added);
      }
    }
    CFRelease(windows);
    if (added.count == 0) return;

    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    for (NSDictionary* entry in [added reverseObjectEnumerator]) {
      int keep = min(g_list.count, NOTIFICATIONS_MAX - 1);
      memmove(&g_list.items[1], &g_list.items[0],
              keep * sizeof(struct notification_item));
      g_list.count = keep + 1;

      struct notification_item* item = &g_list.items[0];
      memset(item, 0, sizeof(*item));
      copy_string(item->id, sizeof(item->id), entry[@"id"]);
      copy_string(item->app, sizeof(item->app), entry[@"app"]);
      copy_string(item->title, sizeof(item->title), entry[@"title"]);
      copy_string(item->body, sizeof(item->body), entry[@"body"]);
      item->time = now;

      // Keep only the newest NOTIFICATIONS_PER_APP of this app.
      int same = 0;
      for (int i = 0; i < g_list.count; i++) {
        if (strcmp(g_list.items[i].app, item->app) != 0) continue;
        if (++same <= NOTIFICATIONS_PER_APP) continue;
        memmove(&g_list.items[i], &g_list.items[i + 1],
                (g_list.count - i - 1) * sizeof(struct notification_item));
        g_list.count--;
        i--;
      }
    }
  }
  notify();
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

// Every NOTIFICATIONS_SCAN seconds: checks Accessibility access (until it is
// granted) and scans for new banners.
static void tick() {
  bool trusted = AXIsProcessTrusted();
  if (trusted != g_list.trusted) {
    g_list.trusted = trusted;
    if (!trusted) detach();
    notify();
  }
  if (!trusted) return;

  attach();
  scan();
}

// ------------------------------------------------------------ public

void notifications_begin(notifications_callback* on_change) {
  if (g_seen) return;
  g_callback = on_change;
  g_seen = [[NSMutableOrderedSet alloc] init];

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
  }

  memmove(&g_list.items[index], &g_list.items[index + 1],
          (g_list.count - index - 1) * sizeof(struct notification_item));
  g_list.count--;
  notify();
}

void notifications_read_all(void) {
  if (g_list.count == 0) return;
  g_list.count = 0;
  notify();
}
