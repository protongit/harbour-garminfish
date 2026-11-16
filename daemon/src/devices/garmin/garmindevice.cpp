#include "garmindevice.h"
#include "services/garmin/communicator_v2.h"
#include "services/garmin/garminfiletransfer.h"
#include "services/garmin/garminmusiccontrolmessage.h"
#include "hrmservice.h"
#include "deviceinfoservice.h"
#include "amazfishconfig.h"
#include "weather/currentweather.h"
#include "weather/city.h"

#include <QTimer>


#include <QDomDocument>
#include <QStringList>

const char* UUID_SERVICE_GARMIN_GFDI_V0 = "9b012401-bc30-ce9a-e111-0f67e491abde";
const char* UUID_CHARACTERISTIC_GARMIN_GFDI_V0_SEND = "df334c80-e6a7-d082-274a-78fc66f85e16";
const char* UUID_CHARACTERISTIC_GARMIN_GFDI_V0_RECEIVE = "4acbcd28-7425-868e-f447-915c8f00d0cb";

const char* UUID_SERVICE_GARMIN_GFDI_V1 = "6a4e2401-667b-11e3-949a-0800200c9a66";
const char* UUID_CHARACTERISTIC_GARMIN_GFDI_V1_SEND = "6a4e4c80-667b-11e3-949a-0800200c9a66";
const char* UUID_CHARACTERISTIC_GARMIN_GFDI_V1_RECEIVE = "6a4ecd28-667b-11e3-949a-0800200c9a66";

const char* UUID_SERVICE_GARMIN_ML_GFDI = "6a4e2800-667b-11e3-949a-0800200c9a66";

GarminDevice::GarminDevice(const QString &pairedName, QObject *parent) : AbstractDevice(pairedName, parent)
{
    qDebug() << Q_FUNC_INFO << pairedName;
    connect(this, &QBLEDevice::propertiesChanged, this, &GarminDevice::onPropertiesChanged, Qt::UniqueConnection);
    m_pairing= false;
}

Amazfish::Features GarminDevice::supportedFeatures() const
{

    //HRM and steps should be supported on all devices

    return  Amazfish::Feature::FEATURE_NONE
        | Amazfish::Feature::FEATURE_HRM
        | Amazfish::Feature::FEATURE_STEPS
        | Amazfish::Feature::FEATURE_ALERT
        ;
}


Amazfish::DataTypes GarminDevice::supportedDataTypes() const
{
    return Amazfish::DataType::TYPE_SPO2
            | Amazfish::DataType::TYPE_HRV
            | Amazfish::DataType::TYPE_HEART_RATE
            ;

}

QString GarminDevice::deviceType() const
{
    return "garmin";
}

AbstractFirmwareInfo *GarminDevice::firmwareInfo(const QByteArray &bytes, const QString &path)
{
    qDebug() << Q_FUNC_INFO;
    return nullptr;
}

void GarminDevice::sendAlert(const Amazfish::WatchNotification &notification)
{
    qDebug() << Q_FUNC_INFO << "notification from " << notification.appName;
    if (mNotificationHandler)
    {
        NotificationSpec note;
        note.body = notification.body;
        note.sourceName = notification.appName;
        note.title = notification.summary;
        // Configure notification type based on Appname (small list so far, needs to be extended);
        if (notification.appName=="Whisperfish")
            note.notificationType=NotificationType::GenericSocial;
        else if (notification.appName=="Signal")
            note.notificationType=NotificationType::GenericSocial;
        // It seems for emails we don't see the app name but the sender address. Bug or feature?
        else if (notification.appName.contains('@'))
            note.notificationType=NotificationType::GenericEmail;
        else if (notification.appName=="Calendar")
            note.notificationType=NotificationType::GenericCalendar;
        else note.notificationType = NotificationType::Generic;
        note.id=notification.id;
        mNotificationHandler->onNotification(note);
    }
    else qDebug() << Q_FUNC_INFO << "Garmin: No notification handler!";
}

static quint8 garminWeatherCondition(int code)
{
    if (code >= 200 && code < 300) return 6;  // thunderstorms
    if (code >= 300 && code < 500) return 16; // drizzle / light rain
    if (code >= 500 && code < 600) {
        if (code == 500) return 16; // light rain
        if (code >= 502) return 17; // heavy rain
        return 3; // rain
    }
    if (code >= 600 && code < 700) {
        if (code == 600) return 18; // light snow
        if (code >= 602) return 19; // heavy snow
        return 4; // snow
    }
    if (code >= 700 && code < 800) return 8; // fog / haze
    if (code == 800) return 0; // clear
    if (code == 801) return 1; // partly cloudy
    if (code == 802) return 2; // mostly cloudy
    if (code >= 803) return 22; // cloudy
    return 0;
}

void GarminDevice::sendWeather(CurrentWeather *weather)
{
    if (!weather || !mCommunicator) {
        qDebug() << Q_FUNC_INFO << "No weather or communicator";
        return;
    }

    qDebug() << Q_FUNC_INFO << *weather;

    const qlonglong unixNow = QDateTime::currentMSecsSinceEpoch() / 1000;
    const qlonglong weatherTime = weather->dateTime() > 0 ? weather->dateTime() : unixNow;
    const quint32 garminTimestamp = quint32(weatherTime + 631065600LL);

    const qreal latitude = weather->city() ? weather->city()->latitude() : 0.0;
    const qreal longitude = weather->city() ? weather->city()->longitude() : 0.0;
    const qint32 latitudeSemicircles = qRound(latitude * 2147483648.0 / 180.0);
    const qint32 longitudeSemicircles = qRound(longitude * 2147483648.0 / 180.0);

    // OpenWeather reports Kelvin; the FIT temperature fields are Celsius.
    const int temperature = qBound(-128, qRound(weather->temperature() - 273.15), 127);
    const int high = qBound(-128, qRound(weather->maxTemperature() - 273.15), 127);
    const int low = qBound(-128, qRound(weather->minTemperature() - 273.15), 127);
    const int windDirection = qBound(0, qRound(weather->windDeg()), 65535);
    const int windSpeed = qBound(0, qRound(weather->windSpeed() * 1000.0), 65535);
    const int humidity = qBound(0, weather->humidity(), 100);
    const quint8 weatherCondition = garminWeatherCondition(weather->weatherCode());
    const quint8 dayOfWeek = quint8(QDate::currentDate().dayOfWeek() % 7);

    // FIT definition for WEATHER_CONDITIONS (global message 128).
    QByteArray definition;
    definition.append(char(0x40)); // definition message, local type 0
    definition.append(char(0x00)); // reserved
    definition.append(char(0x00)); // little endian
    writeU16le(definition, 128);
    struct Field { quint8 number; quint8 size; quint8 base; };
    const Field fields[] = {
        {0, 1, 0x00},   // weather_report (enum)
        {1, 1, 0x01},   // temperature (sint8)
        {2, 1, 0x00},   // condition (enum)
        {3, 2, 0x04},   // wind_direction (uint16)
        {4, 2, 0x04},   // wind_speed (uint16, 1/1000 m/s)
        {5, 1, 0x02},   // precipitation_probability (uint8)
        {6, 1, 0x01},   // temperature_feels_like (sint8)
        {7, 1, 0x02},   // relative_humidity (uint8)
        {9, 4, 0x06},   // observed_at_time (uint32)
        {10, 4, 0x05},  // observed_location_lat (sint32)
        {11, 4, 0x05},  // observed_location_long (sint32)
        {12, 1, 0x00},  // day_of_week (enum)
        {13, 1, 0x01},  // high_temperature (sint8)
        {14, 1, 0x01},  // low_temperature (sint8)
        {17, 1, 0x00},  // air_quality (enum)
        {253, 4, 0x06}  // timestamp (uint32)
    };
    definition.append(char(sizeof(fields) / sizeof(Field)));
    for (const Field &field : fields) {
        definition.append(char(field.number));
        definition.append(char(field.size));
        definition.append(char(field.base));
    }

    // FIT data for the current conditions.
    QByteArray data;
    auto appendWeatherRecord = [&](quint8 report, int recordTemperature,
                                   int recordHigh, int recordLow) {
        data.append(char(0x00)); // data message, local type 0
        data.append(char(report));
        data.append(char(qint8(recordTemperature)));
        data.append(char(weatherCondition));
        writeU16le(data, quint16(windDirection));
        writeU16le(data, quint16(windSpeed));
        data.append(char(0x00)); // precipitation probability
        data.append(char(qint8(recordTemperature))); // feels like
        data.append(char(quint8(humidity)));
        writeU32le(data, garminTimestamp);
        writeU32le(data, quint32(latitudeSemicircles));
        writeU32le(data, quint32(longitudeSemicircles));
        data.append(char(dayOfWeek));
        data.append(char(qint8(recordHigh)));
        data.append(char(qint8(recordLow)));
        data.append(char(0x00)); // air quality = good
        writeU32le(data, garminTimestamp);
    };

    // The watch expects current + forecast records. Gadgetbridge sends
    // current, hourly and daily entries; the daily entry is what the
    // watch face uses for the main high/low temperature.
    appendWeatherRecord(0, temperature, high, low); // current
    appendWeatherRecord(1, temperature, high, low); // hourly
    appendWeatherRecord(2, high, high, low);        // daily (today)

    mCommunicator->sendMessage("WEATHER DEFINITION",
                               wrapInGfdiEnvelope(MessageId::FitDefinition, definition));

    const QByteArray dataMessage = wrapInGfdiEnvelope(MessageId::FitData, data);
    QTimer::singleShot(300, this, [this, dataMessage]() {
        if (mCommunicator) {
            mCommunicator->sendMessage("WEATHER DATA", dataMessage);
        }
    });
}

void GarminDevice::incomingCall(const QString &caller)
{
    qDebug() << Q_FUNC_INFO << caller;
    if (mNotificationHandler)
    {
        CallSpec call;
        call.name = caller;
        call.command = CallCommand::Incoming;
        mNotificationHandler->onSetCallState(call);
    }
    else qDebug() << Q_FUNC_INFO << "Garmin: No notification handler!";
}

void GarminDevice::incomingCallEnded()
{
    qDebug() << Q_FUNC_INFO;
    if (mNotificationHandler)
    {
        CallSpec call;
        call.command = CallCommand::End;
        mNotificationHandler->onSetCallState(call);
    }
    else qDebug() << Q_FUNC_INFO << "Garmin: No notification handler!";
}

void GarminDevice::setMusicStatus(bool playing, const QString &title, const QString &artist,
                                  const QString &album, int duration, int position)
{
    const bool changed = !m_hasMusicStatus
            || playing != m_musicPlaying
            || duration != m_musicDuration
            || title != m_musicTitle
            || artist != m_musicArtist
            || album != m_musicAlbum;

    m_hasMusicStatus = true;
    m_musicPlaying = playing;
    m_musicDuration = duration;
    m_musicPosition = position;
    m_musicTitle = title;
    m_musicArtist = artist;
    m_musicAlbum = album;

    if (!changed) {
        return;
    }

    sendMusicStatus();
}

void GarminDevice::setMusicPosition(int positionSeconds)
{
    if (positionSeconds == m_musicPosition) {
        return;
    }
    m_musicPosition = positionSeconds;

    if (!mCommunicator || !mCommunicator->handshakeComplete() || !m_hasMusicStatus) {
        return;
    }

    mCommunicator->sendMessage("MUSIC POSITION",
                               GarminMusicControlMessage::positionUpdate(
                                       m_musicPlaying, positionSeconds));
}

void GarminDevice::setMusicVolume(int volume)
{
    m_musicVolume = volume;

    if (!mCommunicator || !mCommunicator->handshakeComplete()) {
        return;
    }

    mCommunicator->sendMessage("MUSIC VOLUME",
                               GarminMusicControlMessage::volumeUpdate(volume));
}

void GarminDevice::sendMusicStatus()
{
    if (!mCommunicator || !mCommunicator->handshakeComplete() || !m_hasMusicStatus) {
        return;
    }

    GarminMusicControlMessage::TrackInfo info;
    info.title = m_musicTitle;
    info.artist = m_musicArtist;
    info.album = m_musicAlbum;
    info.durationSeconds = m_musicDuration;
    info.positionSeconds = m_musicPosition;
    info.playing = m_musicPlaying;
    info.volume = m_musicVolume;

    mCommunicator->sendMessage("MUSIC TRACK",
                               GarminMusicControlMessage::trackUpdate(info));
    mCommunicator->sendMessage("MUSIC STATE",
                               GarminMusicControlMessage::playbackUpdate(info));
}

void GarminDevice::onMusicControlCommand(quint8 command)
{
    const std::optional<GarminMusicControlMessage::Command> parsed =
            GarminMusicControlMessage::commandFromByte(command);
    if (!parsed.has_value()) {
        qDebug() << Q_FUNC_INFO << "Garmin: unsupported music command" << command;
        return;
    }

    switch (*parsed) {
    case GarminMusicControlMessage::TogglePlayPause:
        emit deviceEvent(AbstractDevice::EVENT_MUSIC_PLAYPAUSE);
        break;
    case GarminMusicControlMessage::SkipToNextItem:
        emit deviceEvent(AbstractDevice::EVENT_MUSIC_NEXT);
        break;
    case GarminMusicControlMessage::SkipToPreviousItem:
        emit deviceEvent(AbstractDevice::EVENT_MUSIC_PREV);
        break;
    case GarminMusicControlMessage::VolumeUp:
        emit deviceEvent(AbstractDevice::EVENT_MUSIC_VOLUP);
        break;
    case GarminMusicControlMessage::VolumeDown:
        emit deviceEvent(AbstractDevice::EVENT_MUSIC_VOLDOWN);
        break;
    case GarminMusicControlMessage::Play:
        emit deviceEvent(AbstractDevice::EVENT_MUSIC_PLAY);
        break;
    case GarminMusicControlMessage::Pause:
        emit deviceEvent(AbstractDevice::EVENT_MUSIC_PAUSE);
        break;
    default:
        qDebug() << Q_FUNC_INFO << "Garmin: music command not handled" << command;
        break;
    }
}

void GarminDevice::onMusicCapabilitiesRequested()
{
    qDebug() << Q_FUNC_INFO;

    // The watch just opened (or refreshed) its music widget: replay the last
    // known track, state and volume so it does not show stale data.
    sendMusicStatus();

    if (m_musicVolume >= 0 && mCommunicator && mCommunicator->handshakeComplete()) {
        mCommunicator->sendMessage("MUSIC VOLUME",
                                   GarminMusicControlMessage::volumeUpdate(m_musicVolume));
    }
}

void GarminDevice::onPropertiesChanged(QString interface, QVariantMap map, QStringList list)
{
    qDebug() << Q_FUNC_INFO << interface << map << list;

    if (interface == "org.bluez.Device1") {
        if (deviceProperty("ServicesResolved").toBool() ) {
            initialise();
            }
        if (map.contains("Connected")) {
            bool value = map["Connected"].toBool();
            CommunicatorV2 *svc = mCommunicator.data();
            if (!value) {
                setConnectionState("disconnected");
                if (svc)
                {
                    qDebug() << Q_FUNC_INFO << "Garmin: Communicator already exists, sending disconnect";
                    svc->onConnectionStateChange(false);
                }
            } else {
                setConnectionState("connected");
                if (svc)
                {
                    qDebug() << Q_FUNC_INFO << "Garmin: Communicator already exists, sending connect";
                    svc->onConnectionStateChange(true);
                }
            }
        }
    }
}
void GarminDevice::onAnswerCallEvent() {
    emit deviceEvent(AbstractDevice::EVENT_ANSWER_CALL);
}

void GarminDevice::onRejectCallEvent(){
    emit deviceEvent(AbstractDevice::EVENT_DECLINE_CALL);
}

QBLEService *GarminDevice::drv_createService(const QString &uuid, const QString &path)
{
    qDebug() << Q_FUNC_INFO << "Creating Communicator service for Garmin";
    // Only the preferred GFDI generation gets a CommunicatorV2.
    if (uuid != mPreferredService) {
        return nullptr;
    }

    QSharedPointer<CommunicatorV2> com = QSharedPointer<CommunicatorV2>::create(path, this);
    if (com) {
        mCommunicator = com;
        connect(com.data(), &CommunicatorV2::informationChanged, this, &GarminDevice::informationChanged, Qt::UniqueConnection);
        // add notification handler
        qDebug() << Q_FUNC_INFO << "Garmin: Adding notification handler";
        if (!mNotificationHandler)
            mNotificationHandler = QSharedPointer<GarminNotificationHandler>::create(mCommunicator);
        connect(com.data(), &CommunicatorV2::NotificationDataRequested, mNotificationHandler.data(), &GarminNotificationHandler::onNotificationDataRequested);
        connect(com.data(), &CommunicatorV2::NotificationPerformAction, mNotificationHandler.data(), &GarminNotificationHandler::onNotificationPerformAction);
        connect(mNotificationHandler.data(), &GarminNotificationHandler::acceptIncomingCall, this, &GarminDevice::onAnswerCallEvent);
        connect(mNotificationHandler.data(), &GarminNotificationHandler::rejectIncomingCall, this, &GarminDevice::onRejectCallEvent);

        // Phone music control (watch widget -> MPRIS, MPRIS -> watch widget)
        connect(com.data(), &CommunicatorV2::musicControlCommand,
                this, &GarminDevice::onMusicControlCommand);
        connect(com.data(), &CommunicatorV2::musicControlCapabilitiesRequested,
                this, &GarminDevice::onMusicCapabilitiesRequested);

        delete mFileTransfer;
        mFileTransfer = new GarminFileTransfer(com.data(), this);
        connect(com.data(), &CommunicatorV2::downloadStatusReceived,
                mFileTransfer, &GarminFileTransfer::onDownloadStatus);
        connect(com.data(), &CommunicatorV2::fileTransferDataReceived,
                mFileTransfer, &GarminFileTransfer::onFileTransferData);
        connect(com.data(), &CommunicatorV2::filterStatusReceived,
                mFileTransfer, &GarminFileTransfer::onFilterStatus);
        connect(com.data(), &CommunicatorV2::weatherRequested,
                this, &GarminDevice::weatherRequested);
        connect(mFileTransfer, &GarminFileTransfer::syncStarted,
                this, &GarminDevice::onSyncStarted);
        connect(mFileTransfer, &GarminFileTransfer::syncProgress,
                this, &GarminDevice::onSyncProgress);
        connect(mFileTransfer, &GarminFileTransfer::syncFinished,
                this, &GarminDevice::onSyncFinished);
        connect(mFileTransfer, &GarminFileTransfer::syncFailed,
                this, &GarminDevice::onSyncFailed);

        setConnectionState("authenticated");
        return com.data();
    }
    // if we are here, no Garmin device was detected or serice already created
    return nullptr;
}

void GarminDevice::initialise()
{
    qDebug() << Q_FUNC_INFO;
    // A watch can advertise more than one GFDI generation. Use the newest one available.
    // The last case is unconditional: if BlueZ reports no UUIDs, the V0 link is still the
    // safest guess.
    const QStringList advertised = deviceProperty("UUIDs").toStringList();
    if (advertised.contains(UUID_SERVICE_GARMIN_ML_GFDI)) {
        mPreferredService = UUID_SERVICE_GARMIN_ML_GFDI;
    } else if (advertised.contains(UUID_SERVICE_GARMIN_GFDI_V1)) {
        mPreferredService = UUID_SERVICE_GARMIN_GFDI_V1;
    } else {
        mPreferredService = UUID_SERVICE_GARMIN_GFDI_V0;
    }
    parseServices();
}


void GarminDevice::refreshInformation()
{

}

void GarminDevice::fetchData(Amazfish::DataTypes dataTypes)
{
    Q_UNUSED(dataTypes);
    if (!mFileTransfer) {
        qDebug() << Q_FUNC_INFO << "No Garmin file transfer available yet";
        return;
    }
    qDebug() << Q_FUNC_INFO << "Starting Garmin activity/sleep sync";
    mFileTransfer->startSync(false);
}

void GarminDevice::fetchFullData(Amazfish::DataTypes dataTypes)
{
    Q_UNUSED(dataTypes);
    if (!mFileTransfer) {
        qDebug() << Q_FUNC_INFO << "No Garmin file transfer available yet";
        return;
    }
    qDebug() << Q_FUNC_INFO << "Starting Garmin full history sync";
    mFileTransfer->startSync(true);
}

bool GarminDevice::operationRunning()
{
    return m_fileTransferRunning;
}

void GarminDevice::onSyncStarted(int totalFiles)
{
    m_fileTransferRunning = true;
    emit operationRunningChanged();
    emit downloadProgress(0);
    if (totalFiles > 0) {
        emit message(tr("Downloading %1 files from watch...").arg(totalFiles));
    } else {
        emit message(tr("Searching for files on watch..."));
    }
}

void GarminDevice::onSyncProgress(int done, int total)
{
    const int percent = total > 0 ? (done * 100) / total : 0;
    emit downloadProgress(percent);
    emit message(tr("Downloading %1/%2 files...").arg(done).arg(total));
}

void GarminDevice::onSyncFinished(int downloaded, int total, int activities)
{
    m_fileTransferRunning = false;
    emit operationRunningChanged();
    emit downloadProgress(100);
    if (total == 0) {
        emit message(tr("No new files on watch"));
    } else {
        emit message(tr("Sync complete: %1 activities, %2 files")
                     .arg(activities).arg(downloaded));
    }
}

void GarminDevice::onSyncFailed(const QString &reason)
{
    m_fileTransferRunning = false;
    emit operationRunningChanged();
    emit message(tr("Sync failed: %1").arg(reason));
}

QString GarminDevice::information(Amazfish::Info i) const
{

    if (!mCommunicator.data()) {
        qDebug() << Q_FUNC_INFO << "No communicator found!";
        return QString();
    }

    struct deviceInfo info=mCommunicator.data()->deviceInfo();
    switch(i) {
    case Amazfish::Info::INFO_SWVER:
        return info.softwareRevision;
        break;
    case Amazfish::Info::INFO_SERIAL:
        return info.serialNumber;
        break;
    case Amazfish::Info::INFO_BATTERY:
        return QString::number(mCommunicator.data()->batteryLevel());
        break;
    case Amazfish::Info::INFO_MODEL:
        return info.deviceModel;
        break;
    case Amazfish::Info::INFO_MANUFACTURER:
        return info.deviceManufacturer;
    case Amazfish::Info::INFO_STEPS:
        qDebug() << Q_FUNC_INFO << "Steps: " << mCommunicator.data()->steps();
        return QString::number(mCommunicator.data()->steps());
        break;
    case Amazfish::Info::INFO_HEARTRATE:
        qDebug() << Q_FUNC_INFO << "Heart rate: " << mCommunicator.data()->heartRate();
        return QString::number(mCommunicator.data()->heartRate());
        break;
    default:
        return QString("");
    }
    return QString();
}

void GarminDevice::authenticated(bool ready)
{
    qDebug() << Q_FUNC_INFO << ready;

    if (ready) {
        setConnectionState("authenticated");
    } else {
        setConnectionState("authfailed");
    }
}
