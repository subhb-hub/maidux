# Maidux

A minimal modular Unix-like shell for coursework.

## Build

```
make
```

## Usage

Run `./maidux` to enter the REPL. Commands are trimmed for whitespace, `quit` exits the shell, and the current working directory is shown in the prompt. Basic builtins include:

- `xcd [dir]` – change directory (defaults to `$HOME`).
- `xpwd` – print the current directory.
- `xecho [args...]` – echo arguments.
- `xhistory` – print the in-memory history buffer.

External commands are resolved via `PATH`, with support for redirection (`>`, `>>`, `2>`) and simple pipelines (`cmd1 | cmd2`). Errors are logged to `maidux.log`.
