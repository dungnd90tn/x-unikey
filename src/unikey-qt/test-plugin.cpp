// -*- coding:unix; mode:c++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-
/* Kiem tra plugin Qt tu nap duoc va tao ra input context.
 *
 * May build khong chac co san ung dung Qt nao de go thu, nen test nay lam dung
 * viec ma Qt lam luc khoi dong: doc metadata cua plugin, kiem tra khoa "unikey",
 * nap plugin roi goi create(). Phan logic go tieng Viet da co test-bridge phu.
 */

#include <QtCore/QCoreApplication>
#include <QtCore/QPluginLoader>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QFileInfo>
#include <QtGui/qpa/qplatforminputcontextplugin_p.h>
#include <QtGui/qpa/qplatforminputcontext.h>

#include <cstdio>

static int Failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("  %s   %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        Failures++;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString path = (argc > 1)
        ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral(".libs/libunikeyplatforminputcontextplugin.so");

    std::printf("plugin Qt (%s):\n", qPrintable(QFileInfo(path).fileName()));

    QPluginLoader loader(path);

    const QJsonObject meta = loader.metaData();
    check(!meta.isEmpty(), "doc duoc metadata");

    const QString iid = meta.value(QStringLiteral("IID")).toString();
    check(iid.startsWith(QStringLiteral("org.qt-project.Qt.QPlatformInputContextFactoryInterface")),
          "IID dung interface cua platform input context");

    const QJsonArray keys =
        meta.value(QStringLiteral("MetaData")).toObject().value(QStringLiteral("Keys")).toArray();
    bool hasUnikey = false;
    for (const QJsonValue &k : keys)
        if (k.toString().compare(QStringLiteral("unikey"), Qt::CaseInsensitive) == 0)
            hasUnikey = true;
    check(hasUnikey, "co khoa \"unikey\" (QT_IM_MODULE=unikey se tim thay)");

    QObject *instance = loader.instance();
    if (!instance) {
        std::printf("  FAIL nap plugin: %s\n", qPrintable(loader.errorString()));
        Failures++;
    } else {
        check(true, "nap duoc plugin");

        QPlatformInputContextPlugin *factory =
            qobject_cast<QPlatformInputContextPlugin *>(instance);
        check(factory != nullptr, "instance la QPlatformInputContextPlugin");

        if (factory) {
            QPlatformInputContext *ic = factory->create(QStringLiteral("unikey"), QStringList());
            check(ic != nullptr, "create(\"unikey\") tra ve input context");
            if (ic) {
                check(ic->isValid(), "input context bao isValid()");
                delete ic;
            }
            check(factory->create(QStringLiteral("khong-ton-tai"), QStringList()) == nullptr,
                  "create() tu choi khoa la");
        }
    }

    if (Failures == 0)
        std::printf("=> tat ca deu dat\n");
    else
        std::printf("=> %d truong hop sai\n", Failures);

    return Failures == 0 ? 0 : 1;
}
