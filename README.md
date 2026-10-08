# omnibox-launchpad

A tiny, fast, no-nonsense **command-line app launcher** written in C++17.
It scans your PC for installed apps, then lets you fuzzy-search and launch
them from a single prompt. No RGB, no bloat, no telemetry.

## Features

- **Automatic app detection**: scans the Windows Start Menu folders
  (all-users + current user) and `%LOCALAPPDATA%\Programs` for `.lnk` / `.exe`
- **Fuzzy search**: ranks prefix matches > substring matches > subsequence matches
  (type `fire` or even `ffx` to find Firefox)
- **One-keystroke launch**: pick a result by number
- **Noise filtering**: uninstallers are skipped, duplicates are merged
- **Zero dependencies**: standard library + the Windows API only

## Build

Requirements: a C++17 compiler (MinGW-w64 `g++` or MSVC) and `make`.

```bash
git clone https://github.com/Cxphr/omnibox-launchpad.git
cd omnibox-launchpad
make
```

Without `make`:

```bash
g++ -std=c++17 -O2 src/main.cpp -o omnibox.exe
```

## Usage

```text
$ ./omnibox
[omnibox] scanning installed apps...
[omnibox] 143 apps detected.

> fire
  [1] Firefox
  [2] Fire Alpaca

> 1
Launching Firefox...
```

| Command   | What it does                          |
|-----------|---------------------------------------|
| `<text>`  | Fuzzy-search detected apps            |
| `<number>`| Launch that result from the last list |
| `:list`   | Print every detected app              |
| `:rescan` | Re-scan for newly installed apps      |
| `:q`      | Quit                                  |

## How detection works

1. Walk the Start Menu `Programs` folders and `%LOCALAPPDATA%\Programs`.
2. Keep `.lnk` and `.exe` files, drop anything named like an uninstaller.
3. De-duplicate by lowercase name and sort alphabetically.
4. Launch via `ShellExecuteA` (Windows) or `xdg-open` (fallback).

## Roadmap

- [ ] Registry scan (`HKLM\...\Uninstall`) for apps without shortcuts
- [ ] Launch-frequency ranking
- [ ] Global hotkey + small popup window
- [ ] Linux `.desktop` file detection
- [ ] Config file for custom folders and aliases

## License

MIT
