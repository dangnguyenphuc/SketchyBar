#!/usr/bin/env python3
"""Read recent macOS Notification Center entries and print them as JSON.

POC: fetch only, no display. Output is a JSON array, newest first:
  [{"app": "com.tinyspeck.slackmacgap", "title": "...", "subtitle": "...",
    "body": "...", "date": "2026-11-15T09:30:00"}, ...]

Needs Full Disk Access for the process that runs it (Terminal/VS Code while
testing; the sketchybar binary once a bar item calls it).

Usage: notifications.py [limit]   (default 20)
"""

import json
import os
import plistlib
import sqlite3
import sys
from datetime import datetime

# macOS 15+ location; older versions kept it under $(getconf DARWIN_USER_DIR)
DB_PATH = os.environ.get("NOTIFICATIONS_DB") or os.path.expanduser(
    "~/Library/Group Containers/group.com.apple.usernoted/db2/db"
)
# Notification Center stores dates as seconds since 2001-01-01 (Mac absolute time)
MAC_EPOCH_OFFSET = 978307200


def decode(blob):
    """Each record's `data` column is a binary plist; the text lives under `req`."""
    try:
        plist = plistlib.loads(blob)
    except Exception:
        return None
    req = plist.get("req", {})
    return {
        "app": plist.get("app", ""),
        "title": req.get("titl", ""),
        "subtitle": req.get("subt", ""),
        "body": req.get("body", ""),
    }


def fetch(limit):
    # Read-only URI so we never take a write lock on usernoted's live database
    conn = sqlite3.connect("file:{}?mode=ro".format(DB_PATH), uri=True)
    try:
        rows = conn.execute(
            "SELECT data, delivered_date FROM record "
            "ORDER BY delivered_date DESC LIMIT ?",
            (limit,),
        ).fetchall()
    finally:
        conn.close()

    out = []
    for blob, delivered in rows:
        item = decode(blob)
        if item is None:
            continue
        item["date"] = (
            datetime.fromtimestamp(delivered + MAC_EPOCH_OFFSET).isoformat(timespec="seconds")
            if delivered
            else ""
        )
        out.append(item)
    return out


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    try:
        items = fetch(limit)
    except sqlite3.Error as e:
        # Most common cause: missing Full Disk Access ("unable to open database file")
        print(json.dumps({"error": str(e), "db": DB_PATH}), file=sys.stderr)
        sys.exit(1)
    print(json.dumps(items, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
