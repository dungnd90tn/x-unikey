// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey GTK4 input method module */
#ifndef __UK_GTK4_IM_CONTEXT_H
#define __UK_GTK4_IM_CONTEXT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define UK_TYPE_IM_CONTEXT (uk_im_context_get_type())
G_DECLARE_FINAL_TYPE(UkImContext, uk_im_context, UK, IM_CONTEXT, GtkIMContext)

void  uk_im_context_register(GTypeModule *module);
GType uk_im_context_type(void);

G_END_DECLS

#endif
