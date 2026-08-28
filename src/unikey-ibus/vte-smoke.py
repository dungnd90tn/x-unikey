#!/usr/bin/env python3
"""Manual end-to-end regression test through GTK3 -> IBus -> VTE -> PTY.

Run after installing/restarting the locally built IBus engine. The `unikey`
input source must be active in the desktop session.
"""

import argparse
import os
import sys

os.environ["GTK_IM_MODULE"] = "ibus"

import gi

gi.require_version("Gdk", "3.0")
gi.require_version("Gtk", "3.0")
gi.require_version("Vte", "2.91")

from gi.repository import Gdk, Gio, GLib, Gtk, Vte

try:
    gi.require_version("GdkX11", "3.0")
    from gi.repository import GdkX11
except (ImportError, ValueError):
    GdkX11 = None


parser = argparse.ArgumentParser()
parser.add_argument(
    "--terminal-mode",
    default="off",
    choices=("off",),
    help="terminal profile is fixed to off",
)
args = parser.parse_args()

KEYS = "xem ddwowcj chuwa naof"
EXPECTED = KEYS
CAPTURE = "/tmp/x-unikey-vte-capture-%d.txt" % os.getpid()

window = Gtk.Window(title="x-unikey VTE regression test (%s)" % args.terminal_mode)
terminal = Vte.Terminal()
window.add(terminal)
window.set_default_size(640, 240)
window.connect("destroy", Gtk.main_quit)
window.show_all()
terminal.grab_focus()

display = Gdk.Display.get_default()
keymap = Gdk.Keymap.get_for_display(display)
keyboard = display.get_default_seat().get_keyboard()
pending = list(KEYS) + ["Return"]
exit_status = 1
cancellable = Gio.Cancellable()


def queue_key(key):
    keyval = Gdk.KEY_Return if key == "Return" else ord(key)
    found, entries = keymap.get_entries_for_keyval(keyval)
    if not found:
        raise RuntimeError("keyval is not in keymap: %r" % key)

    for event_type in (Gdk.EventType.KEY_PRESS, Gdk.EventType.KEY_RELEASE):
        event = Gdk.Event.new(event_type)
        event.window = window.get_window()
        event.send_event = True
        event.time = Gdk.CURRENT_TIME
        event.state = Gdk.ModifierType(0)
        event.keyval = keyval
        event.hardware_keycode = entries[0].keycode
        event.group = entries[0].group
        event.set_device(keyboard)
        event.put()


def send_next():
    if pending:
        queue_key(pending.pop(0))
        return GLib.SOURCE_CONTINUE
    GLib.timeout_add(800, finish)
    return GLib.SOURCE_REMOVE


def finish():
    global exit_status

    if not os.path.exists(CAPTURE):
        print("FAIL: capture file was not created", file=sys.stderr)
    else:
        with open(CAPTURE, encoding="utf-8") as stream:
            got = stream.read()
        os.unlink(CAPTURE)
        if got == EXPECTED:
            print("PASS: %s -> %s" % (KEYS, got))
            exit_status = 0
        else:
            print("FAIL: got %r; expected %r" % (got, EXPECTED),
                  file=sys.stderr)
    Gtk.main_quit()
    return GLib.SOURCE_REMOVE


def start_typing():
    if not window.is_active() or not terminal.has_focus():
        # Wayland may deny focus stealing even though synthetic GDK events can
        # still be delivered to this window. Continue; a missing IM focus will
        # make the captured output fail explicitly below.
        print("warning: compositor did not mark the test window active",
              file=sys.stderr)
    GLib.timeout_add(40, send_next)
    return GLib.SOURCE_REMOVE


def begin():
    event_time = Gdk.CURRENT_TIME
    if GdkX11 is not None and isinstance(display, GdkX11.X11Display):
        event_time = GdkX11.x11_get_server_time(window.get_window())
    window.present_with_time(event_time)
    window.get_window().focus(event_time)
    terminal.grab_focus()
    GLib.timeout_add(500, start_typing)
    return GLib.SOURCE_REMOVE


def spawned(_terminal, child_pid, error, _user_data):
    if error is not None or child_pid < 0:
        print("FAIL: cannot spawn the VTE capture process: %s" % error,
              file=sys.stderr)
        Gtk.main_quit()
        return
    GLib.timeout_add(1500, begin)


terminal.spawn_async(
    pty_flags=Vte.PtyFlags.DEFAULT,
    working_directory=None,
    argv=[
        "/bin/bash",
        "-c",
        'IFS= read -r line; printf "%s" "$line" > ' + CAPTURE,
    ],
    envv=None,
    spawn_flags=GLib.SpawnFlags.DEFAULT,
    child_setup=None,
    timeout=-1,
    cancellable=cancellable,
    callback=spawned,
    user_data=window,
)
Gtk.main()
raise SystemExit(exit_status)
