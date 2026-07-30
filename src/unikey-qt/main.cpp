// -*- coding:unix; mode:c++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey Qt platform input context plugin -- diem vao */

#include <QtGui/qpa/qplatforminputcontextplugin_p.h>
#include <QtCore/QStringList>

#include "ukinputcontext.h"

class UkInputContextPlugin : public QPlatformInputContextPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformInputContextFactoryInterface_iid FILE "unikey.json")

public:
    QPlatformInputContext *create(const QString &key, const QStringList &params) override
    {
        Q_UNUSED(params)
        if (key.compare(QLatin1String("unikey"), Qt::CaseInsensitive) == 0)
            return new UkInputContext();
        return nullptr;
    }
};

#include "main.moc"
