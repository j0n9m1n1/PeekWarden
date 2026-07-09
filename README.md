# PeekWarden

## References and Acknowledgements

PeekWarden was designed with reference to:

- [1Password Quick Access](https://support.1password.com/quick-access/) and [1Password keyboard shortcuts](https://support.1password.com/keyboard-shortcuts/)
- [Trikzon/quickwarden](https://github.com/Trikzon/quickwarden)
- [ixnas/Quickwarden](https://github.com/ixnas/Quickwarden)

Development was assisted by OpenAI ChatGPT 5.5.

PeekWarden is a small Qt Widgets desktop quick-access app for Bitwarden vault
items. It is inspired by 1Password Quick Access, but uses the Bitwarden CLI
(`bw`) as the backend instead of implementing Bitwarden encryption or APIs
directly.

This project is early Windows-first software.

## Features

- Global quick-access hotkey on Windows.
- Frameless always-on-top search window.
- Bitwarden CLI login and unlock flow.
- Startup preload so unlocked vault items are cached before the search window is opened.
- In-memory search over item names, usernames, URIs, notes, and basic metadata.
- Fast keyboard actions for copying username, password, TOTP, and opening details.
- Item detail window with redacted raw JSON, clickable website links, notes, and metadata.
- Optional web favicon loading for search results.
- System tray menu with open, lock, settings, and quit.
- Configurable hotkey, shortcut preset, quick window size, position, opacity, corner style, background color, font, language, result fields, result count, auto-sync interval, and focus-loss behavior.
- Optional Windows Credential Manager session storage.
- Optional Windows startup registration without administrator rights.

## Requirements

- Windows 10/11
- Qt 6.11 or a compatible Qt 6 version
- CMake 3.21+
- C++17 compiler
- Bitwarden CLI (`bw`)

PeekWarden first looks for `bw.exe` next to the application executable. If it is
not found there, it falls back to `bw` from `PATH`.

Bitwarden CLI download and setup:
https://bitwarden.com/help/cli/

## Build

Install Qt and the Bitwarden CLI first.

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\mingw_64"
cmake --build build --config Release
```

If Qt is already discoverable by CMake, the `CMAKE_PREFIX_PATH` argument is not
needed.

## Usage

Default quick-access hotkey:

```text
Ctrl+Shift+Space
```

1Password-style shortcut preset:

```text
Ctrl+C          Copy username or primary field
Ctrl+Shift+C    Copy password
Ctrl+Alt+C      Copy TOTP
Ctrl+Shift+O    Open item details
Ctrl+R          Sync vault
Ctrl+/          Open settings
Esc             Close
```

Bitwarden-style shortcut preset:

```text
Ctrl+Enter      Copy username
Enter           Copy password
Alt+T           Copy TOTP
Ctrl+Shift+O    Open item details
Ctrl+R          Sync vault
Ctrl+/          Open settings
Esc             Close
```

## Security Notes

- The master password is never stored by PeekWarden.
- PeekWarden uses `bw login` and `bw unlock --raw` to obtain a Bitwarden CLI
  session.
- Vault item fields needed for quick search and copy are cached in memory while
  the app is running.
- In-memory vault data is cleared when the vault is locked or the app exits.
- Passwords copied to the clipboard are cleared after 30 seconds if the clipboard
  still contains that same password.
- TOTP secrets and passwords are not written to PeekWarden settings.
- Raw item JSON shown in the detail window redacts password and TOTP fields.
- Optional Windows Credential Manager storage persists the Bitwarden CLI session
  key, not the master password.

## Current Limitations

- Windows is the primary target.
- macOS/Linux global hotkey support is not implemented yet.
- Multi-account support is not implemented yet.
- The app depends on the Bitwarden CLI being installed and logged in/unlocked.
- Vault items are cached in memory for fast searching.

## Status

This is a personal prototype and should be treated as preview software.

## License

PeekWarden application source code is licensed under the MIT License. See
[LICENSE](LICENSE).

PeekWarden currently links against Qt Widgets, Qt Concurrent, and Qt Network.
Qt is not relicensed by this project. When distributing Windows builds made with
open-source Qt, PeekWarden should use Qt as dynamically linked DLLs and include
the notices, license texts, and source-code access or written offer required by
the applicable Qt/LGPL terms. Users must be able to replace the Qt DLLs with
compatible versions.

This project does not currently use Qt modules that Qt documents as GPL-only.
If that changes, or if Qt is statically linked, the distribution requirements may
change significantly.

This is not legal advice.
