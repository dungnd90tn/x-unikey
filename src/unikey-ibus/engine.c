// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey IBus engine
 *
 * Vi sao co file nay, khi da co module GTK3/GTK4/Qt?
 *
 * Vi GNOME Settings -> Keyboard -> Input Sources chi nhan dung hai loai nguon:
 * layout "xkb" va engine "ibus" (xem `gsettings describe
 * org.gnome.desktop.input-sources sources`). Module toolkit du viet tot den dau
 * cung khong the xuat hien trong danh sach do -- fcitx5 cung vay, no phai tu lam
 * tray icon rieng. Muon "them bo go vao he thong la xong" thi bat buoc phai la
 * mot IBus engine.
 *
 * Doi lai, IBus engine phu duoc ca nhung app ma module toolkit khong voi toi:
 * Mutter noi zwp_text_input_v3 vao ibus, nen VSCode/Electron va app GTK4 chay
 * Wayland deu di qua day.
 *
 * Toan bo logic go tieng Viet van la ukbridge -- file nay chi la mot front-end
 * nua, khong lap lai gi.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ibus.h>
#include <glib-unix.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "ukbridge.h"
#include "ukkeys.h"
#include "keycons.h"
#include "ukopt.h"
#include "ibus-policy.h"

#define UK_TYPE_IBUS_ENGINE (uk_ibus_engine_get_type())

/*--------------------------------------------------------------------
  Log go roi.

  Engine do ibus-daemon sinh ra nen khong dat bien moi truong vao duoc.
  Vi vay bat log bang cach TAO FILE:

      touch ~/.unikey/debug && ibus restart

  Moi thong tin ghi vao ~/.unikey/debug.log. Xoa file debug de tat.

  Co cai nay de lan sau chan doan bang so lieu, khong phai suy doan: no in ra
  purpose/caps that su cua tung o nhap.
 --------------------------------------------------------------------*/
static int uk_debug_on(void)
{
    static int checked = -1;
    if (checked < 0) {
        char path[256];
        const char *home = g_getenv("HOME");
        g_snprintf(path, sizeof(path), "%s/.unikey/debug", home ? home : "/tmp");
        checked = g_file_test(path, G_FILE_TEST_EXISTS) ? 1 : 0;
    }
    return checked;
}

static void uk_log(const char *fmt, ...)
{
    char path[256];
    const char *home;
    FILE *f;
    va_list ap;

    if (!uk_debug_on())
        return;

    home = g_getenv("HOME");
    g_snprintf(path, sizeof(path), "%s/.unikey/debug.log", home ? home : "/tmp");
    f = fopen(path, "a");
    if (!f)
        return;

    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

typedef struct _UkIBusEngine      UkIBusEngine;
typedef struct _UkIBusEngineClass UkIBusEngineClass;

struct _UkIBusEngine {
    IBusEngine parent;
    UkBridge  *bridge;
    guint      state_source;
    guint      capabilities;
    guint      purpose;
    guint      hints;
    gboolean   preedit_visible;
    gboolean   capabilities_known;
    gboolean   focused;
    gboolean   mode_applied;
    UkIBusMode mode;
};

struct _UkIBusEngineClass {
    IBusEngineClass parent_class;
};

GType uk_ibus_engine_get_type(void);
G_DEFINE_TYPE(UkIBusEngine, uk_ibus_engine, IBUS_TYPE_ENGINE)

/*--------------------------------------------------------------------
  Callback tu bridge -> IBus
 --------------------------------------------------------------------*/
static void on_commit(void *user_data, const char *utf8)
{
    UkIBusEngine *self = (UkIBusEngine *)user_data;
    IBusText *text = ibus_text_new_from_string(utf8);
    ibus_engine_commit_text(IBUS_ENGINE(self), text);
}

/* Xoa n ky tu truoc con tro. IBus policy khong tu bat DIRECT tu capability:
 * callback nay chi con la primitive cho frontend/client duoc tin cay. */
static void on_erase_before_cursor(void *user_data, int nchars)
{
    UkIBusEngine *self = (UkIBusEngine *)user_data;
    /* offset am = lui ve truoc con tro */
    ibus_engine_delete_surrounding_text(IBUS_ENGINE(self), -nchars, (guint)nchars);
}

static void on_preedit_changed(void *user_data, const char *utf8)
{
    UkIBusEngine *self = (UkIBusEngine *)user_data;
    IBusText *text;
    glong len;
    gboolean visible;

    if (!utf8)
        utf8 = "";
    visible = *utf8 != '\0';
    text = ibus_text_new_from_string(utf8);
    len = visible ? g_utf8_strlen(utf8, -1) : 0;

    /* Khong them underline: trong terminal, preedit trong suot nhu chu thuong.
       PREEDIT_CLEAR de x-unikey la chu so huu commit duy nhat. GNOME Shell
       bat client_commit_preedit; neu gui PREEDIT_COMMIT, Mutter co the tu
       commit khi Chromium reset Wayland text-input, trong khi bridge cung
       CommitText o dau cach -- xterm.js nhan cung mot tu hai lan.

       Gui ca empty update voi mode CLEAR, khong chi HidePreeditText: nhu vay
       cache mode o IBus/Mutter duoc ha xuong CLEAR truoc moi reset. */
    ibus_engine_update_preedit_text_with_mode(IBUS_ENGINE(self), text, len,
                                              visible,
                                              IBUS_ENGINE_PREEDIT_CLEAR);
    self->preedit_visible = visible;
}

static const UkBridgeVTable BridgeVTable = {
    on_commit,
    on_preedit_changed,
    on_erase_before_cursor
};

/*--------------------------------------------------------------------
  Trang thai bat/tat dung chung voi cac module GTK/Qt (~/.unikey/state)
 --------------------------------------------------------------------*/
static gboolean on_state_changed(gint fd, GIOCondition cond, gpointer data)
{
    UkIBusEngine *self = (UkIBusEngine *)data;
    (void)fd; (void)cond;
    uk_bridge_state_dispatch(self->bridge);
    return G_SOURCE_CONTINUE;
}

/*--------------------------------------------------------------------
  Xu ly phim
 --------------------------------------------------------------------*/
static gboolean uk_ibus_engine_process_key_event(IBusEngine *engine,
                                                 guint keyval,
                                                 guint keycode,
                                                 guint modifiers)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;
    UkKeyInfo ki;
    guint32 uch;

    (void)keycode;

    /* O mat khau/PIN, hoac fallback TerminalMode=Off: cho phim di thang. */
    if (self->mode == UK_IBUS_MODE_OFF)
        return FALSE;

    /* IBus gui ca su kien nha phim, danh dau bang IBUS_RELEASE_MASK. */
    ki.keysym   = keyval;
    ki.ctrl     = (modifiers & IBUS_CONTROL_MASK) ? 1 : 0;
    ki.shift    = (modifiers & IBUS_SHIFT_MASK) ? 1 : 0;
    ki.alt      = (modifiers & IBUS_MOD1_MASK) ? 1 : 0;
    ki.super    = (modifiers & IBUS_SUPER_MASK) ? 1 : 0;
    ki.is_press = (modifiers & IBUS_RELEASE_MASK) ? 0 : 1;

    if (uk_bridge_apply_action(self->bridge, uk_keys_shortcut(&ki)))
        return TRUE;

    if (!ki.is_press)
        return FALSE;

    if (uk_keys_is_modifier(keyval))
        return FALSE;

    if (modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK)) {
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    if (keyval == IBUS_KEY_BackSpace)
        return uk_bridge_backspace(self->bridge) == UK_BRIDGE_CONSUMED;

    if (uk_keys_is_editing(keyval)) {
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    if (uk_keys_is_keypad_digit(keyval)) {
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    uch = ibus_keyval_to_unicode(keyval);
    if (uch == 0)
        return FALSE;

    return uk_bridge_key(self->bridge, uch,
                         (modifiers & IBUS_SHIFT_MASK) ? 1 : 0,
                         (modifiers & IBUS_LOCK_MASK) ? 1 : 0) == UK_BRIDGE_CONSUMED;
}

/*--------------------------------------------------------------------
  Policy IBus.

  Capability SURROUNDING_TEXT khong du de chon DIRECT: VTE co capability gia
  trong luc khoi tao, con VS Code/xterm.js co textarea surrounding that nhung
  no khong phai editable buffer -- delete/commit duoc bien thanh byte cua PTY.
  Vi vay IBus dung PREEDIT trong suot cho moi context khong bi mat, tru terminal
  khai purpose dung thi van ton trong TerminalMode.
 --------------------------------------------------------------------*/
static const char *uk_ibus_mode_name(UkIBusMode mode)
{
    switch (mode) {
    case UK_IBUS_MODE_OFF:     return "OFF";
    case UK_IBUS_MODE_PREEDIT: return "PREEDIT";
    case UK_IBUS_MODE_DIRECT:  return "DIRECT";
    }
    return "?";
}

static void uk_ibus_engine_update_mode(UkIBusEngine *self, const char *reason)
{
    gboolean is_secret;
    gboolean is_terminal;
    gboolean can_surround;
    gboolean fallback_preedit;
    UkIBusMode old_mode;
    UkIBusMode new_mode;

    is_secret = (self->purpose == IBUS_INPUT_PURPOSE_PASSWORD ||
                 self->purpose == IBUS_INPUT_PURPOSE_PIN);
#if IBUS_CHECK_VERSION(1, 5, 34)
    if ((self->hints & IBUS_INPUT_HINT_HIDDEN_TEXT) != 0)
        is_secret = TRUE;
#endif
    is_terminal = self->purpose == IBUS_INPUT_PURPOSE_TERMINAL;
    can_surround = (self->capabilities & IBUS_CAP_SURROUNDING_TEXT) != 0;
    fallback_preedit =
        uk_bridge_get_terminal_mode(self->bridge) == UkTerminalPreedit;

    old_mode = self->mode;
    new_mode = uk_ibus_policy_choose(is_secret, is_terminal,
                                     self->capabilities_known, can_surround,
                                     fallback_preedit);
    self->mode = new_mode;

    if (self->focused && (!self->mode_applied || old_mode != new_mode)) {
        if (new_mode == UK_IBUS_MODE_OFF) {
            uk_bridge_reset(self->bridge);
            uk_bridge_set_direct_mode(self->bridge, 0);
        } else {
            uk_bridge_set_direct_mode(self->bridge,
                                      new_mode == UK_IBUS_MODE_DIRECT);
        }
        self->mode_applied = TRUE;
    }

    uk_log("engine=%p reason=%s focus=%s caps=%s0x%x purpose=%u hints=0x%x "
           "surrounding=%s -> %s%s",
           (void *)self, reason, self->focused ? "yes" : "no",
           self->capabilities_known ? "" : "?", self->capabilities,
           self->purpose, self->hints, can_surround ? "yes" : "no",
           uk_ibus_mode_name(new_mode), self->focused ? "" : " (deferred)");
}

static void uk_ibus_engine_focus_in(IBusEngine *engine)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;

    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->focus_in(engine);
    /* Khong mang purpose/capability cua focus truoc sang context moi. */
    self->capabilities = 0;
    self->purpose = IBUS_INPUT_PURPOSE_FREE_FORM;
    self->hints = IBUS_INPUT_HINT_NONE;
    self->capabilities_known = FALSE;
    self->focused = TRUE;
    self->mode_applied = FALSE;
    uk_bridge_reset(self->bridge);
    uk_ibus_engine_update_mode(self, "focus-in");
}

static void uk_ibus_engine_focus_out(IBusEngine *engine)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;

    if (self->focused)
        uk_bridge_flush(self->bridge);
    self->preedit_visible = FALSE;
    self->focused = FALSE;
    self->mode_applied = FALSE;
    uk_log("engine=%p focus-out", (void *)self);
    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->focus_out(engine);
}

/* Reset la huy composition (click/doi caret/app cancel), khong phai focus-out.
   Commit o day se chen partial word ngoai y muon, co the tai caret moi. */
static void uk_ibus_engine_reset(IBusEngine *engine)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;

    if (self->focused)
        uk_bridge_reset(self->bridge);
    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->reset(engine);
}

static void uk_ibus_engine_set_capabilities(IBusEngine *engine, guint caps)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;

    self->capabilities = caps;
    self->capabilities_known = TRUE;
    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->set_capabilities(engine, caps);
    uk_ibus_engine_update_mode(self, "capabilities");
}

static void uk_ibus_engine_set_content_type(IBusEngine *engine,
                                            guint purpose, guint hints)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;

    self->purpose = purpose;
    self->hints = hints;
    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->set_content_type(engine,
                                                                    purpose, hints);
    uk_ibus_engine_update_mode(self, "content-type");
}

/* Yeu cau client cong bo surrounding text de log/chan doan dung capability
   that. Policy khong dung rieng bit nay de bat DIRECT: textarea an cua xterm.js
   co bit do nhung khong dai dien cho buffer cua shell. */
static void uk_ibus_engine_enable(IBusEngine *engine)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;

    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->enable(engine);
    ibus_engine_get_surrounding_text(engine, NULL, NULL, NULL);
    uk_ibus_engine_update_mode(self, "enable");
}

static void uk_ibus_engine_disable(IBusEngine *engine)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;

    if (self->focused)
        uk_bridge_flush(self->bridge);
    self->preedit_visible = FALSE;
    self->focused = FALSE;
    self->mode_applied = FALSE;
    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->disable(engine);
}

/*--------------------------------------------------------------------*/
static void uk_ibus_engine_init(UkIBusEngine *self)
{
    int fd;

    self->preedit_visible = FALSE;
    self->state_source = 0;
    self->capabilities = 0;
    self->purpose = IBUS_INPUT_PURPOSE_FREE_FORM;
    self->hints = IBUS_INPUT_HINT_NONE;
    self->capabilities_known = FALSE;
    self->focused = FALSE;
    self->mode_applied = FALSE;
    self->mode = UK_IBUS_MODE_PREEDIT;
    self->bridge = uk_bridge_new(&BridgeVTable, self);

    /* Mutter gui CommitText kem lenh ket thuc preedit trong cung transaction.
       Commit truoc roi moi phat empty update tranh xterm.js finalize preedit cu
       thanh du lieu PTY truoc khi no nhan CommitText. */
    uk_bridge_set_commit_before_preedit_clear(self->bridge, 1);

    /* Khi la ibus engine thi nguoi dung bat/tat bang Super-Space cua GNOME,
       nen engine luon o trang thai "dang go tieng Viet" khi duoc chon. */
    uk_bridge_set_enabled(self->bridge, 1);

    fd = uk_bridge_state_fd(self->bridge);
    if (fd >= 0)
        self->state_source = g_unix_fd_add(fd, G_IO_IN, on_state_changed, self);
}

static void uk_ibus_engine_destroy(IBusObject *object)
{
    UkIBusEngine *self = (UkIBusEngine *)object;

    if (self->state_source) {
        g_source_remove(self->state_source);
        self->state_source = 0;
    }
    if (self->bridge) {
        uk_bridge_free(self->bridge);
        self->bridge = NULL;
    }
    IBUS_OBJECT_CLASS(uk_ibus_engine_parent_class)->destroy(object);
}

static void uk_ibus_engine_class_init(UkIBusEngineClass *klass)
{
    IBusEngineClass *engine = IBUS_ENGINE_CLASS(klass);
    IBusObjectClass *object = IBUS_OBJECT_CLASS(klass);

    object->destroy = uk_ibus_engine_destroy;

    engine->process_key_event = uk_ibus_engine_process_key_event;
    engine->focus_in          = uk_ibus_engine_focus_in;
    engine->focus_out         = uk_ibus_engine_focus_out;
    engine->reset             = uk_ibus_engine_reset;
    engine->enable            = uk_ibus_engine_enable;
    engine->disable           = uk_ibus_engine_disable;
    engine->set_capabilities  = uk_ibus_engine_set_capabilities;
    engine->set_content_type  = uk_ibus_engine_set_content_type;
}

/*--------------------------------------------------------------------
  main: ket noi toi ibus-daemon va dang ky engine
 --------------------------------------------------------------------*/
static IBusBus *Bus = NULL;

static void on_disconnected(IBusBus *bus, gpointer data)
{
    (void)bus; (void)data;
    ibus_quit();
}

int main(int argc, char **argv)
{
    IBusFactory *factory;
    gboolean by_ibus = FALSE;
    int i;

    /* ibus-daemon goi chuong trinh nay voi --ibus. Chay tay (khong co co do)
       thi tu dang ky component, tien cho viec thu nghiem. */
    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "--ibus") == 0 || strcmp(argv[i], "-i") == 0)
            by_ibus = TRUE;

    ibus_init();

    Bus = ibus_bus_new();
    if (!ibus_bus_is_connected(Bus)) {
        g_printerr("Khong ket noi duoc toi ibus-daemon.\n");
        return 1;
    }
    g_signal_connect(Bus, "disconnected", G_CALLBACK(on_disconnected), NULL);

    factory = ibus_factory_new(ibus_bus_get_connection(Bus));
    ibus_factory_add_engine(factory, "unikey", UK_TYPE_IBUS_ENGINE);

    if (by_ibus) {
        ibus_bus_request_name(Bus, "org.freedesktop.IBus.Unikey", 0);
    } else {
        IBusComponent *component =
            ibus_component_new("org.freedesktop.IBus.Unikey",
                               "Unikey", VERSION, "GPL",
                               "UniKey team <unikey@gmail.com>",
                               "https://unikey.org",
                               "", "x-unikey");
        ibus_component_add_engine(
            component,
            ibus_engine_desc_new("unikey",
                                 "Vietnamese (UniKey)",
                                 "Bo go tieng Viet UniKey",
                                 "vi", "GPL",
                                 "UniKey team <unikey@gmail.com>",
                                 "", "us"));
        ibus_bus_register_component(Bus, component);
    }

    ibus_main();
    return 0;
}
