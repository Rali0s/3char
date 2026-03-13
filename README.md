# 3Char-TTY // 3TTY

SysAdmin-focused, cross-platform terminal workstation.

![GUI](/3tty.png)

## Highlights

- Local shell profiles + SSH profiles
- MultiMode grid terminals (open 2, 4, 6, etc. at once)
- Proxy-aware remote workflows
- Script scheduler with log capture
- Guest Mode (no password, no storage)
- Local-only OLLAMA integration (default loopback endpoint)
- History settings + keyboard copy/paste modes + right-click auto-copy
- Local bundled xterm.js assets (no CDN required at runtime)

## Binary Name

The built executable is named:

- `3TTY`

## Build

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build -j
```

## Run

```bash
./build/3TTY
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Dependencies

- Qt6: Core, Gui, Widgets, Network, WebEngineWidgets, WebChannel
- Optional: `libssh` for native scheduled SSH workflows
- Optional: `connect-proxy` and/or `nc` for proxy auth tunneling
- Optional: local OLLAMA daemon on `http://127.0.0.1:11434`

## Security Notes

- Default OLLAMA endpoint is local loopback (`127.0.0.1`) only.
- Scheduler logs are written under `QStandardPaths::AppDataLocation/workflow-logs`.
- Profile/secrets config is encrypted when not in Guest Mode.

## Bash/Regex References

- [Regex Quickstart (RexEgg)](https://www.rexegg.com/regex-quickstart.php)
- [grep Cheatsheet (Ryan's Tutorials)](https://ryanstutorials.net/linuxtutorial/cheatsheetgrep.php)
