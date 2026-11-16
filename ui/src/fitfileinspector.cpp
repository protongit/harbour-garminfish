#include "fitfileinspector.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include <algorithm>
#include <cstring>

#include <QtMath>

namespace {

constexpr quint32 GarminEpochOffset = 631065600u;

struct FieldDef {
    quint8 num = 0;
    quint8 size = 0;
    quint8 base = 0;
};

struct DevFieldDef {
    quint8 num = 0;
    quint8 size = 0;
    quint8 developerIndex = 0;
};

struct MsgDef {
    quint16 global = 0;
    bool littleEndian = true;
    bool valid = false;
    QVector<FieldDef> fields;
    QVector<DevFieldDef> developerFields;
};

quint64 readUnsigned(const QByteArray &data, int offset, int size, bool littleEndian)
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

qint64 signedValue(quint64 raw, int size)
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

int baseTypeSize(quint8 base)
{
    switch (base) {
    case 0x01:
    case 0x02:
    case 0x0A:
    case 0x0D:
        return 1;
    case 0x83:
    case 0x84:
    case 0x8B:
        return 2;
    case 0x85:
    case 0x86:
    case 0x88:
    case 0x8C:
        return 4;
    case 0x89:
    case 0x8E:
    case 0x8F:
    case 0x90:
        return 8;
    case 0x07:
        return 1;
    default:
        return 1;
    }
}

bool isSignedBase(quint8 base)
{
    return base == 0x01 || base == 0x83 || base == 0x85 || base == 0x8E;
}

bool isInvalidValue(quint64 raw, quint8 base, int size)
{
    if (size <= 0 || size > 8) {
        return true;
    }
    const quint64 mask = size == 8 ? ~quint64(0) : ((quint64(1) << (size * 8)) - 1);
    if (isSignedBase(base)) {
        return raw == (mask >> 1);
    }
    return raw == mask;
}

QVariant decodeValue(const QByteArray &raw, quint8 base, bool littleEndian)
{
    if (raw.isEmpty()) {
        return QVariant();
    }

    if (base == 0x07) {
        QString value = QString::fromUtf8(raw);
        const int nul = value.indexOf(QChar('\0'));
        if (nul >= 0) {
            value.truncate(nul);
        }
        return value;
    }

    QByteArray bytes = raw;
    if (!littleEndian) {
        std::reverse(bytes.begin(), bytes.end());
    }

    if (bytes.size() > baseTypeSize(base)) {
        return QString::fromLatin1(raw.toHex());
    }

    if (base == 0x88 && bytes.size() == 4) {
        float value = 0.0f;
        memcpy(&value, bytes.constData(), sizeof(value));
        if (qIsNaN(value)) {
            return QVariant();
        }
        return double(value);
    }
    if (base == 0x89 && bytes.size() == 8) {
        double value = 0.0;
        memcpy(&value, bytes.constData(), sizeof(value));
        if (qIsNaN(value)) {
            return QVariant();
        }
        return value;
    }

    const quint64 rawValue = readUnsigned(bytes, 0, bytes.size(), true);
    if (isInvalidValue(rawValue, base, bytes.size())) {
        return QVariant();
    }
    if (isSignedBase(base)) {
        return qint64(signedValue(rawValue, bytes.size()));
    }
    return rawValue;
}

qint64 garminToUnix(qint64 timestamp)
{
    return timestamp > 0 ? timestamp + qint64(GarminEpochOffset) : 0;
}

QString timestampString(qint64 unixTimestamp)
{
    if (unixTimestamp <= 0) {
        return QString();
    }
    return QDateTime::fromMSecsSinceEpoch(unixTimestamp * 1000LL)
            .toString("yyyy-MM-dd HH:mm:ss");
}

QString formatSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QString::number(bytes) + " B";
    }
    if (bytes < 1024 * 1024) {
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    }
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
}

QString messageName(int global)
{
    switch (global) {
    case 0: return "FILE_ID";
    case 2: return "DEVICE_SETTINGS";
    case 3: return "USER_PROFILE";
    case 7: return "ZONES_TARGET";
    case 12: return "SPORT";
    case 18: return "SESSION";
    case 19: return "LAP";
    case 20: return "RECORD";
    case 21: return "EVENT";
    case 23: return "DEVICE_INFO";
    case 34: return "ACTIVITY";
    case 49: return "FILE_CREATOR";
    case 55: return "MONITORING";
    case 103: return "MONITORING_INFO";
    case 140: return "PHYSIOLOGICAL_METRICS";
    case 160: return "GPS_METADATA";
    case 206: return "FIELD_DESCRIPTION";
    case 207: return "DEVELOPER_DATA_ID";
    case 211: return "MONITORING_HR_DATA";
    case 216: return "TIME_IN_ZONE";
    case 227: return "STRESS_LEVEL";
    case 232: return "GARMIN_232";
    case 233: return "GARMIN_233";
    case 269: return "SPO2";
    case 273: return "SLEEP_DATA_INFO";
    case 274: return "SLEEP_DATA_RAW";
    case 275: return "SLEEP_LEVEL";
    case 281: return "GARMIN_281";
    case 284: return "GARMIN_284";
    case 288: return "GARMIN_288";
    case 294: return "GARMIN_294";
    case 297: return "RESPIRATION_RATE";
    case 326: return "GARMIN_326";
    case 327: return "GARMIN_327";
    case 339: return "GARMIN_339";
    case 346: return "SLEEP_STATS";
    case 355: return "GARMIN_355";
    case 356: return "GARMIN_356";
    case 370: return "HRV_SUMMARY";
    case 371: return "HRV_VALUE";
    case 378: return "GARMIN_378";
    case 382: return "GARMIN_382";
    default: return QString("MSG_%1").arg(global);
    }
}

double scaledValue(const QHash<quint8, QVariant> &values, quint8 field, double scale)
{
    if (!values.contains(field) || !values.value(field).isValid()) {
        return 0.0;
    }
    return values.value(field).toDouble() / scale;
}

qint64 valueToTimestamp(const QHash<quint8, QVariant> &values, quint8 field)
{
    if (!values.contains(field) || !values.value(field).isValid()) {
        return 0;
    }
    return garminToUnix(values.value(field).toLongLong());
}

QVariantMap fileIdMap(const QHash<quint8, QVariant> &values)
{
    QVariantMap id;
    if (values.contains(0) && values.value(0).isValid()) {
        id["type"] = values.value(0).toInt();
    }
    if (values.contains(1) && values.value(1).isValid()) {
        id["manufacturer"] = values.value(1).toInt();
    }
    if (values.contains(2) && values.value(2).isValid()) {
        id["product"] = values.value(2).toInt();
    }
    if (values.contains(3) && values.value(3).isValid()) {
        id["serial"] = values.value(3).toString();
    }
    if (values.contains(4) && values.value(4).isValid()) {
        const qint64 created = garminToUnix(values.value(4).toLongLong());
        id["timeCreated"] = timestampString(created);
    }
    if (values.contains(8) && values.value(8).isValid()) {
        id["productName"] = values.value(8).toString();
    }
    return id;
}

QVariantMap sessionMap(const QHash<quint8, QVariant> &values)
{
    QVariantMap session;
    session["valid"] = true;
    if (values.contains(2) && values.value(2).isValid()) {
        const qint64 start = garminToUnix(values.value(2).toLongLong());
        session["startEpoch"] = start;
        session["start"] = timestampString(start);
    }
    if (values.contains(253) && values.value(253).isValid()) {
        const qint64 end = garminToUnix(values.value(253).toLongLong());
        session["endEpoch"] = end;
        session["end"] = timestampString(end);
    }
    if (values.contains(7) && values.value(7).isValid()) {
        session["elapsed"] = values.value(7).toLongLong() / 1000.0;
    }
    if (values.contains(8) && values.value(8).isValid()) {
        session["timer"] = values.value(8).toLongLong() / 1000.0;
    }
    if (values.contains(9) && values.value(9).isValid()) {
        session["distance"] = values.value(9).toLongLong() / 100.0;
    }
    if (values.contains(11) && values.value(11).isValid()) {
        session["calories"] = values.value(11).toInt();
    }
    if (values.contains(16) && values.value(16).isValid()) {
        session["averageHeartRate"] = values.value(16).toInt();
    }
    if (values.contains(17) && values.value(17).isValid()) {
        session["maxHeartRate"] = values.value(17).toInt();
    }
    if (values.contains(5) && values.value(5).isValid()) {
        session["sport"] = values.value(5).toInt();
    }
    if (values.contains(6) && values.value(6).isValid()) {
        session["subSport"] = values.value(6).toInt();
    }
    if (values.contains(26) && values.value(26).isValid()) {
        session["laps"] = values.value(26).toInt();
    }
    if (values.contains(24) && values.value(24).isValid()) {
        session["trainingEffect"] = values.value(24).toInt() / 10.0;
    }
    if (values.contains(137) && values.value(137).isValid()) {
        session["anaerobicEffect"] = values.value(137).toInt() / 10.0;
    }
    if (values.contains(168) && values.value(168).isValid()) {
        session["trainingLoad"] = values.value(168).toLongLong() / 65536.0;
    }
    if (values.contains(35) && values.value(35).isValid()) {
        session["trainingStressScore"] = values.value(35).toInt() / 10.0;
    }
    if (values.contains(195) && values.value(195).isValid()) {
        session["averageStress"] = values.value(195).toInt();
    }
    if (values.contains(197) && values.value(197).isValid()) {
        session["sdrr"] = values.value(197).toInt();
    }
    if (values.contains(198) && values.value(198).isValid()) {
        session["rmssd"] = values.value(198).toInt();
    }
    if (values.contains(110) && values.value(110).isValid()) {
        session["profileName"] = values.value(110).toString();
    }
    return session;
}

QVariantMap sampleMap(qint64 timestamp, const QVariantMap &values)
{
    QVariantMap sample = values;
    sample["timestamp"] = timestamp;
    sample["time"] = timestampString(timestamp);
    return sample;
}

} // namespace

QVariantMap FitFileInspector::inspect(const QString &path)
{
    QVariantMap result;
    QFileInfo info(path);
    result["name"] = info.fileName();
    result["path"] = path;
    result["type"] = info.dir().dirName();
    result["size"] = qint64(info.size());
    result["sizeText"] = formatSize(info.size());
    result["modified"] = info.lastModified().toString("yyyy-MM-dd HH:mm:ss");
    result["valid"] = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result["error"] = QString("Cannot open file");
        return result;
    }
    const QByteArray data = file.readAll();
    file.close();

    if (data.size() < 12) {
        result["error"] = QString("File is too small");
        return result;
    }

    const int headerSize = quint8(data.at(0));
    if (headerSize < 12 || headerSize > data.size()) {
        result["error"] = QString("Invalid FIT header size");
        return result;
    }
    if (data.mid(8, 4) != QByteArray(".FIT")) {
        result["error"] = QString("Not a FIT file");
        return result;
    }

    const quint32 dataSize = quint32(readUnsigned(data, 4, 4, true));
    const int end = qMin<int>(data.size(), int(headerSize + dataSize));
    result["headerSize"] = headerSize;
    result["dataSize"] = dataSize;
    result["profileVersion"] = QString("%1.%2")
            .arg(quint8(data.at(2)))
            .arg(quint8(data.at(3)));

    QHash<quint8, MsgDef> definitions;
    QMap<int, int> counts;
    QMap<int, QSet<int>> fields;
    QVariantMap fileId;
    QVariantMap session;
    QVariantMap lastMonitoring;
    QVariantMap lastStress;
    QVariantMap lastSpo2;
    QVariantMap lastRespiration;
    QVariantMap sleepStats;
    QVariantMap hrvSummary;
    QVariantMap metrics;
    QVariantList sleepStages;
    QVariantList hrvValues;
    QVariantList records;
    QString sportName;
    int recordCount = 0;
    qint64 firstTimestamp = 0;
    qint64 lastTimestamp = 0;
    qint64 referenceTimestamp = 0;

    int position = headerSize;
    while (position < end) {
        const quint8 header = quint8(data.at(position++));
        const bool compressedTimestamp = (header & 0x80) != 0;
        const bool definitionMessage = !compressedTimestamp && (header & 0x40) != 0;
        const quint8 localMessageType = compressedTimestamp
                ? quint8((header >> 5) & 0x03)
                : quint8(header & 0x0f);

        qint64 headerTimestamp = 0;
        if (compressedTimestamp) {
            const quint8 timeOffset = quint8(header & 0x1f);
            if (referenceTimestamp > 0) {
                const qint64 base = referenceTimestamp & ~qint64(0x1f);
                if (timeOffset >= (referenceTimestamp & 0x1f)) {
                    referenceTimestamp = base + timeOffset;
                } else {
                    referenceTimestamp = base + timeOffset + 0x20;
                }
                headerTimestamp = garminToUnix(referenceTimestamp);
            }
        }

        if (definitionMessage) {
            if (position + 5 > end) {
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
            if (def.valid && (header & 0x20) != 0 && position < end) {
                const quint8 devCount = quint8(data.at(position++));
                for (int i = 0; i < devCount; ++i) {
                    if (position + 3 > end) {
                        def.valid = false;
                        break;
                    }
                    DevFieldDef dev;
                    dev.num = quint8(data.at(position++));
                    dev.size = quint8(data.at(position++));
                    dev.developerIndex = quint8(data.at(position++));
                    def.developerFields.append(dev);
                }
            }
            if (def.valid) {
                definitions.insert(localMessageType, def);
            }
            continue;
        }

        if (!definitions.contains(localMessageType)) {
            break;
        }

        const MsgDef def = definitions.value(localMessageType);
        QHash<quint8, QVariant> values;
        for (const FieldDef &field : def.fields) {
            if (position + field.size > end) {
                break;
            }
            values.insert(field.num, decodeValue(data.mid(position, field.size),
                                                 field.base, def.littleEndian));
            position += field.size;
        }
        for (const DevFieldDef &dev : def.developerFields) {
            position += dev.size;
        }

        if (values.contains(253) && values.value(253).isValid()) {
            referenceTimestamp = values.value(253).toLongLong();
        }

        counts[def.global]++;
        for (const FieldDef &field : def.fields) {
            fields[def.global].insert(field.num);
        }

        qint64 timestamp = valueToTimestamp(values, 253);
        if (timestamp == 0 && def.global == 227) {
            timestamp = valueToTimestamp(values, 1);
        }
        if (timestamp == 0 && def.global == 0) {
            timestamp = valueToTimestamp(values, 4);
        }
        if (timestamp == 0) {
            timestamp = headerTimestamp;
        }
        if (timestamp > 0) {
            if (firstTimestamp == 0 || timestamp < firstTimestamp) {
                firstTimestamp = timestamp;
            }
            if (timestamp > lastTimestamp) {
                lastTimestamp = timestamp;
            }
        }

        if (def.global == 0) {
            fileId = fileIdMap(values);
        } else if (def.global == 12) {
            if (values.contains(3) && values.value(3).isValid()) {
                sportName = values.value(3).toString();
            }
        } else if (def.global == 18) {
            session = sessionMap(values);
        } else if (def.global == 20) {
            ++recordCount;
            if (records.size() < 20) {
                QVariantMap record;
                record["time"] = timestampString(timestamp);
                if (values.contains(0) && values.value(0).isValid()
                        && values.contains(1) && values.value(1).isValid()) {
                    record["latitude"] = values.value(0).toInt() * (180.0 / 2147483648.0);
                    record["longitude"] = values.value(1).toInt() * (180.0 / 2147483648.0);
                }
                if (values.contains(2) && values.value(2).isValid()) {
                    record["altitude"] = values.value(2).toInt() / 5.0 - 500.0;
                }
                if (values.contains(3) && values.value(3).isValid()) {
                    record["heartRate"] = values.value(3).toInt();
                }
                if (values.contains(5) && values.value(5).isValid()) {
                    record["distance"] = values.value(5).toLongLong() / 100.0;
                }
                if (values.contains(6) && values.value(6).isValid()) {
                    record["speed"] = values.value(6).toInt() / 1000.0;
                }
                records << record;
            }
        } else if (def.global == 55) {
            QVariantMap sample;
            if (values.contains(27) && values.value(27).isValid()) {
                sample["heartRate"] = values.value(27).toInt();
            }
            if (values.contains(3) && values.value(3).isValid()) {
                sample["cycles"] = values.value(3).toLongLong();
            } else if (values.contains(9) && values.value(9).isValid()) {
                sample["cycles"] = values.value(9).toLongLong();
            }
            if (values.contains(28) && values.value(28).isValid()) {
                sample["intensity"] = values.value(28).toInt() / 10.0;
            }
            if (values.contains(24) && values.value(24).isValid()) {
                sample["activityType"] = values.value(24).toInt();
            }
            lastMonitoring = sampleMap(timestamp, sample);
        } else if (def.global == 227) {
            QVariantMap sample;
            if (values.contains(0) && values.value(0).isValid()) {
                sample["stress"] = values.value(0).toInt();
            }
            if (values.contains(3) && values.value(3).isValid()) {
                sample["bodyEnergy"] = values.value(3).toInt();
            }
            lastStress = sampleMap(timestamp, sample);
        } else if (def.global == 269) {
            QVariantMap sample;
            if (values.contains(0) && values.value(0).isValid()) {
                sample["spo2"] = values.value(0).toInt();
            }
            lastSpo2 = sampleMap(timestamp, sample);
        } else if (def.global == 275) {
            if (values.contains(0) && values.value(0).isValid()) {
                QVariantMap stage;
                stage["time"] = timestampString(timestamp);
                stage["stage"] = values.value(0).toInt();
                sleepStages << stage;
            }
        } else if (def.global == 297) {
            QVariantMap sample;
            if (values.contains(0) && values.value(0).isValid()) {
                sample["respiration"] = values.value(0).toInt() / 100.0;
            }
            lastRespiration = sampleMap(timestamp, sample);
        } else if (def.global == 346) {
            if (values.contains(6) && values.value(6).isValid()) {
                sleepStats["overallScore"] = values.value(6).toInt();
            }
            if (values.contains(7) && values.value(7).isValid()) {
                sleepStats["qualityScore"] = values.value(7).toInt();
            }
            if (values.contains(8) && values.value(8).isValid()) {
                sleepStats["recoveryScore"] = values.value(8).toInt();
            }
            if (values.contains(15) && values.value(15).isValid()) {
                sleepStats["averageStress"] = values.value(15).toInt() / 100.0;
            }
        } else if (def.global == 370) {
            if (values.contains(0) && values.value(0).isValid()) {
                hrvSummary["weeklyAverage"] = values.value(0).toInt() / 128.0;
            }
            if (values.contains(1) && values.value(1).isValid()) {
                hrvSummary["lastNightAverage"] = values.value(1).toInt() / 128.0;
            }
            if (values.contains(2) && values.value(2).isValid()) {
                hrvSummary["lastNight5MinHigh"] = values.value(2).toInt() / 128.0;
            }
            if (values.contains(3) && values.value(3).isValid()) {
                hrvSummary["baselineLow"] = values.value(3).toInt() / 128.0;
            }
            if (values.contains(4) && values.value(4).isValid()) {
                hrvSummary["baselineBalancedLow"] = values.value(4).toInt() / 128.0;
            }
            if (values.contains(5) && values.value(5).isValid()) {
                hrvSummary["baselineBalancedHigh"] = values.value(5).toInt() / 128.0;
            }
            if (values.contains(6) && values.value(6).isValid()) {
                hrvSummary["status"] = values.value(6).toInt();
            }
            hrvSummary["time"] = timestampString(timestamp);
        } else if (def.global == 371) {
            if (values.contains(0) && values.value(0).isValid() && hrvValues.size() < 100) {
                QVariantMap sample;
                sample["time"] = timestampString(timestamp);
                sample["value"] = values.value(0).toInt() / 128.0;
                hrvValues << sample;
            }
        } else if (def.global == 140) {
            if (values.contains(4) && values.value(4).isValid()) {
                metrics["aerobicEffect"] = values.value(4).toDouble();
            }
            if (values.contains(20) && values.value(20).isValid()) {
                metrics["anaerobicEffect"] = values.value(20).toDouble();
            }
            if (values.contains(9) && values.value(9).isValid()) {
                metrics["recoveryTime"] = values.value(9).toInt();
            }
            if (values.contains(7) && values.value(7).isValid()) {
                metrics["metMax"] = values.value(7).toDouble();
            }
            if (values.contains(14) && values.value(14).isValid()) {
                metrics["lactateThresholdHeartRate"] = values.value(14).toInt();
            }
            metrics["time"] = timestampString(timestamp);
        }
    }

    QVariantList messages;
    QVariantList unknownMessages;
    const QList<int> keys = counts.keys();
    for (int global : keys) {
        QVariantMap message;
        message["number"] = global;
        message["name"] = messageName(global);
        message["count"] = counts.value(global);
        QStringList fieldList;
        const QSet<int> fieldNumbers = fields.value(global);
        QList<int> sortedFields = fieldNumbers.values();
        std::sort(sortedFields.begin(), sortedFields.end());
        for (int field : sortedFields) {
            fieldList << QString::number(field);
        }
        message["fields"] = fieldList.join(", ");
        messages << message;
        if (messageName(global).startsWith("MSG_")) {
            unknownMessages << message;
        }
    }

    result["valid"] = true;
    result["fileId"] = fileId;
    result["sportName"] = sportName;
    result["session"] = session;
    result["lastMonitoring"] = lastMonitoring;
    result["lastStress"] = lastStress;
    result["lastSpo2"] = lastSpo2;
    result["lastRespiration"] = lastRespiration;
    result["sleepStats"] = sleepStats;
    result["sleepStages"] = sleepStages;
    result["hrvSummary"] = hrvSummary;
    result["hrvValues"] = hrvValues;
    result["metrics"] = metrics;
    result["records"] = records;
    result["recordCount"] = recordCount;
    result["messages"] = messages;
    result["unknownMessages"] = unknownMessages;
    result["firstTimestamp"] = timestampString(firstTimestamp);
    result["lastTimestamp"] = timestampString(lastTimestamp);

    return result;
}
