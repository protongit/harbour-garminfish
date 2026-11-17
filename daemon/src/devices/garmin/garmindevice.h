#ifndef GARMIN_DEVICE__H
#define GARMIN_DEVICE__H

#include <QObject>
#include <QSharedPointer>
#include "abstractdevice.h"
#include "services/garmin/communicator_v2.h"
#include "services/garmin/garminnotificationhandler.h"
#include "services/garmin/protobufhandler.h"


class NotificationSpec;
class GarminNotificationHandler;
class ProtobufHandler;
class GarminFileTransfer;

class GarminDevice : public AbstractDevice
{
    Q_OBJECT
public:
    explicit GarminDevice(const QString &pairedName, QObject *parent = 0);
    QBLEService *drv_createService(const QString &uuid, const QString &path) override;

    Amazfish::Features supportedFeatures() const override;
    Amazfish::DataTypes supportedDataTypes() const override;
    QString deviceType() const override;
    void setPaired() {m_pairing = false;};
    bool isPairing() {return m_pairing;};
    void sendAlert(const Amazfish::WatchNotification &notification) override;
    void sendWeather(CurrentWeather *weather) override;
    void incomingCall(const QString &caller) override;
    void incomingCallEnded() override;
    void setMusicStatus(bool playing, const QString &title, const QString &artist,
                        const QString &album, int duration = 0, int position = 0) override;
    void setMusicPosition(int positionSeconds) override;
    void setMusicVolume(int volume) override;

    AbstractFirmwareInfo *firmwareInfo(const QByteArray &bytes, const QString &path) override;
    Q_SLOT void authenticated(bool ready);
    Q_SLOT void onSessionReady();

    void fetchData(Amazfish::DataTypes dataTypes) override;
    void fetchFullData(Amazfish::DataTypes dataTypes) override;
    QString information(Amazfish::Info i) const override;
    bool operationRunning() override;

public slots:

    void onPropertiesChanged(QString interface, QVariantMap map, QStringList list);
    void onAnswerCallEvent();
    void onRejectCallEvent();
    void onMusicControlCommand(quint8 command);
    void onMusicCapabilitiesRequested();
    void onSyncStarted(int totalFiles);
    void onSyncProgress(int done, int total);
    void onSyncFinished(int downloaded, int total, int activities);
    void onSyncFailed(const QString &reason);

signals:
    void sendAlertToDevice(NotificationSpec &note);

private:
    int mSteps = 0;
    bool m_fileTransferRunning = false;

    // Last music state, replayed when the watch opens its music widget.
    bool m_hasMusicStatus = false;
    bool m_musicPlaying = false;
    int m_musicVolume = -1;
    int m_musicDuration = 0;
    int m_musicPosition = 0;
    QString m_musicTitle;
    QString m_musicArtist;
    QString m_musicAlbum;

    void sendMusicStatus();

    void initialise();
    virtual void refreshInformation() override;

    QString mPreferredService;
    QSharedPointer<GarminNotificationHandler> mNotificationHandler;
    QSharedPointer<ProtobufHandler> mProtobufHandler;
    QSharedPointer<CommunicatorV2> mCommunicator;
    GarminFileTransfer *mFileTransfer = nullptr;

};

#endif // GARMIN_DEVICE__H
