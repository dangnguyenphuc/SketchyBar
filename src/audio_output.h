#pragma once
#include "misc/helpers.h"

// Volume, mute and name of the default output device, for the island's Music
// tab. Only listens for changes between audio_output_start() and
// audio_output_stop() (while an island is open).

struct audio_output {
  bool has_volume;  // false if the device has no volume control (e.g. HDMI)
  float volume;     // 0..1
  bool has_mute;
  bool muted;
  char name[128];
};

// Called on the main thread whenever the state above changed.
typedef void audio_output_callback(void);

void audio_output_start(audio_output_callback* on_change);
void audio_output_stop(void);
const struct audio_output* audio_output_get(void);
void audio_output_set_volume(float volume);
void audio_output_set_muted(bool muted);
