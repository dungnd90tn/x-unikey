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
    IBusPropList *properties;
    IBusProperty *prop_telex;
    IBusProperty *prop_vni;
    IBusProperty *prop_viqr;
    guint      state_source;
    guint      capabilities;
    guint      purpose;
    guint      hints;
    gboolean   preedit_visible;
    gboolean   capabilities_known;
    gboolean   saw_url;
    UkFirefoxEntryState firefox_entry;
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
    uk_log("engine=%p direct-commit chars=%ld", (void *)self,
           (long)g_utf8_strlen(utf8, -1));
    ibus_engine_commit_text(IBUS_ENGINE(self), text);
}

/* Ket thuc mot PREEDIT ma khong de client thay trang thai trung gian.

   Thu tu C -> empty truoc day de preedit cu con ton tai khi CommitText den;
   Firefox/Electron co luc dung composition range cu de replace va tao chu lap
   nhu "dangắng". Thu tu empty -> C lai lam xterm.js tu finalize preedit, roi
   nhan them CommitText. Cach ma cac IBus engine chuan (vi du Mozc) dung la
   HidePreeditText -> CommitText: ket thuc presentation cua composition truoc,
   nhung khong phat mot empty-preedit de client tu commit no. */
static void on_commit_preedit(void *user_data, const char *utf8)
{
    UkIBusEngine *self = (UkIBusEngine *)user_data;
    IBusText *text;

    if (self->preedit_visible)
        ibus_engine_hide_preedit_text(IBUS_ENGINE(self));
    self->preedit_visible = FALSE;

    uk_log("engine=%p preedit-commit chars=%ld", (void *)self,
           (long)g_utf8_strlen(utf8, -1));
    text = ibus_text_new_from_string(utf8);
    ibus_engine_commit_text(IBUS_ENGINE(self), text);
}

/* Xoa n ky tu truoc con tro. IBus policy khong tu bat DIRECT tu capability:
 * callback nay chi con la primitive cho frontend/client duoc tin cay. */
static void on_erase_before_cursor(void *user_data, int nchars)
{
    UkIBusEngine *self = (UkIBusEngine *)user_data;
    /* offset am = lui ve truoc con tro */
    uk_log("engine=%p direct-delete chars=%d", (void *)self, nchars);
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

       Khi dang go, moi update mang mode CLEAR de focus/reset khong tu commit.
       Khi chot tu, bridge goi on_commit_preedit() rieng de phat
       HidePreeditText -> CommitText, khong gui empty update trung gian. Empty
       o day chi con xuat hien khi reset/huy composition that su. */
    ibus_engine_update_preedit_text_with_mode(IBUS_ENGINE(self), text, len,
                                              visible,
                                              IBUS_ENGINE_PREEDIT_CLEAR);
    self->preedit_visible = visible;
}

static const UkBridgeVTable BridgeVTable = {
    on_commit,
    on_preedit_changed,
    on_erase_before_cursor,
    on_commit_preedit
};

static void uk_ibus_engine_update_mode(UkIBusEngine *self,
                                        const char *reason);
static void uk_ibus_engine_sync_properties(UkIBusEngine *self,
                                           gboolean publish);

/*--------------------------------------------------------------------
  Trang thai bat/tat dung chung voi cac module GTK/Qt (~/.unikey/state)
 --------------------------------------------------------------------*/
static gboolean on_state_changed(gint fd, GIOCondition cond, gpointer data)
{
    UkIBusEngine *self = (UkIBusEngine *)data;
    (void)fd; (void)cond;
    if (uk_bridge_state_dispatch(self->bridge)) {
        uk_ibus_engine_update_mode(self, "config-state");
        uk_ibus_engine_sync_properties(self, self->focused);
    }
    return G_SOURCE_CONTINUE;
}

/*--------------------------------------------------------------------
  Menu thuoc tinh IBus.

  Day khong phai cua so trang thai nhu XIM cu. Panel chi hien menu khi engine
  dang duoc chon; cua so setup chi khoi dong khi nguoi dung bam "Cai dat...".
 --------------------------------------------------------------------*/
static IBusProperty *uk_ibus_property_new(const char *key,
                                          IBusPropType type,
                                          const char *label,
                                          const char *tooltip,
                                          IBusPropState state,
                                          IBusPropList *children)
{
    IBusText *label_text = ibus_text_new_from_string(label);
    IBusText *tooltip_text = ibus_text_new_from_string(tooltip);
    IBusProperty *prop;

    g_object_ref_sink(label_text);
    g_object_ref_sink(tooltip_text);
    prop = ibus_property_new(key, type, label_text, NULL, tooltip_text,
                             TRUE, TRUE, state, children);
    g_object_ref_sink(prop);
    g_object_unref(label_text);
    g_object_unref(tooltip_text);
    return prop;
}

static void uk_ibus_engine_build_properties(UkIBusEngine *self)
{
    IBusPropList *methods = ibus_prop_list_new();
    IBusProperty *menu;
#ifdef HAVE_UNIKEY_SETUP
    IBusProperty *setup;
#endif

    g_object_ref_sink(methods);
    self->properties = ibus_prop_list_new();
    g_object_ref_sink(self->properties);

    self->prop_telex = uk_ibus_property_new(
        "InputMethod.Telex", PROP_TYPE_RADIO, "Telex",
        "Chon kieu go Telex", PROP_STATE_UNCHECKED, NULL);
    self->prop_vni = uk_ibus_property_new(
        "InputMethod.Vni", PROP_TYPE_RADIO, "VNI",
        "Chon kieu go VNI", PROP_STATE_UNCHECKED, NULL);
    self->prop_viqr = uk_ibus_property_new(
        "InputMethod.Viqr", PROP_TYPE_RADIO, "VIQR",
        "Chon kieu go VIQR", PROP_STATE_UNCHECKED, NULL);
    ibus_prop_list_append(methods, self->prop_telex);
    ibus_prop_list_append(methods, self->prop_vni);
    ibus_prop_list_append(methods, self->prop_viqr);

    menu = uk_ibus_property_new(
        "InputMethod", PROP_TYPE_MENU, "Kiểu gõ",
        "Chọn kiểu gõ tiếng Việt", PROP_STATE_UNCHECKED, methods);
    ibus_prop_list_append(self->properties, menu);
    g_object_unref(menu);
    g_object_unref(methods);

#ifdef HAVE_UNIKEY_SETUP
    setup = uk_ibus_property_new(
        "Setup", PROP_TYPE_NORMAL, "Cài đặt…",
        "Mở cấu hình x-unikey", PROP_STATE_UNCHECKED, NULL);
    ibus_property_set_icon(setup, "preferences-system");
    ibus_prop_list_append(self->properties, setup);
    g_object_unref(setup);
#endif

    uk_ibus_engine_sync_properties(self, FALSE);
}

static void uk_ibus_engine_sync_properties(UkIBusEngine *self,
                                           gboolean publish)
{
    int method;

    if (!self->properties)
        return;
    method = uk_bridge_get_input_method(self->bridge);
    ibus_property_set_state(self->prop_telex,
        method == UkTelex ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED);
    ibus_property_set_state(self->prop_vni,
        method == UkVni ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED);
    ibus_property_set_state(self->prop_viqr,
        method == UkViqr ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED);

    if (publish) {
        ibus_engine_update_property(IBUS_ENGINE(self), self->prop_telex);
        ibus_engine_update_property(IBUS_ENGINE(self), self->prop_vni);
        ibus_engine_update_property(IBUS_ENGINE(self), self->prop_viqr);
    }
}

#ifdef HAVE_UNIKEY_SETUP
static void uk_ibus_engine_open_setup(UkIBusEngine *self)
{
    gchar *engine_path;
    gchar *engine_dir;
    gchar *parent_dir;
    gchar *setup_path;
    gchar *argv[2];
    GError *error = NULL;

    (void)self;
    engine_path = g_file_read_link("/proc/self/exe", &error);
    if (!engine_path) {
        uk_log("khong tim duoc executable de mo setup: %s",
               error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }
    engine_dir = g_path_get_dirname(engine_path);
    setup_path = g_build_filename(engine_dir, "ibus-setup-unikey", NULL);
    /* Ban hien tai dat hai binary cung thu muc. Fallback thu thu muc cha de
       van mo duoc setup cua cac goi/ban cai thu cong theo layout cu. */
    if (!g_file_test(setup_path, G_FILE_TEST_IS_EXECUTABLE)) {
        g_free(setup_path);
        parent_dir = g_path_get_dirname(engine_dir);
        setup_path = g_build_filename(parent_dir, "ibus-setup-unikey", NULL);
        g_free(parent_dir);
    }
    argv[0] = setup_path;
    argv[1] = NULL;
    if (!g_spawn_async(NULL, argv, NULL, 0, NULL, NULL, NULL, &error)) {
        uk_log("khong mo duoc %s: %s", setup_path,
               error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    g_free(setup_path);
    g_free(engine_dir);
    g_free(engine_path);
}
#endif

static void uk_ibus_engine_property_activate(IBusEngine *engine,
                                             const gchar *prop_name,
                                             guint prop_state)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;

    (void)prop_state;
    if (strcmp(prop_name, "InputMethod.Telex") == 0)
        uk_bridge_set_input_method(self->bridge, UkTelex);
    else if (strcmp(prop_name, "InputMethod.Vni") == 0)
        uk_bridge_set_input_method(self->bridge, UkVni);
    else if (strcmp(prop_name, "InputMethod.Viqr") == 0)
        uk_bridge_set_input_method(self->bridge, UkViqr);
#ifdef HAVE_UNIKEY_SETUP
    else if (strcmp(prop_name, "Setup") == 0) {
        uk_ibus_engine_open_setup(self);
        return;
    }
#endif
    else {
        IBusEngineClass *parent = IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class);
        if (parent->property_activate)
            parent->property_activate(engine, prop_name, prop_state);
        return;
    }
    uk_ibus_engine_sync_properties(self, TRUE);
}

/*--------------------------------------------------------------------
  Xu ly phim
 --------------------------------------------------------------------*/
static void uk_ibus_engine_note_context_input(UkIBusEngine *self)
{
    UkFirefoxEntryState old_state = self->firefox_entry;

    self->firefox_entry = uk_ibus_policy_note_input(
        self->firefox_entry, self->mode == UK_IBUS_MODE_DIRECT);
    if (old_state != self->firefox_entry) {
        uk_log("engine=%p firefox-entry=rejected reason=input-before-direct",
               (void *)self);
        /* CONFIRMED nhung chua co surrounding dang o OFF. Khi phim dau tien
           den, reject candidate va ap PREEDIT TRUOC khi xu ly phim; khong de
           ky tu raw lot vao app roi moi chuyen DIRECT sau do. */
        uk_ibus_engine_update_mode(self, "input-before-direct");
    }
}

static gboolean uk_ibus_engine_process_key_event(IBusEngine *engine,
                                                 guint keyval,
                                                 guint keycode,
                                                 guint modifiers)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;
    UkKeyInfo ki;
    guint32 uch;

    (void)keycode;

    /* Neu candidate Firefox da confirm metadata nhung surrounding chua toi,
       mode tam la OFF. Phim nhap dau tien phai huy candidate va quay ve
       PREEDIT; password/terminal OFF that su van cho phim di thang. */
    if (self->mode == UK_IBUS_MODE_OFF &&
        !(modifiers & IBUS_RELEASE_MASK) &&
        !(modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK)) &&
        !uk_keys_is_modifier(keyval) &&
        (keyval == IBUS_KEY_BackSpace || uk_keys_is_editing(keyval) ||
         uk_keys_is_keypad_digit(keyval) || ibus_keyval_to_unicode(keyval) != 0))
        uk_ibus_engine_note_context_input(self);
    if (self->mode == UK_IBUS_MODE_OFF)
        return FALSE;

    /* IBus gui ca su kien nha phim, danh dau bang IBUS_RELEASE_MASK. */
    ki.keysym   = keyval;
    ki.ctrl     = (modifiers & IBUS_CONTROL_MASK) ? 1 : 0;
    ki.shift    = (modifiers & IBUS_SHIFT_MASK) ? 1 : 0;
    ki.alt      = (modifiers & IBUS_MOD1_MASK) ? 1 : 0;
    ki.super    = (modifiers & IBUS_SUPER_MASK) ? 1 : 0;
    ki.is_press = (modifiers & IBUS_RELEASE_MASK) ? 0 : 1;

    if (uk_bridge_apply_action(self->bridge, uk_keys_shortcut(&ki))) {
        /* Shortcut doi kieu go cap nhat bridge truoc khi inotify cua chinh
           tien trinh nay quay lai, nen dispatch co the thay "khong doi".
           Publish radio menu ngay tai nguon de panel khong hien stale. */
        uk_ibus_engine_sync_properties(self, self->focused);
        return TRUE;
    }

    if (!ki.is_press)
        return FALSE;

    if (uk_keys_is_modifier(keyval))
        return FALSE;

    if (modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK)) {
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    if (keyval == IBUS_KEY_BackSpace) {
        uk_ibus_engine_note_context_input(self);
        return uk_bridge_backspace(self->bridge) == UK_BRIDGE_CONSUMED;
    }

    if (uk_keys_is_editing(keyval)) {
        uk_ibus_engine_note_context_input(self);
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    if (uk_keys_is_keypad_digit(keyval)) {
        uk_ibus_engine_note_context_input(self);
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    uch = ibus_keyval_to_unicode(keyval);
    if (uch == 0)
        return FALSE;

    uk_ibus_engine_note_context_input(self);

    return uk_bridge_key(self->bridge, uch,
                         (modifiers & IBUS_SHIFT_MASK) ? 1 : 0,
                         (modifiers & IBUS_LOCK_MASK) ? 1 : 0) == UK_BRIDGE_CONSUMED;
}

/*--------------------------------------------------------------------
  Policy IBus.

  Capability SURROUNDING_TEXT khong du de chon DIRECT: VTE co capability gia
  trong luc khoi tao, con VS Code/xterm.js co textarea surrounding that nhung
  no khong phai editable buffer -- delete/commit duoc bien thanh byte cua PTY.
  Vi vay IBus dung PREEDIT trong suot cho van ban tu do. Purpose URL duoc vao
  DIRECT sau handshake. Firefox/GTK lai gui thanh dia chi mozAwesomebar thanh
  FREE_FORM; chi chuoi hint sentence-only -> none moi confirm context do, vi
  input web cung co UPPERCASE_SENTENCES. Terminal khai purpose dung van ton
  trong TerminalMode truoc moi rule thanh dia chi.
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

static const char *uk_firefox_entry_name(UkFirefoxEntryState state)
{
    switch (state) {
    case UK_FIREFOX_ENTRY_NONE:          return "none";
    case UK_FIREFOX_ENTRY_SENTENCE_ONLY: return "pending";
    case UK_FIREFOX_ENTRY_CONFIRMED:     return "confirmed";
    case UK_FIREFOX_ENTRY_REJECTED:      return "rejected";
    }
    return "?";
}

static void uk_ibus_engine_update_mode(UkIBusEngine *self, const char *reason)
{
    gboolean is_secret;
    gboolean is_terminal;
    gboolean is_direct_entry;
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
    is_direct_entry = self->saw_url ||
        self->firefox_entry == UK_FIREFOX_ENTRY_CONFIRMED;
    can_surround = (self->capabilities & IBUS_CAP_SURROUNDING_TEXT) != 0;
    fallback_preedit =
        uk_bridge_get_terminal_mode(self->bridge) == UkTerminalPreedit;

    old_mode = self->mode;
    new_mode = uk_ibus_policy_choose(is_secret, is_terminal, is_direct_entry,
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
           "url=%s firefox-entry=%s surrounding=%s -> %s%s",
           (void *)self, reason, self->focused ? "yes" : "no",
           self->capabilities_known ? "" : "?", self->capabilities,
           self->purpose, self->hints, self->saw_url ? "yes" : "no",
           uk_firefox_entry_name(self->firefox_entry),
           can_surround ? "yes" : "no",
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
    self->saw_url = FALSE;
    self->firefox_entry = UK_FIREFOX_ENTRY_NONE;
    self->focused = TRUE;
    self->mode_applied = FALSE;
    uk_bridge_reset(self->bridge);
    uk_ibus_engine_update_mode(self, "focus-in");
    ibus_engine_register_properties(engine, self->properties);
    uk_ibus_engine_sync_properties(self, TRUE);
}

static void uk_ibus_engine_focus_out(IBusEngine *engine)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;

    if (self->focused)
        uk_bridge_flush(self->bridge);
    self->preedit_visible = FALSE;
    self->saw_url = FALSE;
    self->firefox_entry = UK_FIREFOX_ENTRY_NONE;
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
    self->saw_url = uk_ibus_policy_update_url_latch(
        self->saw_url,
        purpose == IBUS_INPUT_PURPOSE_URL,
        purpose == IBUS_INPUT_PURPOSE_FREE_FORM);
    self->firefox_entry = uk_ibus_policy_update_firefox_entry(
        self->firefox_entry,
        purpose == IBUS_INPUT_PURPOSE_FREE_FORM,
        hints == IBUS_INPUT_HINT_UPPERCASE_SENTENCES,
        hints == IBUS_INPUT_HINT_NONE);
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
    self->saw_url = FALSE;
    self->firefox_entry = UK_FIREFOX_ENTRY_NONE;
    self->focused = FALSE;
    self->mode_applied = FALSE;
    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->disable(engine);
}

/*--------------------------------------------------------------------*/
static void uk_ibus_engine_init(UkIBusEngine *self)
{
    int fd;

    self->preedit_visible = FALSE;
    self->properties = NULL;
    self->prop_telex = NULL;
    self->prop_vni = NULL;
    self->prop_viqr = NULL;
    self->state_source = 0;
    self->capabilities = 0;
    self->purpose = IBUS_INPUT_PURPOSE_FREE_FORM;
    self->hints = IBUS_INPUT_HINT_NONE;
    self->capabilities_known = FALSE;
    self->saw_url = FALSE;
    self->firefox_entry = UK_FIREFOX_ENTRY_NONE;
    self->focused = FALSE;
    self->mode_applied = FALSE;
    self->mode = UK_IBUS_MODE_PREEDIT;
    self->bridge = uk_bridge_new(&BridgeVTable, self);
    uk_ibus_engine_build_properties(self);

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
    if (self->properties) {
        g_object_unref(self->prop_telex);
        g_object_unref(self->prop_vni);
        g_object_unref(self->prop_viqr);
        g_object_unref(self->properties);
        self->properties = NULL;
        self->prop_telex = NULL;
        self->prop_vni = NULL;
        self->prop_viqr = NULL;
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
    engine->property_activate = uk_ibus_engine_property_activate;
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
