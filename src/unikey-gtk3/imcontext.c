// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey GTK3 input method module
 *
 * Khac han module GTK2 cu (src/unikey-gtk/): khong con gdk_event_put() de gia lap
 * phim BackSpace. Am tiet dang go nam trong preedit, nen module chay duoc tren
 * ca X11 lan Wayland va khong pha undo cua ung dung.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib-unix.h>
#include <string.h>

#include "imcontext.h"
#include "ukbridge.h"
#include "ukkeys.h"
#include "keycons.h"

struct _UkImContext {
    GtkIMContext  parent;
    UkBridge     *bridge;
    gboolean      preedit_visible;
    gboolean      has_focus;
    guint         state_source;
};

struct _UkImContextClass {
    GtkIMContextClass parent_class;
};

static GType         UkImContextType = 0;
static GObjectClass *ParentClass = NULL;

/*--------------------------------------------------------------------
  Callback tu bridge
 --------------------------------------------------------------------*/
static void on_commit(void *user_data, const char *utf8)
{
    g_signal_emit_by_name(G_OBJECT(user_data), "commit", utf8);
}

static void on_preedit_changed(void *user_data, const char *utf8)
{
    UkImContext *self = UK_IM_CONTEXT(user_data);
    gboolean now = (utf8 && *utf8);

    if (now && !self->preedit_visible) {
        self->preedit_visible = TRUE;
        g_signal_emit_by_name(self, "preedit-start");
    }

    g_signal_emit_by_name(self, "preedit-changed");

    if (!now && self->preedit_visible) {
        self->preedit_visible = FALSE;
        g_signal_emit_by_name(self, "preedit-end");
    }
}

static const UkBridgeVTable BridgeVTable = {
    on_commit,
    on_preedit_changed
};

/*--------------------------------------------------------------------*/
static gboolean uk_im_context_filter_keypress(GtkIMContext *context, GdkEventKey *ev)
{
    UkImContext *self = UK_IM_CONTEXT(context);
    UkKeyInfo ki;
    guint32 uch;

    /* Bang phim tat nam trong ukbridge (ukkeys.c) de GTK3/GTK4/Qt dung chung. */
    ki.keysym   = ev->keyval;
    ki.ctrl     = (ev->state & GDK_CONTROL_MASK) ? 1 : 0;
    ki.shift    = (ev->state & GDK_SHIFT_MASK) ? 1 : 0;
    ki.alt      = (ev->state & GDK_MOD1_MASK) ? 1 : 0;
    ki.super    = (ev->state & GDK_SUPER_MASK) ? 1 : 0;
    ki.is_press = (ev->type == GDK_KEY_PRESS) ? 1 : 0;

    if (uk_bridge_apply_action(self->bridge, uk_keys_shortcut(&ki)))
        return TRUE;

    if (ev->type != GDK_KEY_PRESS)
        return FALSE;

    if (uk_keys_is_modifier(ev->keyval))
        return FALSE;

    /* Ctrl/Alt/Super + phim -> to hop lenh cua ung dung, khong phai van ban */
    if (ev->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK)) {
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    if (ev->keyval == GDK_KEY_BackSpace)
        return uk_bridge_backspace(self->bridge) == UK_BRIDGE_CONSUMED;

    if (uk_keys_is_editing(ev->keyval)) {
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    uch = gdk_keyval_to_unicode(ev->keyval);
    if (uch == 0)
        return FALSE;

    /* Ban phim so khong duoc dien giai thanh dau thanh kieu VNI */
    if (uk_keys_is_keypad_digit(ev->keyval)) {
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    return uk_bridge_key(self->bridge, uch,
                         (ev->state & GDK_SHIFT_MASK) ? 1 : 0,
                         (ev->state & GDK_LOCK_MASK) ? 1 : 0) == UK_BRIDGE_CONSUMED;
}

/*--------------------------------------------------------------------*/
static void uk_im_context_get_preedit_string(GtkIMContext  *context,
                                             gchar        **str,
                                             PangoAttrList **attrs,
                                             gint          *cursor_pos)
{
    UkImContext *self = UK_IM_CONTEXT(context);
    const char *p = uk_bridge_preedit(self->bridge);

    if (str)
        *str = g_strdup(p);

    if (attrs) {
        *attrs = pango_attr_list_new();
        if (*p) {
            PangoAttribute *a = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
            a->start_index = 0;
            a->end_index = strlen(p);
            pango_attr_list_insert(*attrs, a);
        }
    }

    if (cursor_pos)
        *cursor_pos = g_utf8_strlen(p, -1);
}

static void uk_im_context_reset(GtkIMContext *context)
{
    uk_bridge_flush(UK_IM_CONTEXT(context)->bridge);
}

static void uk_im_context_focus_in(GtkIMContext *context)
{
    UkImContext *self = UK_IM_CONTEXT(context);
    self->has_focus = TRUE;
    /* Engine la mot instance dung chung ca tien trinh: doi o nhap thi phai
       xoa trang thai cu, neu khong am tiet cua o truoc se ro ri sang. */
    uk_bridge_reset(self->bridge);
}

static void uk_im_context_focus_out(GtkIMContext *context)
{
    UkImContext *self = UK_IM_CONTEXT(context);
    self->has_focus = FALSE;
    uk_bridge_flush(self->bridge);
}

/*--------------------------------------------------------------------*/
/* Trang thai bat/tat duoc chia se giua cac tien trinh qua ~/.unikey/state;
   gan fd inotify vao vong lap GLib de app nay biet app kia vua doi trang thai. */
static gboolean on_state_changed(gint fd, GIOCondition cond, gpointer data)
{
    UkImContext *self = UK_IM_CONTEXT(data);
    (void)fd; (void)cond;
    uk_bridge_state_dispatch(self->bridge);
    return G_SOURCE_CONTINUE;
}

static void uk_im_context_init(UkImContext *self)
{
    int fd;

    self->preedit_visible = FALSE;
    self->has_focus = FALSE;
    self->state_source = 0;
    self->bridge = uk_bridge_new(&BridgeVTable, self);

    fd = uk_bridge_state_fd(self->bridge);
    if (fd >= 0)
        self->state_source = g_unix_fd_add(fd, G_IO_IN, on_state_changed, self);
}

static void uk_im_context_finalize(GObject *obj)
{
    UkImContext *self = UK_IM_CONTEXT(obj);
    if (self->state_source) {
        g_source_remove(self->state_source);
        self->state_source = 0;
    }
    uk_bridge_free(self->bridge);
    self->bridge = NULL;
    ParentClass->finalize(obj);
}

static void uk_im_context_class_init(UkImContextClass *klass)
{
    GtkIMContextClass *im = GTK_IM_CONTEXT_CLASS(klass);
    GObjectClass *obj = G_OBJECT_CLASS(klass);

    ParentClass = g_type_class_peek_parent(klass);

    im->filter_keypress    = uk_im_context_filter_keypress;
    im->get_preedit_string = uk_im_context_get_preedit_string;
    im->reset              = uk_im_context_reset;
    im->focus_in           = uk_im_context_focus_in;
    im->focus_out          = uk_im_context_focus_out;

    obj->finalize = uk_im_context_finalize;
}

static void uk_im_context_class_finalize(UkImContextClass *klass)
{
    (void)klass;
}

/*--------------------------------------------------------------------*/
GType uk_im_context_get_type(void)
{
    return UkImContextType;
}

void uk_im_context_register_type(GTypeModule *module)
{
    static const GTypeInfo info = {
        sizeof(UkImContextClass),
        NULL, NULL,
        (GClassInitFunc)uk_im_context_class_init,
        (GClassFinalizeFunc)uk_im_context_class_finalize,
        NULL,
        sizeof(UkImContext),
        0,
        (GInstanceInitFunc)uk_im_context_init,
        NULL
    };

    if (UkImContextType == 0)
        UkImContextType = g_type_module_register_type(module, GTK_TYPE_IM_CONTEXT,
                                                     "UkImContext", &info, 0);
}

GtkIMContext *uk_im_context_new(void)
{
    return GTK_IM_CONTEXT(g_object_new(UK_TYPE_IM_CONTEXT, NULL));
}
