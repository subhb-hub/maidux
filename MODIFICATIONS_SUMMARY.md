# Modifications Overview

This project now ensures maid can read both user input and every process's output while preserving correct shell semantics (parent vs. child execution). Key points:

- **Transcript logging** captures prompts, user commands, and stdout/stderr from child processes, forwarding them both to the terminal and a log for maid deltas.
- **Parent-only builtins** (e.g., `xcd`) run in the parent process through a capture helper that tees their output to the terminal, transcript, and maid buffers, so directory changes take effect and remain visible.
- **Delta delivery** reads only the newly appended transcript section after each command, sending maid the exact additions without altering normal shell behavior.

These changes keep shell interactivity intact while guaranteeing maid sees the full conversation between the user and the shell.
