#pragma once
#include "misc/helpers.h"

// What Spotify or Apple Music is playing, for the island's Music tab.
//
// The MediaRemote framework (system-wide "now playing") is locked for third
// party apps since macOS 15.4, so this talks to the two players directly:
// AppleScript (run as osascript off the main thread) for state and controls,
// their distributed notifications for track/state changes.
//
// Only active between now_playing_start() and now_playing_stop(): nothing is
// observed or queried while the island is closed. Players that aren't running
// are never launched. The first query asks the user once to allow SketchyBar
// to control Spotify/Music (System Settings > Privacy & Security >
// Automation).

#define NOW_PLAYING_TEXT_LENGTH 256

struct now_playing {
  bool has_player;  // a supported player is running and has a track
  bool playing;
  char title[NOW_PLAYING_TEXT_LENGTH];
  char artist[NOW_PLAYING_TEXT_LENGTH];
  char app[32];     // "Spotify" / "Music"
  CGImageRef artwork;  // may be NULL (Apple Music, or still loading)
};

enum now_playing_command {
  NOW_PLAYING_PREVIOUS,
  NOW_PLAYING_PLAY_PAUSE,
  NOW_PLAYING_NEXT
};

// Called on the main thread whenever the state above changed.
typedef void now_playing_callback(void);

void now_playing_start(now_playing_callback* on_change);
void now_playing_stop(void);
const struct now_playing* now_playing_get(void);
void now_playing_send(enum now_playing_command command);
