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

External commands are resolved via `PATH`, with support for redirection (`>`, `>>`, `2>`) and pipelines (`cmd1 | cmd2 | ...`). Errors are reported via `perror` and logged to `maidux.log`.
