#pragma once
#include "misc/helpers.h"

// A real secure text field for the island's Wi-Fi password prompt.
//
// The bar's own windows can't take keyboard focus, so the field is a small
// borderless, non-activating panel laid exactly over the field the island
// draws: it gets the keystrokes while the app you were using stays active.
// Return submits, Escape cancels.

// `submitted` false: cancelled. `password` is only valid during the call.
typedef void password_field_callback(bool submitted, const char* password);

// `rect` in global screen coordinates (CG: origin top left, y down).
void password_field_show(CGRect rect,
                         const char* text,
                         password_field_callback* callback);
void password_field_hide(void);
// Submits the current text, as if Return was pressed (the Join button).
void password_field_submit(void);
bool password_field_visible(void);
