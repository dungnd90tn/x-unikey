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
    gboolean   preedit_visible;
    gboolean   suppressed;   /* o mat khau/PIN */
    gboolean   in_terminal;  /* o nhap khong ho tro surrounding text */
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

/* Xoa n ky tu truoc con tro.
 *
 * Dung delete-surrounding-text. Da thu thay bang forward_key_event(BackSpace)
 * va HONG NANG HON: phim forward di vao hang doi su kien cua toolkit, con
 * commit_text ap thang vao widget -- hai duong khong bao dam thu tu, ket qua
 * la ca trinh duyet cung loan chu.
 *
 * Han che da biet: VTE (gnome-terminal) khai bao ho tro roi lo di lenh xoa.
 * Cho nen terminal duoc TAT HAN o set_content_type ben duoi, thay vi co sua
 * bang mot co che khac.
 */
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

    if (!utf8 || !*utf8) {
        if (self->preedit_visible) {
            ibus_engine_hide_preedit_text(IBUS_ENGINE(self));
            self->preedit_visible = FALSE;
        }
        return;
    }

    text = ibus_text_new_from_string(utf8);
    len = g_utf8_strlen(utf8, -1);
    ibus_text_append_attribute(text, IBUS_ATTR_TYPE_UNDERLINE,
                               IBUS_ATTR_UNDERLINE_SINGLE, 0, (guint)len);

    /* PREEDIT_CLEAR chu khong phai PREEDIT_COMMIT.
       Voi COMMIT, khi nguoi dung dang go do ma bam chuot sang cho khac, ibus
       commit phan dang go SAU KHI con tro da doi -- chu roi vao dung cho vua
       click. Ta tu quyet dinh commit trong focus_out, con reset() thi bo di,
       dung nghia "huy soan thao" ma toolkit mong doi. */
    ibus_engine_update_preedit_text_with_mode(IBUS_ENGINE(self), text, len, TRUE,
                                              IBUS_ENGINE_PREEDIT_CLEAR);
    self->preedit_visible = TRUE;
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

    /* O mat khau, hoac terminal khi TerminalMode=Off: cho phim di thang. */
    if (self->suppressed || self->in_terminal)
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

/*--------------------------------------------------------------------*/
static void uk_ibus_engine_focus_in(IBusEngine *engine)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;
    uk_bridge_reset(self->bridge);
    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->focus_in(engine);
}

static void uk_ibus_engine_focus_out(IBusEngine *engine)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;
    uk_bridge_flush(self->bridge);
    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->focus_out(engine);
}

/* reset = HUY phan dang soan, khong phai commit no.
   Ung dung goi reset khi nguoi dung bam chuot sang cho khac; commit luc do se
   dat chu vao vi tri con tro moi. */
static void uk_ibus_engine_reset(IBusEngine *engine)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;
    uk_bridge_reset(self->bridge);
    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->reset(engine);
}

/* Ung dung bao no lam duoc gi. DAY moi la tin hieu dung de chon che do.
 *
 * Do bang log that (~/.unikey/debug.log) tren Ubuntu 26.04 / GNOME:
 *     caps=0x29  surrounding=yes   <- trinh duyet, o nhap thong thuong
 *     caps=0x9   surrounding=no    <- gnome-terminal (VTE)
 *
 * Terminal khai bao TRUNG THUC rang no khong lam duoc surrounding text. Truoc
 * do toi tuong co nay noi doi nen bo di va di tim co che khac -- sai. Chi can
 * ton trong no:
 *     co surrounding  -> DIRECT  (commit thang, khong gach chan)
 *     khong co        -> PREEDIT (co gach chan, nhung luon dung)
 *
 * Con IBUS_INPUT_PURPOSE_TERMINAL thi vo dung: log cho thay purpose luon bang
 * 0 (FREE_FORM) ke ca trong gnome-terminal.
 */
static void uk_ibus_engine_set_capabilities(IBusEngine *engine, guint caps)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;
    gboolean can_surround = (caps & IBUS_CAP_SURROUNDING_TEXT) != 0;
    const char *mode;

    if (can_surround) {
        /* O nhap binh thuong: commit thang, khong gach chan. */
        self->in_terminal = FALSE;
        uk_bridge_set_direct_mode(self->bridge, 1);
        mode = "DIRECT";
    } else if (uk_bridge_get_terminal_mode(self->bridge) == UkTerminalPreedit) {
        /* Nguoi dung chon van go trong terminal: dung preedit, luon dung. */
        self->in_terminal = FALSE;
        uk_bridge_set_direct_mode(self->bridge, 0);
        mode = "PREEDIT";
    } else {
        /* Mac dinh: tat han. Lenh shell khong can dau tieng Viet. */
        uk_bridge_reset(self->bridge);
        self->in_terminal = TRUE;
        mode = "TAT (TerminalMode=Off)";
    }

    uk_log("caps=0x%x surrounding=%s preedit=%s -> %s", caps,
           can_surround ? "yes" : "no",
           (caps & IBUS_CAP_PREEDIT_TEXT) ? "yes" : "no", mode);

    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->set_capabilities(engine, caps);
}

/* O mat khau/PIN thi tat han bo go.
 *
 * Khong con dung purpose de nhan dien terminal: log that cho thay purpose luon
 * bang 0 ngay ca trong gnome-terminal, nen tin hieu do vo dung. Viec chon che
 * do da chuyen len set_capabilities().
 */
static void uk_ibus_engine_set_content_type(IBusEngine *engine,
                                            guint purpose, guint hints)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;
    gboolean is_secret = (purpose == IBUS_INPUT_PURPOSE_PASSWORD ||
                          purpose == IBUS_INPUT_PURPOSE_PIN);

    uk_log("purpose=%u hints=%u -> %s", purpose, hints,
           is_secret ? "tat han (mat khau)" : "bat");

    if (is_secret != self->suppressed) {
        uk_bridge_reset(self->bridge);
        self->suppressed = is_secret;
    }

    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->set_content_type(engine,
                                                                    purpose, hints);
}

static void uk_ibus_engine_disable(IBusEngine *engine)
{
    UkIBusEngine *self = (UkIBusEngine *)engine;
    uk_bridge_flush(self->bridge);
    IBUS_ENGINE_CLASS(uk_ibus_engine_parent_class)->disable(engine);
}

/*--------------------------------------------------------------------*/
static void uk_ibus_engine_init(UkIBusEngine *self)
{
    int fd;

    self->preedit_visible = FALSE;
    self->state_source = 0;
    self->bridge = uk_bridge_new(&BridgeVTable, self);

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
