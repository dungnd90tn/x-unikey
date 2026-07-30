# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

x-unikey 1.0.4 (2006) — the Vietnamese UniKey input method for X11, by Pham Kim Long. GNU
autotools, C++ engine + C X11 front-ends. It ships three deliverables:

| Target | Built from | Kind |
|---|---|---|
| `ukxim` | [src/xim/](src/xim/) | XIM server (the actual input method) |
| `unikey` | [src/gui/](src/gui/) | Floating status window; also forks/supervises `ukxim` |
| `im-vn.la` | [src/unikey-gtk/](src/unikey-gtk/) | GTK2 `immodule` (optional, off by default) |

This is a CVS-era release tarball, not a git checkout (`doc/CVS/`, `src/IMdkit/doc/CVS/` are
leftovers). The generated autotools output (`configure`, `Makefile.in`, `aclocal.m4`) is checked
in — edit `configure.ac` / `Makefile.am` and re-run `autoreconf -i` only if you must; the shipped
`configure` works as-is.

## Build

```sh
./configure                        # ukxim + unikey
./configure --with-unikey-gtk      # also build the GTK2 im module
make
```

Other `configure` knobs: `--with-gtk-sysconfdir=PATH` (default `/etc/gtk-2.0`), `--with-libdir=PATH`
(where `im-vn.so` lands; default is gtk+-2.0's `libdir`).

There is **no test suite** — `make check` just recurses and does nothing. Verification means
compiling and running `ukxim`/`unikey` against a real X server.

### Modern-toolchain reality (verified on gcc 15)

`./configure` succeeds, but `make` does not. Two classes of breakage, both pre-C++11 idioms:

- `src/vnconv/data.cpp:243` — hundreds of `narrowing conversion of '\377…' from 'char' to
  'unsigned char'` errors in the charset tables.
- `src/ukengine/usrkeymap.cpp` and friends — `strchr`/`strlen`/`strcmp` used without
  `<cstring>`/`<cstdlib>`, which older headers pulled in transitively.

To get the pure engine libraries compiling without touching sources:

```sh
make CXXFLAGS="-g -O2 -w -fpermissive -include cstring -include cstdlib" \
     CFLAGS="-g -O2 -w -fpermissive -include string.h -include stdlib.h -include unistd.h"
```

That builds `libbyteio`, `libvnconv`, `libUnikey`, `libukint`. Everything past that needs X11 and
(for the GTK module) gtk+-2.0 development packages, neither of which is installed on this machine
(`/usr/include/X11/Xlib.h` absent, `pkg-config --exists x11` fails). Prefer these flags over
"fixing" the warnings unless the task is explicitly a modernization pass — see *Windows-shared
code* below.

### Do not run `make install` casually

Both install hooks mutate system state outside `$prefix`:

- [src/xim/Makefile.am](src/xim/Makefile.am) `install-data-hook` runs [src/xim/install.sh](src/xim/install.sh),
  which **interactively rewrites `/etc/profile`** (via `install.sed`/`uninstall.sed`) to export
  `LANG`, `GTK_IM_MODULE=xim`, `XMODIFIERS=@im=unikey`.
- [src/unikey-gtk/Makefile.am](src/unikey-gtk/Makefile.am) `install-data-hook` overwrites
  `$(gtk_sysconfdir)/gtk.immodules` with fresh `gtk-query-immodules-2.0` output.

## Architecture

### Library stack (strictly bottom-up, each a `noinst_LTLIBRARIES` convenience lib)

```
byteio      low-level byte/char stream I/O + file preamble (BOM) sniffing
  ↑
vnconv      Vietnamese charset conversion: CONV_CHARSET_* ↔ CONV_CHARSET_*
  ↑
ukengine    the typing engine (UkEngine, UkInputProcessor, CMacroTable)
  ↑
ukinterface plain-C façade over the C++ engine  →  linked into every front-end
```

[src/ukinterface/unikey.cpp](src/ukinterface/unikey.cpp) holds **one process-global engine**
(`MyKbEngine`) plus one `UkSharedMem *pShMem`, exposed through the `extern "C"` API in
[src/ukinterface/unikey.h](src/ukinterface/unikey.h). Front-ends never touch `UkEngine` directly;
they call `UnikeySetup` / `UnikeySetCapsState` / `UnikeyFilter` / `UnikeyBackspacePress` /
`UnikeyResetBuf` and then read the globals `UnikeyBackspaces`, `UnikeyBufChars`, `UnikeyBuf`,
`UnikeyOutput`. The header's leading comment block documents the required call order — follow it.

The engine's contract is *"send N backspaces, then these bytes"*: it never sees the text field, so
every front-end must synthesize backspaces before committing. `UnikeyFilter` must not be called for
Enter/Tab/arrows/Delete — use `UnikeyResetBuf` for those, `UnikeyBackspacePress` for Backspace.

### The engine

[src/ukengine/](src/ukengine/) is table-driven, in two layers:

1. `UkInputProcessor` ([inputproc.h](src/ukengine/inputproc.h)) maps a raw keycode to a
   `UkKeyEvent` (`vneTone1`, `vneRoof_a`, `vneHook_uo`, `vneDd`, `vneMapChar`, …) using a 256-entry
   `m_keyMap`. The built-in methods are just static `UkKeyMapping[]` arrays
   (`TelexMethodMapping`, `VniMethodMapping`, `VIQRMethodMapping`, …); a user-defined method is the
   same array loaded from a text file by [usrkeymap.cpp](src/ukengine/usrkeymap.cpp).
   **Adding an input method = adding a table, not adding code.**
2. `UkEngine::process()` ([ukengine.cpp](src/ukengine/ukengine.cpp):1732) dispatches the event to a
   `processTone`/`processRoof`/`processHook`/`processDd`/… member and maintains
   `WordInfo m_buffer[]` — a per-syllable model (`vnw_cvc` and friends, with `c1Offset/vOffset/c2Offset`,
   `VowelSeq`/`ConSeq`) that drives Vietnamese spell-checking. Spell-check is what lets 1.0+ leave
   `linux`/`changes` alone instead of requiring `linuxx`; it is also why abbreviations like `HDDQT`
   need `setSingleMode()` (CTRL-SHIFT-Z) first. `m_keyStrokes[]` keeps raw keys so
   `restoreKeyStrokes()` (CTRL-SHIFT-ESC) and auto-restore can undo processing.

`UkSharedMem` ([ukengine.h](src/ukengine/ukengine.h)) is deliberately pointer-free — it is placed in
shared memory in the Windows build. Do not add pointers or non-POD members to it.

### Front-ends and how they talk to each other

There is no socket or D-Bus. **All state lives in properties on the X root window** and every actor
reacts to `PropertyNotify`:

- Atom names in [src/gui/xvnkb.h](src/gui/xvnkb.h): `UK_CHARSET`, `UK_METHOD`, `UK_USING`,
  `UK_GUI_X_POSITION`, `UK_GUI_Y_POSITION`, `UK_GUI_VISIBLE`. The parallel `VK_*` names are xvnkb's;
  `ukxim -xvnkb-sync` binds to those instead so xvnkb's GUI can drive UniKey's engine
  (xvnkb's own core must then be disabled).
- Property *values* use xvnkb's enums (`vk_charsets`/`vk_methods`, e.g. `VKC_UTF8`, `VKM_TELEX`,
  `VKM_OFF`), which are **not** vnconv's `CONV_CHARSET_*` nor the engine's `UkInputMethod`.
  Translation lives in `uksync.c`.
- Consequence for control flow: a shortcut handler in `ukxim` does *not* flip its own state — it
  calls `UkSetPropValue(...)` and the change comes back through `handlePropertyChanged()`. Preserve
  that indirection; it is what keeps GUI, XIM server and GTK module consistent.
- `unikey` (GUI) `fork`/`execvp`s `ukxim` ([src/gui/gui.c](src/gui/gui.c):764), which reports launch
  success/failure back with `SIGUSR1`/`SIGUSR2` to its parent. `SIGUSR1` sent to `ukxim` means
  *reload config* (`kill -s USR1 $(pidof ukxim)`, or CTRL-SHIFT + left-click the GUI).
- Both `ukxim` and `unikey` enforce single-instance by owning an atom (`singleLaunch()`).

**`uksync.c` exists in three copies** — [src/xim/uksync.c](src/xim/uksync.c),
[src/gui/uksync.c](src/gui/uksync.c), [src/unikey-gtk/uksync.c](src/unikey-gtk/uksync.c) — and they
have intentionally diverged: the GUI version maps its own `UNIKEY_*` display enums instead of
`CONV_CHARSET_*`, and the GTK version *owns* `display`/`RootWindow` (plus `UkInitSync()`) where the
others `extern` them. A change to charset/method mapping must be mirrored into all three by hand.
Several other files are shared by being compiled from a sibling directory
(`../xim/optparse.c`, `../xim/ukopt.c`, `../gui/xvnkb.h` appear in other modules' `_SOURCES`).

`ukxim` is built on IMdkit ([src/IMdkit/](src/IMdkit/)) — Hidetoshi Tajima's XIM server toolkit,
vendored verbatim from the Sun/HP sample. Treat it as a third-party library; the UniKey code is the
`My*Handler` callbacks in [src/xim/xim.c](src/xim/xim.c) and the IC list in
[src/xim/IC.c](src/xim/IC.c). Two option-controlled quirks in that layer are the usual cause of
"app X doesn't work": `CommitMethod` (`XSendEvent` vs XIM forward-event) and `XimFlow`
(`Static` vs `Dynamic`, needed by rxvt-unicode). Both require fully restarting `unikey`, not a
config reload.

The GTK module ([src/unikey-gtk/gtkimcontextvn.c](src/unikey-gtk/gtkimcontextvn.c)) is a
`GtkIMContext` subclass wired into the same engine via `filter_keypress`; it stays passive unless the
`unikey` GUI is running, unless `GtkImAlone = Yes`.

### Configuration

Runtime config is `~/.unikey/options` (`~/.unikeyrc` was the pre-1.0 location; `doc/unikeyrc` is the
stale sample, `doc/options` is current). Parsing is generic and table-driven:

- [src/xim/optparse.c](src/xim/optparse.c) walks an `OptItem[]` of `{name, comment, offset, type, lookup}`
  where `offset` is a byte offset into the options struct and `type` is `LongOpt`/`BoolOpt`/`StrOpt`/`LookupOpt`.
  It both reads and writes the file (comments are regenerated from the table on save when
  `AutoSave = Yes`).
- [src/xim/ukopt.c](src/xim/ukopt.c) is that table for `UkXimOpt` ([ukopt.h](src/xim/ukopt.h));
  [src/gui/guiopt.c](src/gui/guiopt.c) is a much smaller one for the GUI's window position.

So **adding an option means three edits**: a field in `UkXimOpt`, a comment string, and an `OptItem`
entry (with `OptMap` lookup table if it's an enum). Nothing else needs to know.

Sample user data files live in [doc/](doc/): `im-samples/` (telex-pro, vni-new, microsoft, … for
`UsrKeyMapFile`; syntax in `doc/keymap-syntax`), `ukmacro` (macro/auto-text sample).

Macro-file encoding is version-detected in [src/ukengine/mactab.cpp](src/ukengine/mactab.cpp): a
leading `version=1` marker line means UTF-8 (the 1.0.4 format); a file without it is parsed as VIQR
and converted on load. `doc/manual` §4.5 still claims VIQR-only — the ChangeLog entry for 1.0.4 is
the accurate one.

## Windows-shared code

The engine, `vnconv` and `byteio` are shared verbatim with the Windows UniKey build (see the 1.0.3b
ChangeLog entry, "unified with Win-Unikey"). Hence `#if defined(WIN32)` blocks, the
`DllInterface`/`DllExport` macros, `stdafx.h`/`stdafx.cpp` precompiled-header stubs, `UnikeySysInfo`,
and the shared-memory-safe `UkSharedMem`. Keep these; deleting them as dead code diverges the
port.

Related: `dummy.cpp` in [src/xim/](src/xim/) and [src/unikey-gtk/](src/unikey-gtk/) contains a single
unused function whose only purpose is to make libtool link those C targets with `g++` against the C++
engine. Do not remove them.

## Conventions

Follow the emacs modeline at the top of each file rather than one house style — the C++ engine files
declare `tab-width:4; c-basic-offset:4; indent-tabs-mode:nil`, while `src/gui/gui.c` and parts of
`src/xim/` use 2-space indents with literal tabs. `AM_CPPFLAGS = -Wall` is set per module; cross-module
includes are done with explicit `-I../ukengine -I../vnconv -I../byteio` rather than a shared include
dir.

## User-facing controls (useful when reasoning about the key handling code)

Shortcuts are the `ShortcutList[]`/`Trigger_Keys[]` tables in [src/xim/xim.c](src/xim/xim.c):
CTRL-SHIFT or CTRL-SHIFT-F9 toggles Vietnamese; CTRL-SHIFT-F1..F4 pick charset
(Unicode/VIQR/TCVN/VNI); F5..F8 pick input method (Telex/VNI/VIQR/user-defined); CTRL-SHIFT-ESC
restores raw keystrokes; CTRL-SHIFT-Z disables spell-check for the next word. On the GUI window:
left-click toggles, right-click rotates charset, CTRL-right-click rotates method,
CTRL-ALT-left-click hides the window and disables the server *without* unloading the processes
(deliberate — killing `ukxim` can crash clients that hold an XIM connection). Full details in
[doc/manual](doc/manual).
