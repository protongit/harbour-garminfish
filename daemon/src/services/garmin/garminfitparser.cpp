#include "garminfitparser.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QDateTime>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QXmlStreamWriter>

#include <KDb3/KDbConnection>
#include <KDb3/KDbTableSchema>
#include <KDb3/KDbTransactionGuard>

#include "activitysummary.h"
#include "activitykind.h"

namespace {

constexpr quint32 GARMIN_EPOCH_OFFSET = 631065600u; // 1989-12-31T00:00:00Z
constexpr quint16 MSG_FILE_ID = 0;
constexpr quint16 MSG_SPORT = 12;
constexpr quint16 MSG_SESSION = 18;
constexpr quint16 MSG_LAP = 19;
constexpr quint16 MSG_RECORD = 20;
constexpr quint16 MSG_ACTIVITY = 34;
constexpr quint16 MSG_MONITORING = 55;
constexpr quint16 MSG_STRESS_LEVEL = 227;
constexpr quint16 MSG_SPO2 = 269;
constexpr quint16 MSG_HRV_SUMMARY = 370;
constexpr quint16 MSG_HRV_VALUE = 371;
constexpr quint16 MSG_SLEEP_LEVEL = 275;
constexpr quint16 MSG_SLEEP_STATS = 346;
constexpr quint16 MSG_SLEEP_DATA_INFO = 273;
constexpr quint16 MSG_EVENT = 21;

// Garmin marks the start and the end of a sleep window with a proprietary
// event. Only SLEEP files carry it, so it is ignored in every other file.
constexpr int EVENT_SLEEP = 74;
constexpr int EVENT_TYPE_START = 0;
constexpr int EVENT_TYPE_STOP = 1;
constexpr int FIT_FILE_TYPE_SLEEP = 49;

struct FieldDef {
    quint8 num = 0;
    quint8 size = 0;
    quint8 base = 0;
};

struct DeveloperFieldDef {
    quint8 num = 0;
    quint8 size = 0;
    quint8 developerIndex = 0;
};

struct MsgDef {
    quint16 global = 0;
    bool littleEndian = true;
    bool valid = false;
    QVector<FieldDef> fields;
    QVector<DeveloperFieldDef> developerFields;
};

struct MonitoringAggregate {
    int steps = 0;
    int heartRate = 0;
    int intensity = 0;
};

struct ActivityRecord {
    qint64 timestamp = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    double distance = 0.0;
    double speed = 0.0;
    int heartRate = 0;
    int cadence = 0;
    bool hasGps = false;
};

struct ActivitySession {
    bool valid = false;
    qint64 start = 0;
    qint64 end = 0;
    double elapsed = 0.0;
    double timer = 0.0;
    double distance = 0.0;
    double trainingEffect = 0.0;
    double anaerobicEffect = 0.0;
    double trainingLoad = 0.0;
    double trainingStressScore = 0.0;
    int calories = 0;
    int averageHeartRate = 0;
    int maxHeartRate = 0;
    int sport = 0;
    int subSport = 0;
    int laps = 0;
    int averageStress = 0;
    int sdrr = 0;
    int rmssd = 0;
    QString sportName;
    QString profileName;
};

struct ActivityLap {
    int index = 0;
    qint64 start = 0;
    double elapsed = 0.0;
    double timer = 0.0;
    double distance = 0.0;
    double averageSpeed = 0.0;
    double maxSpeed = 0.0;
    int averageHeartRate = 0;
    int maxHeartRate = 0;
    int calories = 0;
    int ascent = 0;
    int descent = 0;
};

struct HrvSummaryData {
    qint64 timestamp = 0;
    double weeklyAverage = 0.0;
    double lastNightAverage = 0.0;
    double lastNight5MinHigh = 0.0;
    double baselineLow = 0.0;
    double baselineBalancedLow = 0.0;
    double baselineBalancedHigh = 0.0;
    int status = 0;
};

struct SleepStatsData {
    qint64 timestamp = 0;
    int overallScore = 0;
    int qualityScore = 0;
    int recoveryScore = 0;
    double averageStress = 0.0;
};

static quint64 readUnsigned(const QByteArray &data, int offset, int size, bool littleEndian)
{
    if (size <= 0 || size > 8 || offset < 0 || offset + size > data.size()) {
        return 0;
    }

    quint64 value = 0;
    if (littleEndian) {
        for (int i = 0; i < size; ++i) {
            value |= (quint64(quint8(data.at(offset + i))) << (8 * i));
        }
    } else {
        for (int i = 0; i < size; ++i) {
            value = (value << 8) | quint64(quint8(data.at(offset + i)));
        }
    }
    return value;
}

static qint64 signedValue(quint64 raw, int size)
{
    if (size <= 0 || size > 8) {
        return 0;
    }
    const quint64 signBit = quint64(1) << (size * 8 - 1);
    if (raw & signBit) {
        return qint64(raw | (~quint64(0) << (size * 8)));
    }
    return qint64(raw);
}

static qint64 garminToUnix(qint64 garminTimestamp)
{
    return garminTimestamp > 0 ? garminTimestamp + qint64(GARMIN_EPOCH_OFFSET) : 0;
}

static QString cleanFitString(const QByteArray &raw)
{
    int end = raw.indexOf('\0');
    if (end < 0) {
        end = raw.size();
    }
    return QString::fromUtf8(raw.left(end)).trimmed();
}

static ActivityKind::Type garminActivityKind(int sport, int subSport, const QString &name)
{
    const QString lower = name.toLower();
    if (lower.contains("jump")) {
        return ActivityKind::JumpRope;
    }
    if (lower.contains("walk")) {
        return ActivityKind::Walking;
    }
    if (lower.contains("run")) {
        return ActivityKind::Running;
    }
    if (lower.contains("cycl") || lower.contains("bike")) {
        return ActivityKind::Cycling;
    }
    if (lower.contains("swim")) {
        return ActivityKind::Swimming;
    }
    if (lower.contains("strength")) {
        return ActivityKind::StrengthTraining;
    }
    if (lower.contains("yoga")) {
        return ActivityKind::Yoga;
    }
    if (lower.contains("hik")) {
        return ActivityKind::Hiking;
    }
    if (lower.contains("row")) {
        return ActivityKind::RowingMachine;
    }

    Q_UNUSED(subSport);
    switch (sport) {
    case 1: return ActivityKind::Running;
    case 2: return ActivityKind::Cycling;
    case 5: return ActivityKind::Swimming;
    case 11: return ActivityKind::Walking;
    case 15: return ActivityKind::RowingMachine;
    case 17: return ActivityKind::Hiking;
    default: return ActivityKind::Activity;
    }
}

static QString writeActivityGpx(const QList<ActivityRecord> &records,
                                const QString &name,
                                qint64 startTimestamp)
{
    if (records.isEmpty()) {
        return QString();
    }

    bool hasGps = false;
    for (const ActivityRecord &record : records) {
        if (record.hasGps) {
            hasGps = true;
            break;
        }
    }

    const QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + "/garmin/GPX";
    QDir().mkpath(dirPath);

    QString baseName = name;
    baseName.replace(QRegularExpression("[^A-Za-z0-9._-]"), "_");
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("activity");
    }
    const QString path = QString("%1/%2_%3.gpx")
            .arg(dirPath)
            .arg(startTimestamp)
            .arg(baseName.left(40));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "GarminFIT: cannot write GPX" << path;
        return QString();
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("gpx");
    xml.writeAttribute("version", "1.1");
    xml.writeAttribute("creator", "Amazfish");
    xml.writeAttribute("xmlns", "http://www.topografix.com/GPX/1/1");
    xml.writeStartElement("trk");
    xml.writeTextElement("name", name);
    xml.writeStartElement("trkseg");
    for (const ActivityRecord &record : records) {
        if (hasGps && !record.hasGps) {
            continue;
        }
        xml.writeStartElement("trkpt");
        xml.writeAttribute("lat", QString::number(record.latitude, 'f', 7));
        xml.writeAttribute("lon", QString::number(record.longitude, 'f', 7));
        if (record.altitude != 0.0) {
            xml.writeTextElement("ele", QString::number(record.altitude, 'f', 1));
        }
        if (record.timestamp > 0) {
            xml.writeTextElement("time",
                    QDateTime::fromMSecsSinceEpoch(record.timestamp * 1000LL)
                    .toString(Qt::ISODate));
        }
        if (record.heartRate > 0 || record.cadence > 0) {
            xml.writeStartElement("extensions");
            xml.writeStartElement("TrackPointExtension");
            if (record.heartRate > 0) {
                xml.writeTextElement("hr", QString::number(record.heartRate));
            }
            if (record.cadence > 0) {
                xml.writeTextElement("cad", QString::number(record.cadence));
            }
            xml.writeEndElement(); // TrackPointExtension
            xml.writeEndElement(); // extensions
        }
        xml.writeEndElement(); // trkpt
    }
    xml.writeEndElement(); // trkseg
    xml.writeEndElement(); // trk
    xml.writeEndElement(); // gpx
    xml.writeEndDocument();
    file.close();

    return path;
}

static int sleepLevelToRawKind(quint64 level)
{
    // Raw kinds expected by ui/src/datasource.cpp (calculateSleep):
    //   9  = light sleep, 11 = deep sleep, anything else = awake/unknown.
    // REM is counted as light sleep so it contributes to the night total.
    switch (level) {
    case 2: // LIGHT
    case 4: // REM
        return 9;
    case 3: // DEEP
        return 11;
    case 1: // AWAKE
    case 0: // UNMEASURABLE
    default:
        return 1; // Activity / not sleep
    }
}

static QString fitTypeName(const QString &sourceType)
{
    return sourceType.isEmpty() ? QStringLiteral("FIT") : sourceType;
}

static bool insertSample(KDbConnection *db,
                         KDbFieldList *fields,
                         qint64 timestamp,
                         uint deviceId,
                         uint userId,
                         int intensity,
                         int steps,
                         int rawKind,
                         int heartRate)
{
    if (!db || !fields) {
        return false;
    }

    // Avoid duplicates when the same FIT file is downloaded again, while keeping
    // different sample kinds (activity vs sleep) at the same timestamp.
    if (!db->executeSql(KDbEscapedString(
            "DELETE FROM mi_band_activity WHERE timestamp = %1 AND device_id = %2 AND raw_kind = %3")
                            .arg(timestamp)
                            .arg(deviceId)
                            .arg(rawKind))) {
        qWarning() << "GarminFIT: failed to delete old sample" << timestamp << rawKind;
    }

    const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(timestamp * 1000LL);

    QList<QVariant> values;
    values << timestamp;
    values << dateTime;
    values << deviceId;
    values << userId;
    values << intensity;
    values << steps;
    values << rawKind;
    values << heartRate;

    if (!db->insertRecord(fields, values)) {
        qWarning() << "GarminFIT: failed to insert sample" << timestamp << rawKind;
        return false;
    }
    return true;
}

} // namespace

int GarminFitParser::importFile(const QByteArray &data,
                                KDbConnection *db,
                                uint deviceId,
                                uint userId,
                                const QString &sourceType)
{
    if (data.size() < 12) {
        qWarning() << "GarminFIT: file too small" << sourceType << data.size();
        return 0;
    }

    const int headerSize = quint8(data.at(0));
    if (headerSize < 12 || headerSize > data.size()) {
        qWarning() << "GarminFIT: invalid header size" << headerSize << sourceType;
        return 0;
    }
    if (data.mid(8, 4) != QByteArray(".FIT")) {
        qWarning() << "GarminFIT: not a FIT file" << sourceType;
        return 0;
    }

    const quint32 dataSize = quint32(readUnsigned(data, 4, 4, true));
    int position = headerSize;
    const int end = qMin<int>(data.size(), int(headerSize + dataSize));

    qDebug() << "GarminFIT: parsing" << sourceType << "size" << data.size()
             << "header" << headerSize << "data" << dataSize;

    QHash<quint8, MsgDef> definitions;
    QMap<qint64, MonitoringAggregate> monitoring;
    QMap<qint64, QMap<int, int>> cumulative; // timestamp -> activity type -> cumulative cycles
    QMap<qint64, QPair<int, int>> sleepLevels; // timestamp -> (FIT sleep level, legacy raw kind)
    QMap<qint64, int> stressValues; // timestamp -> stress level
    QMap<qint64, int> spo2Values; // timestamp -> SpO2 value
    QMap<qint64, int> bodyEnergyValues; // timestamp -> body energy
    QMap<qint64, int> hrvValues; // timestamp -> HRV value (ms)
    QMap<qint64, HrvSummaryData> hrvSummaries;
    QMap<qint64, SleepStatsData> sleepStats;
    QMap<qint64, qint64> sleepWindows; // sleep window start -> end (Unix time)
    qint64 pendingSleepStart = 0;
    QList<ActivityRecord> activityRecords;
    QList<ActivityLap> activityLaps;
    ActivitySession activitySession;
    QString activitySportName;
    int fileType = -1;
    qint64 lastMonitoringTimestamp = 0;
    qint64 referenceTimestamp = 0;

    while (position < end) {
        const quint8 header = quint8(data.at(position++));
        const bool compressedTimestamp = (header & 0x80) != 0;
        const bool definitionMessage = !compressedTimestamp && (header & 0x40) != 0;
        const quint8 localMessageType = compressedTimestamp
                ? quint8((header >> 5) & 0x03)
                : quint8(header & 0x0f);

        if (compressedTimestamp) {
            const quint8 timeOffset = quint8(header & 0x1f);
            if (referenceTimestamp > 0) {
                const qint64 base = referenceTimestamp & ~qint64(0x1f);
                if (timeOffset >= (referenceTimestamp & 0x1f)) {
                    referenceTimestamp = base + timeOffset;
                } else {
                    referenceTimestamp = base + timeOffset + 0x20;
                }
            }
        }

        if (definitionMessage) {
            if (position + 5 > end) {
                qWarning() << "GarminFIT: truncated definition" << sourceType;
                break;
            }
            ++position; // reserved
            const quint8 architecture = quint8(data.at(position++));
            const bool littleEndian = (architecture != 1);
            const quint16 globalMessage = quint16(readUnsigned(data, position, 2, littleEndian));
            position += 2;
            const quint8 fieldCount = quint8(data.at(position++));

            MsgDef def;
            def.global = globalMessage;
            def.littleEndian = littleEndian;
            def.valid = true;

            for (int i = 0; i < fieldCount; ++i) {
                if (position + 3 > end) {
                    def.valid = false;
                    break;
                }
                FieldDef field;
                field.num = quint8(data.at(position++));
                field.size = quint8(data.at(position++));
                field.base = quint8(data.at(position++));
                def.fields.append(field);
            }

            if (def.valid && (header & 0x20) != 0) {
                // Developer fields carry their own size in the definition. We do
                // not need their values, but we must skip their bytes while
                // decoding subsequent records.
                if (position < end) {
                    const quint8 devFieldCount = quint8(data.at(position++));
                    for (int i = 0; i < devFieldCount; ++i) {
                        if (position + 3 > end) {
                            def.valid = false;
                            break;
                        }
                        DeveloperFieldDef devField;
                        devField.num = quint8(data.at(position++));
                        devField.size = quint8(data.at(position++));
                        devField.developerIndex = quint8(data.at(position++));
                        def.developerFields.append(devField);
                    }
                }
            }

            if (def.valid) {
                definitions.insert(localMessageType, def);
            }
            continue;
        }

        if (!definitions.contains(localMessageType)) {
            qWarning() << "GarminFIT: data message without definition" << int(localMessageType);
            break;
        }

        const MsgDef def = definitions.value(localMessageType);
        QHash<quint8, quint64> values;
        QHash<quint8, QByteArray> stringValues;
        QSet<quint8> present;

        for (const FieldDef &field : def.fields) {
            if (position + field.size > end) {
                break;
            }
            if (field.base == 0x07
                    && (def.global == MSG_SPORT || def.global == MSG_SESSION)) {
                stringValues.insert(field.num, data.mid(position, field.size));
            }
            values.insert(field.num, readUnsigned(data, position, field.size, def.littleEndian));
            present.insert(field.num);
            position += field.size;
        }

        for (const DeveloperFieldDef &devField : def.developerFields) {
            position += devField.size;
        }

        if (def.global == MSG_FILE_ID) {
            if (present.contains(0)) {
                fileType = int(values.value(0));
            }
            continue;
        }

        if (def.global == MSG_SPORT) {
            if (stringValues.contains(3)) {
                activitySportName = cleanFitString(stringValues.value(3));
            }
            continue;
        }

        if (def.global == MSG_SESSION) {
            activitySession.valid = true;
            if (present.contains(2)) {
                activitySession.start = garminToUnix(qint64(values.value(2)));
            }
            if (present.contains(253)) {
                activitySession.end = garminToUnix(qint64(values.value(253)));
            }
            if (present.contains(7)) {
                activitySession.elapsed = values.value(7) / 1000.0;
            }
            if (present.contains(8)) {
                activitySession.timer = values.value(8) / 1000.0;
            }
            if (present.contains(9) && quint32(values.value(9)) != 0xffffffffu) {
                activitySession.distance = values.value(9) / 100.0;
            }
            if (present.contains(11) && quint16(values.value(11)) != 0xffffu) {
                activitySession.calories = int(values.value(11));
            }
            if (present.contains(16) && quint8(values.value(16)) != 0xffu) {
                activitySession.averageHeartRate = int(values.value(16));
            }
            if (present.contains(17) && quint8(values.value(17)) != 0xffu) {
                activitySession.maxHeartRate = int(values.value(17));
            }
            if (present.contains(5)) {
                activitySession.sport = int(values.value(5));
            }
            if (present.contains(6)) {
                activitySession.subSport = int(values.value(6));
            }
            if (present.contains(26) && quint16(values.value(26)) != 0xffffu) {
                activitySession.laps = int(values.value(26));
            }
            if (present.contains(24) && quint8(values.value(24)) != 0xffu) {
                activitySession.trainingEffect = values.value(24) / 10.0;
            }
            if (present.contains(137) && quint8(values.value(137)) != 0xffu) {
                activitySession.anaerobicEffect = values.value(137) / 10.0;
            }
            if (present.contains(168)) {
                const qint64 rawLoad = signedValue(values.value(168), 4);
                if (rawLoad != 0x7fffffffLL) {
                    activitySession.trainingLoad = rawLoad / 65536.0;
                }
            }
            if (present.contains(35) && quint16(values.value(35)) != 0xffffu) {
                activitySession.trainingStressScore = values.value(35) / 10.0;
            }
            if (present.contains(195) && quint8(values.value(195)) != 0xffu) {
                activitySession.averageStress = int(values.value(195));
            }
            if (present.contains(197) && quint8(values.value(197)) != 0xffu) {
                activitySession.sdrr = int(values.value(197));
            }
            if (present.contains(198) && quint8(values.value(198)) != 0xffu) {
                activitySession.rmssd = int(values.value(198));
            }
            if (stringValues.contains(110)) {
                activitySession.profileName = cleanFitString(stringValues.value(110));
            }
            continue;
        }

        if (def.global == MSG_LAP) {
            ActivityLap lap;
            lap.index = activityLaps.size();
            if (present.contains(2)) {
                lap.start = garminToUnix(qint64(values.value(2)));
            }
            if (present.contains(7) && quint32(values.value(7)) != 0xffffffffu) {
                lap.elapsed = values.value(7) / 1000.0;
            }
            if (present.contains(8) && quint32(values.value(8)) != 0xffffffffu) {
                lap.timer = values.value(8) / 1000.0;
            }
            if (present.contains(9) && quint32(values.value(9)) != 0xffffffffu) {
                lap.distance = values.value(9) / 100.0;
            }
            if (present.contains(14) && quint16(values.value(14)) != 0xffffu) {
                lap.averageSpeed = values.value(14) / 1000.0;
            }
            if (present.contains(15) && quint16(values.value(15)) != 0xffffu) {
                lap.maxSpeed = values.value(15) / 1000.0;
            }
            if (present.contains(16) && quint8(values.value(16)) != 0xffu) {
                lap.averageHeartRate = int(values.value(16));
            }
            if (present.contains(17) && quint8(values.value(17)) != 0xffu) {
                lap.maxHeartRate = int(values.value(17));
            }
            if (present.contains(11) && quint16(values.value(11)) != 0xffffu) {
                lap.calories = int(values.value(11));
            }
            if (present.contains(22) && quint16(values.value(22)) != 0xffffu) {
                lap.ascent = int(values.value(22));
            }
            if (present.contains(23) && quint16(values.value(23)) != 0xffffu) {
                lap.descent = int(values.value(23));
            }
            activityLaps.append(lap);
            continue;
        }

        if (def.global == MSG_RECORD) {
            if (!present.contains(253)) {
                continue;
            }
            ActivityRecord record;
            record.timestamp = garminToUnix(qint64(values.value(253)));
            if (present.contains(0) && present.contains(1)) {
                const quint32 rawLat = quint32(values.value(0));
                const quint32 rawLon = quint32(values.value(1));
                if (rawLat != 0x7fffffffu && rawLon != 0x7fffffffu) {
                    const qint32 lat = qint32(signedValue(rawLat, 4));
                    const qint32 lon = qint32(signedValue(rawLon, 4));
                    record.latitude = lat * (180.0 / 2147483648.0);
                    record.longitude = lon * (180.0 / 2147483648.0);
                    record.hasGps = (lat != 0 || lon != 0);
                }
            }
            if (present.contains(2) && quint16(values.value(2)) != 0xffffu) {
                record.altitude = values.value(2) / 5.0 - 500.0;
            }
            if (present.contains(3) && quint8(values.value(3)) != 0xffu) {
                record.heartRate = int(values.value(3));
            }
            if (present.contains(4) && quint8(values.value(4)) != 0xffu) {
                record.cadence = int(values.value(4));
            }
            if (present.contains(5) && quint32(values.value(5)) != 0xffffffffu) {
                record.distance = values.value(5) / 100.0;
            }
            if (present.contains(6) && quint16(values.value(6)) != 0xffffu) {
                record.speed = values.value(6) / 1000.0;
            }
            if (record.timestamp > 0) {
                activityRecords.append(record);
            }
            continue;
        }

        if (def.global == MSG_ACTIVITY) {
            if (activitySession.end == 0 && present.contains(253)) {
                activitySession.end = garminToUnix(qint64(values.value(253)));
            }
            continue;
        }

        if (def.global == MSG_MONITORING) {
            qint64 timestamp = -1;
            if (present.contains(253)) {
                timestamp = qint64(values.value(253));
                referenceTimestamp = timestamp;
            } else if (present.contains(26) && lastMonitoringTimestamp > 0) {
                const int timestamp16 = int(values.value(26)) & 0xffff;
                const qint64 garminReference = lastMonitoringTimestamp;
                int difference = timestamp16 - int(garminReference & 0xffff);
                if (difference < -32768) {
                    difference += 65536;
                } else if (difference > 32768) {
                    difference -= 65536;
                }
                timestamp = lastMonitoringTimestamp + difference;
            } else if (referenceTimestamp > 0) {
                timestamp = referenceTimestamp;
            }

            if (timestamp <= 0) {
                continue;
            }

            int cycles = -1;
            if (present.contains(3)) {
                cycles = int(values.value(3));
            } else if (present.contains(9)) {
                cycles = int(values.value(9));
            }

            const int activityType = present.contains(5)
                    ? int(values.value(5))
                    : (present.contains(24)
                       ? int(values.value(24) & 0x1f)
                       : 0);

            const int heartRate = present.contains(27) ? int(values.value(27)) : 0;
            int intensity = 0;
            if (present.contains(28)) {
                intensity = int(values.value(28)) / 10;
            } else if (present.contains(24)) {
                intensity = int((values.value(24) >> 5) & 0x07);
            }

            lastMonitoringTimestamp = timestamp;

            if (cycles < 0 && heartRate == 0 && intensity == 0) {
                continue;
            }

            const qint64 unixTimestamp = timestamp + GARMIN_EPOCH_OFFSET;
            if (cycles >= 0) {
                // Garmin reports cumulative step counters. Keep them in their
                // own table and derive per-minute deltas for mi_band_activity.
                cumulative[unixTimestamp][activityType] = cycles;
            }

            MonitoringAggregate &aggregate = monitoring[unixTimestamp];
            aggregate.steps = 0;
            if (heartRate > 0) {
                aggregate.heartRate = heartRate;
            }
            if (intensity > aggregate.intensity) {
                aggregate.intensity = intensity;
            }
            continue;
        }

        if (def.global == MSG_EVENT) {
            // The onset and the wake time of the night are not part of the
            // public FIT profile: Garmin writes them as proprietary event 74.
            if (fileType != FIT_FILE_TYPE_SLEEP || !present.contains(0)
                    || !present.contains(1) || !present.contains(253)) {
                continue;
            }
            if (int(values.value(0)) != EVENT_SLEEP) {
                continue;
            }
            const qint64 timestamp = qint64(values.value(253));
            if (timestamp <= 0) {
                continue;
            }
            const int eventType = int(values.value(1));
            if (eventType == EVENT_TYPE_START) {
                pendingSleepStart = timestamp;
            } else if (eventType == EVENT_TYPE_STOP
                       && pendingSleepStart > 0 && timestamp > pendingSleepStart) {
                sleepWindows.insert(pendingSleepStart + GARMIN_EPOCH_OFFSET,
                                    timestamp + GARMIN_EPOCH_OFFSET);
                pendingSleepStart = 0;
            }
            continue;
        }

        if (def.global == MSG_SLEEP_LEVEL) {
            if (!present.contains(253)) {
                continue;
            }
            const qint64 timestamp = qint64(values.value(253));
            if (timestamp <= 0) {
                continue;
            }
            const int level = int(values.contains(0) ? values.value(0) : 0);
            sleepLevels.insert(timestamp + GARMIN_EPOCH_OFFSET,
                               qMakePair(level, sleepLevelToRawKind(level)));
            referenceTimestamp = timestamp;
            continue;
        }

        if (def.global == MSG_SLEEP_DATA_INFO) {
            if (present.contains(253)) {
                referenceTimestamp = qint64(values.value(253));
            } else if (present.contains(2)) {
                referenceTimestamp = qint64(values.value(2));
            }
            continue;
        }

        if (def.global == MSG_SLEEP_STATS) {
            const qint64 garminTimestamp = present.contains(253)
                    ? qint64(values.value(253))
                    : referenceTimestamp;
            const qint64 timestamp = garminToUnix(garminTimestamp);
            if (timestamp <= 0) {
                continue;
            }
            SleepStatsData stats;
            stats.timestamp = timestamp;
            if (present.contains(6)) {
                stats.overallScore = int(values.value(6));
            }
            if (present.contains(7)) {
                stats.qualityScore = int(values.value(7));
            }
            if (present.contains(8)) {
                stats.recoveryScore = int(values.value(8));
            }
            if (present.contains(15)) {
                stats.averageStress = values.value(15) / 100.0;
            }
            sleepStats.insert(timestamp, stats);
            continue;
        }

        if (def.global == MSG_SPO2) {
            if (!present.contains(253)) {
                continue;
            }
            const qint64 timestamp = garminToUnix(qint64(values.value(253)));
            const int reading = int(values.value(0));
            if (timestamp <= 0 || reading <= 0 || reading > 100) {
                continue;
            }
            spo2Values.insert(timestamp, reading);
            continue;
        }

        if (def.global == MSG_STRESS_LEVEL) {
            const qint64 garminTimestamp = present.contains(1)
                    ? qint64(values.value(1))
                    : referenceTimestamp;
            const qint64 unixTimestamp = garminToUnix(garminTimestamp);
            if (garminTimestamp > 0 && present.contains(3)) {
                const int energy = int(signedValue(values.value(3), 1));
                if (energy >= 0 && energy <= 100) {
                    bodyEnergyValues.insert(unixTimestamp, energy);
                }
            }
            const qint32 stress = qint32(signedValue(values.value(0), 2));
            if (garminTimestamp <= 0 || stress < 0 || stress > 100) {
                continue;
            }
            stressValues.insert(unixTimestamp, int(stress));
            referenceTimestamp = garminTimestamp;
            continue;
        }

        if (def.global == MSG_HRV_VALUE) {
            if (!present.contains(253)) {
                continue;
            }
            const quint16 rawValue = quint16(values.value(0));
            if (rawValue == 0xffffu) {
                continue;
            }
            const qint64 timestamp = garminToUnix(qint64(values.value(253)));
            if (timestamp <= 0) {
                continue;
            }
            hrvValues.insert(timestamp, qRound(rawValue / 128.0));
            continue;
        }

        if (def.global == MSG_HRV_SUMMARY) {
            if (!present.contains(253)) {
                continue;
            }
            HrvSummaryData summary;
            summary.timestamp = garminToUnix(qint64(values.value(253)));
            summary.weeklyAverage = values.value(0) / 128.0;
            summary.lastNightAverage = values.value(1) / 128.0;
            summary.lastNight5MinHigh = values.value(2) / 128.0;
            summary.baselineLow = values.value(3) / 128.0;
            summary.baselineBalancedLow = values.value(4) / 128.0;
            summary.baselineBalancedHigh = values.value(5) / 128.0;
            summary.status = present.contains(6) ? int(values.value(6)) : 0;
            if (summary.timestamp > 0) {
                hrvSummaries.insert(summary.timestamp, summary);
            }
            continue;
        }
    }

    qDebug() << "GarminFIT:" << fitTypeName(sourceType)
             << "activity samples" << monitoring.size()
             << "sleep samples" << sleepLevels.size()
             << "stress samples" << stressValues.size()
             << "hrv samples" << hrvValues.size();

    if (!db || !db->isDatabaseUsed()) {
        qWarning() << "GarminFIT: no database, samples not stored";
        return 0;
    }

    int inserted = 0;

    if ((fileType == 4 || activitySession.valid)
            && db->containsTable("sports_data") == true
            && db->containsTable("sports_meta") == true) {
        QString name = activitySportName;
        if (name.isEmpty()) {
            name = activitySession.profileName;
        }
        if (name.isEmpty()) {
            name = QStringLiteral("Garmin Activity");
        }

        qint64 start = activitySession.start;
        qint64 end = activitySession.end;
        if (start == 0 && !activityRecords.isEmpty()) {
            start = activityRecords.first().timestamp;
        }
        if (end == 0 && !activityRecords.isEmpty()) {
            end = activityRecords.last().timestamp;
        }
        if (end <= start) {
            const double duration = activitySession.timer > 0
                    ? activitySession.timer : activitySession.elapsed;
            end = start + qMax<qint64>(1, qint64(qRound(duration)));
        }

        if (start > 0) {
            const QDateTime startTime = QDateTime::fromMSecsSinceEpoch(start * 1000LL);
            const QDateTime endTime = QDateTime::fromMSecsSinceEpoch(end * 1000LL);
            const QString gpxPath = writeActivityGpx(activityRecords, name, start);

            double baseLat = 0.0;
            double baseLon = 0.0;
            double baseAlt = 0.0;
            for (const ActivityRecord &record : activityRecords) {
                if (record.hasGps) {
                    baseLat = record.latitude;
                    baseLon = record.longitude;
                    baseAlt = record.altitude;
                    break;
                }
            }

            db->executeSql(KDbEscapedString(
                    "DELETE FROM sports_meta WHERE sport_id IN ("
                    "SELECT id FROM sports_data WHERE device_id = %1 AND start_timestamp = %2)")
                           .arg(deviceId).arg(start));
            db->executeSql(KDbEscapedString(
                    "DELETE FROM sports_data WHERE device_id = %1 AND start_timestamp = %2")
                           .arg(deviceId).arg(start));

            ActivitySummary summary;
            summary.setVersion(1);
            summary.setName(name);
            summary.setStartTime(startTime);
            summary.setEndTime(endTime);
            summary.setActivityKind(garminActivityKind(activitySession.sport,
                                                       activitySession.subSport,
                                                       name));
            summary.setBaseLatitude(baseLat);
            summary.setBaseLongitude(baseLon);
            summary.setBaseAltitude(qRound(baseAlt));
            summary.setDeviceId(deviceId);
            summary.setProfileId(userId);
            summary.setGPX(gpxPath);

            const double duration = activitySession.timer > 0
                    ? activitySession.timer : activitySession.elapsed;
            summary.addMetaData("totalDuration",
                                QString::number(qRound(duration)), "seconds");
            if (activitySession.distance > 0) {
                summary.addMetaData("distanceMeters",
                                    QString::number(activitySession.distance, 'f', 1),
                                    "meters");
            }
            if (activitySession.calories > 0) {
                summary.addMetaData("calories",
                                    QString::number(activitySession.calories), "calories");
            }
            if (activitySession.averageHeartRate > 0) {
                summary.addMetaData("averageHeartRate",
                                    QString::number(activitySession.averageHeartRate), "bpm");
            }
            if (activitySession.maxHeartRate > 0) {
                summary.addMetaData("maxHeartRate",
                                    QString::number(activitySession.maxHeartRate), "bpm");
            }
            if (activitySession.laps > 0) {
                summary.addMetaData("laps",
                                    QString::number(activitySession.laps), "laps");
            }
            if (activitySession.trainingEffect > 0) {
                summary.addMetaData("aerobicTrainingEffect",
                                    QString::number(activitySession.trainingEffect, 'f', 1), "");
            }
            if (activitySession.anaerobicEffect > 0) {
                summary.addMetaData("anaerobicTrainingEffect",
                                    QString::number(activitySession.anaerobicEffect, 'f', 1), "");
            }
            if (activitySession.trainingLoad > 0) {
                summary.addMetaData("currentWorkoutLoad",
                                    QString::number(activitySession.trainingLoad, 'f', 1), "");
            }
            if (activitySession.trainingStressScore > 0) {
                summary.addMetaData("trainingStressScore",
                                    QString::number(activitySession.trainingStressScore, 'f', 1), "");
            }
            if (activitySession.averageStress > 0) {
                summary.addMetaData("avgStress",
                                    QString::number(activitySession.averageStress), "");
            }
            if (activitySession.sdrr > 0) {
                summary.addMetaData("hrvSdrr",
                                    QString::number(activitySession.sdrr), "ms");
            }
            if (activitySession.rmssd > 0) {
                summary.addMetaData("hrvRmssd",
                                    QString::number(activitySession.rmssd), "ms");
            }

            if (!summary.saveToDatabase(db)) {
                qWarning() << "GarminFIT: failed to save activity" << name << start;
            } else {
                ++inserted;
                qDebug() << "GarminFIT: saved activity" << name
                         << startTime.toString(Qt::ISODate)
                         << "load" << activitySession.trainingLoad
                         << "TE" << activitySession.trainingEffect
                         << "anaTE" << activitySession.anaerobicEffect;

                const uint activityId = summary.id();
                QList<ActivityLap> laps = activityLaps;
                if (laps.isEmpty() && activitySession.valid) {
                    ActivityLap lap;
                    lap.index = 0;
                    lap.start = start;
                    lap.elapsed = activitySession.elapsed;
                    lap.timer = activitySession.timer;
                    lap.distance = activitySession.distance;
                    lap.averageHeartRate = activitySession.averageHeartRate;
                    lap.maxHeartRate = activitySession.maxHeartRate;
                    lap.calories = activitySession.calories;
                    laps.append(lap);
                }
                if (activityId > 0 && !laps.isEmpty()) {
                    if (!db->executeSql(KDbEscapedString(
                            "CREATE TABLE IF NOT EXISTS garmin_activity_laps ("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                            "sport_id INTEGER, lap_index INTEGER, start_timestamp INTEGER, "
                            "timestamp_dt DATETIME, elapsed REAL, timer REAL, distance REAL, "
                            "average_speed REAL, max_speed REAL, average_heart_rate INTEGER, "
                            "max_heart_rate INTEGER, calories INTEGER, ascent INTEGER, "
                            "descent INTEGER)"))) {
                        qWarning() << "GarminFIT: cannot create garmin_activity_laps table";
                    }
                    db->executeSql(KDbEscapedString(
                            "DELETE FROM garmin_activity_laps WHERE sport_id = %1")
                                   .arg(activityId));
                    for (const ActivityLap &lap : laps) {
                        const QDateTime lapTime = QDateTime::fromMSecsSinceEpoch(lap.start * 1000LL);
                        db->executeSql(KDbEscapedString(
                                "INSERT INTO garmin_activity_laps "
                                "(sport_id, lap_index, start_timestamp, timestamp_dt, "
                                "elapsed, timer, distance, average_speed, max_speed, "
                                "average_heart_rate, max_heart_rate, calories, ascent, descent) "
                                "VALUES (%1, %2, %3, '%4', %5, %6, %7, %8, %9, %10, %11, %12, %13, %14)")
                                       .arg(activityId)
                                       .arg(lap.index)
                                       .arg(lap.start)
                                       .arg(lapTime.toString("yyyy-MM-ddTHH:mm:ss"))
                                       .arg(lap.elapsed)
                                       .arg(lap.timer)
                                       .arg(lap.distance)
                                       .arg(lap.averageSpeed)
                                       .arg(lap.maxSpeed)
                                       .arg(lap.averageHeartRate)
                                       .arg(lap.maxHeartRate)
                                       .arg(lap.calories)
                                       .arg(lap.ascent)
                                       .arg(lap.descent));
                    }
                }
            }
        }
    }

    if (!stressValues.isEmpty() && db->containsTable("stress") == true) {
        for (auto it = stressValues.constBegin(); it != stressValues.constEnd(); ++it) {
            const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(it.key() * 1000LL);
            db->executeSql(KDbEscapedString(
                    "DELETE FROM stress WHERE stress_timestamp = %1 AND stress_type = 1")
                           .arg(it.key()));
            db->executeSql(KDbEscapedString(
                    "INSERT INTO stress (stress_timestamp, stress_timestamp_dt, stress_value, stress_type) "
                    "VALUES (%1, '%2', %3, 1)")
                           .arg(it.key())
                           .arg(dateTime.toString("yyyy-MM-ddTHH:mm:ss"))
                           .arg(it.value()));
            ++inserted;
        }
    }

    if (!spo2Values.isEmpty() && db->containsTable("spo2") == true) {
        for (auto it = spo2Values.constBegin(); it != spo2Values.constEnd(); ++it) {
            const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(it.key() * 1000LL);
            db->executeSql(KDbEscapedString(
                    "DELETE FROM spo2 WHERE spo2_timestamp = %1")
                           .arg(it.key()));
            db->executeSql(KDbEscapedString(
                    "INSERT INTO spo2 (spo2_timestamp, spo2_timestamp_dt, "
                    "spo2_automatic, spo2_value) "
                    "VALUES (%1, '%2', 1, %3)")
                           .arg(it.key())
                           .arg(dateTime.toString("yyyy-MM-ddTHH:mm:ss"))
                           .arg(it.value()));
            ++inserted;
        }
    }

    if (!bodyEnergyValues.isEmpty()) {
        if (!db->executeSql(KDbEscapedString(
                "CREATE TABLE IF NOT EXISTS garmin_body_energy ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER, "
                "timestamp_dt DATETIME, device_id INTEGER, value INTEGER)"))) {
            qWarning() << "GarminFIT: cannot create garmin_body_energy table";
        }
        for (auto it = bodyEnergyValues.constBegin(); it != bodyEnergyValues.constEnd(); ++it) {
            const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(it.key() * 1000LL);
            db->executeSql(KDbEscapedString(
                    "DELETE FROM garmin_body_energy WHERE timestamp = %1 AND device_id = %2")
                           .arg(it.key()).arg(deviceId));
            db->executeSql(KDbEscapedString(
                    "INSERT INTO garmin_body_energy (timestamp, timestamp_dt, device_id, value) "
                    "VALUES (%1, '%2', %3, %4)")
                           .arg(it.key())
                           .arg(dateTime.toString("yyyy-MM-ddTHH:mm:ss"))
                           .arg(deviceId)
                           .arg(it.value()));
            ++inserted;
        }
    }

    if (!hrvValues.isEmpty() && db->containsTable("hrv") == true) {
        for (auto it = hrvValues.constBegin(); it != hrvValues.constEnd(); ++it) {
            const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(it.key() * 1000LL);
            db->executeSql(KDbEscapedString(
                    "DELETE FROM hrv WHERE hrv_timestamp = %1")
                           .arg(it.key()));
            db->executeSql(KDbEscapedString(
                    "INSERT INTO hrv (hrv_timestamp, hrv_timestamp_dt, hrv_value) "
                    "VALUES (%1, '%2', %3)")
                           .arg(it.key())
                           .arg(dateTime.toString("yyyy-MM-ddTHH:mm:ss"))
                           .arg(it.value()));
            ++inserted;
        }
    }

    if (!hrvSummaries.isEmpty()) {
        if (!db->executeSql(KDbEscapedString(
                "CREATE TABLE IF NOT EXISTS garmin_hrv_summary ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "timestamp INTEGER, timestamp_dt DATETIME, device_id INTEGER, "
                "weekly_average REAL, last_night_average REAL, last_night_5min_high REAL, "
                "baseline_low REAL, baseline_balanced_low REAL, baseline_balanced_high REAL, "
                "status INTEGER)"))) {
            qWarning() << "GarminFIT: cannot create garmin_hrv_summary table";
        }
        for (auto it = hrvSummaries.constBegin(); it != hrvSummaries.constEnd(); ++it) {
            const HrvSummaryData &summary = it.value();
            const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(summary.timestamp * 1000LL);
            db->executeSql(KDbEscapedString(
                    "DELETE FROM garmin_hrv_summary WHERE timestamp = %1 AND device_id = %2")
                           .arg(summary.timestamp).arg(deviceId));
            db->executeSql(KDbEscapedString(
                    "INSERT INTO garmin_hrv_summary "
                    "(timestamp, timestamp_dt, device_id, weekly_average, last_night_average, "
                    "last_night_5min_high, baseline_low, baseline_balanced_low, "
                    "baseline_balanced_high, status) "
                    "VALUES (%1, '%2', %3, %4, %5, %6, %7, %8, %9, %10)")
                           .arg(summary.timestamp)
                           .arg(dateTime.toString("yyyy-MM-ddTHH:mm:ss"))
                           .arg(deviceId)
                           .arg(summary.weeklyAverage)
                           .arg(summary.lastNightAverage)
                           .arg(summary.lastNight5MinHigh)
                           .arg(summary.baselineLow)
                           .arg(summary.baselineBalancedLow)
                           .arg(summary.baselineBalancedHigh)
                           .arg(summary.status));
            ++inserted;
        }
    }

    if (!sleepStats.isEmpty()) {
        if (!db->executeSql(KDbEscapedString(
                "CREATE TABLE IF NOT EXISTS garmin_sleep_stats ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "timestamp INTEGER, timestamp_dt DATETIME, device_id INTEGER, "
                "overall_score INTEGER, quality_score INTEGER, recovery_score INTEGER, "
                "average_stress REAL)"))) {
            qWarning() << "GarminFIT: cannot create garmin_sleep_stats table";
        }
        for (auto it = sleepStats.constBegin(); it != sleepStats.constEnd(); ++it) {
            const SleepStatsData &stats = it.value();
            const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(stats.timestamp * 1000LL);
            db->executeSql(KDbEscapedString(
                    "DELETE FROM garmin_sleep_stats WHERE timestamp = %1 AND device_id = %2")
                           .arg(stats.timestamp).arg(deviceId));
            db->executeSql(KDbEscapedString(
                    "INSERT INTO garmin_sleep_stats "
                    "(timestamp, timestamp_dt, device_id, overall_score, quality_score, "
                    "recovery_score, average_stress) "
                    "VALUES (%1, '%2', %3, %4, %5, %6, %7)")
                           .arg(stats.timestamp)
                           .arg(dateTime.toString("yyyy-MM-ddTHH:mm:ss"))
                           .arg(deviceId)
                           .arg(stats.overallScore)
                           .arg(stats.qualityScore)
                           .arg(stats.recoveryScore)
                           .arg(stats.averageStress));
            ++inserted;
        }
    }

    // The sleep window (onset and wake time) is needed to give the first
    // stage its duration: Garmin timestamps a stage at the END of the span it
    // covers, so the first span starts at the window onset.
    if (!sleepWindows.isEmpty()) {
        if (!db->executeSql(KDbEscapedString(
                "CREATE TABLE IF NOT EXISTS garmin_sleep_window ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "device_id INTEGER, start_ts INTEGER, end_ts INTEGER)"))) {
            qWarning() << "GarminFIT: cannot create garmin_sleep_window table";
        }
        db->executeSql(KDbEscapedString(
                "CREATE INDEX IF NOT EXISTS idx_garmin_sleep_window_ts "
                "ON garmin_sleep_window(device_id, start_ts)"));
        for (auto it = sleepWindows.constBegin(); it != sleepWindows.constEnd(); ++it) {
            db->executeSql(KDbEscapedString(
                    "DELETE FROM garmin_sleep_window "
                    "WHERE start_ts = %1 AND device_id = %2")
                           .arg(it.key()).arg(deviceId));
            db->executeSql(KDbEscapedString(
                    "INSERT INTO garmin_sleep_window (device_id, start_ts, end_ts) "
                    "VALUES (%1, %2, %3)")
                           .arg(deviceId)
                           .arg(it.key())
                           .arg(it.value()));
            ++inserted;
        }
    }

    KDbTableSchema *table = db->tableSchema("mi_band_activity");
    if (!table) {
        qWarning() << "GarminFIT: mi_band_activity table missing";
        return inserted;
    }

    // Older builds of this port stored Garmin-epoch timestamps as Unix timestamps.
    // Remove those impossible pre-2010 samples for this device.
    db->executeSql(KDbEscapedString(
            "DELETE FROM mi_band_activity WHERE device_id = %1 AND timestamp < 1262304000")
                           .arg(deviceId));

    // Garmin step counters are cumulative. Keep them separately so repeated
    // imports and overlapping files can be converted into correct deltas.
    if (!db->executeSql(KDbEscapedString(
            "CREATE TABLE IF NOT EXISTS garmin_monitor ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "timestamp INTEGER, device_id INTEGER, "
            "activity_type INTEGER, cycles INTEGER)"))) {
        qWarning() << "GarminFIT: cannot create garmin_monitor table";
    }
    db->executeSql(KDbEscapedString(
            "CREATE INDEX IF NOT EXISTS idx_garmin_monitor_ts "
            "ON garmin_monitor(device_id, timestamp)"));

    // Garmin sleep stages are change events, not per-minute samples. Keep them
    // in their own table so the sleep calculator does not mix them with the
    // normal monitoring samples.
    if (!db->executeSql(KDbEscapedString(
            "CREATE TABLE IF NOT EXISTS garmin_sleep ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "timestamp INTEGER, timestamp_dt DATETIME, "
            "device_id INTEGER, stage INTEGER, raw_kind INTEGER)"))) {
        qWarning() << "GarminFIT: cannot create garmin_sleep table";
    }
    db->executeSql(KDbEscapedString(
            "CREATE INDEX IF NOT EXISTS idx_garmin_sleep_ts "
            "ON garmin_sleep(device_id, timestamp)"));

    // Remove the old, mixed sleep samples written by previous builds. Their
    // data is re-imported into garmin_sleep below.
    db->executeSql(KDbEscapedString(
            "DELETE FROM mi_band_activity WHERE device_id = %1 AND raw_kind IN (9,11)")
                           .arg(deviceId));

    KDbFieldList fields;
    fields.addField(table->field("timestamp"));
    fields.addField(table->field("timestamp_dt"));
    fields.addField(table->field("device_id"));
    fields.addField(table->field("user_id"));
    fields.addField(table->field("raw_intensity"));
    fields.addField(table->field("steps"));
    fields.addField(table->field("raw_kind"));
    fields.addField(table->field("heartrate"));

    KDbTransaction transaction = db->beginTransaction();
    KDbTransactionGuard transactionGuard(transaction);

    for (auto tsIt = cumulative.constBegin(); tsIt != cumulative.constEnd(); ++tsIt) {
        const QMap<int, int> &perType = tsIt.value();
        for (auto typeIt = perType.constBegin(); typeIt != perType.constEnd(); ++typeIt) {
            db->executeSql(KDbEscapedString(
                    "DELETE FROM garmin_monitor WHERE timestamp = %1 AND device_id = %2 AND activity_type = %3")
                                   .arg(tsIt.key())
                                   .arg(deviceId)
                                   .arg(typeIt.key()));
            db->executeSql(KDbEscapedString(
                    "INSERT INTO garmin_monitor (timestamp, device_id, activity_type, cycles) "
                    "VALUES (%1, %2, %3, %4)")
                                   .arg(tsIt.key())
                                   .arg(deviceId)
                                   .arg(typeIt.key())
                                   .arg(typeIt.value()));
        }
    }

    for (auto it = monitoring.constBegin(); it != monitoring.constEnd(); ++it) {
        if (insertSample(db, &fields, it.key(), deviceId, userId,
                         it.value().intensity, 0,
                         1 /* ActivityKind::Activity */, it.value().heartRate)) {
            ++inserted;
        }
    }
    for (auto it = sleepLevels.constBegin(); it != sleepLevels.constEnd(); ++it) {
        db->executeSql(KDbEscapedString(
                "DELETE FROM garmin_sleep WHERE timestamp = %1 AND device_id = %2")
                               .arg(it.key())
                               .arg(deviceId));
        db->executeSql(KDbEscapedString(
                "INSERT INTO garmin_sleep (timestamp, device_id, stage, raw_kind) "
                "VALUES (%1, %2, %3, %4)")
                               .arg(it.key())
                               .arg(deviceId)
                               .arg(it.value().first)
                               .arg(it.value().second));
        ++inserted;
    }

    if (!transactionGuard.commit()) {
        qWarning() << "GarminFIT: failed to commit samples";
        return 0;
    }

    if (!cumulative.isEmpty()) {
        QSet<QDate> days;
        for (auto it = cumulative.constBegin(); it != cumulative.constEnd(); ++it) {
            days.insert(QDateTime::fromMSecsSinceEpoch(it.key() * 1000LL).date());
        }

        for (const QDate &day : days) {
            const qint64 dayStart = QDateTime(day, QTime(0, 0)).toMSecsSinceEpoch() / 1000;
            const qint64 dayEnd = QDateTime(day.addDays(1), QTime(0, 0)).toMSecsSinceEpoch() / 1000;

            const QString query = QString(
                    "SELECT timestamp, activity_type, cycles FROM garmin_monitor "
                    "WHERE device_id = %1 AND timestamp >= %2 AND timestamp < %3 "
                    "ORDER BY timestamp ASC, activity_type ASC")
                    .arg(deviceId)
                    .arg(dayStart)
                    .arg(dayEnd);

            KDbCursor *cursor = db->executeQuery(KDbEscapedString(query));
            if (!cursor) {
                continue;
            }

            QMap<int, int> previous;
            QMap<qint64, int> deltas;

            // The first monitor sample at midnight is usually a cumulative
            // summary of the previous day. Never count it as new steps.
            db->executeSql(KDbEscapedString(
                    "UPDATE mi_band_activity SET steps = 0 "
                    "WHERE timestamp = %1 AND device_id = %2 AND raw_kind = 1")
                                   .arg(dayStart).arg(deviceId));

            if (cursor->open() && cursor->moveFirst()) {
                while (!cursor->eof()) {
                    const qint64 timestamp = cursor->value(0).toLongLong();
                    const int activityType = cursor->value(1).toInt();
                    const int cycles = cursor->value(2).toInt();

                    if (timestamp == dayStart) {
                        previous[activityType] = cycles;
                        cursor->moveNext();
                        continue;
                    }

                    const int previousCycles = previous.value(activityType, 0);
                    int delta = cycles - previousCycles;
                    if (delta < 0) {
                        // Counter reset (new day or watch reset): count from zero.
                        delta = cycles;
                    }
                    previous[activityType] = cycles;
                    if (delta > 0) {
                        deltas[timestamp] += delta;
                    }
                    cursor->moveNext();
                }
            }
            db->deleteCursor(cursor);

            for (auto it = deltas.constBegin(); it != deltas.constEnd(); ++it) {
                db->executeSql(KDbEscapedString(
                        "UPDATE mi_band_activity SET steps = %1 "
                        "WHERE timestamp = %2 AND device_id = %3 AND raw_kind = 1")
                                       .arg(it.value())
                                       .arg(it.key())
                                       .arg(deviceId));
            }
        }
    }

    qDebug() << "GarminFIT:" << fitTypeName(sourceType) << "stored" << inserted << "samples";
    return inserted;
}
