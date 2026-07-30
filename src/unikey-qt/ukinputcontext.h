// -*- coding:unix; mode:c++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey Qt platform input context */
#ifndef __UK_QT_INPUT_CONTEXT_H
#define __UK_QT_INPUT_CONTEXT_H

#include <QtGui/qpa/qplatforminputcontext.h>
#include <QtCore/QPointer>

QT_BEGIN_NAMESPACE
class QSocketNotifier;
QT_END_NAMESPACE

extern "C" {
#include "ukbridge.h"
}

/* Khong dat Q_OBJECT o day: lop nay chi override ham ao, khong khai bao
   signal/slot moi, nen khong can moc. Chi rieng lop plugin trong main.cpp
   moi bat buoc phai qua moc (vi Q_PLUGIN_METADATA). */
class UkInputContext : public QPlatformInputContext
{
public:
    UkInputContext();
    ~UkInputContext() override;

    bool isValid() const override { return true; }
    void reset() override;
    void commit() override;
    bool filterEvent(const QEvent *event) override;
    void setFocusObject(QObject *object) override;

private:
    static void bridgeCommit(void *user, const char *utf8);
    static void bridgePreedit(void *user, const char *utf8);

    void sendPreedit(const QString &text);
    void sendCommit(const QString &text);

    UkBridge         *m_bridge;
    QPointer<QObject> m_focus;
    QSocketNotifier  *m_stateNotifier;
};

#endif
