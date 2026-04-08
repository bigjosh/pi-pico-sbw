---
name: par
description: Push and Run — push a Python file and its changed dependencies to the Pico, then run it
argument-hint: "[filename.py]"
---

Push a MicroPython program and its locally-changed dependencies to the Pico, then run it.

## Steps

### 1. Determine the target file

- If `$ARGUMENTS` is provided, use it as the filename.
- If not, read `.claude/par_last_file` for the most recently used file.
- If neither exists, ask the user which file to run.

Save the resolved filename to `.claude/par_last_file` for next time.

### 2. Build the dependency tree

Read the target file and recursively find all local dependencies:

- Scan for `import X` and `from X import Y` statements.
- For each `X`, check if `X.py` exists in the working directory.
- Recursively scan those files too.
- Ignore standard MicroPython modules (`machine`, `network`, `time`, `os`, `sys`, `json`, `socket`, `ssl`, `_thread`, `rp2`, `ntptime`, `struct`, `gc`, `micropython`).
- The result is a set of local `.py` files (including the target).

### 3. Determine which files have changed since last push

Compare files in the dependency tree against `.claude/par_manifest.json`, which stores the mtime of each file at the time it was last pushed to the Pico.

- If a file's current mtime differs from the manifest (or the file is not in the manifest), it needs pushing.
- **Always include the target file itself** regardless of mtime.
- If the manifest doesn't exist yet, push everything in the dependency tree.

### 4. Push files to the Pico

Build a single mpremote command that copies all files in the push list. Use PID tracking:

```bash
mpremote connect COM11 cp file1.py :file1.py + cp file2.py :file2.py & echo "MPREMOTE_PID:$!"; wait $!
```

Report which files were pushed.

After a successful push, update `.claude/par_manifest.json` with the current mtime of every pushed file. Use `os.path.getmtime()` to read mtimes.

### 5. Run the target on the Pico

Run the target file with mpremote in the background with PID tracking:

```bash
mpremote connect COM11 run <target_file> & echo "MPREMOTE_PID:$!"; wait $!
```

Run this as a **background task** so output streams. Report the PID to the user so they know what's running. Read and display output after a reasonable delay.

### Important

- Always use the PID tracking pattern: `& echo "MPREMOTE_PID:$!"; wait $!`
- Before any mpremote command, check if there's a previously running mpremote PID that needs killing.
- The target file is pushed as-is (same name on the Pico), NOT renamed to `main.py`.
- Dependencies are pushed to the Pico root with their original filenames.
