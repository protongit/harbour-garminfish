#ifndef GARMINFILETRANSFER_H
#define GARMINFILETRANSFER_H

#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QQueue>
#include <QString>

class CommunicatorV2;
class GarminDevice;

/**
 * GFDI file transfer for older Garmin devices (Multi-Link / GFDI).
 *
 * Flow:
 *   1. Download the directory (file index 0).
 *   2. Parse 16-byte directory entries.
 *   3. Download MONITOR / MONITOR_DAILY / SLEEP / ACTIVITY / METRICS / HRV_STATUS files.
 *   4. Archive each downloaded file on the watch.
 *   5. Import monitoring, sleep, stress, HRV and activity data into the local database.
 */
class GarminFileTransfer : public QObject
{
    Q_OBJECT
public:
    explicit GarminFileTransfer(CommunicatorV2 *communicator,
                                GarminDevice *device,
                                QObject *parent = nullptr);

    void startSync(bool fullHistory = false);

    bool isRunning() const { return m_state != State::Idle; }

signals:
    void syncStarted(int totalFiles);
    void syncProgress(int done, int total);
    void syncFinished(int downloaded, int total, int activities);
    void syncFailed(const QString &reason);

public slots:
    void onDownloadStatus(const QByteArray &data);
    void onFileTransferData(const QByteArray &data);
    void onFilterStatus();

private:
    struct Entry {
        quint16 fileIndex = 0;
        quint8 dataType = 0;
        quint8 subType = 0;
        quint16 fileNumber = 0;
        quint8 specificFlags = 0;
        quint8 fileFlags = 0;
        quint32 fileSize = 0;
        QDateTime date;

        bool isAllNull() const
        {
            return fileIndex == 0 && dataType == 0 && subType == 0
                    && fileNumber == 0 && fileSize == 0;
        }
    };

    enum class State {
        Idle,
        WaitingFilterStatus,
        WaitingDirectoryStatus,
        DownloadingDirectory,
        WaitingFileStatus,
        DownloadingFile
    };

    void sendDownloadRequest(quint16 fileIndex);
    void sendFileDataStatus(quint32 offset);
    void sendArchive(quint16 fileIndex);
    void sendFilter(quint8 filterType);
    void requestDirectory();
    void checkDirectoryTimeout();

    void reimportLocalFiles();
    void processDirectory();
    void processFile();
    void nextFile();
    void failCurrent(const QString &reason);

    bool isInteresting(const Entry &entry) const;
    QString typeName(const Entry &entry) const;
    QString fileName(const Entry &entry) const;
    QString storagePath(const Entry &entry) const;
    bool isTooOld(const Entry &entry) const;

    CommunicatorV2 *m_communicator = nullptr;
    GarminDevice *m_device = nullptr;

    State m_state = State::Idle;
    Entry m_current;
    QQueue<Entry> m_queue;

    QByteArray m_buffer;
    quint32 m_expectedSize = 0;
    quint32 m_receivedSize = 0;
    bool m_reimportDone = false;
    bool m_fullHistory = false;
    int m_totalFiles = 0;
    int m_downloadedFiles = 0;
    int m_activityFiles = 0;
    int m_directoryRetries = 0;
};

#endif // GARMINFILETRANSFER_H
