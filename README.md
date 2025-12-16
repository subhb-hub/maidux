# Maidux

A minimal modular Unix-like shell for coursework.

## Build

```
make
```

## Usage

Run `./maidux` to enter the REPL. A welcome banner is printed on start, prompts show the current working directory, and `quit` exits with a farewell message. Builtins include:

- `xcd [path|-]` – change directory (default `$HOME`, `-` goes to previous directory).
- `xpwd` – print the current directory.
- `xls [path]` – list directory contents (Dir/File prefix).
- `xtouch file` – create a file if it does not exist.
- `xecho [args...]` – echo arguments.
- `xcat file` – print a file.
- `xcp [-r] src dst` – copy files or directories.
- `xrm [-r] target` – remove files or directories.
- `xmv [-r] src dst` – move/rename files or directories.
- `xtee file` – copy stdin to stdout and a file.
- `xjournalctl` – print the log file (`maidux.log`).
- `xhistory` – print the in-memory history buffer.
- `maid` – ask the Maid assistant for a suggested next command (requires `maid_bridge.py` helper and Ollama).

On startup, maidux launches `maid_bridge.py` via `python3` (override with `MAID_PYTHON`) to communicate with an Ollama server. You can point to a custom helper path with `MAID_BRIDGE`. The helper script uses environment variables `MAID_MODEL`, `MAID_MAX_CHARS`, `MAID_MAX_OUT_CHARS`, `MAID_MAX_ERR_CHARS`, and `MAID_SYSTEM_PROMPT` to control model, context size, and prompting.

External commands are resolved via `PATH`, with support for redirection (`>`, `>>`, `2>`) and pipelines (`cmd1 | cmd2 | ...`). Errors are reported via `perror` and logged to `maidux.log`.
