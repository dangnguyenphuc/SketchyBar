#include "password_field.h"
#import <AppKit/AppKit.h>

#define PASSWORD_FONT_SIZE 11

// Borderless panels can't become key by default.
@interface password_panel : NSPanel
@end

@implementation password_panel
- (BOOL)canBecomeKeyWindow { return YES; }
@end

@interface password_field_delegate : NSObject <NSTextFieldDelegate>
@end

static password_panel* g_panel = nil;
static NSSecureTextField* g_field = nil;
static password_field_delegate* g_delegate = nil;
static password_field_callback* g_callback = NULL;

static void finish(bool submitted) {
  password_field_callback* callback = g_callback;
  char password[256];
  snprintf(password, sizeof(password), "%s",
           g_field ? [g_field.stringValue UTF8String] : "");

  password_field_hide();
  if (callback) callback(submitted, password);
  memset(password, 0, sizeof(password));
}

@implementation password_field_delegate
- (BOOL)control:(NSControl*)control
       textView:(NSTextView*)view
  doCommandBySelector:(SEL)selector {
  if (selector == @selector(insertNewline:)) {
    finish(true);
    return YES;
  }
  if (selector == @selector(cancelOperation:)) {
    finish(false);
    return YES;
  }
  return NO;
}
@end

// Cocoa windows in this Carbon-driven process need NSApp set up once. The
// accessory policy lets the panel become key without a Dock icon.
static void ensure_application() {
  static bool loaded = false;
  if (loaded) return;
  NSApplicationLoad();
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  loaded = true;
}

static void create_panel() {
  ensure_application();

  g_panel = [[password_panel alloc]
              initWithContentRect:NSMakeRect(0, 0, 100, 20)
                        styleMask:NSWindowStyleMaskBorderless
                                  | NSWindowStyleMaskNonactivatingPanel
                          backing:NSBackingStoreBuffered
                            defer:NO                                   ];
  g_panel.opaque = NO;
  g_panel.backgroundColor = [NSColor clearColor];
  g_panel.hasShadow = NO;
  g_panel.floatingPanel = YES;
  g_panel.hidesOnDeactivate = NO;
  g_panel.becomesKeyOnlyIfNeeded = NO;
  g_panel.level = NSPopUpMenuWindowLevel;
  g_panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                               | NSWindowCollectionBehaviorFullScreenAuxiliary;

  g_field = [[NSSecureTextField alloc] initWithFrame:NSMakeRect(0, 0, 100, 20)];
  g_field.bezeled = NO;
  g_field.bordered = NO;
  g_field.drawsBackground = NO;
  g_field.focusRingType = NSFocusRingTypeNone;
  g_field.textColor = [NSColor whiteColor];
  g_field.font = [NSFont monospacedSystemFontOfSize:PASSWORD_FONT_SIZE
                                             weight:NSFontWeightRegular];
  g_field.placeholderAttributedString = [[[NSAttributedString alloc]
    initWithString:@"Password"
        attributes:@{ NSForegroundColorAttributeName:
                        [NSColor colorWithWhite:1 alpha:0.35],
                      NSFontAttributeName: g_field.font         }]
                                                       autorelease];

  g_delegate = [[password_field_delegate alloc] init];
  g_field.delegate = g_delegate;
  g_panel.contentView = g_field;
}

void password_field_show(CGRect rect,
                         const char* text,
                         password_field_callback* callback) {
  @autoreleasepool {
    if (!g_panel) create_panel();
    g_callback = callback;

    // CG (top-left origin) to Cocoa (bottom-left of the main screen).
    CGFloat main_height = NSScreen.screens.firstObject.frame.size.height;
    NSRect frame = NSMakeRect(rect.origin.x,
                              main_height - rect.origin.y - rect.size.height,
                              rect.size.width,
                              rect.size.height                             );
    [g_panel setFrame:frame display:NO];

    // Vertically center the single line in the field.
    CGFloat line = ceil(g_field.font.ascender - g_field.font.descender) + 2;
    g_field.frame = NSMakeRect(0, round((rect.size.height - line) / 2.0),
                               rect.size.width, line                    );
    g_field.stringValue = text ? @(text) : @"";

    [g_panel orderFrontRegardless];
    [g_panel makeKeyWindow];
    [g_panel makeFirstResponder:g_field];
    // Put the caret at the end of a pre-filled password.
    [[g_field currentEditor] moveToEndOfDocument:nil];
  }
}

void password_field_hide(void) {
  g_callback = NULL;
  if (!g_panel) return;
  g_field.stringValue = @"";
  [g_panel orderOut:nil];
}

void password_field_submit(void) {
  if (password_field_visible()) finish(true);
}

bool password_field_visible(void) {
  return g_panel && g_panel.visible;
}
