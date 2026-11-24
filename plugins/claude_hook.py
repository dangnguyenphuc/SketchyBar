#!/usr/bin/env python3
"""Claude Code hook -> SketchyBar notification ("<session> is done").

Register it in ~/.claude/settings.json (see CHEATSHEET.md):

  "hooks": {
    "Stop": [{"hooks": [{"type": "command",
      "command": "python3 /path/to/plugins/claude_hook.py /path/to/bin/sketchybar"}]}]
  }

Optionally also for "Notification" (Claude waits for you: "<session> needs
you"). Claude Code passes the event as JSON on stdin; this sends

  sketchybar --claude event=stop session=<id> name=<session name>
                      cwd=<project folder> message=<end of Claude's reply>

The first argument is the sketchybar binary (default: `sketchybar` on the
PATH). It never fails the hook: errors are ignored, and it exits 0 quickly
even when the bar isn't running.
"""

import json
import os
import re
import subprocess
import sys

TAIL_BYTES = 256 * 1024
MESSAGE_CHARS = 200


def session_name(session_id):
    """The session's name from Claude Code's running-sessions folder."""
    folder = os.path.expanduser("~/.claude/sessions")
    try:
        for file in os.listdir(folder):
            if not file.endswith(".json"):
                continue
            with open(os.path.join(folder, file)) as handle:
                data = json.load(handle)
            if data.get("sessionId") == session_id:
                return data.get("name") or ""
    except Exception:
        pass
    return ""


def last_reply(transcript):
    """The text of Claude's last reply, from the end of the transcript."""
    try:
        with open(transcript, "rb") as handle:
            handle.seek(0, os.SEEK_END)
            handle.seek(max(handle.tell() - TAIL_BYTES, 0))
            lines = handle.read().decode("utf-8", "replace").splitlines()
    except Exception:
        return ""
    for line in reversed(lines):
        try:
            entry = json.loads(line)
        except ValueError:
            continue
        if entry.get("type") != "assistant" or entry.get("isSidechain"):
            continue
        content = (entry.get("message") or {}).get("content")
        if isinstance(content, str):
            texts = [content]
        else:
            texts = [part.get("text", "") for part in content or []
                     if isinstance(part, dict) and part.get("type") == "text"]
        text = " ".join(t for t in texts if t.strip())
        if text:
            # Plain one-liner: no markdown marks, no line breaks.
            text = re.sub(r"[`*_#>\[\]]", "", text)
            text = re.sub(r"\s+", " ", text).strip()
            if len(text) > MESSAGE_CHARS:
                text = text[:MESSAGE_CHARS - 1].rstrip() + "…"
            return text
    return ""


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "sketchybar"
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return

    hook = payload.get("hook_event_name", "")
    if hook == "Stop":
        event = "stop"
        message = last_reply(payload.get("transcript_path") or "")
    elif hook == "Notification":
        event = "attention"
        message = payload.get("message") or ""
    else:
        return

    session = payload.get("session_id") or ""
    arguments = [binary, "--claude", f"event={event}", f"session={session}",
                 f"name={session_name(session)}",
                 f"cwd={payload.get('cwd') or os.getcwd()}",
                 f"message={message}"]
    try:
        subprocess.run(arguments, timeout=3, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    except Exception:
        pass


if __name__ == "__main__":
    try:
        main()
    except Exception:
        pass
    sys.exit(0)
