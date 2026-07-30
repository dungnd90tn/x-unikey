// -*- coding:unix; mode:c++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* x-unikey Qt platform input context
 *
 * Cung mo hinh preedit nhu hai module GTK. Khac biet duy nhat la cach gui
 * ket qua: Qt khong co tin hieu "commit"/"preedit-changed" ma dung
 * QInputMethodEvent gui toi focus object.
 */

#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QTextCharFormat>
#include <QtCore/QCoreApplication>
#include <QtCore/QSocketNotifier>

#include "ukinputcontext.h"

extern "C" {
#include "ukkeys.h"
#include "keycons.h"
}

/*--------------------------------------------------------------------*/
UkInputContext::UkInputContext()
    : m_bridge(nullptr), m_stateNotifier(nullptr)
{
    static const UkBridgeVTable vt = { &UkInputContext::bridgeCommit,
                                       &UkInputContext::bridgePreedit };
    m_bridge = uk_bridge_new(&vt, this);

    /* Trang thai bat/tat chia se voi cac app GTK qua ~/.unikey/state.
       Lop nay khong co Q_OBJECT nen dung connect kieu ham, nhan notifier
       lam context object. */
    const int fd = uk_bridge_state_fd(m_bridge);
    if (fd >= 0) {
        m_stateNotifier = new QSocketNotifier(fd, QSocketNotifier::Read);
        QObject::connect(m_stateNotifier, &QSocketNotifier::activated,
                         m_stateNotifier, [this]() {
                             uk_bridge_state_dispatch(m_bridge);
                         });
    }
}

UkInputContext::~UkInputContext()
{
    delete m_stateNotifier;
    m_stateNotifier = nullptr;
    uk_bridge_free(m_bridge);
    m_bridge = nullptr;
}

/*--------------------------------------------------------------------*/
void UkInputContext::bridgeCommit(void *user, const char *utf8)
{
    static_cast<UkInputContext *>(user)->sendCommit(QString::fromUtf8(utf8));
}

void UkInputContext::bridgePreedit(void *user, const char *utf8)
{
    static_cast<UkInputContext *>(user)->sendPreedit(QString::fromUtf8(utf8 ? utf8 : ""));
}

/*--------------------------------------------------------------------*/
void UkInputContext::sendPreedit(const QString &text)
{
    QObject *fo = qGuiApp ? qGuiApp->focusObject() : nullptr;
    if (!fo)
        return;

    QList<QInputMethodEvent::Attribute> attrs;

    if (!text.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setFontUnderline(true);
        attrs.append(QInputMethodEvent::Attribute(QInputMethodEvent::TextFormat,
                                                  0, text.length(), fmt));
    }
    attrs.append(QInputMethodEvent::Attribute(QInputMethodEvent::Cursor,
                                              text.length(), 1, QVariant()));

    QInputMethodEvent ev(text, attrs);
    QCoreApplication::sendEvent(fo, &ev);
}

void UkInputContext::sendCommit(const QString &text)
{
    QObject *fo = qGuiApp ? qGuiApp->focusObject() : nullptr;
    if (!fo || text.isEmpty())
        return;

    QInputMethodEvent ev;
    ev.setCommitString(text);
    QCoreApplication::sendEvent(fo, &ev);
}

/*--------------------------------------------------------------------*/
void UkInputContext::reset()
{
    uk_bridge_reset(m_bridge);
}

void UkInputContext::commit()
{
    uk_bridge_flush(m_bridge);
}

void UkInputContext::setFocusObject(QObject *object)
{
    /* Engine dung chung ca tien trinh: doi o nhap thi phai chot phan dang go
       cua o cu, neu khong am tiet se ro ri sang o moi. */
    if (m_focus && m_focus != object)
        uk_bridge_flush(m_bridge);
    m_focus = object;
}

/*--------------------------------------------------------------------*/
bool UkInputContext::filterEvent(const QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease)
        return false;

    const QKeyEvent *ke = static_cast<const QKeyEvent *>(event);
    const Qt::KeyboardModifiers mods = ke->modifiers();

    /* nativeVirtualKey la keysym X11 tren X11, va keysym xkb tren Wayland --
       hai thu nay trung nhau, nen dung thang duoc voi ukkeys. */
    UkKeyInfo ki;
    ki.keysym   = ke->nativeVirtualKey();
    ki.ctrl     = (mods & Qt::ControlModifier) ? 1 : 0;
    ki.shift    = (mods & Qt::ShiftModifier) ? 1 : 0;
    ki.alt      = (mods & Qt::AltModifier) ? 1 : 0;
    ki.super    = (mods & Qt::MetaModifier) ? 1 : 0;
    ki.is_press = (type == QEvent::KeyPress) ? 1 : 0;

    if (uk_bridge_apply_action(m_bridge, uk_keys_shortcut(&ki)))
        return true;

    if (type != QEvent::KeyPress)
        return false;

    if (uk_keys_is_modifier(ki.keysym))
        return false;

    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        uk_bridge_flush(m_bridge);
        return false;
    }

    if (ke->key() == Qt::Key_Backspace)
        return uk_bridge_backspace(m_bridge) == UK_BRIDGE_CONSUMED;

    if (uk_keys_is_editing(ki.keysym)) {
        uk_bridge_flush(m_bridge);
        return false;
    }

    if (ke->key() >= Qt::Key_0 && ke->key() <= Qt::Key_9 &&
        (mods & Qt::KeypadModifier)) {
        uk_bridge_flush(m_bridge);
        return false;
    }

    const QString text = ke->text();
    if (text.isEmpty())
        return false;

    const uint uch = text.at(0).unicode();
    if (uch < 0x20)   /* ky tu dieu khien */
        return false;

    return uk_bridge_key(m_bridge, uch,
                         (mods & Qt::ShiftModifier) ? 1 : 0,
                         0) == UK_BRIDGE_CONSUMED;
}
