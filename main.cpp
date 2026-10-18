#include "ElaApplication.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace
{
void debugMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Q_UNUSED(type)
    Q_UNUSED(context)
    if (!qEnvironmentVariableIsSet("IPHONE_GALLERY_DEBUG"))
    {
        return;
    }
    QFile file(QDir::tempPath() + QStringLiteral("/IphoneGallery.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        QTextStream stream(&file);
        stream << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) << ' '
               << msg << '\n';
    }
}
} // namespace

int main(int argc, char* argv[])
{
    qInstallMessageHandler(debugMessageHandler);
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("IphoneGallery"));
    application.setOrganizationDomain(QStringLiteral("local.iphonegallery"));
    application.setApplicationName(QStringLiteral("IphoneGallery"));

    eApp->init();

    MainWindow window;
    window.show();
    return application.exec();
}
