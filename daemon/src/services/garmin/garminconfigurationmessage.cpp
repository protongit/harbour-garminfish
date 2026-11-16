#include "garminconfigurationmessage.h"
#include "garmintypes.h"
#include "garminmusiccontrolmessage.h"


void GarminConfigurationMessage::parse(const QByteArray& data) {
    //QByteArray capabilities(15, char(0xFF));
    QByteArray capabilities=generateCapabilities();
    QByteArray payload;
    payload.append(char(capabilities.size()));
    payload.append(capabilities);

    QByteArray resp = wrapInGfdiEnvelope(MessageId::Configuration, payload);

    if (mCommunicator) {
        mCommunicator->sendMessage("CONFIGURATION RESPOSE",resp);
        mCommunicator->completePairing();

        // The watch tells us here what it supports. Some models only request
        // the music control commands on a fresh pairing, so announce them on
        // every connection; otherwise the music widget stays waiting.
        if (!data.isEmpty()) {
            const int length = qMin(int(quint8(data.at(0))), data.size() - 1);
            const QSet<quint16> watchCapabilities = parseCapabilities(data.mid(1, length));
            constexpr quint16 CAP_ADVANCED_MUSIC_CONTROLS = 7;
            constexpr quint16 CAP_BASIC_MUSIC_CONTROLS = 52;
            if (watchCapabilities.contains(CAP_ADVANCED_MUSIC_CONTROLS)
                    || watchCapabilities.contains(CAP_BASIC_MUSIC_CONTROLS)) {
                mCommunicator->sendMessage("MUSIC CAPABILITIES",
                        GarminMusicControlMessage::capabilitiesResponse(
                                quint16(MessageId::MusicControlCapabilities)));
                mCommunicator->onMusicControlCapabilities();
            }
        }
    }
}

QByteArray GarminConfigurationMessage::generateOutgoing(const QByteArray& data) {
    QByteArray r;

    // Generate our capabilities
    const QByteArray caps = generateCapabilities();
    // Number of capability bytes
    r.append(char(quint8(caps.size())));
    // Capability bytes
    r.append(caps);
    return r;
}


QSet<quint16> GarminConfigurationMessage::parseCapabilities(const QByteArray& bytes)
{
    QSet<quint16> caps;
    quint16 current = 0;
    for (auto ch : bytes) {
        const quint8 byte = quint8(ch);
        for (int i=0;i<8;i++) {
            if (byte & (1u << i)) caps.insert(current);
            current++;
        }
    }
    return caps;
}

QByteArray GarminConfigurationMessage::generateCapabilities() {
    QByteArray caps(15, char(0));

    // Set ALL capabilities (0-119) except the unknown/unsupported ones
    // This matches Gadgetbridge Java's OUR_CAPABILITIES behavior
    const int unsupported[] = {
        104,105,106,107,108,109,110,111,
        114,115,116,117,118,119
    };

    for (int cap = 0; cap < 120; ++cap) {
        bool skip = false;

        for (int u : unsupported) if (u == cap) { skip = true; break; }
        if (skip) continue;

        const int byteIdx = cap / 8;
        const int bitIdx = cap % 8;
        if (byteIdx < caps.size()) {
            caps[byteIdx] = char(quint8(caps[byteIdx]) | (1u << bitIdx));
        }
    }
    return caps;
}

QByteArray GarminConfigurationMessage::setDeviceSettings(bool autoUpload, bool weatherConditions, bool weatherAlerts)
{
    // Generate a SetDeviceSettings message
    //
    // Sends device settings to the watch (auto upload, weather, etc.)
    // Sent after Configuration exchange during initialization.
    //
    // # Arguments
    // * `autoUpload` - Enable auto upload of activities
    // * `weatherConditions` - Enable weather conditions
    // * `weatherAlerts` - Enable weather alerts
    QByteArray m;

    // Number of settings (always 3 for now)
    m.append(char(3));

    // Each setting consists of an ordinal/index, data length and status byte
    // Setting 1: AUTO_UPLOAD_ENABLED (index 6)
    m.append(char(6)); m.append(char(1)); m.append(char(autoUpload ? 1 : 0)); // GarminDeviceSetting.AUTO_UPLOAD_ENABLED ordinal
    // Setting 2: WEATHER_CONDITIONS_ENABLED (index 7)
    m.append(char(7)); m.append(char(1)); m.append(char(weatherConditions ? 1 : 0));
    // Setting 3: WEATHER_ALERTS_ENABLED (index 8)
    m.append(char(8)); m.append(char(1)); m.append(char(weatherAlerts ? 1 : 0));

    return m;
}
