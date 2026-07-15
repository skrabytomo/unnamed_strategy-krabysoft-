# Packaging — one-click installer

The game reads/writes save data in the OS per-user data dir
(`%APPDATA%\krabysoft\unnamed_strategy` on Windows), so installing/uninstalling
never touches player progress, and the database self-creates on first run.

## Make a distributable build

```bash
./packaging/build_installer.sh
```

This:
1. builds a Release exe,
2. assembles `dist/` — a **self-contained folder** with the exe, every runtime
   DLL (SDL2, sqlite3, lua, glew, gcc/stdc++ runtime), and all assets. This
   folder runs on a clean Windows machine with **no msys2 installed**.
3. if Inno Setup (`iscc`) is on PATH → produces
   `packaging/Output/UnnamedStrategy-Setup.exe` (the one-click installer).
   Otherwise → a portable ZIP via CPack.

## Testing on a fresh machine / VM

- **Installer:** copy `UnnamedStrategy-Setup.exe`, run it, click through, launch.
- **Portable:** copy the whole `dist/` folder, double-click `unnamed_strategy.exe`.

Either works with zero dependencies. Missing art just renders as fallback
tiles/dots — the game is still playable while you generate assets.

## Dev workflow is unchanged

`./run.sh` still builds+runs in place from `build/bin/` as before. Packaging is
a separate, on-demand step.

## One-time setup for the .exe installer

Install Inno Setup (free): https://jrsoftware.org/isdl.php — then `iscc` is
available and `build_installer.sh` produces the Setup.exe automatically.
