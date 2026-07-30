// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey GTK4 input method module -- diem vao.
 *
 * GTK4 bo API im_module_* cua GTK2/GTK3, thay bang GIO extension point:
 * module phai xuat g_io_module_load/unload/query va tu dang ky vao
 * "gtk-im-module".
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gtk/gtkimmodule.h>

#include "imcontext.h"

#define UK_CONTEXT_ID "unikey"

G_MODULE_EXPORT void g_io_module_load(GIOModule *module)
{
    g_type_module_use(G_TYPE_MODULE(module));

    uk_im_context_register(G_TYPE_MODULE(module));

    g_io_extension_point_implement(GTK_IM_MODULE_EXTENSION_POINT_NAME,
                                   uk_im_context_type(),
                                   UK_CONTEXT_ID,
                                   10);
}

G_MODULE_EXPORT void g_io_module_unload(GIOModule *module)
{
    (void)module;
}

G_MODULE_EXPORT char **g_io_module_query(void)
{
    char *eps[] = {
        (char *)GTK_IM_MODULE_EXTENSION_POINT_NAME,
        NULL
    };
    return g_strdupv(eps);
}
