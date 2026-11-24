#include "claude.h"
#include "notifications.h"
#import <Foundation/Foundation.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>

extern char** environ;
extern char g_config_file[4096];

static struct claude_state g_state = { .reachability = CLAUDE_CHECKING };
static claude_callback* g_callback = NULL;

static bool g_watching = false;
static dispatch_source_t g_sessions_timer = NULL;
static dispatch_source_t g_check_timer = NULL;

static char g_check_url[512] = CLAUDE_CHECK_URL_DEFAULT;
static char g_check_command[1024] = "";
static double g_check_interval = CLAUDE_CHECK_INTERVAL_DEFAULT;
static bool g_checking = false;
static uint64_t g_check_generation = 0;

static char g_models_script[1024] = CLAUDE_MODELS_SCRIPT_DEFAULT;
static char g_models_file[1024] = CLAUDE_MODELS_FILE_DEFAULT;
static CFAbsoluteTime g_models_tried = 0;
static struct timespec g_models_stamp;

static void notify() {
  if (g_callback) g_callback();
}

static void copy_string(char* destination, size_t size, id value) {
  NSString* string = [value isKindOfClass:[NSString class]] ? value : nil;
  snprintf(destination, size, "%s", string ? [string UTF8String] : "");
}

static double json_number(id value, double fallback) {
  if ([value isKindOfClass:[NSNumber class]]) return [value doubleValue];
  if ([value isKindOfClass:[NSString class]] && [value length] > 0)
    return [value doubleValue];
  return fallback;
}

// The config file's folder (relative paths in the settings start there).
static NSString* config_directory() {
  if (g_config_file[0])
    return [@(g_config_file) stringByDeletingLastPathComponent];
  const char* directory = getenv("CONFIG_DIR");
  return directory && directory[0] ? @(directory) : nil;
}

static NSString* claude_resolve_path(const char* path) {
  NSString* string = [@(path) stringByExpandingTildeInPath];
  if ([string isAbsolutePath]) return string;
  NSString* directory = config_directory();
  return directory ? [directory stringByAppendingPathComponent:string] : nil;
}

// ------------------------------------------------------------ code

// VS Code's `code` command: on the PATH, or where VS Code's "Install 'code'
// command in PATH" puts it (a bar started by launchd has a short PATH). NULL
// when it isn't set up.
static NSString* code_path() {
  NSMutableArray* folders = [NSMutableArray array];
  const char* path = getenv("PATH");
  if (path) {
    [folders addObjectsFromArray:[@(path) componentsSeparatedByString:@":"]];
  }
  [folders addObjectsFromArray:@[ @"/usr/local/bin", @"/opt/homebrew/bin" ]];
  for (NSString* folder in folders) {
    if (folder.length == 0) continue;
    NSString* candidate = [folder stringByAppendingPathComponent:@"code"];
    if (access([candidate fileSystemRepresentation], X_OK) == 0)
      return candidate;
  }
  return nil;
}

bool claude_open_path(const char* path) {
  if (!path || !path[0]) return false;
  bool opened = false;
  @autoreleasepool {
    NSString* code = code_path();
    if (code) {
      // Finished children are reaped automatically (SIGCHLD is ignored).
      char* argv[] = { (char*)[code fileSystemRepresentation],
                       (char*)path, NULL                        };
      pid_t pid;
      opened = posix_spawn(&pid, argv[0], NULL, NULL, argv, environ) == 0;
    }
  }
  return opened;
}

// ------------------------------------------------------------ sessions

static NSString* where_name(NSString* entrypoint) {
  if (![entrypoint isKindOfClass:[NSString class]]) return @"";
  if ([entrypoint containsString:@"vscode"]) return @"VS Code";
  if ([entrypoint containsString:@"jetbrains"]) return @"JetBrains";
  if ([entrypoint containsString:@"desktop"]) return @"Desktop";
  if ([entrypoint isEqualToString:@"cli"]) return @"CLI";
  return entrypoint;
}

static int compare_sessions(const void* a, const void* b) {
  CFAbsoluteTime x = ((const struct claude_session*)a)->started;
  CFAbsoluteTime y = ((const struct claude_session*)b)->started;
  return x < y ? 1 : (x > y ? -1 : 0);
}

static void read_sessions() {
  static struct claude_session sessions[CLAUDE_SESSIONS_MAX];
  int count = 0;
  memset(sessions, 0, sizeof(sessions));

  @autoreleasepool {
    NSString* folder = [@"~/.claude/sessions" stringByExpandingTildeInPath];
    NSArray* files = [[NSFileManager defaultManager]
                       contentsOfDirectoryAtPath:folder error:NULL];
    for (NSString* file in files) {
      if (count >= CLAUDE_SESSIONS_MAX) break;
      if (![file hasSuffix:@".json"]) continue;
      NSData* data = [NSData dataWithContentsOfFile:
                       [folder stringByAppendingPathComponent:file]];
      if (!data) continue;
      NSDictionary* json = [NSJSONSerialization JSONObjectWithData:data
                                                           options:0
                                                             error:NULL];
      if (![json isKindOfClass:[NSDictionary class]]) continue;

      int pid = (int)json_number(json[@"pid"], 0);
      if (pid <= 0 || (kill(pid, 0) != 0 && errno != EPERM)) continue;

      struct claude_session* session = &sessions[count++];
      session->pid = pid;
      copy_string(session->id, sizeof(session->id), json[@"sessionId"]);
      copy_string(session->name, sizeof(session->name), json[@"name"]);
      copy_string(session->cwd, sizeof(session->cwd), json[@"cwd"]);
      NSString* cwd = [json[@"cwd"] isKindOfClass:[NSString class]]
                      ? json[@"cwd"] : @"";
      copy_string(session->project, sizeof(session->project),
                  cwd.lastPathComponent                       );
      copy_string(session->where, sizeof(session->where),
                  where_name(json[@"entrypoint"])        );
      if (!session->name[0])
        snprintf(session->name, sizeof(session->name), "%s", session->project);
      session->busy = [json[@"status"] isKindOfClass:[NSString class]]
                      && [json[@"status"] isEqualToString:@"busy"];
      double started = json_number(json[@"startedAt"], 0) / 1000.0;
      session->started = started > 0
                         ? started - kCFAbsoluteTimeIntervalSince1970
                         : 0;
    }
  }
  qsort(sessions, count, sizeof(struct claude_session), compare_sessions);

  bool has_code = false;
  @autoreleasepool { has_code = code_path() != nil; }

  if (count == g_state.session_count && has_code == g_state.has_code
      && memcmp(sessions, g_state.sessions, count * sizeof(sessions[0])) == 0)
    return;
  memcpy(g_state.sessions, sessions, sizeof(sessions));
  g_state.session_count = count;
  g_state.has_code = has_code;
  notify();
}

void claude_stop_session(int index) {
  if (index < 0 || index >= g_state.session_count) return;
  kill(g_state.sessions[index].pid, SIGTERM);
  // It takes a moment to exit and remove its file.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.6 * NSEC_PER_SEC),
                 dispatch_get_main_queue(), ^{ read_sessions(); });
}

// ------------------------------------------------------------ reachability

static void set_reachability(int reachability, uint64_t generation) {
  if (generation != g_check_generation) return;  // settings changed since
  g_checking = false;
  if (g_state.reachability == reachability) return;
  g_state.reachability = reachability;
  notify();
}

// The command's exit status, through its output: with SIGCHLD ignored a
// child's status can't be waited for.
static bool run_check_command(const char* command) {
  char line[1024];
  snprintf(line, sizeof(line), "(%s) >/dev/null 2>&1; echo $?", command);
  FILE* output = popen(line, "r");
  if (!output) return false;
  char result[16] = "";
  fgets(result, sizeof(result), output);
  pclose(output);
  return atoi(result) == 0 && result[0] == '0';
}

void claude_check_now(void) {
  if (g_checking) return;
  g_checking = true;
  uint64_t generation = g_check_generation;

  if (g_check_command[0]) {
    char* command = strdup(g_check_command);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
      bool ok = run_check_command(command);
      free(command);
      dispatch_async(dispatch_get_main_queue(), ^{
        set_reachability(ok ? CLAUDE_REACHABLE : CLAUDE_UNREACHABLE,
                         generation                                 );
      });
    });
    return;
  }

  @autoreleasepool {
    NSURL* url = [NSURL URLWithString:@(g_check_url)];
    if (!url) {
      set_reachability(CLAUDE_UNREACHABLE, generation);
      return;
    }
    NSMutableURLRequest* request = [NSMutableURLRequest
      requestWithURL:url
         cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
     timeoutInterval:CLAUDE_CHECK_TIMEOUT                    ];
    request.HTTPMethod = @"HEAD";
    // Any HTTP answer (even 401 / 404) means the server can be reached.
    NSURLSessionDataTask* task = [[NSURLSession sharedSession]
      dataTaskWithRequest:request
        completionHandler:^(NSData* data, NSURLResponse* response,
                            NSError* error                        ) {
      bool ok = [response isKindOfClass:[NSHTTPURLResponse class]];
      dispatch_async(dispatch_get_main_queue(), ^{
        set_reachability(ok ? CLAUDE_REACHABLE : CLAUDE_UNREACHABLE,
                         generation                                 );
      });
    }];
    [task resume];
  }
}

// ------------------------------------------------------------ models

// Reads the script's JSON file (again only when it changed). Returns its
// age in seconds, or -1 without a readable file.
static double read_models() {
  NSString* path = claude_resolve_path(g_models_file);
  struct stat info;
  if (!path || stat([path fileSystemRepresentation], &info) != 0) return -1;
  double age = [[NSDate date] timeIntervalSince1970] - info.st_mtimespec.tv_sec;
  if (info.st_mtimespec.tv_sec == g_models_stamp.tv_sec
      && info.st_mtimespec.tv_nsec == g_models_stamp.tv_nsec
      && g_state.model_count > 0) {
    return age;
  }
  g_models_stamp = info.st_mtimespec;

  NSData* data = [NSData dataWithContentsOfFile:path];
  NSDictionary* json = data ? [NSJSONSerialization JSONObjectWithData:data
                                                              options:0
                                                                error:NULL]
                            : nil;
  if (![json isKindOfClass:[NSDictionary class]]) return -1;
  NSArray* models = json[@"models"];
  if (![models isKindOfClass:[NSArray class]]) return -1;

  int count = 0;
  struct claude_model list[CLAUDE_MODELS_MAX];
  memset(list, 0, sizeof(list));
  for (NSDictionary* entry in models) {
    if (count >= CLAUDE_MODELS_MAX) break;
    if (![entry isKindOfClass:[NSDictionary class]]) continue;
    struct claude_model* model = &list[count];
    copy_string(model->id, sizeof(model->id), entry[@"id"]);
    copy_string(model->name, sizeof(model->name), entry[@"name"]);
    if (!model->name[0]) continue;
    copy_string(model->provider, sizeof(model->provider), entry[@"provider"]);
    copy_string(model->features, sizeof(model->features), entry[@"features"]);
    copy_string(model->usage, sizeof(model->usage), entry[@"usage"]);
    model->context = (int)json_number(entry[@"context"], 0);
    model->max_output = (int)json_number(entry[@"max_output"], 0);
    model->price_in = json_number(entry[@"price_in"], -1);
    model->price_out = json_number(entry[@"price_out"], -1);
    count++;
  }

  memcpy(g_state.models, list, sizeof(list));
  g_state.model_count = count;
  copy_string(g_state.models_source, sizeof(g_state.models_source),
              json[@"source"]                                     );
  double updated = json_number(json[@"updated"], 0);
  g_state.models_updated = updated > 0
                           ? updated - kCFAbsoluteTimeIntervalSince1970
                           : 0;
  return age;
}

static void run_models_script() {
  if (g_state.models_loading) return;
  NSString* script = claude_resolve_path(g_models_script);
  NSString* file = claude_resolve_path(g_models_file);
  if (!script || !file) return;

  g_state.models_loading = true;
  g_models_tried = CFAbsoluteTimeGetCurrent();
  notify();

  [[NSFileManager defaultManager]
    createDirectoryAtPath:[file stringByDeletingLastPathComponent]
withIntermediateDirectories:YES attributes:nil error:NULL        ];

  // Single quotes around the paths; a quote inside one ends and reopens it.
  NSString* (^quote)(NSString*) = ^NSString*(NSString* s) {
    return [NSString stringWithFormat:@"'%@'",
             [s stringByReplacingOccurrencesOfString:@"'"
                                          withString:@"'\\''"]];
  };
  char* command = strdup([[NSString stringWithFormat:@"python3 %@ %@",
                            quote(script), quote(file)] UTF8String]);

  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    run_check_command(command);  // waits for it; the status doesn't matter
    free(command);
    dispatch_async(dispatch_get_main_queue(), ^{
      g_state.models_loading = false;
      @autoreleasepool {
        g_state.models_failed = read_models() < 0
                                || g_state.model_count == 0;
      }
      notify();
    });
  });
}

static void update_models(bool force) {
  @autoreleasepool {
    double age = read_models();
    g_state.models_failed = age < 0 && g_models_tried > 0;
    bool stale = age < 0 || age > CLAUDE_MODELS_TTL;
    bool may_retry = CFAbsoluteTimeGetCurrent() - g_models_tried
                     > CLAUDE_MODELS_RETRY;
    if (force || (stale && may_retry)) run_models_script();
  }
}

void claude_refresh_models(void) {
  update_models(true);
}

// ------------------------------------------------------------ watching

static dispatch_source_t start_timer(double interval, dispatch_block_t block) {
  dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,
                                                   0, 0,
                                                   dispatch_get_main_queue());
  uint64_t nanoseconds = interval * NSEC_PER_SEC;
  dispatch_source_set_timer(timer,
                            dispatch_time(DISPATCH_TIME_NOW, nanoseconds),
                            nanoseconds, nanoseconds / 4               );
  dispatch_source_set_event_handler(timer, block);
  dispatch_resume(timer);
  return timer;
}

static void stop_timer(dispatch_source_t* timer) {
  if (!*timer) return;
  dispatch_source_cancel(*timer);
  dispatch_release(*timer);
  *timer = NULL;
}

void claude_watch(bool on) {
  if (on == g_watching) return;
  g_watching = on;
  if (!on) {
    stop_timer(&g_sessions_timer);
    stop_timer(&g_check_timer);
    return;
  }

  read_sessions();
  claude_check_now();
  update_models(false);
  g_sessions_timer = start_timer(CLAUDE_SESSIONS_POLL, ^{ read_sessions(); });
  g_check_timer = start_timer(g_check_interval, ^{ claude_check_now(); });
}

// ------------------------------------------------------------ hook events

void claude_handle_event(const char* event,
                         const char* session,
                         const char* name,
                         const char* cwd,
                         const char* message) {
  bool attention = event && strcmp(event, "attention") == 0;
  if (!attention && !(event && strcmp(event, "stop") == 0)) return;

  struct notification_item item;
  memset(&item, 0, sizeof(item));
  item.claude = true;
  item.time = CFAbsoluteTimeGetCurrent();
  snprintf(item.app, sizeof(item.app), "%s", CLAUDE_APP_NAME);
  snprintf(item.path, sizeof(item.path), "%s", cwd ? cwd : "");
  snprintf(item.id, sizeof(item.id), "claude-%.24s-%.0f",
           session ? session : "", item.time * 1000);

  const char* project = cwd ? strrchr(cwd, '/') : NULL;
  project = project && project[1] ? project + 1 : (cwd ? cwd : "");
  const char* label = name && name[0] ? name : project;
  snprintf(item.title, sizeof(item.title),
           attention ? "%s needs you" : "%s is done", label[0] ? label
                                                               : "Claude");
  if (message && message[0])
    snprintf(item.body, sizeof(item.body), "%s", message);
  else
    snprintf(item.body, sizeof(item.body), "%s", project);

  notifications_add(&item);
  if (g_watching) read_sessions();
}

// ------------------------------------------------------------ public

void claude_set_callback(claude_callback* on_change) {
  g_callback = on_change;
}

const struct claude_state* claude_get(void) {
  return &g_state;
}

static void restart_check() {
  // A check still running for the old settings is ignored.
  g_check_generation++;
  g_checking = false;
  g_state.reachability = CLAUDE_CHECKING;
  if (g_watching) {
    stop_timer(&g_check_timer);
    claude_check_now();
    g_check_timer = start_timer(g_check_interval, ^{ claude_check_now(); });
  }
  notify();
}

void claude_set_check_url(const char* url) {
  if (!url || strcmp(url, g_check_url) == 0) return;
  snprintf(g_check_url, sizeof(g_check_url), "%s", url);
  restart_check();
}

void claude_set_check_command(const char* command) {
  if (!command) command = "";
  if (strcmp(command, g_check_command) == 0) return;
  snprintf(g_check_command, sizeof(g_check_command), "%s", command);
  restart_check();
}

void claude_set_check_interval(double seconds) {
  seconds = max(seconds, 5.0);
  if (seconds == g_check_interval) return;
  g_check_interval = seconds;
  restart_check();
}

void claude_set_models_script(const char* path) {
  if (!path || !path[0]) return;
  snprintf(g_models_script, sizeof(g_models_script), "%s", path);
}

void claude_set_models_file(const char* path) {
  if (!path || !path[0] || strcmp(path, g_models_file) == 0) return;
  snprintf(g_models_file, sizeof(g_models_file), "%s", path);
  memset(&g_models_stamp, 0, sizeof(g_models_stamp));
  g_state.model_count = 0;
  if (g_watching) update_models(false);
}
