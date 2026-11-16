#include "island.h"
#include "font.h"

// Layout (points)
#define ISLAND_PADDING        10
#define ISLAND_PADDING_TOP    4
#define ISLAND_SERVICE_WIDTH  24
#define ISLAND_SERVICE_HEIGHT 24
#define ISLAND_ICON_SIZE      13
#define ISLAND_NOTICE_HEIGHT  16
#define ISLAND_SERVICE_GAP    3
#define ISLAND_DIVIDER_GAP    7
#define ISLAND_SCROLL_WIDTH   3
#define ISLAND_SCROLL_GAP     5
#define ISLAND_SCROLL_MIN     14
#define ISLAND_CARD_HEIGHT    46
#define ISLAND_CARD_GAP       6
#define ISLAND_CARD_RADIUS    10
#define ISLAND_CARD_INSET     9
#define ISLAND_PAGE_GAP       12

// During a page transition the outgoing page fades to this alpha and the
// incoming one fades in from it, on top of the slide.
#define ISLAND_PAGE_MIN_ALPHA 0.35

// Content stays invisible for the first part of the expand animation and then
// fades in, so it never looks squeezed while the shape is still small.
#define ISLAND_FADE_START     0.55

struct island_notification {
  const char* app;
  const char* title;
  const char* body;
  const char* time;
};

// Mock data for the first look. Replace island_load_notifications() with the
// real Notification Center reader (plugins/notifications.py) later.
static const struct island_notification g_mock_notifications[] = {
  { "Messages", "Lan", "Are we still on for lunch tomorrow?", "now" },
  { "Slack", "#sketchybar", "Minh: the island panel looks great 🎉", "2m" },
  { "WhatsApp · iPhone", "Family", "Mom: Call me when you're free", "9m" },
  { "Calendar", "Standup in 10 minutes", "Daily sync · Zoom", "14m" },
  { "Mail", "Your invoice is ready", "Billing: Invoice #2048 for November", "1h" },
  { "GitHub", "Review requested", "notch-island: add services panel", "3h" },
};

// The services column shows Nerd Font icons (Font Awesome: bell, music,
// calendar). Without FuraCode Nerd Font installed those glyphs don't exist,
// so the column falls back to the service's initial.
static const struct {
  const char* icon;
  const char* fallback;
} g_services[ISLAND_SERVICE_COUNT] = {
  [ISLAND_SERVICE_ALERTS]   = { "\xEF\x83\xB3", "A" },  // U+F0F3
  [ISLAND_SERVICE_MUSIC]    = { "\xEF\x80\x81", "M" },  // U+F001
  [ISLAND_SERVICE_CALENDAR] = { "\xEF\x81\xB3", "C" },  // U+F073
};

static const struct island_notification* g_notifications = NULL;
static uint32_t g_notification_count = 0;

static struct {
  CTFontRef caption;   // app name, time, install hint
  CTFontRef body;      // notification body
  CTFontRef title;     // notification title
  CTFontRef icon;      // services column
} g_fonts;

static bool g_loaded = false;
static bool g_has_icons = false;

static void island_load_notifications() {
  g_notifications = g_mock_notifications;
  g_notification_count = sizeof(g_mock_notifications)
                         / sizeof(g_mock_notifications[0]);
}

static void island_unload_notifications() {
  g_notifications = NULL;
  g_notification_count = 0;
}

static CTFontRef island_create_font(const char* family,
                                    const char* style,
                                    CGFloat size       ) {
  CFStringRef family_ref = CFStringCreateWithCString(NULL,
                                                     family,
                                                     kCFStringEncodingUTF8);
  CFStringRef style_ref = CFStringCreateWithCString(NULL,
                                                    style,
                                                    kCFStringEncodingUTF8);
  const void* keys[] = { kCTFontFamilyNameAttribute,
                         kCTFontStyleNameAttribute   };
  const void* values[] = { family_ref, style_ref };
  CFDictionaryRef attributes = CFDictionaryCreate(NULL, keys, values, 2,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);

  CTFontDescriptorRef descriptor
    = CTFontDescriptorCreateWithAttributes(attributes);
  CTFontRef font = CTFontCreateWithFontDescriptor(descriptor, size, NULL);

  CFRelease(descriptor);
  CFRelease(attributes);
  CFRelease(style_ref);
  CFRelease(family_ref);
  return font;
}

void island_open(struct island* island, const char* family) {
  island->scroll_offset = 0;
  island->scroll_target = 0;
  island->previous_service = island->service;
  island->previous_scroll_offset = 0;
  island->transition = 1.0;
  if (g_loaded) return;

  if (!family) family = font_default_family();
  g_has_icons = font_default_family_installed();

  g_fonts.caption = island_create_font(family, "Regular", 9);
  g_fonts.body = island_create_font(family, "Regular", 10);
  g_fonts.title = island_create_font(family, "Bold", 11);
  g_fonts.icon = g_has_icons
                 ? island_create_font(FONT_DEFAULT_FAMILY,
                                      "Regular",
                                      ISLAND_ICON_SIZE    )
                 : island_create_font(family, "Bold", 11);

  island_load_notifications();
  g_loaded = true;
}

void island_release(void) {
  if (!g_loaded) return;

  CTFontRef* fonts[] = { &g_fonts.caption, &g_fonts.body, &g_fonts.title,
                         &g_fonts.icon                                   };

  for (int i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
    if (*fonts[i]) CFRelease(*fonts[i]);
    *fonts[i] = NULL;
  }

  island_unload_notifications();
  g_loaded = false;
}

// ---------------------------------------------------------------- layout

struct island_layout {
  CGRect content;
  CGRect services[ISLAND_SERVICE_COUNT];
  CGRect divider;
  CGRect list;
  CGRect scroll_track;
};

static struct island_layout island_get_layout(CGRect panel) {
  struct island_layout layout;

  // Without the icon font the bottom edge holds the install hint instead of
  // padding.
  CGFloat bottom = g_has_icons ? ISLAND_PADDING : ISLAND_NOTICE_HEIGHT;
  layout.content = (CGRect){{ panel.origin.x + ISLAND_PADDING,
                              panel.origin.y + bottom          },
                            { panel.size.width - 2*ISLAND_PADDING,
                              panel.size.height - bottom
                                                - ISLAND_PADDING_TOP }};

  CGFloat top = CGRectGetMaxY(layout.content);
  for (int i = 0; i < ISLAND_SERVICE_COUNT; i++) {
    layout.services[i] = (CGRect){{ layout.content.origin.x,
                                    top - (i + 1)*ISLAND_SERVICE_HEIGHT
                                        - i*ISLAND_SERVICE_GAP          },
                                  { ISLAND_SERVICE_WIDTH,
                                    ISLAND_SERVICE_HEIGHT }};
  }

  CGFloat divider_x = layout.content.origin.x + ISLAND_SERVICE_WIDTH
                                              + ISLAND_DIVIDER_GAP;
  layout.divider = (CGRect){{ divider_x, layout.content.origin.y },
                            { 1, layout.content.size.height }};

  layout.scroll_track = (CGRect){{ CGRectGetMaxX(layout.content)
                                   - ISLAND_SCROLL_WIDTH,
                                   layout.content.origin.y      },
                                 { ISLAND_SCROLL_WIDTH,
                                   layout.content.size.height }};

  CGFloat list_x = divider_x + 1 + ISLAND_DIVIDER_GAP;
  layout.list = (CGRect){{ list_x, layout.content.origin.y },
                         { layout.scroll_track.origin.x - ISLAND_SCROLL_GAP
                                                        - list_x,
                           layout.content.size.height                  }};
  return layout;
}

static CGFloat island_list_height() {
  if (g_notification_count == 0) return 0;
  return g_notification_count * ISLAND_CARD_HEIGHT
         + (g_notification_count - 1) * ISLAND_CARD_GAP;
}

static CGFloat island_max_scroll(struct island_layout* layout) {
  return max(island_list_height() - layout->list.size.height, 0);
}

// The area a service page occupies: the list plus its scroll indicator.
static CGRect island_page_rect(struct island_layout* layout) {
  return (CGRect){{ layout->list.origin.x, layout->content.origin.y },
                  { CGRectGetMaxX(layout->content) - layout->list.origin.x,
                    layout->content.size.height                          }};
}

static CGRect island_card_rect(struct island_layout* layout,
                               uint32_t index,
                               CGFloat scroll_offset        ) {
  CGFloat top = CGRectGetMaxY(layout->list)
                - index * (ISLAND_CARD_HEIGHT + ISLAND_CARD_GAP)
                + scroll_offset;

  return (CGRect){{ layout->list.origin.x, top - ISLAND_CARD_HEIGHT },
                  { layout->list.size.width, ISLAND_CARD_HEIGHT     }};
}

// ---------------------------------------------------------------- drawing

static void island_add_rounded_rect(CGContextRef context,
                                    CGRect rect,
                                    CGFloat radius      ) {
  radius = min(radius, min(rect.size.width, rect.size.height) / 2.0);
  CGPathRef path = CGPathCreateWithRoundedRect(rect, radius, radius, NULL);
  CGContextAddPath(context, path);
  CGPathRelease(path);
}

enum island_align { ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTER };

// Draws one line of white text and returns its width. Text wider than
// `max_width` is truncated with an ellipsis. `x` is the left edge, right edge
// or center of the text depending on `align`.
static CGFloat island_draw_text(CGContextRef context,
                                const char* text,
                                CTFontRef font,
                                CGFloat alpha,
                                CGFloat x,
                                CGFloat baseline,
                                CGFloat max_width,
                                enum island_align align) {
  if (!text || !font || max_width <= 0) return 0;

  CFStringRef string = CFStringCreateWithCString(NULL,
                                                 text,
                                                 kCFStringEncodingUTF8);
  if (!string) return 0;

  const void* keys[] = { kCTFontAttributeName,
                         kCTForegroundColorFromContextAttributeName };
  const void* values[] = { font, kCFBooleanTrue };
  CFDictionaryRef attributes = CFDictionaryCreate(NULL, keys, values, 2,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);

  CFAttributedStringRef attributed = CFAttributedStringCreate(NULL,
                                                              string,
                                                              attributes);
  CTLineRef line = CTLineCreateWithAttributedString(attributed);
  CGFloat width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);

  if (width > max_width) {
    CFAttributedStringRef ellipsis_string
      = CFAttributedStringCreate(NULL, CFSTR("…"), attributes);
    CTLineRef ellipsis = CTLineCreateWithAttributedString(ellipsis_string);
    CTLineRef truncated = CTLineCreateTruncatedLine(line,
                                                    max_width,
                                                    kCTLineTruncationEnd,
                                                    ellipsis             );
    if (truncated) {
      CFRelease(line);
      line = truncated;
      width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    }
    CFRelease(ellipsis);
    CFRelease(ellipsis_string);
  }

  CGContextSetRGBFillColor(context, 1, 1, 1, alpha);
  CGContextSetTextMatrix(context, CGAffineTransformIdentity);
  if (align == ALIGN_RIGHT) x -= width;
  else if (align == ALIGN_CENTER) x -= width / 2.0;
  CGContextSetTextPosition(context, x, baseline);
  CTLineDraw(line, context);

  CFRelease(line);
  CFRelease(attributed);
  CFRelease(attributes);
  CFRelease(string);
  return width;
}

// Approximation of Apple's liquid glass material: a translucent fill, a
// top-down highlight gradient and a thin bright rim. A real NSGlassEffectView
// can't be used because the bar is drawn straight into a SkyLight window.
static void island_draw_glass(CGContextRef context,
                              CGRect rect,
                              CGFloat radius,
                              CGFloat strength ) {
  CGContextSaveGState(context);

  island_add_rounded_rect(context, rect, radius);
  CGContextSetRGBFillColor(context, 1, 1, 1, 0.07 * strength);
  CGContextFillPath(context);

  island_add_rounded_rect(context, rect, radius);
  CGContextClip(context);
  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGFloat components[] = { 1, 1, 1, 0.16 * strength,
                           1, 1, 1, 0.02 * strength };
  CGFloat locations[] = { 0, 1 };
  CGGradientRef gradient = CGGradientCreateWithColorComponents(color_space,
                                                               components,
                                                               locations,
                                                               2           );
  CGContextDrawLinearGradient(context,
                              gradient,
                              CGPointMake(0, CGRectGetMaxY(rect)),
                              CGPointMake(0, CGRectGetMidY(rect)),
                              kCGGradientDrawsAfterEndLocation    );
  CGGradientRelease(gradient);
  CGColorSpaceRelease(color_space);
  CGContextRestoreGState(context);

  CGContextSaveGState(context);
  island_add_rounded_rect(context, CGRectInset(rect, 0.375, 0.375), radius);
  CGContextSetRGBStrokeColor(context, 1, 1, 1, 0.22 * strength);
  CGContextSetLineWidth(context, 0.75);
  CGContextStrokePath(context);
  CGContextRestoreGState(context);
}

static double island_transition_progress(struct island* island) {
  if (island->previous_service == island->service) return 1.0;
  return max(min(island->transition, 1.0), 0.0);
}

static void island_draw_services(struct island* island,
                                 CGContextRef context,
                                 struct island_layout* layout) {
  // The selection pill glides from the previous row to the new one.
  double t = island_transition_progress(island);
  CGRect from = layout->services[island->previous_service];
  CGRect pill = layout->services[island->service];
  pill.origin.y = from.origin.y + (pill.origin.y - from.origin.y) * t;
  island_draw_glass(context, pill, 7, 1.0);

  for (int i = 0; i < ISLAND_SERVICE_COUNT; i++) {
    CGRect row = layout->services[i];

    // How "selected" the row is right now, so the icons cross-fade too.
    double weight = 0;
    if (i == island->service) weight += t;
    if (i == island->previous_service) weight += 1.0 - t;

    // Center the glyph vertically on its line box.
    CGFloat baseline = CGRectGetMidY(row)
                       - (CTFontGetAscent(g_fonts.icon)
                          - CTFontGetDescent(g_fonts.icon)) / 2.0;

    island_draw_text(context,
                     g_has_icons ? g_services[i].icon : g_services[i].fallback,
                     g_fonts.icon,
                     0.5 + 0.5 * weight,
                     CGRectGetMidX(row),
                     baseline,
                     row.size.width,
                     ALIGN_CENTER                                            );

    // Unread dot instead of a count, the column is only an icon wide.
    if (i == ISLAND_SERVICE_ALERTS && g_notification_count > 0) {
      CGContextSetRGBFillColor(context, 1.0, 0.36, 0.33, 0.95);
      CGContextFillEllipseInRect(context,
                                 CGRectMake(CGRectGetMaxX(row) - 7,
                                            CGRectGetMaxY(row) - 7,
                                            4, 4                   ));
    }
  }

  CGContextSetRGBFillColor(context, 1, 1, 1, 0.14);
  CGContextFillRect(context, layout->divider);
}

static void island_draw_card(CGContextRef context,
                             const struct island_notification* notification,
                             CGRect card                                    ) {
  island_draw_glass(context, card, ISLAND_CARD_RADIUS, 1.0);

  CGFloat x = card.origin.x + ISLAND_CARD_INSET;
  CGFloat right = CGRectGetMaxX(card) - ISLAND_CARD_INSET;
  CGFloat width = right - x;
  CGFloat top = CGRectGetMaxY(card);

  CGFloat time_width = island_draw_text(context, notification->time,
                                        g_fonts.caption, 0.5,
                                        right, top - 13, width, ALIGN_RIGHT);

  island_draw_text(context, notification->app, g_fonts.caption, 0.55,
                   x, top - 13, width - time_width - 6, ALIGN_LEFT);
  island_draw_text(context, notification->title, g_fonts.title, 0.95,
                   x, top - 27, width, ALIGN_LEFT);
  island_draw_text(context, notification->body, g_fonts.body, 0.7,
                   x, top - 40, width, ALIGN_LEFT);
}

static void island_draw_scroll_indicator(CGContextRef context,
                                         struct island_layout* layout,
                                         CGFloat scroll_offset       ) {
  CGFloat max_scroll = island_max_scroll(layout);
  if (max_scroll <= 0) return;

  CGRect track = layout->scroll_track;
  island_add_rounded_rect(context, track, track.size.width / 2.0);
  CGContextSetRGBFillColor(context, 1, 1, 1, 0.10);
  CGContextFillPath(context);

  CGFloat visible = layout->list.size.height / island_list_height();
  CGFloat thumb_height = max(track.size.height * visible, ISLAND_SCROLL_MIN);
  CGFloat progress = scroll_offset / max_scroll;

  CGRect thumb = track;
  thumb.size.height = thumb_height;
  thumb.origin.y = CGRectGetMaxY(track) - thumb_height
                   - (track.size.height - thumb_height) * progress;

  island_add_rounded_rect(context, thumb, track.size.width / 2.0);
  CGContextSetRGBFillColor(context, 1, 1, 1, 0.55);
  CGContextFillPath(context);
}

static void island_draw_placeholder(CGContextRef context,
                                    struct island_layout* layout,
                                    const char* text            ) {
  island_draw_text(context, text, g_fonts.body, 0.45,
                   CGRectGetMidX(layout->list),
                   CGRectGetMidY(layout->list) - 3,
                   layout->list.size.width,
                   ALIGN_CENTER                    );
}

static void island_draw_alerts(CGContextRef context,
                               struct island_layout* layout,
                               CGFloat scroll_offset       ) {
  if (g_notification_count == 0) {
    island_draw_placeholder(context, layout, "No notifications");
    return;
  }

  CGContextSaveGState(context);
  CGContextClipToRect(context, layout->list);
  for (uint32_t i = 0; i < g_notification_count; i++) {
    CGRect card = island_card_rect(layout, i, scroll_offset);
    if (!CGRectIntersectsRect(card, layout->list)) continue;
    island_draw_card(context, &g_notifications[i], card);
  }
  CGContextRestoreGState(context);

  island_draw_scroll_indicator(context, layout, scroll_offset);
}

static void island_draw_page(CGContextRef context,
                             struct island_layout* layout,
                             int service,
                             CGFloat scroll_offset       ) {
  switch (service) {
    case ISLAND_SERVICE_ALERTS:
      island_draw_alerts(context, layout, scroll_offset);
      break;
    case ISLAND_SERVICE_MUSIC:
      island_draw_placeholder(context, layout, "Music isn't connected yet");
      break;
    case ISLAND_SERVICE_CALENDAR:
      island_draw_placeholder(context, layout, "Calendar isn't connected yet");
      break;
  }
}

// Draws one page shifted vertically by `dy` with the given alpha.
static void island_draw_page_shifted(CGContextRef context,
                                     struct island_layout* layout,
                                     int service,
                                     CGFloat scroll_offset,
                                     CGFloat dy,
                                     CGFloat alpha               ) {
  CGContextSaveGState(context);
  CGContextTranslateCTM(context, 0, dy);
  CGContextSetAlpha(context, alpha);
  island_draw_page(context, layout, service, scroll_offset);
  CGContextRestoreGState(context);
}

static void island_draw_pages(struct island* island,
                              CGContextRef context,
                              struct island_layout* layout,
                              CGFloat alpha               ) {
  CGRect page = island_page_rect(layout);
  CGContextSaveGState(context);
  CGContextClipToRect(context, page);

  double t = island_transition_progress(island);
  if (t >= 1.0) {
    island_draw_page(context, layout, island->service, island->scroll_offset);
  } else {
    // A lower service (higher index) pushes the pages up (y-up coordinates:
    // positive dy), an upper one pushes them down.
    CGFloat direction = island->service > island->previous_service ? 1 : -1;
    CGFloat distance = page.size.height + ISLAND_PAGE_GAP;
    CGFloat fade = 1.0 - ISLAND_PAGE_MIN_ALPHA;

    island_draw_page_shifted(context, layout,
                             island->previous_service,
                             island->previous_scroll_offset,
                             direction * distance * t,
                             alpha * (1.0 - fade * t)       );

    island_draw_page_shifted(context, layout,
                             island->service,
                             island->scroll_offset,
                             -direction * distance * (1.0 - t),
                             alpha * (ISLAND_PAGE_MIN_ALPHA + fade * t));
  }

  CGContextRestoreGState(context);
}

void island_draw(struct island* island,
                 CGContextRef context,
                 CGRect panel,
                 CGRect clip,
                 CGFloat corner_radius,
                 double progress                    ) {
  if (!g_loaded || panel.size.height <= 0) return;

  double alpha = (progress - ISLAND_FADE_START) / (1.0 - ISLAND_FADE_START);
  if (alpha <= 0) return;
  if (alpha > 1) alpha = 1;

  struct island_layout layout = island_get_layout(panel);

  CGContextSaveGState(context);
  island_add_rounded_rect(context, clip, corner_radius);
  CGContextClip(context);
  CGContextSetAlpha(context, alpha);

  island_draw_services(island, context, &layout);
  island_draw_pages(island, context, &layout, alpha);

  if (!g_has_icons) {
    island_draw_text(context,
                     "Install " FONT_DEFAULT_FAMILY " for the best look",
                     g_fonts.caption,
                     0.4,
                     CGRectGetMidX(panel),
                     panel.origin.y + 5,
                     panel.size.width - 2*ISLAND_PADDING,
                     ALIGN_CENTER                                        );
  }

  CGContextRestoreGState(context);
}

// ---------------------------------------------------------------- input

bool island_handle_click(struct island* island, CGRect panel, CGPoint point) {
  if (!g_loaded) return false;
  struct island_layout layout = island_get_layout(panel);

  for (int i = 0; i < ISLAND_SERVICE_COUNT; i++) {
    if (CGRectContainsPoint(layout.services[i], point)) {
      if (island->service != i) {
        // Starts a page transition; the bar animates `transition` to 1.
        island->previous_service = island->service;
        island->previous_scroll_offset = island->scroll_offset;
        island->service = i;
        island->scroll_offset = 0;
        island->scroll_target = 0;
        island->transition = 0;
      }
      return true;
    }
  }

  // Cards swallow clicks for now so that reading them doesn't close the
  // island. Opening the source app can hook in here later.
  if (island->service == ISLAND_SERVICE_ALERTS
      && CGRectContainsPoint(layout.list, point)) {
    for (uint32_t i = 0; i < g_notification_count; i++) {
      if (CGRectContainsPoint(island_card_rect(&layout, i,
                                               island->scroll_offset),
                              point                                  ))
        return true;
    }
  }

  return false;
}

bool island_handle_scroll(struct island* island,
                          CGRect panel,
                          CGPoint point,
                          CGFloat delta          ) {
  if (!g_loaded || !CGRectContainsPoint(panel, point)) return false;
  if (island->service != ISLAND_SERVICE_ALERTS) return true;

  struct island_layout layout = island_get_layout(panel);
  CGFloat target = island->scroll_target - delta;
  island->scroll_target = max(min(target, island_max_scroll(&layout)), 0);
  return true;
}
