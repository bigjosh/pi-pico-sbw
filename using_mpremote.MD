# Using mpremote Without Losing the COM Port

## The Problem

On Windows, the serial COM port is a single-owner resource. When `mpremote`
hangs (waiting for REPL access, watching a running program, etc.) and gets
killed, Windows often fails to release the COM port handle. The only recovery
is physically unplugging and replugging the Pico USB cable.

This happens because:

- **Any mpremote command can hang** if the Pico is running code and doesn't
  yield the REPL. Not just `mpremote run` — even `cp`, `ls`, and `reset` need
  REPL access and will block if the Pico is busy.
- **`taskkill /F`** terminates the process but doesn't guarantee the kernel
  releases the serial port handle.
- **IDE extensions** (e.g. VS Code "Pi Pico Project") can silently hold the
  port even when showing "disconnected."

## The Solution: Always Track the PID

Every mpremote invocation should capture its PID:

```bash
mpremote connect COM11 <command> & echo "MPREMOTE_PID:$!"; wait $!
```

### When to Kill

Not every command needs killing. The PID is a safety net, not a default action.

- **Short commands** (`cp`, `ls`, `reset`, `exec`): Let them finish normally.
  They complete on their own and release the port. Only kill as a last resort
  if a command that should have finished is still hanging.
- **Open-ended commands** (`run` on a long-running script, `repl`): These hold
  the port indefinitely by design. Kill the PID when you need the port back.
- **Last resort**: If a command you expected to finish is still running after a
  reasonable time, kill the PID. Don't kill preemptively — a large `cp` may
  just need more time.

```bash
# Only when needed:
kill $LAST_PID 2>/dev/null; sleep 3
```

### Why This Works

- The PID is always visible in the output stream, so you (or an AI agent) can
  always find it.
- mpremote can stay running as long as needed (e.g. watching serial output from
  a long-running program) — you only kill it when you need the port again.
- No wrapper scripts, no proxy processes, no extra infrastructure.

## Workflow Examples

### Push files to the Pico

```bash
mpremote connect COM11 cp main.py :main.py & echo "MPREMOTE_PID:$!"; wait $!
```

### Push and reset

```bash
mpremote connect COM11 cp main.py :main.py & echo "MPREMOTE_PID:$!"; wait $!
mpremote connect COM11 reset & echo "MPREMOTE_PID:$!"; wait $!
```

### Run a script and watch output

```bash
mpremote connect COM11 run test.py & echo "MPREMOTE_PID:$!"; wait $!
```

If the script is long-running, mpremote stays connected and streams output.
When you need the port back:

```bash
kill <PID> 2>/dev/null; sleep 3
```

### Quick REPL check

```bash
mpremote connect COM11 exec "print('alive')" & echo "MPREMOTE_PID:$!"; wait $!
```

## For AI Agents

If you are an AI coding assistant working with a Pico on this project:

1. **Always** run mpremote with the PID tracking pattern shown above.
2. **Remember** the PID from stdout — it will be on the `MPREMOTE_PID:` line.
3. **Before** any new mpremote command, kill the previous PID and wait 3 seconds.
4. If you get "port in use" errors despite killing the PID, ask the user to
   unplug and replug the Pico USB. This is the only recovery when a handle
   leaks at the kernel level.
5. Close VS Code or any IDE with Pico extensions before starting a session.
