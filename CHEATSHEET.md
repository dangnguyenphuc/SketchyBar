# Notch Island Cheat Sheet

A quick reference for the notch island in this fork: what each tab does,
every setting you can change, and fixes for common problems. For everything
else in SketchyBar (items, events, scripting), see the
[official documentation](https://felixkratz.github.io/SketchyBar/config/bar).

## Contents
- [Build and run](#build-and-run)
- [Using the island](#using-the-island)
- [Tabs](#tabs)
- [All settings](#all-settings)
- [Recipes](#recipes)
- [Choosing which apps are captured](#choosing-which-apps-are-captured)
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
| `music` | Now playing (Spotify / Apple Music), artwork, volume, output device | Previous, play/pause, next, mute, volume slider |
| `system` | OS, hardware, network, Wi-Fi switch, brightness, CPU/RAM graphs | Wi-Fi on/off, Wi-Fi settings, brightness slider |
| `bluetooth` | Paired devices with battery levels | Bluetooth on/off, settings, click a device to connect or disconnect |
| `apps` | Every app that can send notifications, each with a switch | Click a row to start or stop capturing that app |
| `screens` | Desktops 1–6 with icons (hidden by default) | Switches to that desktop |

The notch band (the strip beside the camera) always shows the date on the
left and battery / CPU / RAM on the right.

## All settings

Put any of these after `--bar` in `sketchybarrc`. To try one without saving
it, run `./bin/sketchybar --bar <key>=<value>`: the change applies at once
and is lost on restart.

### Island (this fork)

| Setting | Default | Accepts | What it does |
|---|---|---|---|
| `island_tabs` | `alerts,music,system,bluetooth,apps` | comma-separated tab names | Which tabs show, top to bottom. Unknown names are rejected. |
| `island_idle_timeout` | `60` | seconds, `0` = never | Seconds without interaction before the open island closes itself |
| `notification_scan_interval` | `1` | `0.2` – `60` seconds | How often notifications are captured. Banners stay on screen about 5 s, so keep it under that. |
| `notification_peek_duration` | `2.5` | seconds, `0` = off | How long a new notification shows in the closed island |
| `notification_peek_chars` | `50` | `1` – `200` | Maximum length of that preview, then "..." |
| `notification_per_app` | `5` | `1` – `50` | Unread notifications kept per app (newest win) |

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
./bin/sketchybar --bar island_tabs=alerts,music,system,bluetooth,apps,screens

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
| Screens tab: "Turn on Switch to Desktop shortcuts" | *Keyboard › Keyboard Shortcuts › Mission Control*: enable "Switch to Desktop 1–6". |
