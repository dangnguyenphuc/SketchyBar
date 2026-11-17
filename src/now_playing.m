#include "now_playing.h"
#import <AppKit/AppKit.h>
#include <ImageIO/ImageIO.h>

#define SPOTIFY_BUNDLE_ID "com.spotify.client"
#define MUSIC_BUNDLE_ID   "com.apple.Music"

// While a player switches tracks its AppleScript `current track` fails for a
// moment. Such a query is polled again at most QUERY_RETRIES times,
// QUERY_RETRY_DELAY seconds apart. If it still fails, the last known state
// stays (never "Nothing playing"); the player's next notification or the next
// button press refreshes it. No polling beyond that.
#define QUERY_RETRIES        3
#define QUERY_RETRY_DELAY    0.3

// Refresh requests (notifications, button presses) are debounced: the query
// runs once nothing new happened for this long. A player sends several
// notifications per change and its state lags a button press, so querying
// right away showed intermediate states (pause -> play -> pause).
#define REFRESH_DEBOUNCE     0.5

// Artwork is shown at ~48pt; keep a small thumbnail instead of the 640px
// original.
#define ARTWORK_MAX_PIXELS 192

static const struct {
  const char* bundle_id;
  const char* name;
  const char* notification;
  bool has_artwork_url;
} g_players[] = {
  { SPOTIFY_BUNDLE_ID, "Spotify", "com.spotify.client.PlaybackStateChanged", true },
  { MUSIC_BUNDLE_ID,   "Music",   "com.apple.Music.playerInfo",              false },
};
#define PLAYER_COUNT (sizeof(g_players) / sizeof(g_players[0]))

// Result of one query, passed by value between threads.
struct query_result {
  struct now_playing state;
  int player;  // index into g_players, -1 without a track
  char artwork_url[512];
  // A running player answered with an error (mid track change): the result
  // says nothing reliable about it.
  bool uncertain;
};

// Main thread only.
static struct now_playing g_state;
static int g_player = -1;
// Last player that had a track: the buttons keep controlling it even while
// the state is momentarily unknown.
static int g_last_player = -1;
static char g_artwork_url[512];
static now_playing_callback* g_callback = NULL;
static id g_observers[PLAYER_COUNT];
// Bumped on start/stop, so results of queries from an older session (that
// finish after the island closed) are dropped.
static uint64_t g_generation = 0;

static dispatch_queue_t g_queue = NULL;

// Main thread only. Bumped by every refresh request: a debounce timer only
// fires if no newer request came in, and a query result is only applied if
// no request came in while it ran (a newer query is on its way then).
static uint64_t g_requests = 0;

static bool player_running(int player) {
  @autoreleasepool {
    NSString* bundle_id = @(g_players[player].bundle_id);
    return [[NSRunningApplication
              runningApplicationsWithBundleIdentifier:bundle_id] count] > 0;
  }
}

// Runs an AppleScript in a separate osascript process and returns its output
// (NULL on failure). NSAppleScript is main-thread only, and this must not
// block the bar.
static NSString* run_osascript(NSString* script) {
  NSTask* task = [[NSTask alloc] init];
  task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/osascript"];
  task.arguments = @[ @"-e", script ];
  NSPipe* pipe = [NSPipe pipe];
  task.standardOutput = pipe;
  task.standardError = [NSFileHandle fileHandleWithNullDevice];

  NSString* output = nil;
  if ([task launchAndReturnError:NULL]) {
    NSData* data = [[pipe fileHandleForReading] readDataToEndOfFile];
    [task waitUntilExit];
    if (task.terminationStatus == 0) {
      output = [[[NSString alloc] initWithData:data
                                      encoding:NSUTF8StringEncoding]
                                                         autorelease];
    }
  }
  [task release];
  return output;
}

// State line, then (unless stopped) title, artist and artwork URL lines.
// "error" if the player couldn't answer right now.
static NSString* query_script(int player) {
  NSString* artwork = g_players[player].has_artwork_url
                      ? @"(artwork url of t)"
                      : @"\"\"";
  return [NSString stringWithFormat:
    @"tell application id \"%s\"\n"
     "  try\n"
     "    set s to player state as string\n"
     "    if s is \"stopped\" then return s\n"
     "    set t to current track\n"
     "    return s & linefeed & (name of t) & linefeed & (artist of t)"
     "           & linefeed & %@\n"
     "  on error\n"
     "    return \"error\"\n"
     "  end try\n"
     "end tell",
    g_players[player].bundle_id, artwork];
}

static void copy_string(char* destination, size_t size, NSString* string) {
  snprintf(destination, size, "%s", string ? [string UTF8String] : "");
}

// Background queue. Asks every running player; a playing one wins over a
// paused one.
static struct query_result query_players() {
  struct query_result result;
  memset(&result, 0, sizeof(result));
  result.player = -1;

  for (int i = 0; i < PLAYER_COUNT; i++) {
    @autoreleasepool {
      // `tell application` would launch a player that isn't running.
      if (!player_running(i)) continue;

      // nil when osascript itself fails, e.g. Automation access was denied:
      // retrying wouldn't help, the player is just not available.
      NSString* output = run_osascript(query_script(i));
      if (!output) continue;

      NSArray* lines = [[output stringByTrimmingCharactersInSet:
                          [NSCharacterSet newlineCharacterSet]]
                          componentsSeparatedByString:@"\n"      ];
      if ([lines[0] isEqualToString:@"stopped"]) continue;
      if ([lines count] < 3) {
        result.uncertain = true;
        continue;
      }

      bool playing = [lines[0] isEqualToString:@"playing"];
      if (result.player >= 0 && (result.state.playing || !playing)) continue;

      result.player = i;
      result.state.has_player = true;
      result.state.playing = playing;
      copy_string(result.state.title, sizeof(result.state.title), lines[1]);
      copy_string(result.state.artist, sizeof(result.state.artist), lines[2]);
      copy_string(result.state.app, sizeof(result.state.app),
                  @(g_players[i].name)                         );
      copy_string(result.artwork_url, sizeof(result.artwork_url),
                  [lines count] > 3 ? lines[3] : nil             );
    }
  }
  return result;
}

static void notify() {
  if (g_callback) g_callback();
}

static void set_artwork(CGImageRef artwork) {
  if (g_state.artwork) CGImageRelease(g_state.artwork);
  g_state.artwork = artwork;
}

static CGImageRef create_thumbnail(NSData* data) {
  CGImageSourceRef source = CGImageSourceCreateWithData((CFDataRef)data, NULL);
  if (!source) return NULL;

  NSDictionary* options = @{
    (id)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
    (id)kCGImageSourceCreateThumbnailWithTransform: @YES,
    (id)kCGImageSourceThumbnailMaxPixelSize: @(ARTWORK_MAX_PIXELS)
  };
  CGImageRef image = CGImageSourceCreateThumbnailAtIndex(source,
                                                         0,
                                                         (CFDictionaryRef)options);
  CFRelease(source);
  return image;
}

static void fetch_artwork(NSString* url_string, uint64_t generation) {
  NSURL* url = [NSURL URLWithString:url_string];
  if (!url) return;

  NSURLSessionDataTask* task = [[NSURLSession sharedSession]
    dataTaskWithURL:url
  completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
    CGImageRef image = (data && !error) ? create_thumbnail(data) : NULL;

    dispatch_async(dispatch_get_main_queue(), ^{
      // Drop it if the island closed or the track changed meanwhile.
      if (generation != g_generation
          || strcmp(g_artwork_url, [url_string UTF8String]) != 0) {
        if (image) CGImageRelease(image);
        return;
      }
      set_artwork(image);
      notify();
    });
  }];
  [task resume];
}

// Main thread.
static void apply_result(struct query_result result) {
  bool artwork_changed = strcmp(result.artwork_url, g_artwork_url) != 0;
  bool changed = artwork_changed
                 || result.player != g_player
                 || result.state.has_player != g_state.has_player
                 || result.state.playing != g_state.playing
                 || strcmp(result.state.title, g_state.title) != 0
                 || strcmp(result.state.artist, g_state.artist) != 0;
  if (!changed) return;

  CGImageRef artwork = g_state.artwork;
  g_state = result.state;
  g_state.artwork = artwork;
  g_player = result.player;
  if (g_player >= 0) g_last_player = g_player;

  if (artwork_changed) {
    snprintf(g_artwork_url, sizeof(g_artwork_url), "%s", result.artwork_url);
    set_artwork(NULL);
    if (g_artwork_url[0])
      fetch_artwork(@(g_artwork_url), g_generation);
  }
  notify();
}

// Queries the players now (main thread).
static void query_now() {
  uint64_t generation = g_generation;
  uint64_t request = g_requests;
  dispatch_async(g_queue, ^{
    struct query_result result = query_players();
    for (int i = 0; i < QUERY_RETRIES && result.uncertain; i++) {
      usleep(QUERY_RETRY_DELAY * 1e6);
      result = query_players();
    }

    dispatch_async(dispatch_get_main_queue(), ^{
      if (generation != g_generation || request != g_requests) return;

      // Retries ran out while a player is mid track change (and no other one
      // is playing): keep what is shown.
      if (result.uncertain && !(result.player >= 0 && result.state.playing))
        return;
      apply_result(result);
    });
  });
}

// Debounced query (main thread), see REFRESH_DEBOUNCE.
static void refresh() {
  uint64_t generation = g_generation;
  uint64_t request = ++g_requests;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                               REFRESH_DEBOUNCE * NSEC_PER_SEC),
                 dispatch_get_main_queue(),
                 ^{
    if (generation == g_generation && request == g_requests) query_now();
  });
}

void now_playing_start(now_playing_callback* on_change) {
  if (g_callback) return;
  if (!g_queue) {
    g_queue = dispatch_queue_create("sketchybar.now_playing",
                                    DISPATCH_QUEUE_SERIAL    );
  }

  g_callback = on_change;
  g_generation++;

  NSDistributedNotificationCenter* center
    = [NSDistributedNotificationCenter defaultCenter];
  for (int i = 0; i < PLAYER_COUNT; i++) {
    g_observers[i] = [[center addObserverForName:@(g_players[i].notification)
                                          object:nil
                                           queue:[NSOperationQueue mainQueue]
                                      usingBlock:^(NSNotification* note) {
                                        refresh();
                                      }] retain];
  }
  // Opening the island shows the state right away.
  query_now();
}

void now_playing_stop(void) {
  if (!g_callback) return;

  NSDistributedNotificationCenter* center
    = [NSDistributedNotificationCenter defaultCenter];
  for (int i = 0; i < PLAYER_COUNT; i++) {
    [center removeObserver:g_observers[i]];
    [g_observers[i] release];
    g_observers[i] = nil;
  }

  g_callback = NULL;
  g_generation++;
  set_artwork(NULL);
  memset(&g_state, 0, sizeof(g_state));
  g_artwork_url[0] = '\0';
  g_player = -1;
  g_last_player = -1;
}

const struct now_playing* now_playing_get(void) {
  return &g_state;
}

void now_playing_send(enum now_playing_command command) {
  if (!g_callback) return;

  static const char* commands[] = {
    [NOW_PLAYING_PREVIOUS]   = "previous track",
    [NOW_PLAYING_PLAY_PAUSE] = "playpause",
    [NOW_PLAYING_NEXT]       = "next track",
  };

  // Show the new play state right away; the query below confirms it.
  if (command == NOW_PLAYING_PLAY_PAUSE) {
    g_state.playing = !g_state.playing;
    notify();
  }

  // The current player, else the last one; if neither is known (the island
  // opened while a query failed), the first running player.
  int known = g_player >= 0 ? g_player : g_last_player;
  dispatch_async(g_queue, ^{
    @autoreleasepool {
      int player = known;
      for (int i = 0; player < 0 && i < PLAYER_COUNT; i++) {
        if (player_running(i)) player = i;
      }
      if (player < 0 || !player_running(player)) return;
      run_osascript([NSString stringWithFormat:
                      @"tell application id \"%s\" to %s",
                      g_players[player].bundle_id,
                      commands[command]                   ]);
    }
  });
  // Serial queue: runs after the command.
  refresh();
}
