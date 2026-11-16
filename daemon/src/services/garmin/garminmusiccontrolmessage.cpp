#include "garminmusiccontrolmessage.h"
#include "garmintypes.h"

namespace {

// Entity ids and ordinals as defined by the Garmin music control protocol.
constexpr quint8 ENTITY_PLAYER = 0;
constexpr quint8 ENTITY_TRACK = 2;

constexpr quint8 PLAYER_NAME = 0;
constexpr quint8 PLAYER_PLAYBACK_INFO = 1;
constexpr quint8 PLAYER_VOLUME = 2;

constexpr quint8 TRACK_ARTIST = 0;
constexpr quint8 TRACK_ALBUM = 1;
constexpr quint8 TRACK_TITLE = 2;
constexpr quint8 TRACK_DURATION = 3;

// One attribute is:
//   [length][entity][ordinal][0x00][UTF-8 value]
// where length counts the three header bytes plus the value.
void appendAttribute(QByteArray &payload, quint8 entity, quint8 ordinal, const QString &value)
{
    QByteArray bytes = value.toUtf8();
    constexpr int MAX_VALUE_BYTES = 252 - 3;
    if (bytes.size() > MAX_VALUE_BYTES) {
        // Truncate on a character boundary so the UTF-8 stays valid.
        QString truncated = value;
        while (truncated.toUtf8().size() > MAX_VALUE_BYTES) {
            truncated.chop(1);
        }
        bytes = truncated.toUtf8();
    }

    payload.append(char(quint8(bytes.size() + 3)));
    payload.append(char(entity));
    payload.append(char(ordinal));
    payload.append(char(0));
    payload.append(bytes);
}

QString playbackInfo(bool playing, int positionSeconds)
{
    // "<playing>,<play rate>,<progress in seconds>".
    return QStringLiteral("%1,%2,%3")
            .arg(playing ? 1 : 0)
            .arg(playing ? QStringLiteral("1.0") : QStringLiteral("0.0"))
            .arg(double(positionSeconds), 0, 'f', 3);
}

QString volumeFraction(int volume)
{
    return QString::number(qBound(0, volume, 100) / 100.0, 'f', 2);
}

} // namespace

QVector<quint8> GarminMusicControlMessage::supportedCommands()
{
    return QVector<quint8>()
            << TogglePlayPause
            << SkipToNextItem
            << SkipToPreviousItem
            << VolumeUp
            << VolumeDown
            << Play
            << Pause;
}

QByteArray GarminMusicControlMessage::trackUpdate(const TrackInfo &info)
{
    QByteArray payload;
    appendAttribute(payload, ENTITY_TRACK, TRACK_TITLE, info.title);
    appendAttribute(payload, ENTITY_TRACK, TRACK_ARTIST, info.artist);
    appendAttribute(payload, ENTITY_TRACK, TRACK_ALBUM, info.album);
    if (info.durationSeconds > 0) {
        appendAttribute(payload, ENTITY_TRACK, TRACK_DURATION,
                        QString::number(info.durationSeconds));
    }

    return wrapInGfdiEnvelope(MessageId::MusicControlEntityUpdate, payload);
}

QByteArray GarminMusicControlMessage::playbackUpdate(const TrackInfo &info)
{
    QByteArray payload;
    // The player name is what the watch shows as the source of the music.
    appendAttribute(payload, ENTITY_PLAYER, PLAYER_NAME, QStringLiteral("Phone"));
    appendAttribute(payload, ENTITY_PLAYER, PLAYER_PLAYBACK_INFO,
                    playbackInfo(info.playing, info.positionSeconds));
    if (info.volume >= 0) {
        appendAttribute(payload, ENTITY_PLAYER, PLAYER_VOLUME, volumeFraction(info.volume));
    }

    return wrapInGfdiEnvelope(MessageId::MusicControlEntityUpdate, payload);
}

QByteArray GarminMusicControlMessage::positionUpdate(bool playing, int positionSeconds)
{
    QByteArray payload;
    appendAttribute(payload, ENTITY_PLAYER, PLAYER_PLAYBACK_INFO,
                    playbackInfo(playing, positionSeconds));
    return wrapInGfdiEnvelope(MessageId::MusicControlEntityUpdate, payload);
}

QByteArray GarminMusicControlMessage::volumeUpdate(int volume)
{
    QByteArray payload;
    appendAttribute(payload, ENTITY_PLAYER, PLAYER_VOLUME, volumeFraction(volume));
    return wrapInGfdiEnvelope(MessageId::MusicControlEntityUpdate, payload);
}

QByteArray GarminMusicControlMessage::capabilitiesResponse(quint16 originalMessageId)
{
    QByteArray payload;
    writeU16le(payload, originalMessageId);
    payload.append(char(quint8(Status::Ack)));

    const QVector<quint8> commands = supportedCommands();
    payload.append(char(quint8(commands.size())));
    for (quint8 command : commands) {
        payload.append(char(command));
    }

    return wrapInGfdiEnvelope(MessageId::Response, payload);
}

std::optional<GarminMusicControlMessage::Command> GarminMusicControlMessage::commandFromByte(quint8 value)
{
    if (value <= quint8(SkipBackwards)) {
        return static_cast<Command>(value);
    }
    return std::nullopt;
}
