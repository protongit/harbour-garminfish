#include "garminfiletransfer.h"

#include "communicator_v2.h"
#include "garmin/garmindevice.h"
#include "garminfitparser.h"
#include "garmintypes.h"
#include "amazfishconfig.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#include <KDb3/KDbConnection>
#include <KDb3/KDbEscapedString>

#include <cstring>

namespace {

constexpr quint32 GARMIN_EPOCH_OFFSET = 631065600u; // 1989-12-31T00:00:00Z
constexpr int MAX_HISTORY_DAYS = 14;

static QString garminDatePart(const QDateTime &date)
{
    return date.isValid() ? date.toString("yyyyMMddTHHmmss") : QStringLiteral("nodate");
}

static void ensureImportTable(KDbConnection *db)
{
    if (!db) {
        return;
    }
    db->executeSql(KDbEscapedString(
            "CREATE TABLE IF NOT EXISTS garmin_imported_files ("
            "path TEXT PRIMARY KEY, device_id INTEGER, size INTEGER, "
            "mtime INTEGER, parser_version INTEGER)"));
}

static bool isAlreadyImported(KDbConnection *db, const QString &path,
                              uint deviceId, const QFileInfo &info)
{
    if (!db) {
        return false;
    }
    ensureImportTable(db);
    KDbCursor *cursor = db->executeQuery(KDbEscapedString(
            "SELECT size, mtime, parser_version, device_id "
            "FROM garmin_imported_files WHERE path = '%1'").arg(path));
    if (!cursor) {
        return false;
    }
    bool imported = false;
    if (cursor->open() && cursor->moveFirst()) {
        imported = cursor->value(0).toLongLong() == info.size()
                && cursor->value(1).toLongLong() == info.lastModified().toMSecsSinceEpoch()
                && cursor->value(2).toInt() == GarminFitParser::ParserVersion
                && cursor->value(3).toUInt() == deviceId;
    }
    db->deleteCursor(cursor);
    return imported;
}

static void markImported(KDbConnection *db, const QString &path,
                         uint deviceId, const QFileInfo &info)
{
    if (!db) {
        return;
    }
    ensureImportTable(db);
    db->executeSql(KDbEscapedString(
            "INSERT OR REPLACE INTO garmin_imported_files "
            "(path, device_id, size, mtime, parser_version) "
            "VALUES ('%1', %2, %3, %4, %5)")
                           .arg(path)
                           .arg(deviceId)
                           .arg(info.size())
                           .arg(info.lastModified().toMSecsSinceEpoch())
                           .arg(GarminFitParser::ParserVersion));
}

} // namespace

GarminFileTransfer::GarminFileTransfer(CommunicatorV2 *communicator,
                                       GarminDevice *device,
                                       QObject *parent)
    : QObject(parent)
    , m_communicator(communicator)
    , m_device(device)
{
}

void GarminFileTransfer::startSync(bool fullHistory)
{
    if (!m_communicator || !m_device) {
        qWarning() << "GarminFileTransfer: not ready";
        return;
    }

    if (m_state != State::Idle) {
        qDebug() << "GarminFileTransfer: sync already in progress";
        return;
    }

    m_queue.clear();
    m_current = Entry();
    m_buffer.clear();
    m_expectedSize = 0;
    m_receivedSize = 0;
    m_fullHistory = fullHistory;
    m_totalFiles = 0;
    m_downloadedFiles = 0;
    m_activityFiles = 0;
    m_directoryRetries = 0;

    emit syncStarted(0);

    if (!m_reimportDone) {
        reimportLocalFiles();
        m_reimportDone = true;
    }

    if (m_fullHistory) {
        qDebug() << "GarminFileTransfer: requesting full history filter";
        sendFilter(0); // ALL_FILES_0
        m_state = State::WaitingFilterStatus;

        // Some firmware versions do not answer FilterStatus. Fall back to a
        // normal directory request instead of leaving the sync stuck.
        QTimer::singleShot(5000, this, [this]() {
            if (m_state == State::WaitingFilterStatus) {
                qDebug() << "GarminFileTransfer: filter status timeout, requesting directory";
                requestDirectory();
            }
        });
        return;
    }

    requestDirectory();
}

void GarminFileTransfer::requestDirectory()
{
    m_current = Entry();
    m_buffer.clear();
    m_expectedSize = 0;
    m_receivedSize = 0;
    m_directoryRetries = 0;

    qDebug() << "GarminFileTransfer: requesting directory";
    m_state = State::WaitingDirectoryStatus;
    sendDownloadRequest(0);

    if (m_state == State::Idle) {
        qWarning() << "GarminFileTransfer: directory request could not be sent";
        emit syncFailed(QStringLiteral("directory request failed"));
        return;
    }

    QTimer::singleShot(5000, this, &GarminFileTransfer::checkDirectoryTimeout);
}

void GarminFileTransfer::checkDirectoryTimeout()
{
    if (m_state != State::WaitingDirectoryStatus) {
        return;
    }
    if (m_directoryRetries >= 2) {
        qWarning() << "GarminFileTransfer: directory request timed out";
        m_state = State::Idle;
        emit syncFailed(QStringLiteral("directory request timed out"));
        return;
    }

    ++m_directoryRetries;
    qDebug() << "GarminFileTransfer: retrying directory request" << m_directoryRetries;
    sendDownloadRequest(0);
    if (m_state == State::Idle) {
        qWarning() << "GarminFileTransfer: retry could not be sent";
        emit syncFailed(QStringLiteral("directory request failed"));
        return;
    }
    QTimer::singleShot(5000, this, &GarminFileTransfer::checkDirectoryTimeout);
}

void GarminFileTransfer::onFilterStatus()
{
    if (m_state == State::WaitingFilterStatus) {
        qDebug() << "GarminFileTransfer: filter accepted, requesting directory";
        requestDirectory();
        return;
    }

    if (m_state == State::Idle) {
        // The watch initiated a synchronization. It already sent Filter(ONLY_NEW)
        // through GarminSynchronizationMessage, so just request the directory.
        qDebug() << "GarminFileTransfer: watch initiated sync, requesting directory";
        m_fullHistory = false;
        requestDirectory();
    }
}

void GarminFileTransfer::sendFilter(quint8 filterType)
{
    QByteArray payload;
    payload.append(char(filterType));

    qDebug() << "GarminFileTransfer: sending filter" << filterType;
    m_communicator->sendMessage(QString("FILTER %1").arg(filterType),
                                wrapInGfdiEnvelope(MessageId::Filter, payload));
}

void GarminFileTransfer::sendDownloadRequest(quint16 fileIndex)
{
    QByteArray payload;
    writeU16le(payload, fileIndex);
    writeU32le(payload, 0);       // data offset
    payload.append(char(1));      // REQUEST_TYPE::NEW
    writeU16le(payload, 0);       // crc seed
    writeU32le(payload, 0);       // data size (0 = whole file)
    payload.append(char(0));      // request flags

    qDebug() << "GarminFileTransfer: download request" << fileIndex;
    if (!m_communicator->sendMessage(QString("DOWNLOAD REQUEST %1").arg(fileIndex),
                                     wrapInGfdiEnvelope(MessageId::DownloadRequest, payload))) {
        qWarning() << "GarminFileTransfer: failed to send download request";
        m_state = State::Idle;
    }
}

void GarminFileTransfer::sendFileDataStatus(quint32 offset)
{
    QByteArray payload;
    writeU16le(payload, quint16(MessageId::FileTransferData));
    payload.append(char(0)); // ACK
    payload.append(char(0)); // transfer status OK
    writeU32le(payload, offset);

    m_communicator->sendMessage("FILE TRANSFER DATA STATUS",
                                wrapInGfdiEnvelope(MessageId::Response, payload));
}

void GarminFileTransfer::sendArchive(quint16 fileIndex)
{
    QByteArray payload;
    writeU16le(payload, fileIndex);
    payload.append(char(0x10)); // ARCHIVE

    m_communicator->sendMessage(QString("ARCHIVE FILE %1").arg(fileIndex),
                                wrapInGfdiEnvelope(MessageId::SetFileFlag, payload));
}

void GarminFileTransfer::onDownloadStatus(const QByteArray &data)
{
    if (m_state != State::WaitingDirectoryStatus && m_state != State::WaitingFileStatus) {
        qDebug() << "GarminFileTransfer: unexpected download status";
        return;
    }

    if (data.size() < 8) {
        failCurrent("download status too short");
        return;
    }

    const quint16 originalMessage = u16le(data.constData(), 0);
    if (originalMessage != quint16(MessageId::DownloadRequest)) {
        qDebug() << "GarminFileTransfer: ignoring status for message" << originalMessage;
        return;
    }

    const quint8 status = quint8(data.at(2));
    const quint8 downloadStatus = quint8(data.at(3));
    const quint32 maxFileSize = u32le(data, 4);

    if (status != quint8(Status::Ack) || downloadStatus != 0) {
        failCurrent(QString("download failed: status=%1 fileStatus=%2")
                            .arg(status)
                            .arg(downloadStatus));
        return;
    }

    if (maxFileSize == 0) {
        failCurrent("download status reported size 0");
        return;
    }

    qDebug() << "GarminFileTransfer: download size" << maxFileSize;
    m_expectedSize = maxFileSize;
    m_receivedSize = 0;
    m_buffer = QByteArray(int(maxFileSize), '\0');
    m_state = (m_current.fileIndex == 0 && m_state == State::WaitingDirectoryStatus)
            ? State::DownloadingDirectory
            : State::DownloadingFile;
}

void GarminFileTransfer::onFileTransferData(const QByteArray &data)
{
    if (m_state != State::DownloadingDirectory && m_state != State::DownloadingFile) {
        qDebug() << "GarminFileTransfer: unexpected file data, state" << int(m_state);
        return;
    }

    if (data.size() < 7) {
        qWarning() << "GarminFileTransfer: file data too short" << data.size();
        return;
    }

    // flags (1), crc (2), data offset (4), payload
    const quint32 offset = u32le(data, 3);
    const QByteArray payload = data.mid(7);

    if (payload.isEmpty()) {
        qWarning() << "GarminFileTransfer: empty file data chunk";
        return;
    }

    const quint32 end = offset + quint32(payload.size());
    if (end > m_expectedSize) {
        qWarning() << "GarminFileTransfer: chunk exceeds expected size, resizing"
                   << end << m_expectedSize;
        m_buffer.resize(int(end));
        m_expectedSize = end;
    }

    if (m_buffer.size() < int(end)) {
        m_buffer.resize(int(end));
    }

    memcpy(m_buffer.data() + offset, payload.constData(), size_t(payload.size()));
    if (end > m_receivedSize) {
        m_receivedSize = end;
    }

    sendFileDataStatus(end);

    if (m_receivedSize >= m_expectedSize) {
        if (m_state == State::DownloadingDirectory) {
            processDirectory();
        } else {
            processFile();
        }
    }
}

void GarminFileTransfer::reimportLocalFiles()
{
    if (!m_device || !m_device->database()) {
        return;
    }

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + "/garmin";
    QDirIterator iterator(base, QStringList() << "*.fit",
                          QDir::Files, QDirIterator::Subdirectories);
    if (!iterator.hasNext()) {
        return;
    }

    auto config = AmazfishConfig::instance();
    const uint deviceId = qHash(config->pairedAddress());
    const uint userId = qHash(config->profileName());
    KDbConnection *db = m_device->database();

    int files = 0;
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info(path);
        if (isAlreadyImported(db, path, deviceId, info)) {
            continue;
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "GarminFileTransfer: cannot read local FIT" << path;
            continue;
        }
        const QByteArray data = file.readAll();
        file.close();

        const QString sourceType = info.dir().dirName();
        GarminFitParser::importFile(data, db, deviceId, userId, sourceType);
        markImported(db, path, deviceId, info);
        ++files;
    }
    qDebug() << "GarminFileTransfer: reimported" << files << "local FIT files";
}

void GarminFileTransfer::processDirectory()
{
    qDebug() << "GarminFileTransfer: directory received" << m_buffer.size() << "bytes";

    if (m_buffer.size() % 16 != 0) {
        qWarning() << "GarminFileTransfer: directory size is not a multiple of 16";
    }

    int queued = 0;
    const int count = m_buffer.size() / 16;
    for (int i = 0; i < count; ++i) {
        const int offset = i * 16;

        Entry entry;
        entry.fileIndex = quint16(quint8(m_buffer.at(offset)))
                | (quint16(quint8(m_buffer.at(offset + 1))) << 8);
        entry.dataType = quint8(m_buffer.at(offset + 2));
        entry.subType = quint8(m_buffer.at(offset + 3));
        entry.fileNumber = quint16(quint8(m_buffer.at(offset + 4)))
                | (quint16(quint8(m_buffer.at(offset + 5))) << 8);
        entry.specificFlags = quint8(m_buffer.at(offset + 6));
        entry.fileFlags = quint8(m_buffer.at(offset + 7));
        entry.fileSize = u32le(m_buffer, offset + 8);
        const quint32 wireTimestamp = u32le(m_buffer, offset + 12);
        if (wireTimestamp != 0) {
            entry.date = QDateTime::fromMSecsSinceEpoch(
                    qint64(wireTimestamp + GARMIN_EPOCH_OFFSET) * 1000LL);
        }

        if (entry.isAllNull() || entry.fileIndex == 0) {
            continue;
        }

        qDebug() << "GarminFileTransfer: directory entry"
                 << entry.fileIndex
                 << entry.dataType << entry.subType
                 << "size" << entry.fileSize
                 << "flags" << entry.fileFlags
                 << entry.date.toString("yyyy-MM-dd HH:mm:ss");

        if (!isInteresting(entry)) {
            continue;
        }
        if (isTooOld(entry)) {
            qDebug() << "GarminFileTransfer: skipping old file" << fileName(entry);
            continue;
        }
        if (QFile::exists(storagePath(entry))) {
            qDebug() << "GarminFileTransfer: already downloaded" << fileName(entry);
            continue;
        }

        m_queue.enqueue(entry);
        ++queued;
    }

    qDebug() << "GarminFileTransfer: queued" << queued << "files";
    m_totalFiles = m_queue.size();
    m_downloadedFiles = 0;
    m_activityFiles = 0;
    emit syncStarted(m_totalFiles);
    if (m_queue.isEmpty()) {
        m_state = State::Idle;
        emit syncFinished(0, 0, 0);
        return;
    }
    nextFile();
}

void GarminFileTransfer::processFile()
{
    const QString path = storagePath(m_current);
    const QDir dir = QFileInfo(path).dir();
    dir.mkpath(".");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "GarminFileTransfer: cannot write" << path << file.errorString();
    } else {
        file.write(m_buffer);
        file.close();
        qDebug() << "GarminFileTransfer: saved" << path << m_buffer.size() << "bytes";
    }

    if (m_device && m_device->database()) {
        auto config = AmazfishConfig::instance();
        const uint deviceId = qHash(config->pairedAddress());
        const uint userId = qHash(config->profileName());
        KDbConnection *db = m_device->database();
        const int imported = GarminFitParser::importFile(
                m_buffer, db, deviceId, userId, typeName(m_current));
        qDebug() << "GarminFileTransfer: imported" << imported << "samples from" << typeName(m_current);
        const QFileInfo info(path);
        if (info.exists() && info.size() == m_buffer.size()) {
            markImported(db, path, deviceId, info);
        }
    } else {
        qWarning() << "GarminFileTransfer: no database, FIT kept on disk only";
    }

    ++m_downloadedFiles;
    if (m_current.subType == 4) {
        ++m_activityFiles;
    }
    emit syncProgress(m_downloadedFiles, m_totalFiles);

    sendArchive(m_current.fileIndex);
    nextFile();
}

void GarminFileTransfer::nextFile()
{
    while (!m_queue.isEmpty()) {
        m_current = m_queue.dequeue();
        if (QFile::exists(storagePath(m_current))) {
            qDebug() << "GarminFileTransfer: already present" << fileName(m_current);
            continue;
        }
        m_buffer.clear();
        m_expectedSize = 0;
        m_receivedSize = 0;

        qDebug() << "GarminFileTransfer: downloading" << fileName(m_current);
        sendDownloadRequest(m_current.fileIndex);
        m_state = State::WaitingFileStatus;
        return;
    }

    m_current = Entry();
    m_state = State::Idle;
    qDebug() << "GarminFileTransfer: sync complete";
    emit syncFinished(m_downloadedFiles, m_totalFiles, m_activityFiles);
}

void GarminFileTransfer::failCurrent(const QString &reason)
{
    qWarning() << "GarminFileTransfer:" << reason
               << "file" << m_current.fileIndex << typeName(m_current);
    m_buffer.clear();
    m_expectedSize = 0;
    m_receivedSize = 0;

    if (m_current.fileIndex == 0) {
        m_state = State::Idle;
        m_queue.clear();
        emit syncFailed(reason);
        return;
    }

    nextFile();
}

bool GarminFileTransfer::isInteresting(const Entry &entry) const
{
    if (entry.fileSize == 0) {
        return false;
    }
    if (entry.dataType != 128) {
        return false;
    }
    switch (entry.subType) {
    case 4:  // ACTIVITY
    case 28: // MONITOR_DAILY
    case 32: // MONITOR
    case 44: // METRICS
    case 49: // SLEEP
    case 68: // HRV_STATUS
        return true;
    default:
        return false;
    }
}

QString GarminFileTransfer::typeName(const Entry &entry) const
{
    if (entry.dataType == 128) {
        switch (entry.subType) {
        case 4: return QStringLiteral("ACTIVITY");
        case 28: return QStringLiteral("MONITOR_DAILY");
        case 32: return QStringLiteral("MONITOR");
        case 44: return QStringLiteral("METRICS");
        case 49: return QStringLiteral("SLEEP");
        case 68: return QStringLiteral("HRV_STATUS");
        default: break;
        }
    }
    return QStringLiteral("TYPE_%1_%2").arg(entry.dataType).arg(entry.subType);
}

QString GarminFileTransfer::fileName(const Entry &entry) const
{
    return QString("%1_%2_%3.fit")
            .arg(typeName(entry))
            .arg(entry.fileIndex)
            .arg(garminDatePart(entry.date));
}

QString GarminFileTransfer::storagePath(const Entry &entry) const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QString("%1/garmin/%2/%3")
            .arg(base)
            .arg(typeName(entry))
            .arg(fileName(entry));
}

bool GarminFileTransfer::isTooOld(const Entry &entry) const
{
    if (!entry.date.isValid()) {
        return false;
    }
    const int days = m_fullHistory ? 90 : MAX_HISTORY_DAYS;
    return entry.date < QDateTime::currentDateTime().addDays(-days);
}
