#!/bin/sh

ITEMS="spotify spotify.prev spotify.play spotify.next"

hide() { for i in $ITEMS; do sketchybar --set "$i" drawing=off; done; }
show() { for i in $ITEMS; do sketchybar --set "$i" drawing=on; done; }

if [ "$SENDER" = "spotify_change" ]; then
  STATE=$(echo "$INFO"  | jq -r '."Player State"')
  TITLE=$(echo "$INFO"  | jq -r '.Name')
  ARTIST=$(echo "$INFO" | jq -r '.Artist')
else
  # Startup / forced update: the notification only fires on change, so query Spotify directly
  if ! pgrep -xq Spotify; then hide; exit 0; fi
  STATE=$(osascript -e 'tell application "Spotify" to player state as string')
  TITLE=$(osascript -e 'tell application "Spotify" to name of current track')
  ARTIST=$(osascript -e 'tell application "Spotify" to artist of current track')
fi

case "$STATE" in
  Playing|playing) show; sketchybar --set spotify.play icon=􀊆 \
                                    --set spotify label="$TITLE – $ARTIST" ;;
  Paused|paused)   show; sketchybar --set spotify.play icon=􀊄 \
                                    --set spotify label="$TITLE – $ARTIST" ;;
  *)               hide ;;
esac