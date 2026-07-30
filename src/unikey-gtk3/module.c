// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey GTK3 input method module -- diem vao cua module */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <gtk/gtkimmodule.h>

#include "imcontext.h"

static const GtkIMContextInfo UkInfo = {
    "unikey",                              /* context id */
    "Vietnamese (UniKey)",                 /* ten hien thi */
    "x-unikey",                            /* gettext domain */
    "",                                    /* thu muc locale */
    "vi"                                   /* ngon ngu mac dinh dung module nay */
};

static const GtkIMContextInfo *InfoList[] = { &UkInfo };

G_MODULE_EXPORT void im_module_init(GTypeModule *module)
{
    uk_im_context_register_type(module);
}

G_MODULE_EXPORT void im_module_exit(void)
{
}

G_MODULE_EXPORT void im_module_list(const GtkIMContextInfo ***contexts,
                                    int *n_contexts)
{
    *contexts = InfoList;
    *n_contexts = G_N_ELEMENTS(InfoList);
}

G_MODULE_EXPORT GtkIMContext *im_module_create(const gchar *context_id)
{
    if (context_id && strcmp(context_id, "unikey") == 0)
        return uk_im_context_new();
    return NULL;
}
