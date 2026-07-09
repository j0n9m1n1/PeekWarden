# Project: PeekWarden

This is a personal cross-platform Bitwarden Quick Access app.

## Goal

Build a small desktop app similar to 1Password Quick Access.

The app should:
- Use Qt 6 Widgets, not QML.
- Use C++17.
- Use Bitwarden CLI (`bw`) as the backend.
- Not implement Bitwarden encryption/API directly.
- Work first on Windows.
- Later be portable to macOS and Linux.

## MVP Features

- Global hotkey: Ctrl+Shift+Space on Windows.
- Show a small frameless always-on-top search window.
- Unlock Bitwarden using `bw unlock --raw` if there is no session.
- Search vault items using `bw list items --search <keyword>`.
- Show item name and username in a list.
- Enter copies password.
- Ctrl+Enter copies username.
- Alt+T copies TOTP.
- Clear password clipboard after 30 seconds.

## Technical Requirements

- Use CMake.
- Use Qt 6.11 or compatible Qt 6 version.
- Use QProcess to call `bw`.
- First try to find `bw.exe` in the application directory.
- If not found, fall back to `bw` from PATH.
- Do not store the master password.
- Do not store the Bitwarden session in plain text for now.
- Keep code simple and MVP-oriented.

## Later Improvements

- Cache all vault items in memory after unlock.
- Add fuzzy search.
- Add tray icon.
- Add settings UI.
- Add macOS/Linux global hotkey support.
- Use OS credential storage for session handling.
