#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import os
import sys
import urllib.request

OLLAMA_HOST = os.environ.get("OLLAMA_HOST", "http://127.0.0.1:11434")
MODEL = os.environ.get("MAID_MODEL", "llama3.2:3b")
MAX_CTX = int(os.environ.get("MAID_MAX_CHARS", "8000"))
MAX_OUT = int(os.environ.get("MAID_MAX_OUT_CHARS", "1200"))
MAX_ERR = int(os.environ.get("MAID_MAX_ERR_CHARS", "600"))

SYSTEM_PROMPT = os.environ.get(
    "MAID_SYSTEM_PROMPT",
    "You are Maid inside the maidux shell. Based on recent commands, exit codes,"
    " and errors, suggest the user's next intended shell command. Prefer the"
    " maidux builtins when relevant: xpwd, xcd, xls, xcat, xcp, xmv, xrm,"
    " xtouch, xtee, xhistory, xjournalctl. If a user entered commands like cats,"
    " cat, or ls, correct them to the x* variant. If the last exit code was"
    " non-zero and the error mentions missing files or bad commands, prioritize"
    " spelling fixes over speculative next steps. Respond only with JSON keys"
    " command and reason, and never suggest the command 'maid'.",
)

def clip(s: str, n: int) -> str:
    if not s:
        return ""
    return s[-n:] if len(s) > n else s


def ollama_chat(system_prompt: str, user_text: str) -> str:
    url = OLLAMA_HOST + "/api/chat"
    payload = {
        "model": MODEL,
        "stream": False,
        "format": {
            "type": "object",
            "properties": {
                "command": {"type": "string"},
                "reason": {"type": "string"},
            },
            "required": ["command", "reason"],
        },
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_text},
        ],
    }
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=20) as resp:
        obj = json.loads(resp.read().decode("utf-8", errors="replace"))
    return obj.get("message", {}).get("content", "")


transcript = ""

def append_turn(cmd: str, out: str, err: str, code: int) -> None:
    global transcript
    piece = (
        f"> {cmd}\n"
        f"<exit={code}>\n"
        f"<out>\n{clip(out, MAX_OUT)}\n"
        f"<err>\n{clip(err, MAX_ERR)}\n"
        "----\n"
    )
    transcript = clip(transcript + piece, MAX_CTX)


def parse_suggest(text: str):
    t = (text or "").strip()
    if not t:
        return "", "empty response"
    if t.startswith("```") and t.endswith("```"):
        t = t.strip("`")
        # remove optional language tag like ```json
        newline = t.find("\n")
        if newline != -1:
            t = t[newline + 1 :]
        t = t.strip()
    if t.startswith("{") and t.endswith("}"):
        try:
            obj = json.loads(t)
            return str(obj.get("command", "")).strip(), str(obj.get("reason", "")).strip()
        except Exception:
            pass
    lines = t.splitlines()
    cmd = lines[0].strip()
    reason = " ".join(x.strip() for x in lines[1:]).strip() if len(lines) > 1 else ""
    return cmd, reason


def send(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    try:
        msg = json.loads(line)
    except Exception:
        send({"type": "error", "message": "bad json from C"})
        continue

    t = msg.get("type")
    if t == "turn":
        append_turn(
            msg.get("cmd", ""),
            msg.get("out", ""),
            msg.get("err", ""),
            int(msg.get("code", 0)),
        )
    elif t == "reset":
        transcript = ""
        send({"type": "ok"})
    elif t == "quit":
        send({"type": "ok"})
        break
    elif t == "maid":
        user_text = (
            transcript
            + "\nTask: Suggest the user's next shell command."
            + " Do NOT suggest 'maid'."
            + " Return JSON with keys command and reason."
        )
        try:
            raw = ollama_chat(SYSTEM_PROMPT, user_text)
            command, reason = parse_suggest(raw)
            send({"type": "suggest", "command": command, "reason": reason})
        except Exception as e:
            send({"type": "error", "message": str(e)})
    else:
        send({"type": "error", "message": f"unknown type: {t}"})
