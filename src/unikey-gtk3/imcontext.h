// -*- coding:unix; mode:c; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey GTK3 input method module */
#ifndef __UK_GTK3_IM_CONTEXT_H
#define __UK_GTK3_IM_CONTEXT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define UK_TYPE_IM_CONTEXT            (uk_im_context_get_type())
#define UK_IM_CONTEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), UK_TYPE_IM_CONTEXT, UkImContext))
#define UK_IS_IM_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), UK_TYPE_IM_CONTEXT))

typedef struct _UkImContext      UkImContext;
typedef struct _UkImContextClass UkImContextClass;

GType         uk_im_context_get_type(void);
void          uk_im_context_register_type(GTypeModule *module);
GtkIMContext *uk_im_context_new(void);

G_END_DECLS

#endif
