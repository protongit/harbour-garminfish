/*
 * (C) 2014 Kimmo Lindholm <kimmo.lindholm@gmail.com> Kimmoli
 *
 * Main, Daemon functions
 *
 */

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QtCore/QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "deviceinterface.h"
#include "devicefactory.h"
#include "amazfishconfig.h"

static void daemonize();
static void signalHandler(int sig);

// The user session journal is not readable by defaultuser on Sailfish, so keep
// a small rotating log next to the application data. It is what makes it
// possible to diagnose why a watch connection was lost.
static QFile *logFile = nullptr;
static qint64 logBytes = 0;
static const qint64 LOG_FILE_MAX_BYTES = 2 * 1024 * 1024;

// The daemon logs every BLE packet, which is far too much for a phone that
// runs for days. Keep only warnings and connection milestones by default;
// create ~/.local/share/harbour-garminfish/harbour-garminfish/debug-log and
// restart the service to get the full protocol trace back.
static bool verboseLogging()
{
    static int cached = -1;
    if (cached < 0) {
        const QString marker = QDir::homePath()
                + QStringLiteral("/.local/share/harbour-garminfish/harbour-garminfish/debug-log");
        cached = QFile::exists(marker) ? 1 : 0;
    }
    return cached == 1;
}

static void appendToLogFile(const QByteArray &line)
{
    if (!logFile) {
        const QString dir = QDir::homePath()
                + QStringLiteral("/.local/share/harbour-garminfish/harbour-garminfish");
        if (!QDir().mkpath(dir)) {
            return;
        }
        const QString path = dir + QStringLiteral("/garminfish-daemon.log");
        const QFileInfo info(path);
        if (info.exists() && info.size() > LOG_FILE_MAX_BYTES) {
            QFile::remove(path + QStringLiteral(".1"));
            QFile::rename(path, path + QStringLiteral(".1"));
        }
        logFile = new QFile(path);
        if (!logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
            delete logFile;
            logFile = nullptr;
            return;
        }
        logBytes = logFile->size();
    }

    logFile->write(line);
    logFile->flush();
    logBytes += line.size();

    // Rotate while running too: the daemon can stay up for days and the log
    // is deliberately verbose.
    if (logBytes > LOG_FILE_MAX_BYTES) {
        const QString path = logFile->fileName();
        logFile->close();
        delete logFile;
        logFile = nullptr;
        logBytes = 0;
        QFile::remove(path + QStringLiteral(".1"));
        QFile::rename(path, path + QStringLiteral(".1"));
    }
}

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(type)

    QByteArray localMsg = msg.toLocal8Bit();
    const QByteArray time = QDateTime::currentDateTime()
            .toString("yyyy-MM-dd hh:mm:ss.zzz").toLocal8Bit();
    QByteArray line;
#ifdef QT_MESSAGELOGCONTEXT
    line = time + " " + QByteArray(context.file) + ":" + QByteArray::number(context.line)
            + ": " + localMsg + "\n";
#else
    line = time + " : " + localMsg + "\n";
#endif
    fputs(line.constData(), stderr);

    const bool important = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg
            || msg.contains(QStringLiteral("Connection state:"))
            || msg.startsWith(QStringLiteral("GarminFish:"));
    if (important || verboseLogging()) {
        appendToLogFile(line);
    }
}

int main(int argc, char **argv)
{
    qInstallMessageHandler(myMessageOutput);

#ifdef PROJECT_GIT_VERSION
    qDebug() << "Starting harbour-garminfishd " << PROJECT_GIT_VERSION;
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("harbour-garminfish");
    QCoreApplication::setApplicationName("harbour-garminfish");
    AmazfishConfig::migrateLegacyData();

    if (app.arguments().contains("--features")) {
        DeviceFactory::printAvailableFeatures();
        return 0;
    }

    {
        QString tr_path(TRANSLATION_FOLDER);
        if (QDir::isRelativePath(tr_path)) {
            tr_path = QDir(QFileInfo(QCoreApplication::applicationFilePath()).absolutePath() + "/" + tr_path ).absolutePath();
        }

        QString locale = QLocale::system().name();
        QTranslator *translator = new QTranslator();
        if ( translator->load(QLocale(), "harbour-garminfishd", "-", tr_path) ) {
            if (app.installTranslator(translator)) {
                qDebug() << "Install translation success for " << locale << " " << tr_path;
            } else {
                qWarning() << "Install translation failed for " << locale << " " << tr_path;
            }
        } else {
            qWarning() << "Failed to load translation for " << locale << " " << tr_path;
        }

    }

    // When started by systemd, stay in the foreground so systemd tracks the
    // real daemon process. The historical double-fork left orphan processes
    // holding the D-Bus name on every service restart.
    const bool underSystemd = !qgetenv("INVOCATION_ID").isEmpty()
            || !qgetenv("NOTIFY_SOCKET").isEmpty();
    if (!underSystemd) {
        daemonize();
    }

    setlinebuf(stdout);
    setlinebuf(stderr);

    qDebug() << "Starting amazfish daemon";

    QDir d;
    d.mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    d.mkpath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));

    DeviceInterface devInterface;


    return app.exec();
}

static void daemonize()
{
    /* Change the file mode mask */
    umask(0);

    /* Change the current working directory */
    if ((chdir("/tmp")) < 0)
        exit(EXIT_FAILURE);

    /* register signals to monitor / ignore */
    signal(SIGCHLD,SIG_IGN); /* ignore child */
    signal(SIGTSTP,SIG_IGN); /* ignore tty signals */
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
    signal(SIGHUP,signalHandler); /* catch hangup signal */
    signal(SIGTERM,signalHandler); /* catch kill signal */
}


static void signalHandler(int sig) /* signal handler function */
{
    switch(sig)
    {
    case SIGHUP:
        printf("Received signal SIGHUP\n");
        break;

    case SIGTERM:
        printf("Received signal SIGTERM\n");
        QCoreApplication::quit();
        break;
    }
}
