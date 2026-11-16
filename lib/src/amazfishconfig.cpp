#include "amazfishconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QProcess>
#include <QStandardPaths>

#ifdef MER_EDITION_SAILFISH
#include <MDConfGroup>
#endif

#include <functional>


AmazfishConfig::AmazfishConfig(QObject *parent)
    : QObject(parent)
#ifdef MER_EDITION_SAILFISH
    , m_group(new MDConfGroup(QStringLiteral("/uk/co/protongit/garminfish"), this))
#endif
{

}

AmazfishConfig *AmazfishConfig::instance()
{
    static AmazfishConfig *inst = nullptr;
    if (!inst) {
        inst = new AmazfishConfig(qApp);
    }
    return inst;
}

void AmazfishConfig::migrateLegacyData()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    // Data directory: ~/.local/share/<organization>/<application>. The old
    // build used "harbour-amazfish" for both, so move that tree (database,
    // Garmin FIT files and caches) to the new name.
    const QString newAppDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString oldAppDir = QDir::homePath()
            + QStringLiteral("/.local/share/harbour-amazfish/harbour-amazfish");

    if (!newAppDir.isEmpty() && !QDir(newAppDir).exists() && QDir(oldAppDir).exists()) {
        QDir().mkpath(QFileInfo(newAppDir).absolutePath());
        if (QDir().rename(oldAppDir, newAppDir)) {
            qDebug() << "GarminFish: migrated data to" << newAppDir;
        }
    }

    // The database file changed its name too.
    const QString newDb = newAppDir + QStringLiteral("/garminfish.kexi");
    const QString oldDb = newAppDir + QStringLiteral("/amazfish.kexi");
    if (!newAppDir.isEmpty() && !QFileInfo::exists(newDb) && QFileInfo::exists(oldDb)) {
        QFile::rename(oldDb, newDb);
    }

    // Settings: /uk/co/piggz/amazfish -> /uk/co/protongit/garminfish
    const QString dconf = QStandardPaths::findExecutable(QStringLiteral("dconf"));
    if (dconf.isEmpty()) {
        return;
    }

    QProcess legacyDump;
    legacyDump.start(dconf, QStringList() << QStringLiteral("dump")
                                          << QStringLiteral("/uk/co/piggz/amazfish/"));
    if (!legacyDump.waitForFinished(3000)) {
        legacyDump.kill();
        return;
    }
    const QByteArray legacy = legacyDump.readAllStandardOutput();
    if (legacy.trimmed().isEmpty()) {
        return;
    }

    QProcess currentDump;
    currentDump.start(dconf, QStringList() << QStringLiteral("dump")
                                           << QStringLiteral("/uk/co/protongit/garminfish/"));
    if (!currentDump.waitForFinished(3000)) {
        currentDump.kill();
        return;
    }
    if (!currentDump.readAllStandardOutput().trimmed().isEmpty()) {
        return; // Already configured: do not overwrite the new settings.
    }

    QProcess load;
    load.start(dconf, QStringList() << QStringLiteral("load")
                                    << QStringLiteral("/uk/co/protongit/garminfish/"));
    if (load.waitForStarted(3000)) {
        load.write(legacy);
        load.closeWriteChannel();
        load.waitForFinished(3000);
        qDebug() << "GarminFish: migrated settings from Amazfish";
    }
}

QVariant AmazfishConfig::value(const QString &key, const QVariant &def) const
{
#ifdef MER_EDITION_SAILFISH
    return m_group->value(key, def);
#else
    QSettings settings;
    return settings.value(key, def);
#endif
}

void AmazfishConfig::setValue(const QString &key, const QVariant &value)
{
#ifdef MER_EDITION_SAILFISH
    m_group->setValue(key, value);
#else
    QSettings settings;
    settings.setValue(key, value);
#endif
}

void AmazfishConfig::setValue(const QString &key, const QVariant &value, signal_ptr signal)
{
#ifdef MER_EDITION_SAILFISH
    auto prev = m_group->value(key);
#else
    QSettings settings;
    auto prev = settings.value(key);
#endif

    if (value != prev) {
#ifdef MER_EDITION_SAILFISH
        m_group->setValue(key, value);
#else
        settings.setValue(key, value);
#endif
        emit std::bind(signal, this)();
    }
}
