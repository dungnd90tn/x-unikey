// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey GTK4 input method module
 *
 * Cung mo hinh preedit nhu module GTK3, chi khac cach lay du lieu su kien:
 * GTK4 khong con GdkEventKey lo cau truc ma dung ham truy van tren GdkEvent.
 * Toan bo logic go tieng Viet va bang phim tat nam trong ukbridge.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
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
    guint         state_source;
};

/* UkImContextClass do G_DECLARE_FINAL_TYPE trong imcontext.h sinh ra. */

G_DEFINE_DYNAMIC_TYPE(UkImContext, uk_im_context, GTK_TYPE_IM_CONTEXT)

/*--------------------------------------------------------------------*/
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

static const UkBridgeVTable BridgeVTable = { on_commit, on_preedit_changed };

/*--------------------------------------------------------------------*/
static gboolean uk_im_context_filter_keypress(GtkIMContext *context, GdkEvent *event)
{
    UkImContext *self = UK_IM_CONTEXT(context);
    GdkModifierType state;
    GdkEventType type;
    UkKeyInfo ki;
    guint keyval;
    guint32 uch;

    type = gdk_event_get_event_type(event);
    if (type != GDK_KEY_PRESS && type != GDK_KEY_RELEASE)
        return FALSE;

    keyval = gdk_key_event_get_keyval(event);
    state  = gdk_event_get_modifier_state(event);

    ki.keysym   = keyval;
    ki.ctrl     = (state & GDK_CONTROL_MASK) ? 1 : 0;
    ki.shift    = (state & GDK_SHIFT_MASK) ? 1 : 0;
    ki.alt      = (state & GDK_ALT_MASK) ? 1 : 0;
    ki.super    = (state & GDK_SUPER_MASK) ? 1 : 0;
    ki.is_press = (type == GDK_KEY_PRESS) ? 1 : 0;

    if (uk_bridge_apply_action(self->bridge, uk_keys_shortcut(&ki)))
        return TRUE;

    if (type != GDK_KEY_PRESS)
        return FALSE;

    if (uk_keys_is_modifier(keyval))
        return FALSE;

    /* Ctrl/Alt/Super + phim = lenh cua ung dung, khong phai van ban */
    if (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) {
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    if (keyval == GDK_KEY_BackSpace)
        return uk_bridge_backspace(self->bridge) == UK_BRIDGE_CONSUMED;

    if (uk_keys_is_editing(keyval)) {
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    uch = gdk_keyval_to_unicode(keyval);
    if (uch == 0)
        return FALSE;

    if (uk_keys_is_keypad_digit(keyval)) {
        uk_bridge_flush(self->bridge);
        return FALSE;
    }

    return uk_bridge_key(self->bridge, uch,
                         (state & GDK_SHIFT_MASK) ? 1 : 0,
                         (state & GDK_LOCK_MASK) ? 1 : 0) == UK_BRIDGE_CONSUMED;
}

/*--------------------------------------------------------------------*/
static void uk_im_context_get_preedit_string(GtkIMContext   *context,
                                             char          **str,
                                             PangoAttrList **attrs,
                                             int            *cursor_pos)
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
    uk_bridge_reset(UK_IM_CONTEXT(context)->bridge);
}

static void uk_im_context_focus_out(GtkIMContext *context)
{
    uk_bridge_flush(UK_IM_CONTEXT(context)->bridge);
}

/*--------------------------------------------------------------------*/
/* Trang thai bat/tat chia se giua cac tien trinh qua ~/.unikey/state. */
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
    G_OBJECT_CLASS(uk_im_context_parent_class)->finalize(obj);
}

static void uk_im_context_class_init(UkImContextClass *klass)
{
    GtkIMContextClass *im = GTK_IM_CONTEXT_CLASS(klass);
    GObjectClass *obj = G_OBJECT_CLASS(klass);

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
void uk_im_context_register(GTypeModule *module)
{
    uk_im_context_register_type(module);
}

GType uk_im_context_type(void)
{
    return uk_im_context_get_type();
}
