#!/usr/bin/env python3
"""Claude Code hook -> SketchyBar: "<session> is done", then "what next?".

Register it in ~/.claude/settings.json as an asyncRewake Stop hook (see
CHEATSHEET.md):

  "hooks": {
    "Stop": [{"hooks": [{"type": "command", "asyncRewake": true, "timeout": 900,
      "command": "python3 /path/to/plugins/claude_hook.py /path/to/bin/sketchybar"}]}]
  }

On Stop it
1. sends the done notification right away:
     sketchybar --claude event=stop token=<id> session=... name=... cwd=...
                         message=<end of Claude's reply>
2. asks a small model (`claude -p`, Haiku by default) for the next step the
   user most likely wants, and sends it:
     sketchybar --claude event=suggest token=<id> suggestion=<text>
   The bar shows it with Yes / No buttons (and keeps it in the Alerts tab).
3. waits in the background for the answer, which the bar writes to
   ~/.cache/sketchybar/claude/answers/<id>:
   - "yes": exits with status 2 and the step on stderr, which wakes Claude
     in the same session to do it (asyncRewake);
   - "no", --wait seconds without an answer, a new prompt in the session or
     the session ending: tells the bar the suggestion expired, exits 0.

Optionally also register it for "Notification" (Claude waits for you:
"<session> needs you"; no suggestion).

Options after the sketchybar binary:
  --no-suggest     only the done notification
  --model NAME     model for the suggestion (default: haiku)
  --wait SECONDS   how long a suggestion can be answered (default: 600)

Suggestions are only made while the bar runs with the suggestions plugin
on (it answers the stop event with "suggestions=on"), so nothing is spent
otherwise. It never fails the hook otherwise: errors are ignored and it
exits 0.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

TAIL_BYTES = 256 * 1024
MESSAGE_CHARS = 200
SUGGESTION_CHARS = 90
PROMPT_CHARS = 1500
REPLY_CHARS = 4000
ANSWERS = os.path.expanduser("~/.cache/sketchybar/claude/answers")
# Set for the `claude -p` this runs, so its own hooks can't start this again.
GUARD = "SKETCHYBAR_CLAUDE_HOOK"

SYSTEM_PROMPT = (
    "You suggest the user's next step in a coding session with an AI "
    "assistant. Reply with ONE instruction the user would most likely give "
    "the assistant next, written as that instruction (imperative, e.g. "
    "\"Run the tests and fix what fails\"). At most 80 characters, plain "
    "text, no quotes, no trailing period. If the assistant's reply offers or "
    "asks about a next step, turn that offer into the instruction. If "
    "nothing sensible is left to do, reply exactly NONE.")


def one_line(text, limit):
    text = re.sub(r"[`*_#>\[\]]", "", text)
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) > limit:
        text = text[:limit - 1].rstrip() + "…"
    return text


def session_info(session_id):
    """(name, pid) of the session from Claude Code's running-sessions folder."""
    folder = os.path.expanduser("~/.claude/sessions")
    try:
        for file in os.listdir(folder):
            if not file.endswith(".json"):
                continue
            with open(os.path.join(folder, file)) as handle:
                data = json.load(handle)
            if data.get("sessionId") == session_id:
                return data.get("name") or "", data.get("pid") or 0
    except Exception:
        pass
    return "", 0


def entry_text(entry):
    content = (entry.get("message") or {}).get("content")
    if isinstance(content, str):
        return content
    return " ".join(part.get("text", "") for part in content or []
                    if isinstance(part, dict) and part.get("type") == "text")


def last_exchange(transcript):
    """(the user's last prompt, Claude's last reply) from the transcript."""
    try:
        with open(transcript, "rb") as handle:
            handle.seek(0, os.SEEK_END)
            handle.seek(max(handle.tell() - TAIL_BYTES, 0))
            lines = handle.read().decode("utf-8", "replace").splitlines()
    except Exception:
        return "", ""
    prompt = reply = ""
    for line in reversed(lines):
        try:
            entry = json.loads(line)
        except ValueError:
            continue
        if entry.get("isSidechain"):
            continue
        text = entry_text(entry).strip()
        if not text:
            continue
        if entry.get("type") == "assistant" and not reply:
            reply = text
        elif entry.get("type") == "user" and reply and not text.startswith("<"):
            prompt = text
            break
    return prompt, reply


def send(binary, **fields):
    """Sends an event to the bar; returns its answer ("" if it isn't running)."""
    arguments = [binary, "--claude"] + [f"{k}={v}" for k, v in fields.items()]
    try:
        result = subprocess.run(arguments, timeout=3, capture_output=True,
                                text=True)
        return result.stdout if result.returncode == 0 else ""
    except Exception:
        return ""


def claude_binary():
    found = shutil.which("claude")
    if found:
        return found
    for path in ("~/.local/bin/claude", "~/.claude/local/claude",
                 "/opt/homebrew/bin/claude", "/usr/local/bin/claude"):
        path = os.path.expanduser(path)
        if os.access(path, os.X_OK):
            return path
    return None


def suggest(prompt, reply, model):
    """The next step, or "" (no claude CLI, no answer, or NONE)."""
    binary = claude_binary()
    if not binary or not reply:
        return ""
    question = (f"<request>\n{prompt[:PROMPT_CHARS]}\n</request>\n"
                f"<reply>\n{reply[-REPLY_CHARS:]}\n</reply>")
    environment = dict(os.environ, **{GUARD: "1"})
    try:
        result = subprocess.run(
            [binary, "-p", question, "--model", model,
             "--system-prompt", SYSTEM_PROMPT, "--no-session-persistence",
             "--settings", json.dumps({"disableAllHooks": True})],
            capture_output=True, text=True, timeout=60,
            cwd=tempfile.gettempdir(), env=environment)
    except Exception:
        return ""
    text = one_line(result.stdout or "", SUGGESTION_CHARS).strip("\"' ")
    if result.returncode != 0 or not text or text.upper().startswith("NONE"):
        return ""
    return text


def moved_on(transcript, size):
    """Whether a new prompt was typed in the session since `size` bytes."""
    try:
        with open(transcript, "rb") as handle:
            handle.seek(size)
            added = handle.read().decode("utf-8", "replace").splitlines()
    except Exception:
        return False
    for line in added:
        try:
            entry = json.loads(line)
        except ValueError:
            continue
        if (entry.get("type") == "user" and not entry.get("isSidechain")
                and not entry.get("isMeta")
                and not entry_text(entry).strip().startswith("<")
                and isinstance((entry.get("message") or {}).get("content"),
                               str)):
            return True
    return False


def alive(pid):
    if not pid:
        return True
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def wait_for_answer(token, transcript, pid, wait):
    """'yes', 'no' or '' (expired / the user moved on / session gone)."""
    path = os.path.join(ANSWERS, token)
    try:
        size = os.path.getsize(transcript)
    except OSError:
        size = 0
    deadline = time.time() + wait
    checked = 0.0
    while time.time() < deadline:
        try:
            with open(path) as handle:
                answer = handle.read().strip().lower()
            os.remove(path)
            return answer
        except OSError:
            pass
        if time.time() - checked > 2:
            checked = time.time()
            if not alive(pid) or moved_on(transcript, size):
                return ""
        time.sleep(0.25)
    return ""


def main():
    if os.environ.get(GUARD):
        return 0
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("binary", nargs="?", default="sketchybar")
    parser.add_argument("--no-suggest", action="store_true")
    parser.add_argument("--model", default="haiku")
    parser.add_argument("--wait", type=float, default=600)
    options, _ = parser.parse_known_args()

    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return 0

    hook = payload.get("hook_event_name", "")
    session = payload.get("session_id") or ""
    name, pid = session_info(session)
    cwd = payload.get("cwd") or os.getcwd()
    transcript = payload.get("transcript_path") or ""
    token = f"{session[:8] or 'session'}-{int(time.time() * 1000)}"

    if hook == "Notification":
        send(options.binary, event="attention", token=token, session=session,
             name=name, cwd=cwd, message=payload.get("message") or "")
        return 0
    if hook != "Stop":
        return 0

    prompt, reply = last_exchange(transcript)
    answer = send(options.binary, event="stop", token=token, session=session,
                  name=name, cwd=cwd, message=one_line(reply, MESSAGE_CHARS))
    # No model call unless the bar runs and wants suggestions
    # (--bar plugin_claude_suggestions).
    if options.no_suggest or "suggestions=on" not in answer:
        return 0

    suggestion = suggest(prompt, reply, options.model)
    if not suggestion:
        return 0
    os.makedirs(ANSWERS, exist_ok=True)
    send(options.binary, event="suggest", token=token, suggestion=suggestion)

    answer = wait_for_answer(token, transcript, pid, options.wait)
    if answer == "yes":
        # Exit status 2 wakes Claude in this session with this message.
        print("From the menu bar, the user accepted your suggested next "
              f"step: \"{suggestion}\". Go ahead and do it now.",
              file=sys.stderr)
        return 2
    send(options.binary, event="expire", token=token)
    return 0


if __name__ == "__main__":
    try:
        status = main()
    except Exception:
        status = 0
    sys.exit(status)
