#pragma once
#include "misc/helpers.h"

// Data for the island's Claude tab, plus the "done" notifications Claude Code
// sends through a hook (plugins/claude_hook.py).
//
// - Reachability: whether Claude can be used from this network. By default an
//   HTTP request to `claude_check_url` (any HTTP answer counts, a network
//   error or timeout doesn't). If `claude_check_command` is set, that shell
//   command decides instead (exit status 0: reachable), e.g. a VPN check.
//   Checked when the tab opens and every `claude_check_interval` seconds
//   while it shows.
// - Sessions: Claude Code sessions running on this Mac, read from
//   ~/.claude/sessions/<pid>.json (only those whose process is alive). Polled
//   every CLAUDE_SESSIONS_POLL seconds while the tab shows. That folder is
//   Claude Code's own and undocumented, so this may need updating when Claude
//   Code changes it.
// - Models: the top CLAUDE_MODELS_MAX models as captured by a scraper script
//   (`claude_models_script`, default plugins/claude_models.py next to the
//   config) into a JSON file (`claude_models_file`). The tab only shows that
//   file; when it is missing or older than CLAUDE_MODELS_TTL the script is run
//   again in the background (python3 <script> <file>).
//
// Opening a session's folder runs `code <path>` (VS Code's command line
// tool). Without `code` on the PATH (VS Code: "Shell Command: Install 'code'
// command in PATH") nothing is opened.

#define CLAUDE_SESSIONS_MAX   24
#define CLAUDE_MODELS_MAX     10
#define CLAUDE_PATH           512

#define CLAUDE_CHECK_URL_DEFAULT      "https://api.anthropic.com"
#define CLAUDE_CHECK_INTERVAL_DEFAULT 30.0
#define CLAUDE_CHECK_TIMEOUT          6.0
#define CLAUDE_MODELS_SCRIPT_DEFAULT  "plugins/claude_models.py"
#define CLAUDE_MODELS_FILE_DEFAULT    "~/.cache/sketchybar/claude_models.json"
#define CLAUDE_MODELS_TTL             (6 * 3600.0)
#define CLAUDE_MODELS_RETRY           60.0
#define CLAUDE_SESSIONS_POLL          2.0

// The app name Claude's notifications carry (Apps tab, Alerts cards).
#define CLAUDE_APP_NAME "Claude Code"

enum claude_reachability {
  CLAUDE_CHECKING,     // not known yet
  CLAUDE_REACHABLE,
  CLAUDE_UNREACHABLE
};

struct claude_session {
  int pid;
  char id[64];
  char name[64];
  char cwd[CLAUDE_PATH];
  char project[96];     // the folder's name
  char where[24];       // "VS Code", "CLI", ...
  bool busy;            // working on a reply right now
  CFAbsoluteTime started;
};

struct claude_model {
  char id[96];
  char name[96];
  char provider[32];
  int context;          // tokens, 0 unknown
  int max_output;       // tokens, 0 unknown
  double price_in;      // USD per million tokens, < 0 unknown
  double price_out;
  char features[96];    // "tools, vision, reasoning"
  char usage[16];       // weekly tokens on the ranking, e.g. "66.8T"
};

struct claude_state {
  int reachability;
  bool has_code;        // `code` found on the PATH

  int session_count;
  struct claude_session sessions[CLAUDE_SESSIONS_MAX];  // newest first

  bool models_loading;  // the script is running
  bool models_failed;   // it ran and no models could be read
  char models_source[48];
  CFAbsoluteTime models_updated;  // when the file was written, 0 unknown
  int model_count;
  struct claude_model models[CLAUDE_MODELS_MAX];
};

// Called on the main thread whenever the state changed.
typedef void claude_callback(void);

void claude_set_callback(claude_callback* on_change);
const struct claude_state* claude_get(void);

// Polls sessions and reachability (and loads the models if needed) while
// on; nothing runs while off.
void claude_watch(bool on);
// Checks reachability again now.
void claude_check_now(void);

// Ends a running session (SIGTERM to its process).
void claude_stop_session(int index);
// Runs `code <path>`. False when `code` isn't on the PATH (nothing opened).
bool claude_open_path(const char* path);

// A hook event from Claude Code (sketchybar --claude event=stop ...): adds a
// notification. `event` is "stop" (a session finished its reply) or
// "attention" (it waits for the user).
void claude_handle_event(const char* event,
                         const char* session,
                         const char* name,
                         const char* cwd,
                         const char* message);

// Runs the models script now (the Models subtab's refresh).
void claude_refresh_models(void);

// Settings (sketchybarrc: --bar claude_*). An empty command clears it.
// Relative script paths are relative to the config file's folder.
void claude_set_check_url(const char* url);
void claude_set_check_command(const char* command);
void claude_set_check_interval(double seconds);
void claude_set_models_script(const char* path);
void claude_set_models_file(const char* path);
