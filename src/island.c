#include "island.h"
#include "font.h"

// Layout (points)
#define ISLAND_PADDING        10
#define ISLAND_PADDING_TOP    4
#define ISLAND_SERVICE_WIDTH  24
#define ISLAND_SERVICE_HEIGHT 22
#define ISLAND_ICON_SIZE      13
#define ISLAND_NOTICE_HEIGHT  16
#define ISLAND_SERVICE_GAP    3
#define ISLAND_DIVIDER_GAP    7
#define ISLAND_SCROLL_WIDTH   3
#define ISLAND_SCROLL_GAP     5
#define ISLAND_SCROLL_MIN     14
#define ISLAND_CARD_HEIGHT    46
// A card with Claude's suggested next step: Yes / No stacked on the left,
// up to three lines of the suggestion.
#define ISLAND_SUGGEST_CARD   60
#define ISLAND_SUGGEST_LINES  3
#define ISLAND_CARD_GAP       6
#define ISLAND_CARD_RADIUS    10
#define ISLAND_CARD_INSET     9
#define ISLAND_PAGE_GAP       12

// Alerts tab header ("N unread" and the Read all button) above the cards.
#define ISLAND_ALERTS_HEADER  18
#define ISLAND_ALERTS_GAP     4
#define ISLAND_READ_ALL_WIDTH 58
#define ISLAND_READ_ALL_HEIGHT 16

// Alerts card animations: a new card starts ENTER points above its place
// and fades in over APPEAR seconds; others glide to their place (TAU: time
// constant); a closed card slides EXIT points right and fades over LEAVE
// seconds, "Read all" staggers that by STAGGER seconds per card.
#define ISLAND_CARD_ENTER      12
#define ISLAND_CARD_APPEAR     0.28
#define ISLAND_CARD_TAU        0.06
#define ISLAND_CARD_EXIT       40
#define ISLAND_CARD_LEAVE      0.24
#define ISLAND_CARD_STAGGER    0.04
// The close button on notification cards.
#define ISLAND_CARD_CLOSE      14

// App icon on notification cards.
#define ISLAND_APP_ICON_SIZE   30
#define ISLAND_APP_ICON_RADIUS 8
#define ISLAND_APP_ICON_GAP    8
#define ISLAND_APP_GLYPH_SIZE  15
#define ISLAND_APP_LETTER_SIZE 16

// Peek text in the closed island.
#define ISLAND_PEEK_FONT       12

// Notch band: date (dd / mm / yy) on the left, battery/CPU/RAM on the right,
// three rows each.
#define ISLAND_DATE_ROWS      3
#define ISLAND_STATS_INSET_X  6
#define ISLAND_STATS_INSET_Y  5
#define ISLAND_STATS_FONT     8

// Music tab (inside the list area, top to bottom): artwork + track info,
// transport buttons, volume slider, output device.
#define ISLAND_ARTWORK_SIZE   48
#define ISLAND_ARTWORK_RADIUS 8
#define ISLAND_MUSIC_GAP      10
#define ISLAND_PLAY_SIZE      30
#define ISLAND_SKIP_SIZE      26
#define ISLAND_SKIP_SPACING   12
#define ISLAND_VOLUME_HEIGHT  16
#define ISLAND_SLIDER_HEIGHT  4
#define ISLAND_SLIDER_KNOB    9
#define ISLAND_VOLUME_LABEL   34

// System tab: an info card (OS, hardware, network) on top, below it one glass
// card per graph (CPU, RAM) side by side.
#define ISLAND_INFO_HEIGHT    34
#define ISLAND_INFO_ICON      14
#define ISLAND_GRAPH_GAP      6
#define ISLAND_GRAPH_HEADER   19
#define ISLAND_GRAPH_LINE     1.25

// Bluetooth tab: a header, device rows below, the result banner. Switches
// and settings buttons (Bluetooth header, System tab's Wi-Fi row).
#define ISLAND_COLUMN_HEADER  18
#define ISLAND_ROW_HEIGHT     19
#define ISLAND_TOGGLE_WIDTH   22
#define ISLAND_TOGGLE_HEIGHT  12
// How much wider the switch knob gets halfway through its slide.
#define ISLAND_TOGGLE_STRETCH 5
#define ISLAND_SETTINGS_SIZE  16
#define ISLAND_BATTERY_ICON   16
#define ISLAND_CONTROL_GAP    8
#define ISLAND_NOTICE_BAR     22

// Claude tab: the two subtabs' switch on top, rows of glass cards below.
#define ISLAND_CLAUDE_TABS    18
#define ISLAND_CLAUDE_ROW     32   // row pitch, the card is ROW - ROW_GAP
#define ISLAND_CLAUDE_ROW_GAP 4
#define ISLAND_CLAUDE_CLOSE   14
// Claude's terracotta, for its spark and Clawd.
#define ISLAND_COLOR_CLAUDE   0xffd97757

// Apps tab: a header, one row per app (icon, name, switch) below it.
#define ISLAND_APP_ROW        22
#define ISLAND_APP_ROW_ICON   16

// Screens tab: 3 x 2 tiles, the hint line below them when switching can't
// work.
#define ISLAND_SCREEN_COLUMNS 3
#define ISLAND_SCREEN_GAP     6
#define ISLAND_SCREEN_ICON    18
#define ISLAND_SCREEN_HINT    14

// Accent colors (ARGB), the Sonokai palette of FelixKratz's own SketchyBar
// setup. Stats go green -> yellow -> orange -> red as they get worse.
#define ISLAND_COLOR_GREEN    0xff9ed072
#define ISLAND_COLOR_YELLOW   0xffe7c664
#define ISLAND_COLOR_ORANGE   0xfff39660
#define ISLAND_COLOR_RED      0xfffc5d7c
#define ISLAND_COLOR_BLUE     0xff76cce0
#define ISLAND_COLOR_MAGENTA  0xffb39df3
#define ISLAND_COLOR_WHITE    0xffffffff

// During a page transition the outgoing page fades to this alpha and the
// incoming one fades in from it, on top of the slide.
#define ISLAND_PAGE_MIN_ALPHA 0.35

// Content stays invisible for the first part of the expand animation and then
// fades in, so it never looks squeezed while the shape is still small.
#define ISLAND_FADE_START     0.55

// The services column shows Nerd Font icons (Font Awesome: bell, music,
// calendar). Without FuraCode Nerd Font installed those glyphs don't exist,
// so the column falls back to the service's initial. `name` is what
// island_tabs calls it.
static const struct {
  const char* name;
  const char* icon;
  const char* fallback;
} g_services[ISLAND_SERVICE_COUNT] = {
  [ISLAND_SERVICE_ALERTS]    = { "alerts",    "\xEF\x83\xB3", "A" },  // U+F0F3 bell
  [ISLAND_SERVICE_MUSIC]     = { "music",     "\xEF\x80\x81", "M" },  // U+F001
  [ISLAND_SERVICE_SYSTEM]    = { "system",    "\xEF\x87\xBE", "S" },  // U+F1FE area-chart
  [ISLAND_SERVICE_BLUETOOTH] = { "bluetooth", "\xEF\x8A\x93", "B" },  // U+F293 bluetooth
  [ISLAND_SERVICE_SCREENS]   = { "screens",   "\xEF\x80\x89", "D" },  // U+F009 th-large
  [ISLAND_SERVICE_APPS]      = { "apps",      "\xEF\x87\x9E", "N" },  // U+F1DE sliders
  // Drawn as Claude's spark (island_draw_spark), not a glyph.
  [ISLAND_SERVICE_CLAUDE]    = { "claude",    NULL,           "C" },
};

// The tabs as configured (see island_set_tabs), and the ones the column
// shows: those whose plugin is on (see island_plugins_changed).
static int g_configured[ISLAND_SERVICE_COUNT] = { ISLAND_SERVICE_ALERTS,
                                                  ISLAND_SERVICE_CLAUDE,
                                                  ISLAND_SERVICE_MUSIC,
                                                  ISLAND_SERVICE_SYSTEM,
                                                  ISLAND_SERVICE_BLUETOOTH,
                                                  ISLAND_SERVICE_APPS       };
static int g_configured_count = 6;
static int g_tabs[ISLAND_SERVICE_COUNT] = { ISLAND_SERVICE_ALERTS,
                                            ISLAND_SERVICE_CLAUDE,
                                            ISLAND_SERVICE_MUSIC,
                                            ISLAND_SERVICE_SYSTEM,
                                            ISLAND_SERVICE_BLUETOOTH,
                                            ISLAND_SERVICE_APPS       };
static int g_tab_count = 6;

static bool island_service_enabled(int service) {
  switch (service) {
    case ISLAND_SERVICE_ALERTS:
      return notifications_capture()
             || claude_plugin(CLAUDE_PLUGIN_NOTIFICATIONS);
    case ISLAND_SERVICE_APPS:
      return notifications_capture();
    case ISLAND_SERVICE_CLAUDE:
      return claude_plugin(CLAUDE_PLUGIN);
    default:
      return true;
  }
}

void island_plugins_changed(void) {
  g_tab_count = 0;
  for (int i = 0; i < g_configured_count; i++) {
    if (island_service_enabled(g_configured[i]))
      g_tabs[g_tab_count++] = g_configured[i];
  }
  // Every configured tab switched off: show them anyway rather than nothing.
  if (g_tab_count == 0) {
    memcpy(g_tabs, g_configured, g_configured_count * sizeof(int));
    g_tab_count = g_configured_count;
  }
}

bool island_set_tabs(const char* list, char* error, size_t size) {
  int tabs[ISLAND_SERVICE_COUNT];
  int count = 0;
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "%s", list ? list : "");

  char* cursor = buffer;
  char* part;
  while ((part = strsep(&cursor, ",")) != NULL) {
    while (*part == ' ') part++;
    char* end = part + strlen(part);
    while (end > part && end[-1] == ' ') *--end = '\0';
    if (!*part) continue;

    int service = -1;
    for (int i = 0; i < ISLAND_SERVICE_COUNT; i++) {
      if (strcasecmp(part, g_services[i].name) == 0) service = i;
    }
    if (service < 0) {
      snprintf(error, size, "unknown tab '%s' (alerts, claude, music, "
                            "system, bluetooth, screens, apps)", part  );
      return false;
    }
    bool duplicate = false;
    for (int i = 0; i < count; i++) duplicate |= tabs[i] == service;
    if (!duplicate) tabs[count++] = service;
  }
  if (count == 0) {
    snprintf(error, size, "no tabs given");
    return false;
  }

  memcpy(g_configured, tabs, count * sizeof(int));
  g_configured_count = count;
  island_plugins_changed();
  return true;
}

// Where `service` is in the column, -1 if it isn't shown.
static int island_tab_slot(int service) {
  for (int i = 0; i < g_tab_count; i++) {
    if (g_tabs[i] == service) return i;
  }
  return -1;
}

// After the tabs changed: a service that is no longer shown falls back to
// the first tab.
static void island_fix_service(struct island* island) {
  if (island_tab_slot(island->service) < 0) {
    island->service = g_tabs[0];
    island->scroll_offset = 0;
    island->scroll_target = 0;
    island->transition = 1.0;
  }
  if (island_tab_slot(island->previous_service) < 0)
    island->previous_service = island->service;
}

static struct {
  CTFontRef caption;   // app name, time, install hint
  CTFontRef body;      // notification body
  CTFontRef title;     // notification title
  CTFontRef icon;      // services column
  CTFontRef stats;     // band: stats values
  CTFontRef stats_icon;
  CTFontRef small_icon;  // music: speaker
  CTFontRef screen_icon; // screens: purpose icons
  CTFontRef app_glyph;   // notification cards: app icon glyph
  CTFontRef app_letter;  // ... or letter
  CTFontRef peek;        // peek text in the closed island
  CTFontRef peek_small;  // ... when it takes two lines
  CTFontRef button;      // Yes / No
} g_fonts;

// Music tab glyphs (Font Awesome in the Nerd Font), letters without it.
enum island_glyph {
  GLYPH_PREVIOUS, GLYPH_PLAY, GLYPH_PAUSE, GLYPH_NEXT,
  GLYPH_VOLUME, GLYPH_VOLUME_LOW, GLYPH_MUTED,
  GLYPH_APPLE, GLYPH_WIFI, GLYPH_ETHERNET,
  GLYPH_BLUETOOTH, GLYPH_LOCK, GLYPH_CHECK, GLYPH_CROSS, GLYPH_BACK,
  GLYPH_SUN, GLYPH_SETTINGS, GLYPH_BELL,
  GLYPH_BATTERY_0, GLYPH_BATTERY_1, GLYPH_BATTERY_2, GLYPH_BATTERY_3,
  GLYPH_BATTERY_4, GLYPH_COUNT
};
static const struct {
  const char* glyph;
  const char* fallback;
} g_glyph_sources[GLYPH_COUNT] = {
  [GLYPH_PREVIOUS]   = { "\xEF\x81\x88", "<<" },  // U+F048 step-backward
  [GLYPH_PLAY]       = { "\xEF\x81\x8B", ">"  },  // U+F04B play
  [GLYPH_PAUSE]      = { "\xEF\x81\x8C", "||" },  // U+F04C pause
  [GLYPH_NEXT]       = { "\xEF\x81\x91", ">>" },  // U+F051 step-forward
  [GLYPH_VOLUME]     = { "\xEF\x80\xA8", "V"  },  // U+F028 volume-up
  [GLYPH_VOLUME_LOW] = { "\xEF\x80\xA7", "v"  },  // U+F027 volume-down
  [GLYPH_MUTED]      = { "\xEF\x80\xA6", "M"  },  // U+F026 volume-off
  [GLYPH_APPLE]      = { "\xEF\x85\xB9", ""   },  // U+F179 apple
  [GLYPH_WIFI]       = { "\xEF\x87\xAB", "W"  },  // U+F1EB wifi
  [GLYPH_ETHERNET]   = { "\xEF\x83\xA8", "E"  },  // U+F0E8 sitemap
  [GLYPH_BLUETOOTH]  = { "\xEF\x8A\x93", "B"  },  // U+F293 bluetooth
  [GLYPH_LOCK]       = { "\xEF\x80\xA3", "*"  },  // U+F023 lock
  [GLYPH_CHECK]      = { "\xEF\x80\x8C", "ok" },  // U+F00C check
  [GLYPH_CROSS]      = { "\xEF\x80\x8D", "x"  },  // U+F00D times
  [GLYPH_BACK]       = { "\xEF\x81\x93", "<"  },  // U+F053 chevron-left
  [GLYPH_SUN]        = { "\xEF\x86\x85", "*"  },  // U+F185 sun
  [GLYPH_SETTINGS]   = { "\xEF\x80\x93", "S"  },  // U+F013 cog
  [GLYPH_BELL]       = { "\xEF\x83\xB3", "!"  },  // U+F0F3 bell
  [GLYPH_BATTERY_0]  = { "\xEF\x89\x84", "["  },  // U+F244 battery-empty
  [GLYPH_BATTERY_1]  = { "\xEF\x89\x83", "["  },  // U+F243 battery-quarter
  [GLYPH_BATTERY_2]  = { "\xEF\x89\x82", "["  },  // U+F242 battery-half
  [GLYPH_BATTERY_3]  = { "\xEF\x89\x81", "["  },  // U+F241 battery-3/4
  [GLYPH_BATTERY_4]  = { "\xEF\x89\x80", "["  },  // U+F240 battery-full
};
static const char* g_glyphs[GLYPH_COUNT];

// The live data and island of the island_draw call in progress.
static const struct island_data* g_data = NULL;
static struct island* g_drawing = NULL;

enum island_stat { STAT_BATTERY, STAT_CPU, STAT_RAM, STAT_COUNT };

// Nerd Font glyphs differ between font versions, so each stat lists a few
// code points (newest first) and uses the first one the font has, falling back
// to a letter.
static const struct {
  const char* candidates[3];
  const char* fallback;
} g_stat_icons[STAT_COUNT] = {
  [STAT_BATTERY] = { { "\xEF\x89\x80" }, "B" },                     // U+F240
  // CPU: square chip (md-memory, v3 / v2 code point). RAM: memory module
  // (fa-microchip).
  [STAT_CPU]     = { { "\xF3\xB0\x8D\x9B", "\xEF\xA1\x9A" }, "C" },  // U+F035B, U+F85A
  [STAT_RAM]     = { { "\xEF\x8B\x9B" }, "R" },                     // U+F2DB
};
static const char* g_stat_glyphs[STAT_COUNT];

static bool g_loaded = false;

static bool island_font_has_glyphs(CTFontRef font, const char* text) {
  CFStringRef string = CFStringCreateWithCString(NULL,
                                                 text,
                                                 kCFStringEncodingUTF8);
  if (!string) return false;

  UniChar characters[4];
  CGGlyph glyphs[4];
  CFIndex length = CFStringGetLength(string);
  bool found = length > 0 && length <= 4;
  if (found) {
    CFStringGetCharacters(string, CFRangeMake(0, length), characters);
    found = CTFontGetGlyphsForCharacters(font, characters, glyphs, length);
  }
  CFRelease(string);
  return found;
}

static void island_pick_media_glyphs() {
  for (int i = 0; i < GLYPH_COUNT; i++) {
    g_glyphs[i] = island_font_has_glyphs(g_fonts.icon,
                                         g_glyph_sources[i].glyph)
                  ? g_glyph_sources[i].glyph
                  : g_glyph_sources[i].fallback;
  }
}

static void island_pick_stat_glyphs() {
  for (int i = 0; i < STAT_COUNT; i++) {
    g_stat_glyphs[i] = g_stat_icons[i].fallback;
    for (int j = 0; j < 3 && g_stat_icons[i].candidates[j]; j++) {
      if (island_font_has_glyphs(g_fonts.stats_icon,
                                 g_stat_icons[i].candidates[j])) {
        g_stat_glyphs[i] = g_stat_icons[i].candidates[j];
        break;
      }
    }
  }
}
static bool g_has_icons = false;

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

static void island_pick_app_icon_marks();

void island_open(struct island* island, const char* family) {
  island_fix_service(island);
  island->scroll_offset = 0;
  island->scroll_target = 0;
  island->previous_service = island->service;
  island->previous_scroll_offset = 0;
  island->transition = 1.0;
  island->toggles_ready = false;
  island->cards_synced = false;
  island->app_toggle_progress = 1.0;
  island->claude_previous_subtab = island->claude_subtab;
  island->claude_transition = 1.0;
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
  g_fonts.stats = island_create_font(family, "Regular", ISLAND_STATS_FONT);
  g_fonts.stats_icon = g_has_icons
                       ? island_create_font(FONT_DEFAULT_FAMILY,
                                            "Regular",
                                            ISLAND_STATS_FONT    )
                       : island_create_font(family,
                                            "Bold",
                                            ISLAND_STATS_FONT    );
  g_fonts.small_icon = g_has_icons
                       ? island_create_font(FONT_DEFAULT_FAMILY, "Regular", 11)
                       : island_create_font(family, "Bold", 9);
  g_fonts.screen_icon = g_has_icons
                        ? island_create_font(FONT_DEFAULT_FAMILY, "Regular",
                                             ISLAND_SCREEN_ICON             )
                        : island_create_font(family, "Bold", 13);
  g_fonts.app_glyph = g_has_icons
                      ? island_create_font(FONT_DEFAULT_FAMILY, "Regular",
                                           ISLAND_APP_GLYPH_SIZE          )
                      : island_create_font(family, "Bold",
                                           ISLAND_APP_GLYPH_SIZE);
  g_fonts.app_letter = island_create_font(family, "Bold",
                                          ISLAND_APP_LETTER_SIZE);
  g_fonts.peek = island_create_font(family, "Regular", ISLAND_PEEK_FONT);
  g_fonts.peek_small = island_create_font(family, "Regular",
                                          ISLAND_PEEK_FONT - 1);
  g_fonts.button = island_create_font(family, "Bold", 10);
  island_pick_stat_glyphs();
  island_pick_media_glyphs();
  island_pick_app_icon_marks();

  g_loaded = true;
}

void island_release(void) {
  if (!g_loaded) return;

  CTFontRef* fonts[] = { &g_fonts.caption, &g_fonts.body, &g_fonts.title,
                         &g_fonts.icon, &g_fonts.stats,
                         &g_fonts.stats_icon, &g_fonts.small_icon,
                         &g_fonts.screen_icon, &g_fonts.app_glyph,
                         &g_fonts.app_letter, &g_fonts.peek,
                         &g_fonts.peek_small, &g_fonts.button            };

  for (int i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
    if (*fonts[i]) CFRelease(*fonts[i]);
    *fonts[i] = NULL;
  }

  g_loaded = false;
}

// ---------------------------------------------------------------- layout

// `services` and `service_hits` are per slot (g_tabs order): the selection
// pill, and the whole share of the column that selects it.
struct island_layout {
  CGRect content;
  CGRect services[ISLAND_SERVICE_COUNT];
  CGRect service_hits[ISLAND_SERVICE_COUNT];
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

  // The column is stretched: each tab gets an equal share of its height,
  // its pill centered in it (shorter when many tabs leave less room).
  CGFloat top = CGRectGetMaxY(layout.content);
  CGFloat slot = layout.content.size.height / max(g_tab_count, 1);
  CGFloat pill = max(min(ISLAND_SERVICE_HEIGHT, slot - ISLAND_SERVICE_GAP),
                     ISLAND_ICON_SIZE                                      );
  for (int i = 0; i < g_tab_count; i++) {
    CGFloat mid = top - (i + 0.5) * slot;
    layout.services[i] = (CGRect){{ layout.content.origin.x,
                                    round(mid - pill / 2.0) },
                                  { ISLAND_SERVICE_WIDTH, pill }};
    layout.service_hits[i] = (CGRect){{ layout.content.origin.x,
                                        top - (i + 1) * slot     },
                                      { ISLAND_SERVICE_WIDTH, slot }};
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

// Alerts: a header ("N unread", "Read all") above the cards.
static CGRect island_alerts_header(struct island_layout* layout) {
  return (CGRect){{ layout->list.origin.x,
                    CGRectGetMaxY(layout->list) - ISLAND_ALERTS_HEADER },
                  { layout->list.size.width, ISLAND_ALERTS_HEADER }};
}

static CGRect island_alerts_read_all(struct island_layout* layout) {
  CGRect header = island_alerts_header(layout);
  return (CGRect){{ CGRectGetMaxX(header) - ISLAND_READ_ALL_WIDTH,
                    CGRectGetMidY(header) - ISLAND_READ_ALL_HEIGHT / 2.0 },
                  { ISLAND_READ_ALL_WIDTH, ISLAND_READ_ALL_HEIGHT }};
}

// The area the cards scroll in.
static CGRect island_alerts_list(struct island_layout* layout) {
  CGRect list = layout->list;
  list.size.height -= ISLAND_ALERTS_HEADER + ISLAND_ALERTS_GAP;
  return list;
}

// Cards are taller while they hold a suggestion that can be answered.
static CGFloat island_card_height(const struct notification_item* item) {
  return claude_suggestion_active(item) ? ISLAND_SUGGEST_CARD
                                        : ISLAND_CARD_HEIGHT;
}

static CGFloat island_list_height() {
  const struct notification_list* list = notifications_get();
  if (list->count == 0) return 0;
  CGFloat height = (list->count - 1) * ISLAND_CARD_GAP;
  for (int i = 0; i < list->count; i++)
    height += island_card_height(&list->items[i]);
  return height;
}

static CGFloat island_max_scroll(struct island_layout* layout) {
  return max(island_list_height() - island_alerts_list(layout).size.height,
             0                                                           );
}

// The area a service page occupies: the list plus its scroll indicator.
static CGRect island_page_rect(struct island_layout* layout) {
  return (CGRect){{ layout->list.origin.x, layout->content.origin.y },
                  { CGRectGetMaxX(layout->content) - layout->list.origin.x,
                    layout->content.size.height                          }};
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

static void island_set_fill(CGContextRef context, uint32_t argb, CGFloat alpha) {
  CGContextSetRGBFillColor(context,
                           ((argb >> 16) & 0xff) / 255.0,
                           ((argb >> 8) & 0xff) / 255.0,
                           (argb & 0xff) / 255.0,
                           ((argb >> 24) & 0xff) / 255.0 * alpha);
}

// Draws one line of text in `color` (ARGB, times `alpha`) and returns its
// width. Text wider than `max_width` is truncated with an ellipsis. `x` is the
// left edge, right edge or center of the text depending on `align`.
static CGFloat island_draw_text_colored(CGContextRef context,
                                const char* text,
                                CTFontRef font,
                                uint32_t color,
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

  island_set_fill(context, color, alpha);
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

// White text, see island_draw_text_colored.
static CGFloat island_draw_text(CGContextRef context,
                                const char* text,
                                CTFontRef font,
                                CGFloat alpha,
                                CGFloat x,
                                CGFloat baseline,
                                CGFloat max_width,
                                enum island_align align) {
  return island_draw_text_colored(context, text, font, ISLAND_COLOR_WHITE,
                                  alpha, x, baseline, max_width, align    );
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

// ---------------------------------------------------------------- claude art

// Claude's spark: rays of slightly different lengths around the center.
static void island_draw_spark(CGContextRef context,
                              CGPoint center,
                              CGFloat radius,
                              uint32_t color,
                              CGFloat alpha  ) {
  static const double lengths[] = { 1.0, 0.8, 0.94, 0.76, 1.0, 0.84,
                                    0.92, 0.78, 0.98, 0.82, 0.9      };
  int rays = sizeof(lengths) / sizeof(lengths[0]);

  CGContextSaveGState(context);
  CGContextSetRGBStrokeColor(context,
                             ((color >> 16) & 0xff) / 255.0,
                             ((color >> 8) & 0xff) / 255.0,
                             (color & 0xff) / 255.0,
                             ((color >> 24) & 0xff) / 255.0 * alpha);
  CGContextSetLineCap(context, kCGLineCapRound);
  CGContextSetLineWidth(context, max(radius * 0.22, 1.0));
  for (int i = 0; i < rays; i++) {
    double angle = M_PI / 2.0 + i * 2.0 * M_PI / rays;
    CGFloat inner = radius * 0.15;
    CGFloat outer = radius * lengths[i] - radius * 0.11;
    CGContextMoveToPoint(context, center.x + inner * cos(angle),
                                  center.y + inner * sin(angle) );
    CGContextAddLineToPoint(context, center.x + outer * cos(angle),
                                     center.y + outer * sin(angle) );
  }
  CGContextStrokePath(context);
  CGContextRestoreGState(context);
}

// Clawd, Claude Code's mascot, as in its terminal banner: a 16 x 5 grid of
// tall pixels (quarter blocks are about twice as tall as wide), top row
// first. Rows: head, eyes (holes at 4 and 11), arms, body, legs.
enum clawd_mood { CLAWD_IDLE, CLAWD_BUSY, CLAWD_HAPPY, CLAWD_SLEEPING };
#define CLAWD_COLUMNS 16
#define CLAWD_ROWS    5
#define CLAWD_ASPECT  1.8

static bool g_animated = false;

bool island_animating(void) {
  return g_animated;
}

void island_animation_reset(void) {
  g_animated = false;
}

static void island_clawd_cell(CGContextRef context,
                              CGPoint top_left,
                              CGFloat width,
                              CGFloat height,
                              CGFloat column,
                              CGFloat row      ) {
  // A hair of overlap so neighbouring cells don't show seams.
  CGContextFillRect(context, CGRectMake(top_left.x + column * width - 0.15,
                                        top_left.y - (row + 1) * height
                                                   - 0.15,
                                        width + 0.3, height + 0.3       ));
}

// Draws Clawd as large as fits in `rect`. Every mood but IDLE moves: BUSY
// walks, HAPPY bounces and waves, SLEEPING breathes with closed eyes and
// floating z's. Moving ones mark the island as animating.
static void island_draw_clawd(CGContextRef context,
                              CGRect rect,
                              int mood,
                              CGFloat alpha       ) {
  double t = CFAbsoluteTimeGetCurrent();
  int frame = (int)(t * ISLAND_SPRITE_FPS);
  if (mood != CLAWD_IDLE) g_animated = true;

  // Room above for the bounce (happy) or the z's (sleeping).
  CGFloat room = mood == CLAWD_HAPPY ? 0.8 : (mood == CLAWD_SLEEPING ? 1.2 : 0);
  CGFloat width = min(rect.size.width / CLAWD_COLUMNS,
                      rect.size.height / ((CLAWD_ROWS + room) * CLAWD_ASPECT));
  CGFloat height = width * CLAWD_ASPECT;
  CGFloat total = (CLAWD_ROWS + room) * height;
  CGPoint top_left = { CGRectGetMidX(rect) - CLAWD_COLUMNS * width / 2.0,
                       CGRectGetMidY(rect) + total / 2.0 - room * height };

  if (mood == CLAWD_HAPPY) {
    top_left.y += fabs(sin(t * M_PI * 2.2)) * height * 0.8;
  } else if (mood == CLAWD_SLEEPING) {
    top_left.y += sin(t * 2.0 * M_PI / 3.0) * height * 0.12;
  }

  CGContextSaveGState(context);
  island_set_fill(context, ISLAND_COLOR_CLAUDE, alpha);

  // Head and body.
  for (int c = 2; c <= 13; c++) {
    island_clawd_cell(context, top_left, width, height, c, 0);
    island_clawd_cell(context, top_left, width, height, c, 3);
    bool eye = c == 4 || c == 11;
    if (!eye || mood == CLAWD_SLEEPING)
      island_clawd_cell(context, top_left, width, height, c, 1);
    island_clawd_cell(context, top_left, width, height, c, 2);
  }

  // Arms: out to the sides; resting low while asleep; waving when happy.
  int left_row = 2, right_row = 2;
  if (mood == CLAWD_SLEEPING) left_row = right_row = 3;
  if (mood == CLAWD_HAPPY) {
    bool swap = (frame / 2) % 2;
    left_row = swap ? 1 : 2;
    right_row = swap ? 2 : 1;
  }
  for (int c = 0; c < 2; c++) {
    island_clawd_cell(context, top_left, width, height, c, left_row);
    island_clawd_cell(context, top_left, width, height, 14 + c, right_row);
  }

  // Legs, stepping while busy.
  static const int legs[2][4] = { { 3, 5, 10, 12 }, { 2, 4, 11, 13 } };
  int step = mood == CLAWD_BUSY ? (frame / 2) % 2 : 0;
  for (int i = 0; i < 4; i++)
    island_clawd_cell(context, top_left, width, height, legs[step][i], 4);

  // Closed eyes: a dark line low in each eye cell.
  if (mood == CLAWD_SLEEPING) {
    island_set_fill(context, 0xff1a1a1a, alpha);
    for (int i = 0; i < 2; i++) {
      CGFloat column = i == 0 ? 4 : 11;
      CGContextFillRect(context,
                        CGRectMake(top_left.x + column * width - width * 0.3,
                                   top_left.y - 2 * height + height * 0.25,
                                   width * 1.6, max(height * 0.18, 0.75)   ));
    }
  }

  // Happy: two sparkles blinking in turn above the arms.
  if (mood == CLAWD_HAPPY) {
    for (int i = 0; i < 2; i++) {
      double blink = 0.5 + 0.5 * sin(t * 6.0 + i * M_PI);
      CGFloat size = width * (0.8 + 0.6 * blink);
      CGPoint at = { top_left.x + (i == 0 ? -0.5 : 16.5) * width,
                     top_left.y + height * 0.4                   };
      island_set_fill(context, 0xffffe08a, alpha * blink);
      CGContextFillRect(context, CGRectMake(at.x - size / 2.0, at.y - width * 0.2,
                                            size, width * 0.4                  ));
      CGContextFillRect(context, CGRectMake(at.x - width * 0.2, at.y - size / 2.0,
                                            width * 0.4, size                  ));
    }
  }
  CGContextRestoreGState(context);

  // Sleeping: z's rising from the head, growing and fading.
  if (mood == CLAWD_SLEEPING && width >= 2.5) {
    for (int i = 0; i < 3; i++) {
      double phase = fmod(t / 2.4 + i / 3.0, 1.0);
      CGFloat x = top_left.x + (13.5 + phase * 3.0) * width;
      CGFloat y = top_left.y + phase * height * 1.4;
      island_draw_text_colored(context, "z",
                               phase < 0.5 ? g_fonts.caption : g_fonts.body,
                               ISLAND_COLOR_WHITE,
                               alpha * 0.8 * sin(phase * M_PI),
                               x, y, 20, ALIGN_LEFT              );
    }
  }
}

// Clawd on a dark glass tile, where other notifications show an app icon.
static void island_draw_clawd_tile(CGContextRef context, CGRect rect, int mood) {
  island_add_rounded_rect(context, rect, ISLAND_APP_ICON_RADIUS);
  island_set_fill(context, 0xff1f1a17, 1.0);
  CGContextFillPath(context);
  island_draw_glass(context, rect, ISLAND_APP_ICON_RADIUS, 1.0);
  island_draw_clawd(context, CGRectInset(rect, rect.size.width * 0.1,
                                         rect.size.height * 0.12     ),
                    mood, 1.0                                         );
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
  int slot = island_tab_slot(island->service);
  int previous = island_tab_slot(island->previous_service);
  if (slot < 0) return;
  if (previous < 0) previous = slot;
  CGRect from = layout->services[previous];
  CGRect pill = layout->services[slot];
  pill.origin.y = from.origin.y + (pill.origin.y - from.origin.y) * t;
  island_draw_glass(context, pill, 7, 1.0);

  for (int i = 0; i < g_tab_count; i++) {
    CGRect row = layout->services[i];
    int service = g_tabs[i];

    // How "selected" the row is right now, so the icons cross-fade too.
    double weight = 0;
    if (service == island->service) weight += t;
    if (service == island->previous_service) weight += 1.0 - t;

    // Center the glyph vertically on its line box.
    CGFloat baseline = CGRectGetMidY(row)
                       - (CTFontGetAscent(g_fonts.icon)
                          - CTFontGetDescent(g_fonts.icon)) / 2.0;

    if (service == ISLAND_SERVICE_CLAUDE) {
      island_draw_spark(context,
                        CGPointMake(CGRectGetMidX(row), CGRectGetMidY(row)),
                        ISLAND_ICON_SIZE / 2.0, ISLAND_COLOR_CLAUDE,
                        0.55 + 0.45 * weight                             );
      continue;
    }
    island_draw_text(context,
                     g_has_icons ? g_services[service].icon
                                 : g_services[service].fallback,
                     g_fonts.icon,
                     0.5 + 0.5 * weight,
                     CGRectGetMidX(row),
                     baseline,
                     row.size.width,
                     ALIGN_CENTER                                            );

    // Unread dot instead of a count, the column is only an icon wide.
    if (service == ISLAND_SERVICE_ALERTS && notifications_get()->count > 0) {
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

// "now", "5m", "2h", "3d" since the notification was captured.
static void island_relative_time(CFAbsoluteTime time, char* out, size_t size) {
  double seconds = CFAbsoluteTimeGetCurrent() - time;
  if (seconds < 60) snprintf(out, size, "now");
  else if (seconds < 3600) snprintf(out, size, "%dm", (int)(seconds / 60));
  else if (seconds < 86400) snprintf(out, size, "%dh", (int)(seconds / 3600));
  else snprintf(out, size, "%dd", (int)(seconds / 86400));
}

// ---------------------------------------------------------------- app icons

static void island_draw_glyph(CGContextRef context,
                              const char* glyph,
                              CTFontRef font,
                              uint32_t color,
                              CGFloat alpha,
                              CGRect rect       );

// Common apps on notification cards: a rounded square in the brand color with
// a Font Awesome glyph (Nerd Font), or a letter where the font has none.
// Matched by the app name Notification Center shows, ignoring case.
static const struct {
  const char* name;
  uint32_t color;
  const char* glyph;   // NULL: letter only
  const char* letter;
} g_app_icons[] = {
  { "Zalo",            0xff0068ff, NULL,              "Z" },
  { "Messenger",       0xffa033ff, "\xEF\x89\xBA",    "m" },  // U+F27A
  { "Messages",        0xff34c759, "\xEF\x81\xB5",    "M" },  // U+F075
  { "WhatsApp",        0xff25d366, "\xEF\x88\xB2",    "W" },  // U+F232
  { "Telegram",        0xff2aabee, "\xEF\x8B\x86",    "T" },  // U+F2C6
  { "Slack",           0xff4a154b, "\xEF\x86\x98",    "S" },  // U+F198
  { "Discord",         0xff5865f2, NULL,              "D" },
  { "Microsoft Teams", 0xff5059c9, NULL,              "T" },
  { "Teams",           0xff5059c9, NULL,              "T" },
  { "Skype",           0xff00aff0, "\xEF\x85\xBE",    "S" },  // U+F17E
  { "FaceTime",        0xff34c759, "\xEF\x80\xBD",    "F" },  // U+F03D
  { "Zoom",            0xff2d8cff, "\xEF\x80\xBD",    "Z" },  // U+F03D
  { "Mail",            0xff1e88e5, "\xEF\x83\xA0",    "M" },  // U+F0E0
  { "Outlook",         0xff0078d4, "\xEF\x83\xA0",    "O" },  // U+F0E0
  { "Gmail",           0xffea4335, "\xEF\x83\xA0",    "G" },  // U+F0E0
  { "Calendar",        0xffff3b30, "\xEF\x81\xB3",    "C" },  // U+F073
  { "Reminders",       0xffff9500, "\xEF\x80\xBA",    "R" },  // U+F03A
  { "Notes",           0xffe5b100, "\xEF\x89\x89",    "N" },  // U+F249
  { "Music",           0xfffa2d48, "\xEF\x80\x81",    "M" },  // U+F001
  { "Spotify",         0xff1db954, "\xEF\x86\xBC",    "S" },  // U+F1BC
  { "YouTube",         0xffff0000, "\xEF\x85\xAA",    "Y" },  // U+F16A
  { "Facebook",        0xff1877f2, "\xEF\x82\x9A",    "f" },  // U+F09A
  { "Instagram",       0xffe1306c, "\xEF\x85\xAD",    "I" },  // U+F16D
  { "X",               0xff1a1a1a, NULL,              "X" },
  { "Twitter",         0xff1da1f2, "\xEF\x82\x99",    "T" },  // U+F099
  { "GitHub",          0xff24292f, "\xEF\x82\x9B",    "G" },  // U+F09B
  { "Google Chrome",   0xff4285f4, "\xEF\x89\xA8",    "C" },  // U+F268
  { "Safari",          0xff0a84ff, "\xEF\x89\xA7",    "S" },  // U+F267
  { "Visual Studio Code", 0xff007acc, "\xEF\x84\xA1",  "C" },  // U+F121
  { "Code",            0xff007acc, "\xEF\x84\xA1",    "C" },  // U+F121
  { "Xcode",           0xff147efb, "\xEF\x84\xA1",    "X" },  // U+F121
  { "Terminal",        0xff2b2b2b, "\xEF\x84\xA0",    ">" },  // U+F120
  { "Jira",            0xff0052cc, NULL,              "J" },
  { "Notion",          0xff1a1a1a, NULL,              "N" },
  { "Figma",           0xffa259ff, NULL,              "F" },
  { "Linear",          0xff5e6ad2, NULL,              "L" },
  { "App Store",       0xff0d84ff, NULL,              "A" },
  { "System Settings", 0xff8e8e93, "\xEF\x80\x93",    "S" },  // U+F013
  { "Finder",          0xff1e90ff, NULL,              "F" },
  { "Photos",          0xffff9f0a, "\xEF\x80\xBE",    "P" },  // U+F03E
  { "Fitness",         0xfffa114f, "\xEF\x88\x9E",    "F" },  // U+F21E
  { "Grab",            0xff00b14f, NULL,              "G" },
  { "Shopee",          0xffee4d2d, NULL,              "S" },
  { "MoMo",            0xffa50064, NULL,              "M" },
  { "myVNG",           0xfff26722, NULL,              "V" },
};
#define APP_ICON_COUNT (sizeof(g_app_icons) / sizeof(g_app_icons[0]))

// Per table entry: the glyph if the icon font has it, else the letter.
static const char* g_app_icon_marks[APP_ICON_COUNT];

static void island_pick_app_icon_marks() {
  for (int i = 0; i < APP_ICON_COUNT; i++) {
    const char* glyph = g_app_icons[i].glyph;
    g_app_icon_marks[i] = g_has_icons && glyph
                          && island_font_has_glyphs(g_fonts.app_glyph, glyph)
                          ? glyph
                          : g_app_icons[i].letter;
  }
}

static int island_app_icon_index(const char* app) {
  for (int i = 0; i < APP_ICON_COUNT; i++) {
    if (strcasecmp(app, g_app_icons[i].name) == 0) return i;
  }
  return -1;
}

// Unknown apps get a blossom. The colors rotate: each new unknown app gets
// the next one, and keeps it for the session.
static const uint32_t g_blossom_colors[] = {
  0xffff7eb6, 0xffb39df3, 0xff76cce0, 0xff9ed072, 0xfff39660, 0xffe7c664
};
#define BLOSSOM_COLOR_COUNT (sizeof(g_blossom_colors) / sizeof(g_blossom_colors[0]))
#define BLOSSOM_APPS        32

static struct {
  char name[64];
  int color;
} g_blossom_apps[BLOSSOM_APPS];
static int g_blossom_count = 0;
static int g_blossom_next = 0;

static uint32_t island_blossom_color(const char* app) {
  for (int i = 0; i < g_blossom_count; i++) {
    if (strcmp(g_blossom_apps[i].name, app) == 0)
      return g_blossom_colors[g_blossom_apps[i].color];
  }

  // A new app: the next color (the table recycles its oldest slot).
  int color = g_blossom_next % BLOSSOM_COLOR_COUNT;
  int slot = g_blossom_next % BLOSSOM_APPS;
  g_blossom_next++;
  snprintf(g_blossom_apps[slot].name, sizeof(g_blossom_apps[slot].name),
           "%s", app                                                    );
  g_blossom_apps[slot].color = color;
  if (g_blossom_count < BLOSSOM_APPS) g_blossom_count++;
  return g_blossom_colors[color];
}

// Five petals around a light center, on a glass tile.
static void island_draw_blossom(CGContextRef context,
                                CGRect rect,
                                uint32_t color     ) {
  island_draw_glass(context, rect, ISLAND_APP_ICON_RADIUS, 1.0);

  CGPoint center = { CGRectGetMidX(rect), CGRectGetMidY(rect) };
  CGFloat size = rect.size.width;
  CGFloat petal_width = size * 0.26;
  CGFloat petal_length = size * 0.36;

  CGContextSaveGState(context);
  CGContextTranslateCTM(context, center.x, center.y);
  island_set_fill(context, color, 0.95);
  for (int i = 0; i < 5; i++) {
    CGContextFillEllipseInRect(context,
                               CGRectMake(-petal_width / 2.0,
                                          size * 0.04,
                                          petal_width,
                                          petal_length          ));
    CGContextRotateCTM(context, 2.0 * M_PI / 5.0);
  }
  CGFloat core = size * 0.2;
  island_set_fill(context, 0xfffff4c2, 1.0);
  CGContextFillEllipseInRect(context,
                             CGRectMake(-core / 2.0, -core / 2.0, core, core));
  CGContextRestoreGState(context);
}

static void island_draw_app_icon(CGContextRef context,
                                 const char* app,
                                 CGRect rect      ) {
  if (strcasecmp(app, CLAUDE_APP_NAME) == 0 || strcasecmp(app, "Claude") == 0) {
    island_add_rounded_rect(context, rect, ISLAND_APP_ICON_RADIUS);
    island_set_fill(context, 0xfff5f0e8, 1.0);
    CGContextFillPath(context);
    island_draw_spark(context,
                      CGPointMake(CGRectGetMidX(rect), CGRectGetMidY(rect)),
                      rect.size.width * 0.36, ISLAND_COLOR_CLAUDE, 1.0     );
    return;
  }
  int index = island_app_icon_index(app);
  if (index < 0) {
    island_draw_blossom(context, rect, island_blossom_color(app));
    return;
  }

  island_add_rounded_rect(context, rect, ISLAND_APP_ICON_RADIUS);
  island_set_fill(context, g_app_icons[index].color, 1.0);
  CGContextFillPath(context);

  // A soft top highlight, like the glass cards.
  CGContextSaveGState(context);
  island_add_rounded_rect(context, rect, ISLAND_APP_ICON_RADIUS);
  CGContextClip(context);
  island_set_fill(context, ISLAND_COLOR_WHITE, 0.12);
  CGContextFillRect(context, CGRectMake(rect.origin.x, CGRectGetMidY(rect),
                                        rect.size.width,
                                        rect.size.height / 2.0          ));
  CGContextRestoreGState(context);

  const char* mark = g_app_icon_marks[index];
  bool glyph = mark != g_app_icons[index].letter;
  island_draw_glyph(context, mark, glyph ? g_fonts.app_glyph
                                         : g_fonts.app_letter,
                    ISLAND_COLOR_WHITE, 1.0, rect             );
}

// ---------------------------------------------------------------- wrapping

// Breaks `text` into at most `max_lines` lines no wider than `width` in
// `font`; if text is left over, the last line ends in an ellipsis. Returns
// the number of lines (each must be released).
static int island_wrap_text(const char* text,
                            CTFontRef font,
                            CGFloat width,
                            int max_lines,
                            CTLineRef* lines  ) {
  if (!text || !font || width <= 0) return 0;
  CFStringRef string = CFStringCreateWithCString(NULL, text,
                                                 kCFStringEncodingUTF8);
  if (!string) return 0;
  const void* keys[] = { kCTFontAttributeName,
                         kCTForegroundColorFromContextAttributeName };
  const void* values[] = { font, kCFBooleanTrue };
  CFDictionaryRef attributes = CFDictionaryCreate(NULL, keys, values, 2,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef attributed = CFAttributedStringCreate(NULL, string,
                                                              attributes  );
  CTTypesetterRef typesetter = CTTypesetterCreateWithAttributedString(
                                                                attributed);
  CFIndex length = CFStringGetLength(string);
  CFIndex start = 0;
  int count = 0;
  while (start < length && count < max_lines) {
    CFIndex fits = CTTypesetterSuggestLineBreak(typesetter, start, width);
    if (fits <= 0) break;
    if (count == max_lines - 1 && start + fits < length) {
      CTLineRef rest = CTTypesetterCreateLine(typesetter,
                                              CFRangeMake(start,
                                                          length - start));
      CFAttributedStringRef dots = CFAttributedStringCreate(NULL,
                                                            CFSTR("…"),
                                                            attributes  );
      CTLineRef ellipsis = CTLineCreateWithAttributedString(dots);
      CTLineRef cut = CTLineCreateTruncatedLine(rest, width,
                                                kCTLineTruncationEnd,
                                                ellipsis             );
      if (cut) {
        CFRelease(rest);
        rest = cut;
      }
      lines[count++] = rest;
      CFRelease(ellipsis);
      CFRelease(dots);
      break;
    }
    lines[count++] = CTTypesetterCreateLine(typesetter,
                                            CFRangeMake(start, fits));
    start += fits;
  }
  CFRelease(typesetter);
  CFRelease(attributed);
  CFRelease(attributes);
  CFRelease(string);
  return count;
}

// Draws (and releases) wrapped lines, the first baseline at `baseline`.
static void island_draw_lines(CGContextRef context,
                              CTLineRef* lines,
                              int count,
                              uint32_t color,
                              CGFloat alpha,
                              CGFloat x,
                              CGFloat baseline,
                              CGFloat spacing   ) {
  island_set_fill(context, color, alpha);
  for (int i = 0; i < count; i++) {
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    CGContextSetTextPosition(context, x, baseline - i * spacing);
    CTLineDraw(lines[i], context);
    CFRelease(lines[i]);
  }
}

// A Yes (green, the default) or No (glass) button.
static void island_draw_choice(CGContextRef context,
                               CGRect rect,
                               bool yes,
                               CGFloat alpha  ) {
  CGFloat radius = rect.size.height / 2.0;
  if (yes) {
    island_add_rounded_rect(context, rect, radius);
    island_set_fill(context, ISLAND_COLOR_GREEN, 0.9 * alpha);
    CGContextFillPath(context);
  } else {
    island_draw_glass(context, rect, radius, 1.4);
  }
  CGFloat baseline = CGRectGetMidY(rect)
                     - (CTFontGetAscent(g_fonts.button)
                        - CTFontGetDescent(g_fonts.button)) / 2.0;
  island_draw_text_colored(context, yes ? "Yes" : "No", g_fonts.button,
                           yes ? 0xff10200a : ISLAND_COLOR_WHITE,
                           0.95 * alpha, CGRectGetMidX(rect), baseline,
                           rect.size.width, ALIGN_CENTER               );
}

// A suggestion card's Yes / No, stacked in its left column.
static void island_card_choices(CGRect card, CGRect* yes, CGRect* no) {
  CGFloat x = card.origin.x + ISLAND_CARD_INSET;
  CGFloat mid = CGRectGetMidY(card);
  CGFloat height = ISLAND_CHOICE_HEIGHT - 2;
  *yes = (CGRect){{ x, mid + 2 }, { ISLAND_CHOICE_WIDTH, height }};
  *no = (CGRect){{ x, mid - 2 - height }, { ISLAND_CHOICE_WIDTH, height }};
}

// The card's close button (top right): marks it read without opening it.
static CGRect island_card_close_rect(CGRect card) {
  return (CGRect){{ CGRectGetMaxX(card) - ISLAND_CARD_INSET + 3
                                        - ISLAND_CARD_CLOSE,
                    CGRectGetMaxY(card) - 5 - ISLAND_CARD_CLOSE },
                  { ISLAND_CARD_CLOSE, ISLAND_CARD_CLOSE }};
}

static void island_draw_card(CGContextRef context,
                             const struct notification_item* notification,
                             CGRect card                                  ) {
  // Opaque base (the island's black): the glass alone is see-through, which
  // let a card fading out behind show through the one gliding over it.
  island_add_rounded_rect(context, card, ISLAND_CARD_RADIUS);
  island_set_fill(context, 0xff000000, 1.0);
  CGContextFillPath(context);
  island_draw_glass(context, card, ISLAND_CARD_RADIUS, 1.0);

  if (claude_suggestion_active(notification)) {
    // Yes / No on the left; header (what finished, when) and the whole
    // suggestion right of them.
    CGRect yes, no;
    island_card_choices(card, &yes, &no);
    island_draw_choice(context, yes, true, 1.0);
    island_draw_choice(context, no, false, 1.0);

    CGFloat x = CGRectGetMaxX(yes) + ISLAND_APP_ICON_GAP;
    CGFloat right = CGRectGetMaxX(card) - ISLAND_CARD_INSET;
    CGFloat top = CGRectGetMaxY(card);
    CGRect close = island_card_close_rect(card);
    island_draw_glass(context, close, close.size.width / 2.0, 1.3);
    island_draw_glyph(context, g_glyphs[GLYPH_CROSS], g_fonts.stats_icon,
                      ISLAND_COLOR_WHITE, 0.8, close                     );

    char time[16];
    island_relative_time(notification->time, time, sizeof(time));
    CGFloat time_right = close.origin.x - 5;
    CGFloat time_width = island_draw_text(context, time, g_fonts.caption,
                                          0.5, time_right, top - 13,
                                          time_right - x, ALIGN_RIGHT  );
    char header[160];
    snprintf(header, sizeof(header), "%s · next?", notification->title);
    island_draw_text_colored(context, header, g_fonts.caption,
                             ISLAND_COLOR_CLAUDE, 0.9,
                             x, top - 13, time_right - x - time_width - 6,
                             ALIGN_LEFT                                   );

    CTLineRef lines[ISLAND_SUGGEST_LINES];
    int count = island_wrap_text(notification->suggestion, g_fonts.body,
                                 right - x, ISLAND_SUGGEST_LINES, lines);
    island_draw_lines(context, lines, count, ISLAND_COLOR_WHITE, 0.92,
                      x, top - 27, 12                                 );
    return;
  }

  // App icon on the left, the texts right of it.
  CGRect icon = {{ card.origin.x + ISLAND_CARD_INSET,
                   CGRectGetMidY(card) - ISLAND_APP_ICON_SIZE / 2.0 },
                 { ISLAND_APP_ICON_SIZE, ISLAND_APP_ICON_SIZE }};
  if (notification->claude) island_draw_clawd_tile(context, icon, CLAWD_HAPPY);
  else island_draw_app_icon(context, notification->app, icon);

  CGFloat x = CGRectGetMaxX(icon) + ISLAND_APP_ICON_GAP;
  CGFloat right = CGRectGetMaxX(card) - ISLAND_CARD_INSET;
  CGFloat width = right - x;
  CGFloat top = CGRectGetMaxY(card);

  // Close button, the time left of it.
  CGRect close = island_card_close_rect(card);
  island_draw_glass(context, close, close.size.width / 2.0, 1.3);
  island_draw_glyph(context, g_glyphs[GLYPH_CROSS], g_fonts.stats_icon,
                    ISLAND_COLOR_WHITE, 0.8, close                     );

  char time[16];
  island_relative_time(notification->time, time, sizeof(time));
  CGFloat time_right = close.origin.x - 5;
  CGFloat time_width = island_draw_text(context, time,
                                        g_fonts.caption, 0.5,
                                        time_right, top - 13,
                                        time_right - x, ALIGN_RIGHT);

  island_draw_text(context, notification->app, g_fonts.caption, 0.55,
                   x, top - 13, time_right - x - time_width - 6,
                   ALIGN_LEFT                                   );
  island_draw_text(context, notification->title, g_fonts.title, 0.95,
                   x, top - 27, width, ALIGN_LEFT);
  island_draw_text(context, notification->body, g_fonts.body, 0.7,
                   x, top - 40, width, ALIGN_LEFT);
}

// A scroll bar in `track` for a list of `content` points of which `visible`
// show, scrolled by `scroll_offset`. Nothing when it all fits.
static void island_draw_scroll_bar(CGContextRef context,
                                   CGRect track,
                                   CGFloat content,
                                   CGFloat visible_height,
                                   CGFloat scroll_offset  ) {
  CGFloat max_scroll = content - visible_height;
  if (max_scroll <= 0) return;

  island_add_rounded_rect(context, track, track.size.width / 2.0);
  CGContextSetRGBFillColor(context, 1, 1, 1, 0.10);
  CGContextFillPath(context);

  CGFloat visible = visible_height / content;
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

static void island_draw_scroll_indicator(CGContextRef context,
                                         struct island_layout* layout,
                                         CGFloat scroll_offset       ) {
  CGRect list = island_alerts_list(layout);
  CGRect track = layout->scroll_track;
  track.origin.y = list.origin.y;
  track.size.height = list.size.height;
  island_draw_scroll_bar(context, track, island_list_height(),
                         list.size.height, scroll_offset      );
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

// ---------------------------------------------------------------- cards

// How far below the list's top the card of notification `index` sits.
static CGFloat island_card_target(int index) {
  const struct notification_list* list = notifications_get();
  CGFloat y = 0;
  for (int i = 0; i < index && i < list->count; i++)
    y += island_card_height(&list->items[i]) + ISLAND_CARD_GAP;
  return y;
}

static int island_notification_index(const char* id) {
  const struct notification_list* list = notifications_get();
  for (int i = 0; i < list->count; i++) {
    if (strcmp(list->items[i].id, id) == 0) return i;
  }
  return -1;
}

static double island_ease_out(double t) {
  t = max(min(t, 1.0), 0.0);
  return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
}

bool island_sync_cards(struct island* island, bool animate) {
  const struct notification_list* list = notifications_get();
  if (!island->cards_synced) animate = false;
  island->cards_synced = true;

  // Cards whose notification is gone leave (staggered top to bottom, so
  // "Read all" sweeps the list).
  int leaving = 0;
  for (int i = 0; i < island->card_count; i++) {
    struct island_card* card = &island->cards[i];
    if (card->leaving || island_notification_index(card->item.id) >= 0)
      continue;
    if (!animate) {
      card->leave = 1.0;
    } else {
      card->leave = -ISLAND_CARD_STAGGER * leaving++;
    }
    card->leaving = true;
  }

  // New notifications get a card, starting a little above their place.
  for (int i = 0; i < list->count; i++) {
    bool found = false;
    for (int j = 0; j < island->card_count; j++) {
      if (!island->cards[j].leaving
          && strcmp(island->cards[j].item.id, list->items[i].id) == 0) {
        island->cards[j].item = list->items[i];
        found = true;
        break;
      }
    }
    if (found) continue;

    // Full: make room by dropping the oldest leaving card, if any.
    if (island->card_count >= ISLAND_CARDS_MAX) {
      int drop = -1;
      for (int j = 0; j < island->card_count; j++) {
        if (island->cards[j].leaving) drop = j;
      }
      if (drop < 0) break;
      island->cards[drop] = island->cards[--island->card_count];
    }

    struct island_card* card = &island->cards[island->card_count++];
    memset(card, 0, sizeof(*card));
    card->item = list->items[i];
    card->y = island_card_target(i) - (animate ? ISLAND_CARD_ENTER : 0);
    card->appear = animate ? 0 : 1;
  }

  if (!animate) island_step_cards(island, 0, true);
  return animate;
}

bool island_step_cards(struct island* island, double dt, bool finish) {
  bool moving = false;
  double follow = finish ? 1.0 : 1.0 - exp(-dt / ISLAND_CARD_TAU);

  for (int i = 0; i < island->card_count; i++) {
    struct island_card* card = &island->cards[i];
    if (card->leaving) {
      card->leave = finish ? 1.0 : card->leave + dt / ISLAND_CARD_LEAVE;
      if (card->leave >= 1.0) {
        island->cards[i--] = island->cards[--island->card_count];
        continue;
      }
      moving = true;
      continue;
    }

    card->appear = finish ? 1.0 : min(card->appear + dt / ISLAND_CARD_APPEAR,
                                      1.0                                  );
    int index = island_notification_index(card->item.id);
    CGFloat target = island_card_target(max(index, 0));
    CGFloat distance = target - card->y;
    card->y = fabs(distance) < 0.3 ? target : card->y + distance * follow;
    if (card->appear < 1.0 || card->y != target) moving = true;
  }
  return moving;
}

static CGRect island_card_frame(struct island_layout* layout,
                                const struct island_card* card,
                                CGFloat scroll_offset         ) {
  CGRect list = island_alerts_list(layout);
  CGFloat top = CGRectGetMaxY(list) - card->y + scroll_offset;
  CGFloat height = island_card_height(&card->item);
  return (CGRect){{ list.origin.x, top - height },
                  { list.size.width, height     }};
}

static void island_draw_alerts(CGContextRef context,
                               struct island_layout* layout,
                               CGFloat scroll_offset       ) {
  const struct notification_list* notifications = notifications_get();
  // Neither source readable: banners need Accessibility, Notification
  // Center's history Full Disk Access.
  if (notifications_capture() && !notifications->trusted
      && !notifications->history && notifications->count == 0) {
    island_draw_placeholder(context, layout,
                            "Allow SketchyBar in Accessibility");
    return;
  }
  struct island* island = g_drawing;
  if (notifications->count == 0 && (!island || island->card_count == 0)) {
    island_draw_placeholder(context, layout, "No notifications");
    return;
  }

  // Header: count, and the button that marks everything read (not while the
  // last cards are sliding out).
  CGRect header = island_alerts_header(layout);
  if (notifications->count > 0) {
    char count[48];
    snprintf(count, sizeof(count),
             notifications->history ? "%d unread" : "%d unread (banners only)",
             notifications->count                                          );
    island_draw_text(context, count, g_fonts.caption, 0.55,
                     header.origin.x, CGRectGetMidY(header) - 3,
                     header.size.width - ISLAND_READ_ALL_WIDTH - 8,
                     ALIGN_LEFT                                    );
    CGRect read_all = island_alerts_read_all(layout);
    island_draw_glass(context, read_all, read_all.size.height / 2.0, 1.2);
    island_draw_text(context, "Read all", g_fonts.caption, 0.85,
                     CGRectGetMidX(read_all), CGRectGetMidY(read_all) - 3,
                     read_all.size.width, ALIGN_CENTER                    );
  }

  CGRect list = island_alerts_list(layout);
  CGContextSaveGState(context);
  CGContextClipToRect(context, list);
  // Leaving cards first, so they fade out behind the ones gliding into
  // their place.
  for (int n = 0; island && n < 2 * island->card_count; n++) {
    const struct island_card* card = &island->cards[n % island->card_count];
    if (card->leaving != (n < island->card_count)) continue;
    CGRect frame = island_card_frame(layout, card, scroll_offset);

    // Appearing: fades in while it slides into place. Leaving: slides out
    // to the right and fades.
    double alpha = 1.0;
    if (card->leaving) {
      double t = island_ease_out(card->leave);
      alpha = 1.0 - t;
      frame.origin.x += ISLAND_CARD_EXIT * t;
    } else {
      alpha = island_ease_out(card->appear);
    }
    if (alpha <= 0 || !CGRectIntersectsRect(frame, list)) continue;

    CGContextSaveGState(context);
    CGContextSetAlpha(context, alpha);
    CGContextBeginTransparencyLayer(context, NULL);
    island_draw_card(context, &card->item, frame);
    CGContextEndTransparencyLayer(context);
    CGContextRestoreGState(context);
  }
  CGContextRestoreGState(context);

  island_draw_scroll_indicator(context, layout, scroll_offset);
}

// ---------------------------------------------------------------- music

struct island_music_layout {
  CGRect artwork;
  CGRect info;
  CGRect previous;
  CGRect play;
  CGRect next;
  CGRect speaker;
  CGRect slider;
  CGRect slider_hit;
  CGRect volume_label;
  CGFloat output_baseline;
};

static struct island_music_layout island_get_music_layout(
                                              struct island_layout* layout) {
  struct island_music_layout music;
  CGRect area = layout->list;
  CGFloat top = CGRectGetMaxY(area);

  music.artwork = (CGRect){{ area.origin.x, top - ISLAND_ARTWORK_SIZE },
                           { ISLAND_ARTWORK_SIZE, ISLAND_ARTWORK_SIZE }};
  CGFloat info_x = CGRectGetMaxX(music.artwork) + ISLAND_MUSIC_GAP;
  music.info = (CGRect){{ info_x, music.artwork.origin.y },
                        { CGRectGetMaxX(area) - info_x, ISLAND_ARTWORK_SIZE }};

  CGFloat controls = music.artwork.origin.y - ISLAND_MUSIC_GAP
                                            - ISLAND_PLAY_SIZE / 2.0;
  CGFloat center = CGRectGetMidX(area);
  music.play = (CGRect){{ center - ISLAND_PLAY_SIZE / 2.0,
                          controls - ISLAND_PLAY_SIZE / 2.0 },
                        { ISLAND_PLAY_SIZE, ISLAND_PLAY_SIZE }};
  CGFloat skip = ISLAND_PLAY_SIZE / 2.0 + ISLAND_SKIP_SPACING
                                        + ISLAND_SKIP_SIZE / 2.0;
  music.previous = (CGRect){{ center - skip - ISLAND_SKIP_SIZE / 2.0,
                              controls - ISLAND_SKIP_SIZE / 2.0       },
                            { ISLAND_SKIP_SIZE, ISLAND_SKIP_SIZE }};
  music.next = music.previous;
  music.next.origin.x = center + skip - ISLAND_SKIP_SIZE / 2.0;

  CGFloat volume = controls - ISLAND_PLAY_SIZE / 2.0 - ISLAND_MUSIC_GAP
                            - ISLAND_VOLUME_HEIGHT / 2.0;
  music.speaker = (CGRect){{ area.origin.x, volume - ISLAND_VOLUME_HEIGHT / 2.0 },
                           { ISLAND_VOLUME_HEIGHT, ISLAND_VOLUME_HEIGHT }};
  CGFloat slider_x = CGRectGetMaxX(music.speaker) + 6;
  music.slider = (CGRect){{ slider_x, volume - ISLAND_SLIDER_HEIGHT / 2.0 },
                          { CGRectGetMaxX(area) - ISLAND_VOLUME_LABEL
                                                - slider_x,
                            ISLAND_SLIDER_HEIGHT                     }};
  music.slider_hit = CGRectInset(music.slider, -ISLAND_SLIDER_KNOB / 2.0,
                                               -ISLAND_VOLUME_HEIGHT / 2.0);
  music.volume_label = (CGRect){{ CGRectGetMaxX(area) - ISLAND_VOLUME_LABEL,
                                  volume - ISLAND_VOLUME_HEIGHT / 2.0     },
                                { ISLAND_VOLUME_LABEL, ISLAND_VOLUME_HEIGHT }};
  music.output_baseline = volume - ISLAND_VOLUME_HEIGHT / 2.0 - 11;
  return music;
}

// Where on a slider (0..1) `point` is.
static float island_slider_fraction(CGRect slider, CGPoint point) {
  if (slider.size.width <= 0) return 0;
  float value = (point.x - slider.origin.x) / slider.size.width;
  return max(min(value, 1.f), 0.f);
}

static float island_slider_value(struct island_music_layout* music,
                                 CGPoint point                    ) {
  return island_slider_fraction(music->slider, point);
}

// A slider: the track, then (if `enabled`) the part up to `value` in `color`
// and the knob at its end.
static void island_draw_slider(CGContextRef context,
                               CGRect slider,
                               bool enabled,
                               float value,
                               uint32_t color,
                               CGFloat alpha       ) {
  island_add_rounded_rect(context, slider, slider.size.height / 2);
  island_set_fill(context, ISLAND_COLOR_WHITE, 0.15);
  CGContextFillPath(context);
  if (!enabled) return;

  CGRect fill = slider;
  fill.size.width *= max(min(value, 1.f), 0.f);
  island_add_rounded_rect(context, fill, fill.size.height / 2);
  island_set_fill(context, color, alpha);
  CGContextFillPath(context);

  CGRect knob = {{ CGRectGetMaxX(fill) - ISLAND_SLIDER_KNOB / 2.0,
                   CGRectGetMidY(fill) - ISLAND_SLIDER_KNOB / 2.0 },
                 { ISLAND_SLIDER_KNOB, ISLAND_SLIDER_KNOB }};
  island_set_fill(context, ISLAND_COLOR_WHITE, 0.95);
  CGContextFillEllipseInRect(context, knob);
}

// Draws a glyph centered in `rect`.
static void island_draw_glyph(CGContextRef context,
                              const char* glyph,
                              CTFontRef font,
                              uint32_t color,
                              CGFloat alpha,
                              CGRect rect       ) {
  CGFloat baseline = CGRectGetMidY(rect)
                     - (CTFontGetAscent(font) - CTFontGetDescent(font)) / 2.0;
  island_draw_text_colored(context, glyph, font, color, alpha,
                           CGRectGetMidX(rect), baseline,
                           rect.size.width + 20, ALIGN_CENTER);
}

static void island_draw_artwork(CGContextRef context,
                                CGImageRef artwork,
                                CGRect rect        ) {
  if (!artwork) {
    island_draw_glass(context, rect, ISLAND_ARTWORK_RADIUS, 1.0);
    island_draw_glyph(context,
                      g_has_icons ? g_services[ISLAND_SERVICE_MUSIC].icon
                                  : g_services[ISLAND_SERVICE_MUSIC].fallback,
                      g_fonts.icon, ISLAND_COLOR_WHITE, 0.4, rect             );
    return;
  }

  // Aspect fill, clipped to a rounded square.
  CGFloat width = CGImageGetWidth(artwork);
  CGFloat height = CGImageGetHeight(artwork);
  CGFloat scale = max(rect.size.width / width, rect.size.height / height);
  CGRect image = {{ CGRectGetMidX(rect) - width * scale / 2.0,
                    CGRectGetMidY(rect) - height * scale / 2.0 },
                  { width * scale, height * scale }};

  CGContextSaveGState(context);
  island_add_rounded_rect(context, rect, ISLAND_ARTWORK_RADIUS);
  CGContextClip(context);
  CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
  CGContextDrawImage(context, image, artwork);
  CGContextRestoreGState(context);

  CGContextSaveGState(context);
  island_add_rounded_rect(context, CGRectInset(rect, 0.25, 0.25),
                          ISLAND_ARTWORK_RADIUS                 );
  CGContextSetRGBStrokeColor(context, 1, 1, 1, 0.15);
  CGContextSetLineWidth(context, 0.5);
  CGContextStrokePath(context);
  CGContextRestoreGState(context);
}

static void island_draw_music(CGContextRef context,
                              struct island_layout* layout) {
  if (!g_data || !g_data->media || !g_data->audio) return;
  const struct now_playing* media = g_data->media;
  const struct audio_output* audio = g_data->audio;
  struct island_music_layout music = island_get_music_layout(layout);

  // Track
  island_draw_artwork(context, media->artwork, music.artwork);

  CGFloat x = music.info.origin.x;
  CGFloat width = music.info.size.width;
  CGFloat top = CGRectGetMaxY(music.info);
  if (media->has_player) {
    island_draw_text(context, media->title, g_fonts.title, 0.95,
                     x, top - 14, width, ALIGN_LEFT);
    island_draw_text(context, media->artist, g_fonts.body, 0.7,
                     x, top - 28, width, ALIGN_LEFT);

    CGFloat dot = 5;
    island_set_fill(context,
                    media->playing ? ISLAND_COLOR_GREEN : ISLAND_COLOR_WHITE,
                    media->playing ? 1.0 : 0.35                             );
    CGContextFillEllipseInRect(context,
                               CGRectMake(x, top - 42 + 1, dot, dot));
    island_draw_text(context, media->app, g_fonts.caption, 0.5,
                     x + dot + 4, top - 42, width - dot - 4, ALIGN_LEFT);
  } else {
    island_draw_text(context, "Nothing playing", g_fonts.title, 0.8,
                     x, top - 20, width, ALIGN_LEFT);
    island_draw_text(context, "Open Spotify or Music", g_fonts.caption, 0.45,
                     x, top - 34, width, ALIGN_LEFT);
  }

  // Transport
  CGFloat controls_alpha = media->has_player ? 0.9 : 0.3;
  island_draw_glass(context, music.play, ISLAND_PLAY_SIZE / 2.0, 1.2);
  island_draw_glyph(context,
                    g_glyphs[media->playing ? GLYPH_PAUSE : GLYPH_PLAY],
                    g_fonts.icon, ISLAND_COLOR_WHITE, controls_alpha,
                    music.play                                         );
  island_draw_glyph(context, g_glyphs[GLYPH_PREVIOUS], g_fonts.icon,
                    ISLAND_COLOR_WHITE, controls_alpha * 0.8, music.previous);
  island_draw_glyph(context, g_glyphs[GLYPH_NEXT], g_fonts.icon,
                    ISLAND_COLOR_WHITE, controls_alpha * 0.8, music.next);

  // Volume
  bool muted = audio->has_mute && audio->muted;
  float volume = audio->has_volume ? audio->volume : 0;
  enum island_glyph speaker = muted || volume == 0 ? GLYPH_MUTED
                              : volume < 0.5       ? GLYPH_VOLUME_LOW
                                                   : GLYPH_VOLUME;
  island_draw_glyph(context, g_glyphs[speaker], g_fonts.small_icon,
                    muted ? ISLAND_COLOR_RED : ISLAND_COLOR_WHITE,
                    audio->has_mute || audio->has_volume ? 0.85 : 0.3,
                    music.speaker                                     );

  island_draw_slider(context, music.slider, audio->has_volume, volume,
                     muted ? ISLAND_COLOR_WHITE : ISLAND_COLOR_BLUE,
                     muted ? 0.35 : 1.0                              );

  char label[16];
  if (!audio->has_volume) snprintf(label, sizeof(label), "--");
  else if (muted) snprintf(label, sizeof(label), "Muted");
  else snprintf(label, sizeof(label), "%d%%", (int)round(volume * 100));
  island_draw_text(context, label, g_fonts.caption, 0.7,
                   CGRectGetMaxX(music.volume_label),
                   CGRectGetMidY(music.volume_label) - 3,
                   music.volume_label.size.width,
                   ALIGN_RIGHT                          );

  // Output device
  if (audio->name[0]) {
    char output[160];
    snprintf(output, sizeof(output), "Output · %s", audio->name);
    island_draw_text(context, output, g_fonts.caption, 0.45,
                     layout->list.origin.x, music.output_baseline,
                     layout->list.size.width, ALIGN_LEFT          );
  }
}

// ---------------------------------------------------------------- controls

// An on/off switch at `progress` (0 off, 1 on; in between while it moves):
// the track fades from grey to green and the knob slides over, stretching a
// little on the way like a drop of liquid.
static void island_draw_toggle(CGContextRef context,
                               CGRect rect,
                               double progress     ) {
  CGFloat t = max(min(progress, 1.0), 0.0);
  CGFloat radius = rect.size.height / 2.0;

  island_add_rounded_rect(context, rect, radius);
  island_set_fill(context, ISLAND_COLOR_WHITE, 0.2);
  CGContextFillPath(context);
  if (t > 0) {
    island_add_rounded_rect(context, rect, radius);
    island_set_fill(context, ISLAND_COLOR_GREEN, t);
    CGContextFillPath(context);
  }

  CGFloat knob = rect.size.height - 2;
  CGFloat stretch = ISLAND_TOGGLE_STRETCH * sin(M_PI * t);
  CGFloat travel = rect.size.width - knob - 2;
  CGRect pill = {{ rect.origin.x + 1 + travel * t - stretch * t,
                   rect.origin.y + 1                          },
                 { knob + stretch, knob }};
  island_add_rounded_rect(context, pill, knob / 2.0);
  island_set_fill(context, ISLAND_COLOR_WHITE, 0.95);
  CGContextFillPath(context);
}

// Small round button with a gear: opens the matching System Settings page.
static void island_draw_settings_button(CGContextRef context, CGRect rect) {
  island_draw_glass(context, rect, rect.size.width / 2.0, 1.0);
  island_draw_glyph(context, g_glyphs[GLYPH_SETTINGS], g_fonts.caption,
                    ISLAND_COLOR_WHITE, 0.8, rect                       );
}

// The switch and the settings button left of it, right-aligned in a row
// whose vertical center is `mid`.
static void island_controls_rects(CGFloat right,
                                  CGFloat mid,
                                  CGRect* toggle,
                                  CGRect* settings) {
  *toggle = (CGRect){{ right - ISLAND_TOGGLE_WIDTH,
                       mid - ISLAND_TOGGLE_HEIGHT / 2.0 },
                     { ISLAND_TOGGLE_WIDTH, ISLAND_TOGGLE_HEIGHT }};
  *settings = (CGRect){{ toggle->origin.x - ISLAND_CONTROL_GAP
                                          - ISLAND_SETTINGS_SIZE,
                         mid - ISLAND_SETTINGS_SIZE / 2.0       },
                       { ISLAND_SETTINGS_SIZE, ISLAND_SETTINGS_SIZE }};
}

// ---------------------------------------------------------------- system

static uint32_t island_level_color(int percent, bool high_is_good);

// Area graph of `values` (0..100, oldest first) in `rect`: the newest value
// at the right edge, one slot per possible history entry, so the graph
// scrolls left as samples come in.
static void island_draw_graph(CGContextRef context,
                              const signed char* values,
                              int count,
                              uint32_t color,
                              CGRect rect              ) {
  // Guide lines at 50% and 100%.
  island_set_fill(context, ISLAND_COLOR_WHITE, 0.08);
  CGContextFillRect(context, CGRectMake(rect.origin.x,
                                        CGRectGetMidY(rect),
                                        rect.size.width, 0.5));
  CGContextFillRect(context, CGRectMake(rect.origin.x,
                                        CGRectGetMaxY(rect) - 0.5,
                                        rect.size.width, 0.5      ));
  if (count < 2) return;

  CGFloat step = rect.size.width / (SYSTEM_STATS_HISTORY - 1);
  CGMutablePathRef line = CGPathCreateMutable();
  for (int i = 0; i < count; i++) {
    CGPoint point = { CGRectGetMaxX(rect) - (count - 1 - i) * step,
                      rect.origin.y
                      + rect.size.height * max(min(values[i], 100), 0) / 100.0 };
    if (i == 0) CGPathMoveToPoint(line, NULL, point.x, point.y);
    else CGPathAddLineToPoint(line, NULL, point.x, point.y);
  }

  CGFloat r = ((color >> 16) & 0xff) / 255.0;
  CGFloat g = ((color >> 8) & 0xff) / 255.0;
  CGFloat b = (color & 0xff) / 255.0;

  // Fill under the line with a fading gradient.
  CGMutablePathRef area = CGPathCreateMutableCopy(line);
  CGPathAddLineToPoint(area, NULL, CGRectGetMaxX(rect), rect.origin.y);
  CGPathAddLineToPoint(area, NULL,
                       CGRectGetMaxX(rect) - (count - 1) * step,
                       rect.origin.y                            );
  CGPathCloseSubpath(area);

  CGContextSaveGState(context);
  CGContextAddPath(context, area);
  CGContextClip(context);
  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGFloat components[] = { r, g, b, 0.35,  r, g, b, 0.02 };
  CGFloat locations[] = { 0, 1 };
  CGGradientRef gradient = CGGradientCreateWithColorComponents(color_space,
                                                               components,
                                                               locations,
                                                               2           );
  CGContextDrawLinearGradient(context, gradient,
                              CGPointMake(0, CGRectGetMaxY(rect)),
                              CGPointMake(0, rect.origin.y),
                              0                                  );
  CGGradientRelease(gradient);
  CGColorSpaceRelease(color_space);
  CGContextRestoreGState(context);

  CGContextSaveGState(context);
  CGContextAddPath(context, line);
  CGContextSetRGBStrokeColor(context, r, g, b, 1.0);
  CGContextSetLineWidth(context, ISLAND_GRAPH_LINE);
  CGContextSetLineJoin(context, kCGLineJoinRound);
  CGContextSetLineCap(context, kCGLineCapRound);
  CGContextStrokePath(context);
  CGContextRestoreGState(context);

  CGPathRelease(area);
  CGPathRelease(line);
}

static void island_draw_graph_card(CGContextRef context,
                                   const char* title,
                                   int value,
                                   const signed char* values,
                                   int count,
                                   uint32_t color,
                                   CGRect card              ) {
  island_draw_glass(context, card, ISLAND_CARD_RADIUS, 1.0);

  CGFloat x = card.origin.x + ISLAND_CARD_INSET;
  CGFloat right = CGRectGetMaxX(card) - ISLAND_CARD_INSET;
  CGFloat top = CGRectGetMaxY(card);

  island_draw_text(context, title, g_fonts.caption, 0.6,
                   x, top - 13, right - x, ALIGN_LEFT);

  char text[8];
  if (value < 0) snprintf(text, sizeof(text), "--");
  else snprintf(text, sizeof(text), "%d%%", value);
  island_draw_text_colored(context, text, g_fonts.title,
                           value < 0 ? ISLAND_COLOR_WHITE
                                     : island_level_color(value, false),
                           value < 0 ? 0.4 : 1.0,
                           right, top - 14, right - x, ALIGN_RIGHT      );

  CGRect graph = {{ x, card.origin.y + 7 },
                  { right - x, card.size.height - ISLAND_GRAPH_HEADER - 7 }};
  island_draw_graph(context, values, count, color, graph);
}

// The info card's Wi-Fi switch and settings button (line 2, right).
static void island_info_controls(CGRect card, CGRect* toggle, CGRect* settings) {
  island_controls_rects(CGRectGetMaxX(card) - ISLAND_CARD_INSET,
                        CGRectGetMaxY(card) - 24, toggle, settings);
}

static void island_draw_info_card(CGContextRef context,
                                  const struct system_info* info,
                                  double wifi_toggle,
                                  CGRect card                   ) {
  island_draw_glass(context, card, ISLAND_CARD_RADIUS, 1.0);

  CGFloat x = card.origin.x + ISLAND_CARD_INSET;
  CGFloat right = CGRectGetMaxX(card) - ISLAND_CARD_INSET;
  CGFloat top = CGRectGetMaxY(card);
  CGFloat text_x = x + ISLAND_INFO_ICON + 4;

  // Line 1: OS, chip and memory in what is left on the right.
  CGRect icon = {{ x, top - 17 }, { ISLAND_INFO_ICON, 12 }};
  island_draw_glyph(context, g_glyphs[GLYPH_APPLE], g_fonts.small_icon,
                    ISLAND_COLOR_WHITE, 0.8, icon                      );
  CGFloat os = island_draw_text(context, info->os, g_fonts.body, 0.9,
                                text_x, top - 14, right - text_x,
                                ALIGN_LEFT                        );
  island_draw_text(context, info->hardware, g_fonts.caption, 0.5,
                   right, top - 14, right - text_x - os - 8, ALIGN_RIGHT);

  // Line 2: the connection (the joined Wi-Fi network's name, green when
  // online), then the Wi-Fi settings button and switch.
  CGRect toggle, settings;
  island_info_controls(card, &toggle, &settings);

  icon.origin.y = top - 30;
  bool wired = info->network == SYSTEM_NETWORK_ETHERNET;
  bool connected = info->online && (wired || info->wifi_ssid[0]);
  island_draw_glyph(context, g_glyphs[wired ? GLYPH_ETHERNET : GLYPH_WIFI],
                    g_fonts.small_icon,
                    connected ? ISLAND_COLOR_GREEN : ISLAND_COLOR_WHITE,
                    connected ? 1.0 : 0.4, icon                        );
  island_draw_text(context, info->network_name, g_fonts.caption,
                   connected ? 0.8 : 0.5,
                   text_x, top - 27, settings.origin.x - 6 - text_x,
                   ALIGN_LEFT                                       );
  island_draw_settings_button(context, settings);
  if (info->wifi_available) island_draw_toggle(context, toggle, wifi_toggle);
}

// System tab, top to bottom: info card, brightness row (like the Music
// tab's volume row), CPU and RAM graphs side by side.
struct island_system_layout {
  CGRect info;
  CGRect sun;
  CGRect slider;
  CGRect slider_hit;
  CGRect label;
  CGRect cpu;
  CGRect ram;
};

static struct island_system_layout island_get_system_layout(
                                            struct island_layout* layout) {
  struct island_system_layout system;
  CGRect area = layout->list;

  system.info = (CGRect){{ area.origin.x,
                           CGRectGetMaxY(area) - ISLAND_INFO_HEIGHT },
                         { area.size.width, ISLAND_INFO_HEIGHT }};

  CGFloat row = system.info.origin.y - ISLAND_GRAPH_GAP
                - ISLAND_VOLUME_HEIGHT / 2.0;
  system.sun = (CGRect){{ area.origin.x + 2, row - ISLAND_VOLUME_HEIGHT / 2.0 },
                        { ISLAND_VOLUME_HEIGHT, ISLAND_VOLUME_HEIGHT }};
  CGFloat slider_x = CGRectGetMaxX(system.sun) + 6;
  system.slider = (CGRect){{ slider_x, row - ISLAND_SLIDER_HEIGHT / 2.0 },
                           { CGRectGetMaxX(area) - ISLAND_VOLUME_LABEL
                                                 - slider_x,
                             ISLAND_SLIDER_HEIGHT                     }};
  system.slider_hit = CGRectInset(system.slider, -ISLAND_SLIDER_KNOB / 2.0,
                                                 -ISLAND_VOLUME_HEIGHT / 2.0);
  system.label = (CGRect){{ CGRectGetMaxX(area) - ISLAND_VOLUME_LABEL,
                            row - ISLAND_VOLUME_HEIGHT / 2.0          },
                          { ISLAND_VOLUME_LABEL, ISLAND_VOLUME_HEIGHT }};

  CGFloat width = (area.size.width - ISLAND_GRAPH_GAP) / 2.0;
  CGFloat height = row - ISLAND_VOLUME_HEIGHT / 2.0 - ISLAND_GRAPH_GAP
                   - area.origin.y;
  system.cpu = (CGRect){{ area.origin.x, area.origin.y }, { width, height }};
  system.ram = (CGRect){{ CGRectGetMaxX(area) - width, area.origin.y },
                        { width, height }};
  return system;
}

static void island_draw_system(CGContextRef context,
                               struct island_layout* layout) {
  if (!g_data || !g_data->stats || !g_data->history || !g_data->info) return;
  const struct system_stats_history* history = g_data->history;
  struct island_system_layout system = island_get_system_layout(layout);

  island_draw_info_card(context, g_data->info,
                        g_drawing
                        ? g_drawing->toggle_progress[ISLAND_TOGGLE_WIFI]
                        : 0,
                        system.info                                     );

  // Brightness
  bool has = g_data->has_brightness;
  island_draw_glyph(context, g_glyphs[GLYPH_SUN], g_fonts.small_icon,
                    has ? ISLAND_COLOR_YELLOW : ISLAND_COLOR_WHITE,
                    has ? 0.95 : 0.3, system.sun                    );
  island_draw_slider(context, system.slider, has, g_data->brightness,
                     ISLAND_COLOR_YELLOW, 1.0                       );
  char label[8];
  if (has) snprintf(label, sizeof(label), "%d%%",
                    (int)round(g_data->brightness * 100));
  else snprintf(label, sizeof(label), "--");
  island_draw_text(context, label, g_fonts.caption, 0.7,
                   CGRectGetMaxX(system.label),
                   CGRectGetMidY(system.label) - 3,
                   system.label.size.width, ALIGN_RIGHT);

  CGRect cpu = system.cpu;
  CGRect ram = system.ram;

  island_draw_graph_card(context, "CPU", g_data->stats->cpu,
                         history->cpu, history->count,
                         ISLAND_COLOR_BLUE, cpu             );
  island_draw_graph_card(context, "RAM", g_data->stats->ram,
                         history->ram, history->count,
                         ISLAND_COLOR_MAGENTA, ram          );
}

// ---------------------------------------------------------------- bluetooth

// A header (icon, title, settings button, on/off switch) and the paired
// devices below it. Connect results show as a banner at the bottom.
struct island_bluetooth_layout {
  CGRect page;
  CGRect header;
  CGRect toggle;
  CGRect settings;
  CGRect list;
  CGRect notice;
};

static struct island_bluetooth_layout island_get_bluetooth_layout(
                                              struct island_layout* layout) {
  struct island_bluetooth_layout b;
  b.page = island_page_rect(layout);
  CGFloat top = CGRectGetMaxY(b.page);

  b.header = (CGRect){{ b.page.origin.x, top - ISLAND_COLUMN_HEADER },
                      { b.page.size.width, ISLAND_COLUMN_HEADER }};
  island_controls_rects(CGRectGetMaxX(b.header), CGRectGetMidY(b.header),
                        &b.toggle, &b.settings                          );
  b.list = (CGRect){{ b.page.origin.x, b.page.origin.y },
                    { b.page.size.width,
                      b.header.origin.y - 4 - b.page.origin.y }};
  b.notice = (CGRect){{ b.page.origin.x, b.page.origin.y },
                      { b.page.size.width, ISLAND_NOTICE_BAR }};
  return b;
}

static int island_bluetooth_rows(const struct connections* connections) {
  return connections->bluetooth_on ? connections->bluetooth_count : 0;
}

static CGFloat island_bluetooth_max_scroll(const struct connections* conn,
                                           struct island_bluetooth_layout* b) {
  return max(island_bluetooth_rows(conn) * ISLAND_ROW_HEIGHT
             - b->list.size.height, 0);
}

static CGRect island_bluetooth_row(struct island_bluetooth_layout* b,
                                   int row,
                                   CGFloat scroll_offset            ) {
  CGFloat top = CGRectGetMaxY(b->list) - row * ISLAND_ROW_HEIGHT
                + scroll_offset;
  return (CGRect){{ b->list.origin.x, top - ISLAND_ROW_HEIGHT },
                  { b->list.size.width, ISLAND_ROW_HEIGHT }};
}

// Battery icon for a level: full above 80%, then 3/4, 1/2, 1/4 and empty
// (10% and below).
static const char* island_battery_glyph(int percent) {
  if (percent > 80) return g_glyphs[GLYPH_BATTERY_4];
  if (percent > 60) return g_glyphs[GLYPH_BATTERY_3];
  if (percent > 35) return g_glyphs[GLYPH_BATTERY_2];
  if (percent > 10) return g_glyphs[GLYPH_BATTERY_1];
  return g_glyphs[GLYPH_BATTERY_0];
}

// The battery of a device right-aligned at `right` in `row`: icon in the
// level's color and the percentage. Returns the width it took.
static CGFloat island_draw_device_battery(CGContextRef context,
                                          CGRect row,
                                          CGFloat right,
                                          int percent      ) {
  char text[8];
  snprintf(text, sizeof(text), "%d%%", percent);
  CGFloat width = island_draw_text(context, text, g_fonts.caption, 0.75,
                                   right, CGRectGetMidY(row) - 3,
                                   row.size.width, ALIGN_RIGHT       );

  CGRect icon = {{ right - width - 4 - ISLAND_BATTERY_ICON, row.origin.y },
                 { ISLAND_BATTERY_ICON, row.size.height }};
  island_draw_glyph(context, island_battery_glyph(percent), g_fonts.small_icon,
                    island_level_color(percent, true), 1.0, icon             );
  return width + 4 + ISLAND_BATTERY_ICON;
}

static void island_draw_bluetooth_message(CGContextRef context,
                                          CGRect list,
                                          const char* text      ) {
  island_draw_text(context, text, g_fonts.caption, 0.4,
                   CGRectGetMidX(list), CGRectGetMaxY(list) - 24,
                   list.size.width, ALIGN_CENTER                 );
}

// Bottom banner with the last connect attempt.
static void island_draw_notice(CGContextRef context,
                               struct island_bluetooth_layout* b,
                               const struct connections* connections) {
  CGRect notice = b->notice;
  island_add_rounded_rect(context, notice, notice.size.height / 2.0);
  island_set_fill(context, 0xff000000, 0.85);
  CGContextFillPath(context);
  island_draw_glass(context, notice, notice.size.height / 2.0, 1.4);

  const char* target = connections->status_target;
  bool off = connections->status_disconnect;
  const char* glyph = NULL;
  uint32_t color = ISLAND_COLOR_WHITE;
  char text[CONNECTIONS_NAME + 32];
  switch (connections->status) {
    case CONNECTION_SUCCEEDED:
      glyph = g_glyphs[GLYPH_CHECK];
      color = ISLAND_COLOR_GREEN;
      snprintf(text, sizeof(text), off ? "Disconnected %s" : "Connected to %s",
               target                                                     );
      break;
    case CONNECTION_FAILED:
      glyph = g_glyphs[GLYPH_CROSS];
      color = ISLAND_COLOR_RED;
      snprintf(text, sizeof(text), off ? "Couldn't disconnect %s"
                                       : "Couldn't connect to %s", target);
      break;
    default:
      snprintf(text, sizeof(text), off ? "Disconnecting %s…"
                                       : "Connecting to %s…", target);
      break;
  }

  CGFloat x = notice.origin.x + 10;
  if (glyph) {
    island_draw_glyph(context, glyph, g_fonts.small_icon, color, 1.0,
                      CGRectMake(x, notice.origin.y, 12,
                                 notice.size.height     ));
    x += 17;
  }
  island_draw_text_colored(context, text, g_fonts.caption, color, 0.95,
                           x, CGRectGetMidY(notice) - 3,
                           CGRectGetMaxX(notice) - 10 - x, ALIGN_LEFT);
}

static void island_draw_bluetooth(CGContextRef context,
                                  struct island_layout* layout,
                                  CGFloat scroll_offset       ) {
  if (!g_data || !g_data->connections || !g_drawing) return;
  struct island* island = g_drawing;
  const struct connections* connections = g_data->connections;
  struct island_bluetooth_layout b = island_get_bluetooth_layout(layout);
  bool on = connections->bluetooth_on;

  // Header
  CGRect icon = {{ b.header.origin.x, b.header.origin.y },
                 { 14, b.header.size.height }};
  island_draw_glyph(context, g_glyphs[GLYPH_BLUETOOTH], g_fonts.small_icon,
                    ISLAND_COLOR_WHITE, on ? 0.9 : 0.4, icon               );
  island_draw_text(context, "Bluetooth", g_fonts.title, on ? 0.95 : 0.5,
                   b.header.origin.x + 18, CGRectGetMidY(b.header) - 4,
                   b.settings.origin.x - b.header.origin.x - 22,
                   ALIGN_LEFT                                     );
  island_draw_settings_button(context, b.settings);
  island_draw_toggle(context, b.toggle,
                     island->toggle_progress[ISLAND_TOGGLE_BLUETOOTH]);

  // Devices
  if (!connections->bluetooth_available) {
    island_draw_bluetooth_message(context, b.list, "No Bluetooth");
  } else if (!on) {
    island_draw_bluetooth_message(context, b.list, "Off");
  } else if (connections->bluetooth_count == 0) {
    island_draw_bluetooth_message(context, b.list, "No paired devices");
  } else {
    CGContextSaveGState(context);
    CGContextClipToRect(context, b.list);
    for (int i = 0; i < connections->bluetooth_count; i++) {
      CGRect row = island_bluetooth_row(&b, i, scroll_offset);
      if (!CGRectIntersectsRect(row, b.list)) continue;
      const struct bluetooth_device* device = &connections->bluetooth[i];

      // Right to left: the tick for a connected device, then its battery.
      CGFloat reserved = 0;
      if (device->connected) {
        CGRect tick = {{ CGRectGetMaxX(row) - 10, row.origin.y },
                       { 10, row.size.height }};
        island_draw_glyph(context, g_glyphs[GLYPH_CHECK], g_fonts.small_icon,
                          ISLAND_COLOR_GREEN, 1.0, tick                      );
        reserved = 14;
      }
      if (device->battery >= 0) {
        reserved += island_draw_device_battery(context, row,
                                               CGRectGetMaxX(row) - reserved,
                                               device->battery              )
                    + 8;
      }
      island_draw_text_colored(context, device->name, g_fonts.body,
                               device->connected ? ISLAND_COLOR_GREEN
                                                 : ISLAND_COLOR_WHITE,
                               device->connected ? 0.95 : 0.75,
                               row.origin.x, CGRectGetMidY(row) - 3,
                               row.size.width - reserved, ALIGN_LEFT);
    }
    CGContextRestoreGState(context);
  }

  if (connections->status != CONNECTION_IDLE)
    island_draw_notice(context, &b, connections);
}

static bool island_handle_bluetooth_click(struct island* island,
                                          struct island_layout* layout,
                                          CGPoint point,
                                          struct island_action* action) {
  struct island_bluetooth_layout b = island_get_bluetooth_layout(layout);
  if (!CGRectContainsPoint(b.page, point)) return false;

  if (CGRectContainsPoint(CGRectInset(b.toggle, -4, -4), point)) {
    action->type = ISLAND_ACTION_BLUETOOTH_POWER;
    return true;
  }
  if (CGRectContainsPoint(CGRectInset(b.settings, -3, -3), point)) {
    action->type = ISLAND_ACTION_BLUETOOTH_SETTINGS;
    return true;
  }

  const struct connections* connections = connections_get();
  if (!CGRectContainsPoint(b.list, point)) return true;
  for (int row = 0; row < island_bluetooth_rows(connections); row++) {
    if (!CGRectContainsPoint(island_bluetooth_row(&b, row,
                                                  island->scroll_offset),
                             point                                    ))
      continue;
    action->type = ISLAND_ACTION_BLUETOOTH_SELECT;
    action->index = row;
    break;
  }
  return true;
}

// ---------------------------------------------------------------- screens

// A 3 x 2 grid of desktops 1..6, each with a purpose: icon, name, number.
// The current desktop is highlighted, desktops that don't exist are dimmed.
static const struct {
  const char* glyph;
  const char* fallback;
  const char* name;
} g_screens[ISLAND_SCREEN_COUNT] = {
  { "\xEF\x82\xB1", "W",  "Work"    },  // U+F0B1 briefcase
  { "\xEF\x82\xAC", "@",  "Web"     },  // U+F0AC globe
  { "\xEF\x84\xA1", "<>", "Code"    },  // U+F121 code
  { "\xEF\x84\xA1", "<>", "Code #2" },
  { "\xEF\x84\xA1", "<>", "Code #3" },
  { "\xEF\x86\xB2", "*",  "Misc"    },  // U+F1B2 cube
};

// Whether the last drawn Screens tab had the hint line, so clicks (handled
// outside of drawing) hit the tiles where they were drawn.
static bool g_screens_hint = false;

static CGRect island_screen_rect(struct island_layout* layout, int index) {
  CGRect area = island_page_rect(layout);
  if (g_screens_hint) {
    area.origin.y += ISLAND_SCREEN_HINT;
    area.size.height -= ISLAND_SCREEN_HINT;
  }

  CGFloat width = (area.size.width - (ISLAND_SCREEN_COLUMNS - 1)
                                     * ISLAND_SCREEN_GAP)
                  / ISLAND_SCREEN_COLUMNS;
  int rows = ISLAND_SCREEN_COUNT / ISLAND_SCREEN_COLUMNS;
  CGFloat height = (area.size.height - (rows - 1) * ISLAND_SCREEN_GAP) / rows;
  int row = index / ISLAND_SCREEN_COLUMNS;
  int column = index % ISLAND_SCREEN_COLUMNS;

  return (CGRect){{ area.origin.x + column * (width + ISLAND_SCREEN_GAP),
                    CGRectGetMaxY(area) - (row + 1) * height
                    - row * ISLAND_SCREEN_GAP                 },
                  { width, height }};
}

static void island_draw_screens(CGContextRef context,
                                struct island_layout* layout) {
  if (!g_data || !g_data->desktops) return;
  const struct desktops* desktops = g_data->desktops;
  g_screens_hint = g_data->desktops_problem != ISLAND_SCREENS_OK;

  for (int i = 0; i < ISLAND_SCREEN_COUNT; i++) {
    CGRect tile = island_screen_rect(layout, i);
    bool exists = i < desktops->count;
    bool active = i == desktops->active;
    CGFloat alpha = exists ? 1.0 : 0.35;

    if (active) {
      island_add_rounded_rect(context, tile, ISLAND_CARD_RADIUS);
      island_set_fill(context, ISLAND_COLOR_BLUE, 0.25);
      CGContextFillPath(context);
    }
    island_draw_glass(context, tile, ISLAND_CARD_RADIUS, exists ? 1.0 : 0.5);
    if (active) {
      CGContextSaveGState(context);
      island_add_rounded_rect(context, CGRectInset(tile, 0.5, 0.5),
                              ISLAND_CARD_RADIUS                  );
      CGContextSetRGBStrokeColor(context, 0x76 / 255.0, 0xcc / 255.0,
                                 0xe0 / 255.0, 0.9                  );
      CGContextSetLineWidth(context, 1.0);
      CGContextStrokePath(context);
      CGContextRestoreGState(context);
    }

    // Number in the corner, purpose icon in the middle, name below it.
    char number[4];
    snprintf(number, sizeof(number), "%d", i + 1);
    island_draw_text(context, number, g_fonts.caption, 0.5 * alpha,
                     tile.origin.x + 7, CGRectGetMaxY(tile) - 12,
                     tile.size.width, ALIGN_LEFT                 );

    CGRect icon = {{ tile.origin.x, CGRectGetMidY(tile) - 2 },
                   { tile.size.width, 20 }};
    island_draw_glyph(context,
                      g_has_icons ? g_screens[i].glyph : g_screens[i].fallback,
                      g_fonts.screen_icon,
                      active ? ISLAND_COLOR_BLUE : ISLAND_COLOR_WHITE,
                      (active ? 1.0 : 0.85) * alpha, icon             );
    island_draw_text(context, g_screens[i].name, g_fonts.caption,
                     (active ? 0.95 : 0.65) * alpha,
                     CGRectGetMidX(tile), tile.origin.y + 8,
                     tile.size.width - 8, ALIGN_CENTER        );
  }

  if (g_data->desktops_problem != ISLAND_SCREENS_OK) {
    CGRect page = island_page_rect(layout);
    island_draw_text(context,
                     g_data->desktops_problem == ISLAND_SCREENS_NO_SHORTCUTS
                     ? "Turn on Switch to Desktop shortcuts"
                     : "Allow SketchyBar in Accessibility",
                     g_fonts.caption, 0.45,
                     CGRectGetMidX(page), page.origin.y + 2,
                     page.size.width, ALIGN_CENTER             );
  }
}

static bool island_handle_screens_click(struct island_layout* layout,
                                        CGPoint point,
                                        struct island_action* action) {
  if (!CGRectContainsPoint(island_page_rect(layout), point)) return false;
  for (int i = 0; i < ISLAND_SCREEN_COUNT; i++) {
    if (!CGRectContainsPoint(island_screen_rect(layout, i), point)) continue;
    action->type = ISLAND_ACTION_SCREEN_SELECT;
    action->index = i;
    break;
  }
  return true;
}

// ---------------------------------------------------------------- apps

// Which apps' notifications are captured: a header with how many are on,
// one row per app (icon, name, switch) that scrolls like the Bluetooth list.
// Clicking anywhere on a row flips its switch.
struct island_apps_layout {
  CGRect page;
  CGRect header;
  CGRect list;
  CGRect track;
};

static struct island_apps_layout island_get_apps_layout(
                                        struct island_layout* layout) {
  struct island_apps_layout a;
  a.page = island_page_rect(layout);
  CGFloat top = CGRectGetMaxY(a.page);
  a.header = (CGRect){{ a.page.origin.x, top - ISLAND_COLUMN_HEADER },
                      { a.page.size.width, ISLAND_COLUMN_HEADER }};
  CGFloat height = a.header.origin.y - 4 - a.page.origin.y;
  a.track = (CGRect){{ CGRectGetMaxX(a.page) - ISLAND_SCROLL_WIDTH,
                       a.page.origin.y                             },
                     { ISLAND_SCROLL_WIDTH, height }};
  a.list = (CGRect){{ a.page.origin.x, a.page.origin.y },
                    { a.track.origin.x - ISLAND_SCROLL_GAP - a.page.origin.x,
                      height                                               }};
  return a;
}

static CGFloat island_apps_max_scroll(struct island_apps_layout* a) {
  return max(notifications_apps()->count * ISLAND_APP_ROW
             - a->list.size.height, 0                    );
}

static CGRect island_apps_row(struct island_apps_layout* a,
                              int row,
                              CGFloat scroll_offset        ) {
  CGFloat top = CGRectGetMaxY(a->list) - row * ISLAND_APP_ROW + scroll_offset;
  return (CGRect){{ a->list.origin.x, top - ISLAND_APP_ROW },
                  { a->list.size.width, ISLAND_APP_ROW }};
}

// The switch's position: on/off, or sliding if it was just clicked.
static double island_app_toggle_value(struct island* island,
                                      const struct notification_app* app) {
  double value = app->enabled ? 1.0 : 0.0;
  if (island->app_toggle_progress < 1.0
      && strcmp(island->app_toggle, app->name) == 0) {
    double t = max(island->app_toggle_progress, 0.0);
    value = app->enabled ? t : 1.0 - t;
  }
  return value;
}

static void island_draw_apps(CGContextRef context,
                             struct island_layout* layout,
                             CGFloat scroll_offset       ) {
  if (!g_drawing) return;
  struct island* island = g_drawing;
  const struct notification_apps* apps = notifications_apps();
  struct island_apps_layout a = island_get_apps_layout(layout);

  // Header: bell, title, "N of M on".
  int on = 0;
  for (int i = 0; i < apps->count; i++) on += apps->items[i].enabled;
  char count[32];
  snprintf(count, sizeof(count), "%d of %d on", on, apps->count);
  CGFloat count_width = island_draw_text(context, count, g_fonts.caption, 0.5,
                                         CGRectGetMaxX(a.header),
                                         CGRectGetMidY(a.header) - 3,
                                         a.header.size.width, ALIGN_RIGHT);
  CGRect icon = {{ a.header.origin.x, a.header.origin.y },
                 { 14, a.header.size.height }};
  island_draw_glyph(context, g_glyphs[GLYPH_BELL], g_fonts.small_icon,
                    ISLAND_COLOR_WHITE, 0.9, icon                     );
  island_draw_text(context, "Capture from", g_fonts.title, 0.95,
                   a.header.origin.x + 18, CGRectGetMidY(a.header) - 4,
                   a.header.size.width - 18 - count_width - 6, ALIGN_LEFT);

  if (apps->count == 0) {
    island_draw_bluetooth_message(context, a.list, "No apps found");
    return;
  }

  CGContextSaveGState(context);
  CGContextClipToRect(context, a.list);
  for (int i = 0; i < apps->count; i++) {
    CGRect row = island_apps_row(&a, i, scroll_offset);
    if (!CGRectIntersectsRect(row, a.list)) continue;
    const struct notification_app* app = &apps->items[i];
    CGFloat mid = CGRectGetMidY(row);

    // The cards' app icon, scaled down.
    CGFloat scale = (CGFloat)ISLAND_APP_ROW_ICON / ISLAND_APP_ICON_SIZE;
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, row.origin.x,
                          mid - ISLAND_APP_ROW_ICON / 2.0);
    CGContextScaleCTM(context, scale, scale);
    if (!app->enabled) CGContextSetAlpha(context, 0.45);
    island_draw_app_icon(context, app->name,
                         CGRectMake(0, 0, ISLAND_APP_ICON_SIZE,
                                    ISLAND_APP_ICON_SIZE       ));
    CGContextRestoreGState(context);

    CGRect toggle = {{ CGRectGetMaxX(row) - ISLAND_TOGGLE_WIDTH,
                       mid - ISLAND_TOGGLE_HEIGHT / 2.0          },
                     { ISLAND_TOGGLE_WIDTH, ISLAND_TOGGLE_HEIGHT }};
    island_draw_toggle(context, toggle, island_app_toggle_value(island, app));

    CGFloat x = row.origin.x + ISLAND_APP_ROW_ICON + 7;
    island_draw_text(context, app->name, g_fonts.body,
                     app->enabled ? 0.9 : 0.45,
                     x, mid - 3, toggle.origin.x - 6 - x, ALIGN_LEFT);
  }
  CGContextRestoreGState(context);

  island_draw_scroll_bar(context, a.track, apps->count * ISLAND_APP_ROW,
                         a.list.size.height, scroll_offset             );
}

static bool island_handle_apps_click(struct island* island,
                                     struct island_layout* layout,
                                     CGPoint point,
                                     struct island_action* action) {
  struct island_apps_layout a = island_get_apps_layout(layout);
  if (!CGRectContainsPoint(a.page, point)) return false;
  if (!CGRectContainsPoint(a.list, point)) return true;

  const struct notification_apps* apps = notifications_apps();
  for (int i = 0; i < apps->count; i++) {
    if (!CGRectContainsPoint(island_apps_row(&a, i, island->scroll_offset),
                             point                                        )) {
      continue;
    }
    action->type = ISLAND_ACTION_APP_TOGGLE;
    action->index = i;
    break;
  }
  return true;
}

// ---------------------------------------------------------------- claude

// While Claude can't be reached: Clawd asleep and "Claude is currently busy"
// (a click checks again). Otherwise two subtabs, Sessions and Top models,
// switched by a segmented control on top; their pages lie side by side and
// slide over (the later one comes in from the right).
struct island_claude_layout {
  CGRect page;
  CGRect tabs;
  CGRect tab[ISLAND_CLAUDE_SUBTABS];
  CGRect content;
  CGRect list;
  CGRect track;
};

static struct island_claude_layout island_get_claude_layout(
                                          struct island_layout* layout) {
  struct island_claude_layout c;
  c.page = island_page_rect(layout);
  CGFloat top = CGRectGetMaxY(c.page);
  // Without the Top models plugin there is nothing to switch to: no
  // segmented control, the sessions take the whole page.
  CGFloat tabs = claude_plugin(CLAUDE_PLUGIN_MODELS) ? ISLAND_CLAUDE_TABS : 0;
  c.tabs = (CGRect){{ c.page.origin.x, top - tabs },
                    { c.page.size.width, tabs }};
  CGFloat half = floor(c.page.size.width / 2.0);
  c.tab[0] = (CGRect){ c.tabs.origin, { half, tabs } };
  c.tab[1] = (CGRect){{ c.tabs.origin.x + half, c.tabs.origin.y },
                      { c.tabs.size.width - half, tabs }};
  c.content = (CGRect){{ c.page.origin.x, c.page.origin.y },
                       { c.page.size.width,
                         c.tabs.origin.y - (tabs > 0 ? 5 : 0)
                         - c.page.origin.y                    }};
  c.track = (CGRect){{ CGRectGetMaxX(c.content) - ISLAND_SCROLL_WIDTH,
                       c.content.origin.y                             },
                     { ISLAND_SCROLL_WIDTH, c.content.size.height }};
  c.list = (CGRect){ c.content.origin,
                     { c.track.origin.x - ISLAND_SCROLL_GAP
                                        - c.content.origin.x,
                       c.content.size.height                  }};
  return c;
}

static int island_claude_rows(int subtab) {
  const struct claude_state* state = claude_get();
  return subtab == ISLAND_CLAUDE_SESSIONS ? state->session_count
                                          : state->model_count;
}

static CGFloat island_claude_list_height(int subtab) {
  int rows = island_claude_rows(subtab);
  return rows > 0 ? rows * ISLAND_CLAUDE_ROW - ISLAND_CLAUDE_ROW_GAP : 0;
}

static CGFloat island_claude_max_scroll(struct island_claude_layout* c,
                                        int subtab                    ) {
  return max(island_claude_list_height(subtab) - c->list.size.height, 0);
}

static CGRect island_claude_row(struct island_claude_layout* c,
                                int row,
                                CGFloat scroll_offset         ) {
  CGFloat top = CGRectGetMaxY(c->list) - row * ISLAND_CLAUDE_ROW
                + scroll_offset;
  CGFloat height = ISLAND_CLAUDE_ROW - ISLAND_CLAUDE_ROW_GAP;
  return (CGRect){{ c->list.origin.x, top - height },
                  { c->list.size.width, height }};
}

static CGRect island_claude_close_rect(CGRect row) {
  return (CGRect){{ CGRectGetMaxX(row) - 7 - ISLAND_CLAUDE_CLOSE,
                    CGRectGetMidY(row) - ISLAND_CLAUDE_CLOSE / 2.0 },
                  { ISLAND_CLAUDE_CLOSE, ISLAND_CLAUDE_CLOSE }};
}

static double island_claude_progress(struct island* island) {
  if (island->claude_previous_subtab == island->claude_subtab) return 1.0;
  return max(min(island->claude_transition, 1.0), 0.0);
}

// "1M", "393K", "128K".
static void island_format_tokens(int tokens, char* out, size_t size) {
  if (tokens >= 1000000) {
    double millions = tokens / 1000000.0;
    snprintf(out, size, fabs(millions - round(millions)) < 0.05 ? "%.0fM"
                                                                 : "%.1fM",
             millions                                                      );
  } else if (tokens >= 1000) {
    snprintf(out, size, "%.0fK", tokens / 1000.0);
  } else {
    snprintf(out, size, "%d", tokens);
  }
}

// "4", "0.2", "0.03": two decimals at most, no trailing zeros.
static void island_format_price(double price, char* out, size_t size) {
  snprintf(out, size, "%.2f", price);
  char* end = out + strlen(out) - 1;
  while (end > out && *end == '0') *end-- = '\0';
  if (end > out && *end == '.') *end = '\0';
}

static void island_draw_claude_busy(CGContextRef context,
                                    struct island_claude_layout* c,
                                    int reachability              ) {
  CGRect page = c->page;
  CGRect sprite = {{ CGRectGetMidX(page) - 44, CGRectGetMidY(page) - 6 },
                   { 88, 50 }};
  bool checking = reachability == CLAUDE_CHECKING;
  island_draw_clawd(context, sprite, checking ? CLAWD_BUSY : CLAWD_SLEEPING,
                    1.0                                                   );
  island_draw_text(context,
                   checking ? "Checking on Claude…" : "Claude is currently busy",
                   g_fonts.title, 0.95,
                   CGRectGetMidX(page), sprite.origin.y - 14,
                   page.size.width, ALIGN_CENTER             );
  if (!checking) {
    island_draw_text(context, "Can't reach Claude from here · click to retry",
                     g_fonts.caption, 0.45,
                     CGRectGetMidX(page), sprite.origin.y - 27,
                     page.size.width, ALIGN_CENTER             );
  }
}

static void island_draw_claude_tabs(CGContextRef context,
                                    struct island* island,
                                    struct island_claude_layout* c) {
  island_add_rounded_rect(context, c->tabs, ISLAND_CLAUDE_TABS / 2.0);
  island_set_fill(context, ISLAND_COLOR_WHITE, 0.06);
  CGContextFillPath(context);

  // The pill glides between the halves.
  double t = island_claude_progress(island);
  CGRect from = c->tab[island->claude_previous_subtab];
  CGRect pill = c->tab[island->claude_subtab];
  pill.origin.x = from.origin.x + (pill.origin.x - from.origin.x) * t;
  island_draw_glass(context, CGRectInset(pill, 1, 1),
                    ISLAND_CLAUDE_TABS / 2.0 - 1, 1.2);

  char sessions[32];
  int count = claude_get()->session_count;
  if (count > 0) snprintf(sessions, sizeof(sessions), "Sessions · %d", count);
  else snprintf(sessions, sizeof(sessions), "Sessions");
  const char* labels[ISLAND_CLAUDE_SUBTABS] = { sessions, "Top models" };
  for (int i = 0; i < ISLAND_CLAUDE_SUBTABS; i++) {
    double weight = 0;
    if (i == island->claude_subtab) weight += t;
    if (i == island->claude_previous_subtab) weight += 1.0 - t;
    island_draw_text(context, labels[i], g_fonts.body, 0.5 + 0.45 * weight,
                     CGRectGetMidX(c->tab[i]), CGRectGetMidY(c->tab[i]) - 3,
                     c->tab[i].size.width - 8, ALIGN_CENTER                );
  }
}

static void island_draw_claude_card(CGContextRef context, CGRect row) {
  island_add_rounded_rect(context, row, 8);
  island_set_fill(context, 0xff000000, 0.6);
  CGContextFillPath(context);
  island_draw_glass(context, row, 8, 0.9);
}

static void island_draw_claude_sessions(CGContextRef context,
                                        struct island_claude_layout* c,
                                        CGFloat scroll_offset         ) {
  const struct claude_state* state = claude_get();
  if (state->session_count == 0) {
    island_draw_bluetooth_message(context, c->list, "No Claude sessions running");
    return;
  }

  for (int i = 0; i < state->session_count; i++) {
    CGRect row = island_claude_row(c, i, scroll_offset);
    if (!CGRectIntersectsRect(row, c->list)) continue;
    const struct claude_session* session = &state->sessions[i];
    island_draw_claude_card(context, row);

    CGRect sprite = {{ row.origin.x + 5, CGRectGetMidY(row) - 8 }, { 24, 16 }};
    island_draw_clawd(context, sprite, session->busy ? CLAWD_BUSY : CLAWD_IDLE,
                      session->busy ? 1.0 : 0.7                              );

    CGRect close = island_claude_close_rect(row);
    island_draw_glass(context, close, close.size.width / 2.0, 1.3);
    island_draw_glyph(context, g_glyphs[GLYPH_CROSS], g_fonts.stats_icon,
                      ISLAND_COLOR_WHITE, 0.8, close                     );

    CGFloat x = CGRectGetMaxX(sprite) + 7;
    CGFloat width = close.origin.x - 6 - x;
    CGFloat top = CGRectGetMaxY(row);
    island_draw_text(context, session->name, g_fonts.title, 0.95,
                     x, top - 12, width, ALIGN_LEFT              );

    char when[16], details[192];
    island_relative_time(session->started, when, sizeof(when));
    if (session->busy) {
      snprintf(details, sizeof(details), "Working · %s · %s",
               session->project, session->where              );
    } else {
      snprintf(details, sizeof(details), "%s · %s · %s",
               session->project, session->where, when    );
    }
    island_draw_text_colored(context, details, g_fonts.caption,
                             session->busy ? ISLAND_COLOR_CLAUDE
                                           : ISLAND_COLOR_WHITE,
                             session->busy ? 0.95 : 0.5,
                             x, top - 23, width, ALIGN_LEFT      );
  }
  island_draw_scroll_bar(context, c->track,
                         island_claude_list_height(ISLAND_CLAUDE_SESSIONS),
                         c->list.size.height, scroll_offset               );
}

static void island_draw_claude_models(CGContextRef context,
                                      struct island_claude_layout* c,
                                      CGFloat scroll_offset         ) {
  const struct claude_state* state = claude_get();
  if (state->model_count == 0) {
    island_draw_bluetooth_message(context, c->list,
                                  state->models_loading
                                  ? "Capturing the top models…"
                                  : state->models_failed
                                    ? "Couldn't capture models · click to retry"
                                    : "No models captured yet"              );
    return;
  }

  for (int i = 0; i < state->model_count; i++) {
    CGRect row = island_claude_row(c, i, scroll_offset);
    if (!CGRectIntersectsRect(row, c->list)) continue;
    const struct claude_model* model = &state->models[i];
    island_draw_claude_card(context, row);

    // Rank badge, Claude's color for Anthropic's models.
    bool anthropic = strcasecmp(model->provider, "Anthropic") == 0;
    CGRect badge = {{ row.origin.x + 6, CGRectGetMidY(row) - 8 }, { 16, 16 }};
    island_add_rounded_rect(context, badge, 8);
    island_set_fill(context, anthropic ? ISLAND_COLOR_CLAUDE
                                       : ISLAND_COLOR_WHITE,
                    anthropic ? 0.9 : 0.14                  );
    CGContextFillPath(context);
    char rank[4];
    snprintf(rank, sizeof(rank), "%d", i + 1);
    island_draw_text(context, rank, g_fonts.caption, 0.95,
                     CGRectGetMidX(badge), CGRectGetMidY(badge) - 3,
                     badge.size.width, ALIGN_CENTER                );

    CGFloat x = CGRectGetMaxX(badge) + 7;
    CGFloat right = CGRectGetMaxX(row) - 7;
    CGFloat top = CGRectGetMaxY(row);

    // Name, and its weekly usage on the right.
    CGFloat usage_width = 0;
    if (model->usage[0]) {
      char usage[24];
      snprintf(usage, sizeof(usage), "%s/wk", model->usage);
      usage_width = island_draw_text_colored(context, usage, g_fonts.caption,
                                             ISLAND_COLOR_CLAUDE, 0.9,
                                             right, top - 12, 60, ALIGN_RIGHT);
    }
    island_draw_text(context, model->name, g_fonts.title, 0.95,
                     x, top - 12, right - x - usage_width - 6, ALIGN_LEFT);

    // Provider · context · output · price · features.
    char details[256], context_text[16], output[16];
    int length = snprintf(details, sizeof(details), "%s", model->provider);
    if (model->context > 0) {
      island_format_tokens(model->context, context_text, sizeof(context_text));
      length += snprintf(details + length, sizeof(details) - length,
                         " · %s ctx", context_text                  );
    }
    if (model->max_output > 0 && length < sizeof(details)) {
      island_format_tokens(model->max_output, output, sizeof(output));
      length += snprintf(details + length, sizeof(details) - length,
                         " · %s out", output                        );
    }
    if (model->price_in >= 0 && model->price_out >= 0
        && length < sizeof(details)                  ) {
      if (model->price_in == 0 && model->price_out == 0) {
        length += snprintf(details + length, sizeof(details) - length,
                           " · free"                                  );
      } else {
        char in[16], out[16];
        island_format_price(model->price_in, in, sizeof(in));
        island_format_price(model->price_out, out, sizeof(out));
        length += snprintf(details + length, sizeof(details) - length,
                           " · $%s/$%s", in, out                      );
      }
    }
    if (model->features[0] && length < sizeof(details)) {
      snprintf(details + length, sizeof(details) - length, " · %s",
               model->features                                     );
    }
    island_draw_text(context, details, g_fonts.caption, 0.5,
                     x, top - 23, right - x, ALIGN_LEFT     );
  }
  island_draw_scroll_bar(context, c->track,
                         island_claude_list_height(ISLAND_CLAUDE_MODELS),
                         c->list.size.height, scroll_offset             );
}

static void island_draw_claude_subtab(CGContextRef context,
                                      struct island_claude_layout* c,
                                      int subtab,
                                      CGFloat scroll_offset,
                                      CGFloat dx,
                                      CGFloat alpha                 ) {
  CGContextSaveGState(context);
  CGContextTranslateCTM(context, dx, 0);
  CGContextSetAlpha(context, alpha);
  if (subtab == ISLAND_CLAUDE_SESSIONS)
    island_draw_claude_sessions(context, c, scroll_offset);
  else
    island_draw_claude_models(context, c, scroll_offset);
  CGContextRestoreGState(context);
}

static void island_draw_claude(CGContextRef context,
                               struct island_layout* layout,
                               CGFloat scroll_offset       ) {
  if (!g_drawing) return;
  struct island* island = g_drawing;
  struct island_claude_layout c = island_get_claude_layout(layout);
  const struct claude_state* state = claude_get();

  if (state->reachability != CLAUDE_REACHABLE) {
    island_draw_claude_busy(context, &c, state->reachability);
    return;
  }

  if (!claude_plugin(CLAUDE_PLUGIN_MODELS)) {
    island->claude_subtab = island->claude_previous_subtab
                          = ISLAND_CLAUDE_SESSIONS;
  } else {
    island_draw_claude_tabs(context, island, &c);
  }

  CGContextSaveGState(context);
  CGContextClipToRect(context, c.content);
  double t = island_claude_progress(island);
  if (t >= 1.0) {
    island_draw_claude_subtab(context, &c, island->claude_subtab,
                              scroll_offset, 0, 1.0              );
  } else {
    // The later subtab lies to the right: going there moves both pages
    // left, going back moves them right.
    CGFloat direction = island->claude_subtab > island->claude_previous_subtab
                        ? -1 : 1;
    CGFloat distance = c.content.size.width + ISLAND_PAGE_GAP;
    CGFloat fade = 1.0 - ISLAND_PAGE_MIN_ALPHA;
    island_draw_claude_subtab(context, &c, island->claude_previous_subtab,
                              island->claude_previous_scroll,
                              direction * distance * t, 1.0 - fade * t    );
    island_draw_claude_subtab(context, &c, island->claude_subtab,
                              scroll_offset,
                              -direction * distance * (1.0 - t),
                              ISLAND_PAGE_MIN_ALPHA + fade * t  );
  }
  CGContextRestoreGState(context);
}

static bool island_handle_claude_click(struct island* island,
                                       struct island_layout* layout,
                                       CGPoint point,
                                       struct island_action* action) {
  struct island_claude_layout c = island_get_claude_layout(layout);
  if (!CGRectContainsPoint(c.page, point)) return false;
  const struct claude_state* state = claude_get();

  if (state->reachability != CLAUDE_REACHABLE) {
    action->type = ISLAND_ACTION_CLAUDE_RETRY;
    return true;
  }

  for (int i = 0; i < ISLAND_CLAUDE_SUBTABS; i++) {
    if (!CGRectContainsPoint(c.tab[i], point)) continue;
    if (island->claude_subtab != i) {
      island->claude_previous_subtab = island->claude_subtab;
      island->claude_previous_scroll = island->scroll_offset;
      island->claude_subtab = i;
      island->claude_transition = 0;
      island->scroll_offset = 0;
      island->scroll_target = 0;
      action->type = ISLAND_ACTION_CLAUDE_SUBTAB;
    }
    return true;
  }
  if (!CGRectContainsPoint(c.list, point)) return true;

  if (island->claude_subtab == ISLAND_CLAUDE_MODELS) {
    if (state->model_count == 0 && !state->models_loading)
      action->type = ISLAND_ACTION_CLAUDE_REFRESH;
    return true;
  }

  for (int i = 0; i < state->session_count; i++) {
    CGRect row = island_claude_row(&c, i, island->scroll_offset);
    if (!CGRectContainsPoint(row, point)) continue;
    bool close = CGRectContainsPoint(
                   CGRectInset(island_claude_close_rect(row), -4, -4), point);
    action->type = close ? ISLAND_ACTION_CLAUDE_STOP
                         : ISLAND_ACTION_CLAUDE_OPEN;
    action->index = i;
    break;
  }
  return true;
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
      island_draw_music(context, layout);
      break;
    case ISLAND_SERVICE_SYSTEM:
      island_draw_system(context, layout);
      break;
    case ISLAND_SERVICE_BLUETOOTH:
      island_draw_bluetooth(context, layout, scroll_offset);
      break;
    case ISLAND_SERVICE_SCREENS:
      island_draw_screens(context, layout);
      break;
    case ISLAND_SERVICE_APPS:
      island_draw_apps(context, layout, scroll_offset);
      break;
    case ISLAND_SERVICE_CLAUDE:
      island_draw_claude(context, layout, scroll_offset);
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
    CGFloat direction = island_tab_slot(island->service)
                        > island_tab_slot(island->previous_service) ? 1 : -1;
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

static double island_content_alpha(double progress) {
  double alpha = (progress - ISLAND_FADE_START) / (1.0 - ISLAND_FADE_START);
  return max(min(alpha, 1.0), 0.0);
}

// Today's date in the band's left side, one part per row (day, month name,
// year),
// mirroring the stats on the right. Read at draw time; the island redraws at
// least every BAR_STATS_INTERVAL while open, so it rolls over at midnight.
static void island_draw_date(CGContextRef context, CGRect area) {
  if (area.size.width <= 0 || area.size.height <= 0) return;

  time_t now = time(NULL);
  struct tm local;
  localtime_r(&now, &local);

  // Month as its short name ("Nov"); the C locale keeps it English.
  static const char* formats[ISLAND_DATE_ROWS] = { "%d", "%b", "%y" };
  CGFloat row_height = area.size.height / ISLAND_DATE_ROWS;
  CGFloat offset = (CTFontGetAscent(g_fonts.stats)
                    - CTFontGetDescent(g_fonts.stats)) / 2.0;

  for (int i = 0; i < ISLAND_DATE_ROWS; i++) {
    char text[8];
    strftime(text, sizeof(text), formats[i], &local);
    CGFloat mid = CGRectGetMaxY(area) - (i + 0.5) * row_height;
    // The day stands out, month and year are quieter.
    island_draw_text_colored(context, text, g_fonts.stats,
                             i == 0 ? ISLAND_COLOR_BLUE : ISLAND_COLOR_WHITE,
                             i == 0 ? 1.0 : 0.7,
                             area.origin.x, mid - offset,
                             area.size.width, ALIGN_LEFT                    );
  }
}

// Green while fine, through yellow and orange to red. For the battery a high
// value is good, for CPU and RAM a low one.
static uint32_t island_level_color(int percent, bool high_is_good) {
  int load = high_is_good ? 100 - percent : percent;
  if (load < 50) return ISLAND_COLOR_GREEN;
  if (load < 70) return ISLAND_COLOR_YELLOW;
  if (load < 85) return ISLAND_COLOR_ORANGE;
  return ISLAND_COLOR_RED;
}

static void island_draw_stats(CGContextRef context,
                              const struct system_stats* stats,
                              CGRect area                     ) {
  if (area.size.width <= 0 || area.size.height <= 0) return;

  const int values[STAT_COUNT] = { [STAT_BATTERY] = stats->battery,
                                   [STAT_CPU]     = stats->cpu,
                                   [STAT_RAM]     = stats->ram      };

  CGFloat row_height = area.size.height / STAT_COUNT;
  CGFloat text_offset = (CTFontGetAscent(g_fonts.stats)
                         - CTFontGetDescent(g_fonts.stats)) / 2.0;
  CGFloat icon_offset = (CTFontGetAscent(g_fonts.stats_icon)
                         - CTFontGetDescent(g_fonts.stats_icon)) / 2.0;

  for (int i = 0; i < STAT_COUNT; i++) {
    CGFloat mid = CGRectGetMaxY(area) - (i + 0.5) * row_height;
    int value = values[i];

    char text[8];
    if (value < 0) snprintf(text, sizeof(text), "--");
    else snprintf(text, sizeof(text), "%d%%", value);

    CGFloat value_width = island_draw_text(context, text, g_fonts.stats, 0.9,
                                           CGRectGetMaxX(area),
                                           mid - text_offset,
                                           area.size.width,
                                           ALIGN_RIGHT          );

    uint32_t color = value < 0
                     ? ISLAND_COLOR_WHITE
                     : island_level_color(value, i == STAT_BATTERY);
    island_draw_text_colored(context, g_stat_glyphs[i], g_fonts.stats_icon,
                             color, value < 0 ? 0.4 : 1.0,
                             area.origin.x, mid - icon_offset,
                             area.size.width - value_width - 2,
                             ALIGN_LEFT                              );
  }
}

void island_draw_band(struct island* island,
                      CGContextRef context,
                      CGRect left,
                      CGRect right,
                      const struct island_data* data,
                      double progress                  ) {
  if (!g_loaded) return;
  double alpha = island_content_alpha(progress);
  if (alpha <= 0) return;

  CGContextSaveGState(context);
  CGContextSetAlpha(context, alpha);
  island_draw_date(context,
                   CGRectInset(left,
                               ISLAND_STATS_INSET_X,
                               ISLAND_STATS_INSET_Y));
  island_draw_stats(context,
                    data->stats,
                    CGRectInset(right,
                                ISLAND_STATS_INSET_X,
                                ISLAND_STATS_INSET_Y));
  CGContextRestoreGState(context);
}

void island_draw(struct island* island,
                 CGContextRef context,
                 CGRect panel,
                 CGRect clip,
                 CGFloat corner_radius,
                 const struct island_data* data,
                 double progress                    ) {
  if (!g_loaded || panel.size.height <= 0) return;

  double alpha = island_content_alpha(progress);
  if (alpha <= 0) return;
  island_fix_service(island);
  g_data = data;
  g_drawing = island;

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
  g_data = NULL;
  g_drawing = NULL;
}

// ---------------------------------------------------------------- input

// Music tab clicks: transport, mute, volume. Any other click on the page is
// swallowed too, so using the controls never closes the island by accident.
static bool island_handle_music_click(struct island_layout* layout,
                                      CGPoint point,
                                      struct island_action* action) {
  struct island_music_layout music = island_get_music_layout(layout);

  if (CGRectContainsPoint(music.previous, point))
    action->type = ISLAND_ACTION_PREVIOUS;
  else if (CGRectContainsPoint(music.play, point))
    action->type = ISLAND_ACTION_PLAY_PAUSE;
  else if (CGRectContainsPoint(music.next, point))
    action->type = ISLAND_ACTION_NEXT;
  else if (CGRectContainsPoint(music.speaker, point))
    action->type = ISLAND_ACTION_TOGGLE_MUTE;
  else if (CGRectContainsPoint(music.slider_hit, point)) {
    action->type = ISLAND_ACTION_SET_VOLUME;
    action->value = island_slider_value(&music, point);
  }

  return CGRectContainsPoint(island_page_rect(layout), point);
}

bool island_handle_drag(struct island* island,
                        CGRect panel,
                        CGPoint point,
                        struct island_action* action) {
  action->type = ISLAND_ACTION_NONE;
  if (!g_loaded) return false;
  struct island_layout layout = island_get_layout(panel);

  if (island->service == ISLAND_SERVICE_SYSTEM) {
    struct island_system_layout system = island_get_system_layout(&layout);
    if (!island->dragging_brightness
        && !CGRectContainsPoint(system.slider_hit, point)) {
      return false;
    }
    island->dragging_brightness = true;
    action->type = ISLAND_ACTION_SET_BRIGHTNESS;
    action->value = island_slider_fraction(system.slider, point);
    return true;
  }

  if (island->service != ISLAND_SERVICE_MUSIC) return false;
  struct island_music_layout music = island_get_music_layout(&layout);
  if (!island->dragging_volume
      && !CGRectContainsPoint(music.slider_hit, point)) {
    return false;
  }

  island->dragging_volume = true;
  action->type = ISLAND_ACTION_SET_VOLUME;
  action->value = island_slider_value(&music, point);
  return true;
}

bool island_handle_click(struct island* island,
                         CGRect panel,
                         CGPoint point,
                         struct island_action* action) {
  action->type = ISLAND_ACTION_NONE;
  if (!g_loaded) return false;
  struct island_layout layout = island_get_layout(panel);

  // The mouse-up that ends a volume drag: wherever it is released, it only
  // finishes the drag.
  if (island->dragging_volume) {
    island->dragging_volume = false;
    struct island_music_layout music = island_get_music_layout(&layout);
    action->type = ISLAND_ACTION_SET_VOLUME;
    action->value = island_slider_value(&music, point);
    return true;
  }
  if (island->dragging_brightness) {
    island->dragging_brightness = false;
    struct island_system_layout system = island_get_system_layout(&layout);
    action->type = ISLAND_ACTION_SET_BRIGHTNESS;
    action->value = island_slider_fraction(system.slider, point);
    return true;
  }

  island_fix_service(island);
  for (int i = 0; i < g_tab_count; i++) {
    if (CGRectContainsPoint(layout.service_hits[i], point)) {
      int service = g_tabs[i];
      if (island->service != service) {
        // Starts a page transition; the bar animates `transition` to 1.
        island->previous_service = island->service;
        island->previous_scroll_offset = island->scroll_offset;
        island->service = service;
        island->scroll_offset = 0;
        island->scroll_target = 0;
        island->transition = 0;
      }
      return true;
    }
  }

  if (island->service == ISLAND_SERVICE_MUSIC
      && island_handle_music_click(&layout, point, action)) {
    return true;
  }

  if (island->service == ISLAND_SERVICE_BLUETOOTH
      && island_handle_bluetooth_click(island, &layout, point, action)) {
    return true;
  }

  if (island->service == ISLAND_SERVICE_SCREENS
      && island_handle_screens_click(&layout, point, action)) {
    return true;
  }

  if (island->service == ISLAND_SERVICE_APPS
      && island_handle_apps_click(island, &layout, point, action)) {
    return true;
  }

  if (island->service == ISLAND_SERVICE_CLAUDE
      && island_handle_claude_click(island, &layout, point, action)) {
    return true;
  }

  // System: the brightness slider; the rest of the page swallows clicks.
  if (island->service == ISLAND_SERVICE_SYSTEM
      && CGRectContainsPoint(island_page_rect(&layout), point)) {
    struct island_system_layout system = island_get_system_layout(&layout);
    CGRect toggle, settings;
    island_info_controls(system.info, &toggle, &settings);

    if (CGRectContainsPoint(system.slider_hit, point)) {
      action->type = ISLAND_ACTION_SET_BRIGHTNESS;
      action->value = island_slider_fraction(system.slider, point);
    } else if (CGRectContainsPoint(CGRectInset(toggle, -4, -4), point)) {
      action->type = ISLAND_ACTION_WIFI_POWER;
    } else if (CGRectContainsPoint(CGRectInset(settings, -3, -3), point)) {
      action->type = ISLAND_ACTION_WIFI_SETTINGS;
    }
    return true;
  }

  // Alerts: a card opens its app (and is marked read); "Read all".
  if (island->service == ISLAND_SERVICE_ALERTS
      && CGRectContainsPoint(layout.list, point)) {
    const struct notification_list* notifications = notifications_get();
    if (notifications->count > 0
        && CGRectContainsPoint(CGRectInset(island_alerts_read_all(&layout),
                                           -3, -3                       ),
                               point                                     )) {
      action->type = ISLAND_ACTION_NOTIFICATIONS_READ_ALL;
      return true;
    }
    if (!CGRectContainsPoint(island_alerts_list(&layout), point)) return true;
    // Where the cards are drawn right now; leaving ones can't be clicked.
    for (int i = 0; i < island->card_count; i++) {
      const struct island_card* card = &island->cards[i];
      if (card->leaving) continue;
      if (!CGRectContainsPoint(island_card_frame(&layout, card,
                                                 island->scroll_offset),
                               point                                   )) {
        continue;
      }
      int index = island_notification_index(card->item.id);
      if (index < 0) return true;
      // The close button only marks it read; the rest opens the app.
      CGRect frame = island_card_frame(&layout, card, island->scroll_offset);
      if (claude_suggestion_active(&card->item)) {
        CGRect yes, no;
        island_card_choices(frame, &yes, &no);
        if (CGRectContainsPoint(CGRectInset(yes, -3, -2), point)
            || CGRectContainsPoint(CGRectInset(no, -3, -2), point)) {
          action->type = CGRectContainsPoint(CGRectInset(yes, -3, -2), point)
                         ? ISLAND_ACTION_SUGGESTION_YES
                         : ISLAND_ACTION_SUGGESTION_NO;
          action->index = index;
          return true;
        }
      }
      bool close = CGRectContainsPoint(
                     CGRectInset(island_card_close_rect(frame), -4, -4),
                     point                                           );
      action->type = close ? ISLAND_ACTION_NOTIFICATION_DISMISS
                           : ISLAND_ACTION_NOTIFICATION_OPEN;
      action->index = index;
      return true;
    }
    return true;
  }

  return false;
}

// ---------------------------------------------------------------- peek

static CGFloat island_text_width(const char* text, CTFontRef font) {
  if (!text || !font) return 0;
  CFStringRef string = CFStringCreateWithCString(NULL, text,
                                                 kCFStringEncodingUTF8);
  if (!string) return 0;
  const void* keys[] = { kCTFontAttributeName };
  const void* values[] = { font };
  CFDictionaryRef attributes = CFDictionaryCreate(NULL, keys, values, 1,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef attributed = CFAttributedStringCreate(NULL, string,
                                                              attributes  );
  CTLineRef line = CTLineCreateWithAttributedString(attributed);
  CGFloat width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
  CFRelease(line);
  CFRelease(attributed);
  CFRelease(attributes);
  CFRelease(string);
  return ceil(width);
}

CGFloat island_peek_text(const struct notification_item* item,
                         int max_chars,
                         char* out,
                         size_t size                          ) {
  char full[512];
  if (item->title[0] && item->body[0])
    snprintf(full, sizeof(full), "%s: %s", item->title, item->body);
  else
    snprintf(full, sizeof(full), "%s", item->title[0] ? item->title
                                                      : item->body );

  // At most `max_chars` characters (whole ones: emoji, accents), then "...".
  if (max_chars <= 0) max_chars = ISLAND_PEEK_CHARS;
  snprintf(out, size, "%s", full);
  CFStringRef string = CFStringCreateWithCString(NULL, full,
                                                 kCFStringEncodingUTF8);
  if (string) {
    CFIndex length = CFStringGetLength(string);
    CFIndex end = 0;
    int characters = 0;
    while (end < length && characters < max_chars) {
      CFRange range = CFStringGetRangeOfComposedCharactersAtIndex(string, end);
      end = range.location + range.length;
      characters++;
    }
    if (end < length) {
      CFStringRef cut = CFStringCreateWithSubstring(NULL, string,
                                                    CFRangeMake(0, end));
      char prefix[512];
      if (cut && CFStringGetCString(cut, prefix, sizeof(prefix),
                                    kCFStringEncodingUTF8       )) {
        snprintf(out, size, "%s...", prefix);
      }
      if (cut) CFRelease(cut);
    }
    CFRelease(string);
  }
  return island_text_width(out, g_fonts.peek);
}

void island_draw_peek(CGContextRef context,
                      const struct notification_item* item,
                      const char* text,
                      CGRect icon,
                      CGRect text_rect,
                      double alpha                       ) {
  if (!g_loaded || alpha <= 0) return;
  CGContextSaveGState(context);
  CGContextSetAlpha(context, max(min(alpha, 1.0), 0.0));

  if (item->claude) island_draw_clawd_tile(context, icon, CLAWD_HAPPY);
  else island_draw_app_icon(context, item->app, icon);
  CGFloat baseline = CGRectGetMidY(text_rect)
                     - (CTFontGetAscent(g_fonts.peek)
                        - CTFontGetDescent(g_fonts.peek)) / 2.0;
  island_draw_text(context, text, g_fonts.peek, 0.95,
                   text_rect.origin.x, baseline,
                   text_rect.size.width + 1, ALIGN_LEFT);
  CGContextRestoreGState(context);
}

// Suggestion peek: one line in the peek font when it fits
// ISLAND_SUGGEST_LINE, else two balanced lines in the smaller font.
static bool island_suggestion_two_lines(const char* text) {
  return island_text_width(text, g_fonts.peek) > ISLAND_SUGGEST_LINE;
}

static CGFloat island_suggestion_wrap_width(const char* text) {
  CGFloat full = island_text_width(text, g_fonts.peek_small);
  return min(ceil(full / 2.0) + 30, ISLAND_SUGGEST_LINE);
}

CGFloat island_suggestion_text_width(const char* text) {
  if (!g_loaded || !text) return 0;
  if (!island_suggestion_two_lines(text))
    return island_text_width(text, g_fonts.peek);
  return island_suggestion_wrap_width(text);
}

void island_split_choices(CGRect area, CGRect* yes, CGRect* no) {
  CGFloat y = CGRectGetMidY(area) - ISLAND_CHOICE_HEIGHT / 2.0;
  CGFloat x = CGRectGetMaxX(area) - ISLAND_CHOICES_WIDTH;
  *yes = (CGRect){{ x, y }, { ISLAND_CHOICE_WIDTH, ISLAND_CHOICE_HEIGHT }};
  *no = (CGRect){{ x + ISLAND_CHOICE_WIDTH + ISLAND_CHOICE_GAP, y },
                 { ISLAND_CHOICE_WIDTH, ISLAND_CHOICE_HEIGHT }};
}

void island_draw_suggestion_peek(CGContextRef context,
                                 const struct notification_item* item,
                                 CGRect choices,
                                 CGRect text_rect,
                                 double alpha                        ) {
  if (!g_loaded || alpha <= 0) return;
  alpha = max(min(alpha, 1.0), 0.0);
  CGContextSaveGState(context);
  CGContextSetAlpha(context, alpha);

  CGRect yes, no;
  island_split_choices(choices, &yes, &no);
  island_draw_choice(context, yes, true, 1.0);
  island_draw_choice(context, no, false, 1.0);

  const char* text = item->suggestion;
  if (!island_suggestion_two_lines(text)) {
    CGFloat baseline = CGRectGetMidY(text_rect)
                       - (CTFontGetAscent(g_fonts.peek)
                          - CTFontGetDescent(g_fonts.peek)) / 2.0;
    island_draw_text(context, text, g_fonts.peek, 0.95, text_rect.origin.x,
                     baseline, text_rect.size.width + 1, ALIGN_LEFT       );
  } else {
    CTLineRef lines[2];
    int count = island_wrap_text(text, g_fonts.peek_small,
                                 island_suggestion_wrap_width(text) + 1,
                                 2, lines                              );
    CGFloat spacing = 13;
    CGFloat offset = (CTFontGetAscent(g_fonts.peek_small)
                      - CTFontGetDescent(g_fonts.peek_small)) / 2.0;
    CGFloat first = CGRectGetMidY(text_rect) + spacing * (count - 1) / 2.0
                    - offset;
    island_draw_lines(context, lines, count, ISLAND_COLOR_WHITE, 0.95,
                      text_rect.origin.x, first, spacing              );
  }
  CGContextRestoreGState(context);
}

void island_clamp_scroll(struct island* island, CGRect panel) {
  if (!g_loaded) return;
  struct island_layout layout = island_get_layout(panel);
  CGFloat max_scroll;
  if (island->service == ISLAND_SERVICE_ALERTS) {
    max_scroll = island_max_scroll(&layout);
  } else if (island->service == ISLAND_SERVICE_APPS) {
    struct island_apps_layout a = island_get_apps_layout(&layout);
    max_scroll = island_apps_max_scroll(&a);
  } else if (island->service == ISLAND_SERVICE_CLAUDE) {
    struct island_claude_layout c = island_get_claude_layout(&layout);
    max_scroll = island_claude_max_scroll(&c, island->claude_subtab);
  } else {
    return;
  }
  island->scroll_target = max(min(island->scroll_target, max_scroll), 0);
  island->scroll_offset = max(min(island->scroll_offset, max_scroll), 0);
}

bool island_handle_scroll(struct island* island,
                          CGRect panel,
                          CGPoint point,
                          CGFloat delta          ) {
  if (!g_loaded || !CGRectContainsPoint(panel, point)) return false;

  // The Alerts, Bluetooth and Apps lists scroll the same way: this only moves the
  // target, the bar glides (wheel) or follows (trackpad) towards it.
  struct island_layout layout = island_get_layout(panel);
  CGFloat max_scroll;
  if (island->service == ISLAND_SERVICE_ALERTS) {
    max_scroll = island_max_scroll(&layout);
  } else if (island->service == ISLAND_SERVICE_BLUETOOTH) {
    struct island_bluetooth_layout b = island_get_bluetooth_layout(&layout);
    max_scroll = island_bluetooth_max_scroll(connections_get(), &b);
  } else if (island->service == ISLAND_SERVICE_APPS) {
    struct island_apps_layout a = island_get_apps_layout(&layout);
    max_scroll = island_apps_max_scroll(&a);
  } else if (island->service == ISLAND_SERVICE_CLAUDE) {
    struct island_claude_layout c = island_get_claude_layout(&layout);
    max_scroll = island_claude_max_scroll(&c, island->claude_subtab);
  } else {
    return true;
  }

  CGFloat target = island->scroll_target - delta;
  island->scroll_target = max(min(target, max_scroll), 0);
  return true;
}
