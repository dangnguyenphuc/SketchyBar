#pragma once
#include <CoreText/CoreText.h>
#include "misc/helpers.h"

struct font {
  CTFontRef ct_font;

  bool font_changed;
  bool typographical_width;
  float size;
  char* family;
  char* style;
  char* features;
};

// Default font family of every item (overridable in the config). Shipped as
// fonts/Fura.zip in the release; when it isn't installed the bar still runs
// with the fallback family and the island shows an install hint.
#define FONT_DEFAULT_FAMILY  "FuraCode Nerd Font"
#define FONT_FALLBACK_FAMILY "Menlo"

bool font_default_family_installed(void);
const char* font_default_family(void);

void font_register(char* font_path);

void font_init(struct font* font);
void font_destroy(struct font* font);
bool font_set(struct font* font, char* font_string, bool forced);
bool font_set_size(struct font* font, float size);
bool font_set_family(struct font* font, char* family, bool forced);
bool font_set_style(struct font* font, char* style, bool forced);
bool font_set_typographical_width(struct font* font, bool typographical_width);
void font_create_ctfont(struct font* font);
void font_clear_pointers(struct font* font);

bool font_parse_sub_domain(struct font* font, FILE* rsp, struct token property, char* message);
