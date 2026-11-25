# Notch Island Cheat Sheet

A quick reference for the notch island in this fork: what each tab does,
every setting you can change, and fixes for common problems. For everything
else in SketchyBar (items, events, scripting), see the
[official documentation](https://felixkratz.github.io/SketchyBar/config/bar).

## Contents
- [Build and run](#build-and-run)
- [Using the island](#using-the-island)
- [Tabs](#tabs)
- [Plugins](#plugins)
- [All settings](#all-settings)
- [Recipes](#recipes)
- [Choosing which apps are captured](#choosing-which-apps-are-captured)
- [Claude tab and "done" notifications](#claude-tab-and-done-notifications)
- [Permissions](#permissions)
- [Troubleshooting](#troubleshooting)

## Build and run

```sh
make                                  # clean build into bin/sketchybar
./bin/sketchybar -c ./sketchybarrc    # run with the repo's config
./bin/sketchybar --reload             # re-run the config without restarting
make test                             # CLI smoke tests (doesn't start a bar)
```

> **Each rebuild produces a new binary**, so macOS may ask you to grant
> Accessibility and Full Disk Access again (see [Permissions](#permissions)).

## Using the island

| Do this | Result |
|---|---|
| Click the bar background (the notch) | Opens or closes the island |
| Click anywhere outside the island | Closes it |
| Leave it alone for `island_idle_timeout` seconds | Closes itself (never while the pointer is over it) |
| Click an icon in the left column | Switches tab |
| Scroll over a list | Scrolls Alerts, Bluetooth and Apps |
| A new notification arrives while closed | The notch widens briefly to show it; click it to open Alerts |

## Tabs

Set the tabs and their order with `island_tabs`. The left column spreads the
icons evenly over its height, so any number of tabs fits.

| Name | Shows | Clicks |
|---|---|---|
| `alerts` | Captured notifications as cards, "N unread", **Read all** | Card opens its app and marks it read; ✕ only marks it read |
| `claude` | Claude Code: **Sessions** running on this Mac and today's **Top models**; a sleeping Clawd and "Claude is currently busy" when Claude can't be reached | Session opens its folder in VS Code; ✕ ends that session; busy screen checks again |
| `music` | Now playing (Spotify / Apple Music), artwork, volume, output device | Previous, play/pause, next, mute, volume slider |
| `system` | OS, hardware, network, Wi-Fi switch, brightness, CPU/RAM graphs | Wi-Fi on/off, Wi-Fi settings, brightness slider |
| `bluetooth` | Paired devices with battery levels | Bluetooth on/off, settings, click a device to connect or disconnect |
| `apps` | Every app that can send notifications, each with a switch | Click a row to start or stop capturing that app |
| `screens` | Desktops 1–6 with icons (hidden by default) | Switches to that desktop |

The notch band (the strip beside the camera) always shows the date on the
left and battery / CPU / RAM on the right.

## Plugins

Each feature of this fork can be switched on or off in `sketchybarrc`
(`on` / `off`, all `on` by default). A plugin that is off does no
background work, and its tabs leave the column even if `island_tabs` lists
them.

```sh
$BIN_FILE --bar plugin_notifications=on \
                plugin_claude=on \
                plugin_claude_notifications=on \
                plugin_claude_suggestions=on \
                plugin_claude_models=on
```

| Plugin | What it does | Off means |
|---|---|---|
| `plugin_notifications` | Captures macOS notifications (Notification Center banners and history), the peek for them, the **Apps** tab | No scanning, no Accessibility prompt, **Apps** tab hidden. **Alerts** stays if Claude's notifications are on |
| `plugin_claude` | Everything Claude: the **Claude** tab and the hook's events | **Claude** tab hidden, hook events ignored. Switches off the three below too |
| `plugin_claude_notifications` | "*session* is done" / "needs you" notifications | Hook events don't show anything |
| `plugin_claude_suggestions` | "What next?" with Yes / No after a session finishes | The hook skips the model call entirely (no usage spent) |
| `plugin_claude_models` | **Top models** subtab and its scraper | Claude tab shows only Sessions, no subtab switch; the scraper never runs |

Music, System, Bluetooth and Screens are plain tabs: leave them out of
`island_tabs` to remove them (they only do work while shown).

## All settings

Put any of these after `--bar` in `sketchybarrc`. To try one without saving
it, run `./bin/sketchybar --bar <key>=<value>`: the change applies at once
and is lost on restart.

### Island (this fork)

| Setting | Default | Accepts | What it does |
|---|---|---|---|
| `island_tabs` | `alerts,claude,music,system,bluetooth,apps` | comma-separated tab names | Which tabs show, top to bottom. Unknown names are rejected. |
| `island_idle_timeout` | `60` | seconds, `0` = never | Seconds without interaction before the open island closes itself |
| `notification_scan_interval` | `1` | `0.2` – `60` seconds | How often notifications are captured. Banners stay on screen about 5 s, so keep it under that. |
| `notification_peek_duration` | `2.5` | seconds, `0` = off | How long a new notification shows in the closed island |
| `notification_peek_chars` | `50` | `1` – `200` | Maximum length of that preview, then "..." |
| `notification_per_app` | `5` | `1` – `50` | Unread notifications kept per app (newest win) |

### Claude tab (this fork)

| Setting | Default | Accepts | What it does |
|---|---|---|---|
| `claude_check_url` | `https://api.anthropic.com` | URL | Claude counts as reachable when this answers at all (any HTTP status) |
| `claude_check_command` | *(none)* | shell command | If set, used instead of the URL: exit status `0` = reachable. Use it for a VPN check. |
| `claude_check_interval` | `30` | seconds, min `5` | How often reachability is checked while the tab is open |
| `claude_models_script` | `plugins/claude_models.py` | path (relative to `sketchybarrc`) | Scraper that captures the top models |
| `claude_models_file` | `~/.cache/sketchybar/claude_models.json` | path | Where the scraper writes; the tab only shows this file. Re-captured when older than 6 h. |
| `claude_suggest_duration` | `5` | seconds, min `1` | How long a suggested next step (Yes / No) shows in the closed island before it moves to the Alerts tab |

### Notch and bar shape

| Setting | Default | What it does |
|---|---|---|
| `height` | `25` | Bar height; also the island's collapsed height |
| `notch_width` | `200` | Camera housing width on screens where macOS doesn't report it |
| `notch_offset` | `0` | Pushes the bar down on displays with a notch |
| `notch_display_height` | `0` | Collapsed height on the built-in display only (`0` = use `height`) |
| `corner_radius` | `0` | Island / bar corner rounding |
| `color` | `0x44000000` | Background color, `0xAARRGGBB` |
| `border_color` | `0xffff0000` | Border color, `0xAARRGGBB` |
| `border_width` | `0` | Border thickness |
| `padding_left` / `padding_right` | `20` / `20` | Space before the first and after the last item |
| `y_offset` | `0` | Vertical offset from the bar's normal position |
| `margin` | `0` | Space between the bar and the screen edges |
| `blur_radius` | `0` | Background blur |
| `shadow` | `off` | Drop shadow |

### Bar behavior

| Setting | Default | Accepts | What it does |
|---|---|---|---|
| `position` | `top` | `top`, `bottom` | Screen edge (the island needs `top`) |
| `display` | `all` | `all`, `main`, or numbers like `1,2` | Which displays get a bar |
| `topmost` | `off` | `on`, `off`, `window` | Keeps the bar above other windows |
| `sticky` | `on` | `on`, `off` | Stays put while switching spaces |
| `hidden` | `off` | `on`, `off`, `current` | Hides the bar (`current`: toggles it on the active display) |
| `show_in_fullscreen` | `off` | `on`, `off` | Shows the bar over fullscreen apps |
| `font_smoothing` | `off` | `on`, `off` | Font smoothing for item text |

Not a setting: the default font is **FuraCode Nerd Font**. Without it, the
island shows letters instead of icons.

## Recipes

```sh
# Current defaults, written out in full
./bin/sketchybar --bar island_tabs=alerts,music,system,bluetooth,apps \
                       island_idle_timeout=60 \
                       notification_scan_interval=1 \
                       notification_peek_duration=2.5 \
                       notification_peek_chars=50 \
                       notification_per_app=5

# Music first, no Bluetooth
./bin/sketchybar --bar island_tabs=music,alerts,system,apps

# Bring the Screens grid back (as the last tab)
./bin/sketchybar --bar island_tabs=alerts,claude,music,system,bluetooth,apps,screens

# Claude only counts as reachable while the company VPN is connected
# (use the VPN's name from System Settings > VPN)
./bin/sketchybar --bar claude_check_command="scutil --nc status 'Company VPN' | head -1 | grep -qx Connected"

# Or: reachable only when an internal host answers
./bin/sketchybar --bar claude_check_url=https://intranet.example.com

# Island never closes by itself
./bin/sketchybar --bar island_idle_timeout=0

# No notification preview while closed (still captured in Alerts)
./bin/sketchybar --bar notification_peek_duration=0

# Longer, more readable preview
./bin/sketchybar --bar notification_peek_duration=5 notification_peek_chars=80
```

## Choosing which apps are captured

- Open the island, go to **Apps**, and click a row to turn capturing on or
  off. Notifications from an app that is off aren't captured or listed.
- The list includes every app in *System Settings › Notifications*, plus any
  other app a notification was seen from (for example ones mirrored from your
  iPhone).
- Your choice is saved in a file named `notification_apps` next to
  `sketchybarrc`, with one disabled app per line. You can edit it by hand;
  changes are picked up within a second or so. Delete the file to turn every
  app back on.

```text
# notification_apps
Microsoft Outlook
Slack
```

## Claude tab and "done" notifications

**Sessions** lists the Claude Code sessions running on this Mac (CLI and VS
Code), newest first: name, project folder, where it runs, and "Working" while
Claude is replying. Click a session to open its folder in VS Code
(`code <folder>`); ✕ ends that session's process. The list comes from
`~/.claude/sessions/`, Claude Code's own (undocumented) folder.

**Top models** shows the 10 most used models on
[OpenRouter's rankings](https://openrouter.ai/rankings) (weekly tokens), with
provider, context size, max output, price per million tokens (in/out) and
features. `plugins/claude_models.py` captures them; run it by hand to refresh:

```sh
python3 plugins/claude_models.py ~/.cache/sketchybar/claude_models.json
```

**Done notifications**: when a session finishes its reply, a notification
"*session* is done" with the end of Claude's reply shows up like any other
(peek + Alerts card), with a happy Clawd instead of an app icon. Clicking it
opens the project in VS Code. Turn it off per app in the **Apps** tab
("Claude Code").

**What next?** Right after that, the hook asks Haiku (`claude -p`, a few
seconds, a small amount of your usage) for the step you most likely want
next. It shows in the closed island for `claude_suggest_duration` seconds:
**Yes / No** left of the camera, the whole suggestion right of it.

| You | Result |
|---|---|
| **Yes** | Claude does it in the same session right away (the hook wakes it) |
| **No** | Dismissed |
| Nothing for 5 s | It moves into the Alerts tab as a card with Yes / No, answerable for 10 minutes |
| Type a new prompt in that session | The suggestion expires (the card turns back into a plain "done" one) |

Both need this hook in `~/.claude/settings.json` (`asyncRewake` lets it wait
for your answer in the background and wake Claude on Yes):

```json
"hooks": {
  "Stop": [{ "hooks": [{
    "type": "command",
    "command": "python3 /path/to/SketchyBar/plugins/claude_hook.py /path/to/SketchyBar/bin/sketchybar",
    "timeout": 900, "asyncRewake": true
  }]}]
}
```

Hook options, after the binary: `--no-suggest` (done notifications only),
`--model sonnet` (another model for suggestions), `--wait 300` (how long a
suggestion stays answerable, seconds).

Add the same hook under `"Notification"` as well to also get
"*session* needs you" when Claude waits for an answer.

> Opening in VS Code needs its `code` command: in VS Code run
> **Shell Command: Install 'code' command in PATH**. Without it, clicks only
> mark the notification read.

## Permissions

Grant these in *System Settings › Privacy & Security* to the `sketchybar`
binary you run.

| Permission | Needed for | Without it |
|---|---|---|
| Accessibility | Notification banners; Screens tab desktop switching | No banners are captured; Screens shows "Allow SketchyBar in Accessibility" |
| Full Disk Access | Notifications already stored in Notification Center | Only banners seen while SketchyBar runs are listed |
| Bluetooth | Bluetooth tab | No device list |

## Troubleshooting

| Problem | Fix |
|---|---|
| No notifications at all | Grant Accessibility, then restart the bar. Check the app is on in the **Apps** tab. |
| Notifications that came in before the bar started are missing | Grant Full Disk Access. |
| Permissions stopped working after `make` | Remove the binary from the permission list, add it again, and restart the bar. |
| Some banners are missed | Lower `notification_scan_interval` (e.g. `0.5`). |
| An app is missing from the Apps tab | It shows up once it has sent a notification, or once it's listed in *System Settings › Notifications*. |
| `[!] Bar: Invalid island_tabs: ...` | A tab name is misspelled. Valid names: `alerts music system bluetooth apps screens` |
| Icons show as letters | Install FuraCode Nerd Font. |
| Claude tab always says "currently busy" | `claude_check_url` doesn't answer from this network, or `claude_check_command` exits non-zero. Click the tab to check again. |
| Claude tab says Claude is reachable while off the VPN | The default URL check only tests the network. Set `claude_check_command` to a VPN check (see [Recipes](#recipes)). |
| No "done" notifications | Check the Stop hook path in `~/.claude/settings.json` points at your `bin/sketchybar`, and that "Claude Code" is on in the Apps tab. |
| No "What next?" after a done notification | Run `claude -p hi --model haiku` in a terminal: it must work without a login prompt. Haiku can also answer that nothing is left to do. |
| "Couldn't capture models" | Run `plugins/claude_models.py` by hand to see the error (network, or the rankings page changed). |
| Screens tab: "Turn on Switch to Desktop shortcuts" | *Keyboard › Keyboard Shortcuts › Mission Control*: enable "Switch to Desktop 1–6". |
