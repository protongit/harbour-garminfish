#ifndef GARMINMUSICCONTROLMESSAGE_H
#define GARMINMUSICCONTROLMESSAGE_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <optional>

/**
 * Garmin phone music control over GFDI.
 *
 * The watch sends MUSIC_CONTROL (5041) with one command byte and
 * MUSIC_CONTROL_CAPABILITIES (5042) to learn which commands the phone
 * supports. The phone answers the capabilities request with a Response
 * message listing the commands and pushes the current track/state with
 * MUSIC_CONTROL_ENTITY_UPDATE (5049).
 */
class GarminMusicControlMessage
{
public:
    enum Command : quint8 {
        TogglePlayPause = 0,
        SkipToNextItem = 1,
        SkipToPreviousItem = 2,
        VolumeUp = 3,
        VolumeDown = 4,
        Play = 5,
        Pause = 6,
        SkipForward = 7,
        SkipBackwards = 8
    };

    struct TrackInfo {
        QString title;
        QString artist;
        QString album;
        int durationSeconds = 0;
        int positionSeconds = 0;
        bool playing = false;
        int volume = -1; // 0..100, -1 when unknown
    };

    // Commands this port can honour. Skipping forward/backwards is not
    // offered because MPRIS exposes no seeking.
    static QVector<quint8> supportedCommands();

    // MUSIC_CONTROL_ENTITY_UPDATE (5049) messages, wrapped in the GFDI
    // envelope and ready to send.
    static QByteArray trackUpdate(const TrackInfo &info);
    static QByteArray playbackUpdate(const TrackInfo &info);
    static QByteArray positionUpdate(bool playing, int positionSeconds);
    static QByteArray volumeUpdate(int volume);

    // Response to MUSIC_CONTROL_CAPABILITIES (5042).
    static QByteArray capabilitiesResponse(quint16 originalMessageId);

    static std::optional<Command> commandFromByte(quint8 value);
};

#endif // GARMINMUSICCONTROLMESSAGE_H
