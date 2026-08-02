# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

UniKey Vietnamese input method. Started life as x-unikey 1.0.4 (2006), an XIM server for X11; the
XIM/GTK2 layers have been removed and it is now a set of modern input-method front-ends sharing the
original typing engine.

| Target | Built from | Kind |
|---|---|---|
| `ibus-engine-unikey` | [src/unikey-ibus/](src/unikey-ibus/) | IBus engine — the primary front-end |
| `im-unikey.so` | [src/unikey-gtk3/](src/unikey-gtk3/) | GTK3 immodule |
| `libim-unikey.so` | [src/unikey-gtk4/](src/unikey-gtk4/) | GTK4 immodule |
| `libunikeyplatforminputcontextplugin.so` | [src/unikey-qt/](src/unikey-qt/) | Qt6 platform input context plugin |
| `libukbridge.la` | [src/ukbridge/](src/ukbridge/) | Toolkit-independent layer every front-end shares |

Design rationale and the measurements behind it: [doc/STANDALONE.md](doc/STANDALONE.md).
User-facing build/install/usage: [README](README).

## Architecture

```
byteio → vnconv → ukengine → ukinterface (C API)   ← unchanged 2006 engine
                                  ↑
                              ukbridge                ← the layer to understand
                    ┌─────────┬────┴────┬──────────┐
                  ibus      GTK3      GTK4        Qt6
```

The engine's contract is *"delete N characters, then insert these bytes"*. `ukbridge` turns that
into either a **preedit string** or **direct commits**, and owns everything else the front-ends
would otherwise duplicate: the shortcut table ([ukkeys.c](src/ukbridge/ukkeys.c), keyed on X11
keysyms — `GDK_KEY_*` and `IBUS_KEY_*` are the same numbers), config parsing
([ukopt.c](src/ukbridge/ukopt.c)), and cross-process state. A front-end should only translate its
toolkit's event type and render the result. If you find yourself writing typing logic in a
front-end, it belongs in `ukbridge`.

`ukinterface` holds one process-global `UkEngine`; `uk_bridge_reset()` on focus-in keeps contexts
from bleeding into each other. `UkSharedMem` ([ukengine.h](src/ukengine/ukengine.h)) is
deliberately pointer-free because the Windows build puts it in shared memory — don't add pointers.

Input methods are *tables* (`UkKeyMapping[]` in [inputproc.h](src/ukengine/inputproc.h)), and
options are a table too (`OptItem[]` with struct byte offsets), so adding either is a mechanical
edit, not new code.

`ibus-setup-unikey` is a non-resident GTK3 preferences dialog. The IBus XML `<setup>` hook,
and panel property open that same executable; it is not an autostart/status UI and does not need
an application `.desktop` launcher. It writes the shared `~/.unikey/options` atomically and
preserves the first pre-rewrite file as `options.bak`. Only `state` (enabled/input method) reloads
live: do not watch/reload all options from every `UkBridge`, because `ukinterface` has one
process-global engine and an unfocused context could reset a focused context's composition.
`UkXimOpt` scalar fields and `BoolOpt`/`LookupOpt` accesses must remain `int`; writing them as
`long` corrupts adjacent fields on 64-bit systems. Modern frontends must keep output fixed to
UTF-8, and global DIRECT/PREEDIT is deliberately not configurable.

### DIRECT vs PREEDIT — read this before touching the commit path

The IBus engine uses the policy in
[ibus-policy.h](src/unikey-ibus/ibus-policy.h), recomputed for each input context:

- password/PIN/hidden text → OFF;
- `IBUS_INPUT_PURPOSE_TERMINAL` → `TerminalMode` (`Off` by default or transparent `Preedit`),
  even if the caps contain `IBUS_CAP_SURROUNDING_TEXT`;
- a latched `IBUS_INPUT_PURPOSE_URL` → DIRECT only after surrounding text is known and present,
  otherwise OFF. Chromium may reset purpose to FREE_FORM before caps settle, so latch URL until
  focus-out; this keeps omnibox search conversion without composition/predict;
- every other IBus context → transparent PREEDIT, regardless of capabilities.

An IBus surrounding-text capability does not prove that it represents the user's editable
document. VS Code/xterm.js advertises `0x29` and FREE_FORM through Chromium's hidden textarea;
deleting there is not an edit of the shell line and commits become PTY input. There is no app-id,
widget role, or terminal purpose on this path, so auto-selecting DIRECT cannot be made reliable.
DIRECT remains a bridge primitive for explicitly trusted frontends only. GTK/Qt modules are also
always PREEDIT.

Three approaches were tried and failed. Do not reintroduce them:

1. **Selecting DIRECT from surrounding-text caps.** VTE may drop the delete, while
   VS Code/xterm.js exposes only Chromium's hidden textarea rather than the shell buffer. Old
   characters survive, or xterm sends a completed word twice.
2. **Only replacing delete with `forward_key_event(BackSpace)`.** Forwarded keys go through the
   toolkit's event queue while `commit_text` is applied directly to the widget; nothing orders the
   two, and this breaks browsers as well. An all-forward backend would be a separate design and
   needs async/sync and GTK4 coverage.
3. **Trusting `0x29` after a surrounding-text handshake.** It can still describe a helper buffer,
   not the text being edited. The handshake is useful for diagnostics, not a DIRECT allowlist.

Two related invariants, both easy to break:

- IBus publishes every preedit update, including the empty one, with `IBUS_ENGINE_PREEDIT_CLEAR`.
  On flush it emits CommitText before clearing preedit. Clearing first lets Chromium finalize and
  send the old composition, then process CommitText as a second copy of the same word.
- In DIRECT mode a pass-through character must be **committed by the bridge**, not returned as
  `UK_BRIDGE_PASS`. Letting the application insert it puts the character on the key-event path
  while the erase goes on the input-method path; fast typing reorders them (`đấy` → `đâdấyd`).
- `uk_bridge_backspace` must **never swallow BackSpace** in DIRECT mode. Where the erase is
  ignored, swallowing it means the key does nothing until the engine buffer drains — it feels like
  "you have to hold backspace for a while".

**Diagnose, don't guess:** `touch ~/.unikey/debug && ibus restart` makes the engine log the engine
pointer, focus state, purpose, caps, and chosen mode to `~/.unikey/debug.log`. An env var is no use
— ibus-daemon spawns the engine. The manual VTE regression harness is
`python3 src/unikey-ibus/vte-smoke.py --terminal-mode off|preedit` after
installing/restarting the engine.

### Cross-process state

Enable/disable and input method live in `~/.unikey/state`, written atomically via `rename()` and
watched with inotify, so a toggle in one app reaches the others without a daemon. Two things that
bit us, both now covered by tests: watch the **directory** (rename swaps the inode, so a watch on
the file dies after the first write), and do **not** filter self-triggered events (a "we're
writing" flag got stuck and swallowed a real change from another process; re-reading your own state
is already a no-op).

## Build

```sh
./autogen.sh          # only after a fresh clone — generated files are gitignored
./configure CFLAGS="-O2 -std=gnu17" \
            CXXFLAGS="-O2 -Wno-narrowing -include cstring -include cstdlib"
make && make check
```

The extra flags are needed because gcc ≥ 14 rejects pre-C++11 idioms in the 2006 engine:
`-std=gnu17` (C23 makes implicit declarations errors), `-Wno-narrowing` (vnconv's charset tables
put `'\xNN'` literals in `unsigned char` arrays; the truncation is bit-identical), and the two
`-include`s (files call `strchr`/`strlen` relying on transitive includes that no longer happen).
One source fix was applied for the same reason:
[src/ukengine/mactab.cpp](src/ukengine/mactab.cpp):289.

`configure` skips any front-end whose libraries are missing, so check its output to see what will
actually be built. **After editing any `Makefile.am` or `configure.ac`, run `autoreconf -i`.**

Three build-system traps, all previously hit:

- **`PKG_PROG_PKG_CONFIG` must be called unconditionally** before any `PKG_CHECK_MODULES`. Every
  such check here sits inside an `if`, so autoconf otherwise buries the `$PKG_CONFIG` assignment in
  the first one — leaving it empty and making *every* later pkg-config check report "not found".
- **Convenience libraries must not `_LIBADD` each other.** `ukengine/stdafx.cpp` and
  `vnconv/stdafx.cpp` both produce `stdafx.o`; nesting them merges two same-named members into one
  archive, which libtool ≥ 2.5 rejects ("object name conflicts in archive"). Every final target
  lists all four convenience libs explicitly.
- **Deleting a source directory leaves stale `.deps/*.Plo`** referring to the old paths, and `make
  clean` does not remove them. `find . -name .deps -type d -exec rm -rf {} +` then re-run
  `config.status`.

## Tests

`make check` runs two binaries — the only tests in the tree:

- [src/ukbridge/test-bridge.c](src/ukbridge/test-bridge.c): drives `ukbridge` with key sequences
  and asserts the resulting text, in both PREEDIT and DIRECT modes, plus mid-session mode switches
  and two-process state sync. It points `$HOME` at a `mkdtemp` directory, so it never touches the
  developer's real `~/.unikey`. Regression cases are kept verbatim from real bug reports —
  `xem ddwowcj chuwa naof → xem được chưa nào` is the terminal corruption above.
- [src/unikey-qt/test-plugin.cpp](src/unikey-qt/test-plugin.cpp): does what Qt does at startup —
  read plugin metadata, check the `unikey` key and factory IID, load with `QPluginLoader`, call
  `create()`. Written because no Qt application is installed to type into.

The GTK and IBus front-ends have no automated test; verify them by launching a real app and
confirming the `.so` is mapped via `/proc/<pid>/maps`, or by reading the debug log above.

## Packaging

`./make-deb.sh` produces `release/`. It installs under `/usr` (dpkg's territory), regenerates the
IBus component XML so `<exec>` matches, and derives `Depends:` from the built binaries with
`dpkg-shlibdeps` rather than a hand-written list. `install-standalone.sh` is the non-dpkg
alternative and installs under `/usr/local` — **the two must not be mixed**, they share the
GTK/Qt module paths but not the engine path.

The IBus component XML must land in ibus's own datadir
(`pkg-config ibus-1.0 --variable=datadir`), not `$prefix/share`. Installing it to
`/usr/local/share/ibus/component` fails silently: ibus never scans there, so the engine simply
doesn't appear in Settings.

## Environment facts worth keeping

- GNOME Settings → Input Sources accepts only two source types, `xkb` and `ibus` (see
  `gsettings describe org.gnome.desktop.input-sources sources`). A toolkit module can never appear
  there; that is why the IBus engine exists and why fcitx5 ships its own tray UI.
- gnome-shell starts ibus **once**, at session start. Installing ibus mid-session and running
  `ibus-daemon` by hand is not equivalent — `ibus list-engine` will show the engine while Settings
  shows nothing. Log out and back in.
- Mutter exposes `zwp_text_input_v3` but not `zwp_input_method_v2`, and routes text-input to its
  built-in ibus. So on GNOME, Electron/VSCode and GTK4-on-Wayland apps are reachable **only**
  through the IBus engine — the toolkit modules (and fcitx5) cannot serve them.

## Conventions

Follow the emacs modeline at the top of each file rather than one house style — the C++ engine uses
4-space indents with no tabs; some older C files use 2-space with literal tabs. `dummy.cpp` in each
front-end directory exists solely to make libtool link with `g++` against the C++ engine; don't
remove them. The engine, `vnconv` and `byteio` are shared verbatim with the Windows UniKey build,
which is why `#if defined(WIN32)` blocks, `DllInterface` macros and `stdafx.h` stubs are still
there — keep them.

Old source/manual installs may leave `/usr/local/bin/unikey`, `/usr/local/bin/ukxim`, and
`~/.config/autostart/unikey.desktop`; that pair creates the obsolete `TX: UTF8` XIM status window
and is unrelated to the IBus engine. `install-standalone.sh cleanup-legacy` removes exactly those
artifacts while deliberately preserving `~/.unikey`, which the modern frontends still share.
