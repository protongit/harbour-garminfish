#include "datasource.h"
#include "amazfish.h"
#include "fitfileinspector.h"
#include <QVariant>
#include <QTimeZone>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QStandardPaths>

#include <algorithm>

static const long MIN_SESSION_LENGTH = 5 * 60;
static const long MAX_WAKE_PHASE_LENGTH = 2 * 60 * 60;
static const int TYPE_LIGHT_SLEEP = 9;
static const int TYPE_DEEP_SLEEP = 11;
static const int TYPE_NO_CHANGE = 0;
static const int TYPE_IGNORE = 10;

int kind(int k)
{
    static int lastRealKind;
    k = k & 0xf; //just look at top 4 bits
    if (k == TYPE_IGNORE || k == TYPE_NO_CHANGE) {
        return lastRealKind;
    }
    lastRealKind = k;
    return k;
}

DataSource::DataSource()
{
}

void DataSource::setConnection(KDbConnection *conn)
{
    m_conn = conn;
}

QVariant DataSource::data(const Type type, const QDate &day)
{
    QList<QVariant> result;

    QString qry;

    QDateTime sd;
    sd.setDate(day);
    sd.setTime(QTime(0,0));
    sd.setTimeZone(QTimeZone::systemTimeZone());

    if (type == DataSource::TrainingLoadSummary) {
        if (!m_conn || !m_conn->isDatabaseUsed()
                || m_conn->containsTable("sports_data") != true
                || m_conn->containsTable("sports_meta") != true) {
            return result;
        }

        QMap<QDate, double> loads;
        const QString query =
                "SELECT date(sd.start_timestamp_dt), SUM(CAST(sm.value AS REAL)) "
                "FROM sports_data sd JOIN sports_meta sm ON sm.sport_id = sd.id "
                "WHERE sm.key = 'currentWorkoutLoad' "
                "AND date(sd.start_timestamp_dt) >= date('" + day.toString("yyyy-MM-dd") + "','-9 day') "
                "AND date(sd.start_timestamp_dt) <= date('" + day.toString("yyyy-MM-dd") + "') "
                "GROUP BY date(sd.start_timestamp_dt) "
                "ORDER BY date(sd.start_timestamp_dt)";
        KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(query));
        if (curs) {
            if (curs->open() && curs->moveFirst()) {
                while (!curs->eof()) {
                    QDate date = curs->value(0).toDate();
                    if (!date.isValid()) {
                        date = QDate::fromString(curs->value(0).toString(), Qt::ISODate);
                    }
                    if (date.isValid()) {
                        loads[date] += curs->value(1).toDouble();
                    }
                    curs->moveNext();
                }
            }
            m_conn->deleteCursor(curs);
        }

        if (loads.isEmpty()) {
            return result;
        }

        for (int i = 9; i >= 0; --i) {
            const QDate date = day.addDays(-i);
            QVariantMap pt;
            pt["x"] = QDateTime(date, QTime(0, 0)).toMSecsSinceEpoch() / 1000;
            pt["y"] = loads.value(date, 0.0);
            result.append(pt);
        }
        return result;
    }

    if (type == DataSource::SleepSummary) {
        // The graph is labelled by the morning date. A night that ends on the
        // morning of D is calculated from the window D-1 12:00 .. D 12:00.
        for (int i = 0; i < 10; i++) {
            const QDate morning = day.addDays(-9 + i);
            QList<DataSource::SleepSession> sessions = calculateSleep(morning.addDays(-1));
            QVariantMap pt;
            pt["x"] = QDateTime(morning).toMSecsSinceEpoch() / 1000;

            float light = 0;
            float deep = 0;
            float rem = 0;
            for (const DataSource::SleepSession &s : std::as_const(sessions)) {
                light += s.lightSleepDuration;
                deep += s.deepSleepDuration;
                rem += s.remSleepDuration;
            }
            pt["y"] = light / (60.0f*60.0f);
            pt["z"] = deep / (60.0f*60.0f);
            pt["w"] = rem / (60.0f*60.0f);

            result.append(pt);
        }
        // qDebug() << "Sleep data:" << result;

#if 0
        qry = "SELECT timestamp_dt, raw_kind, raw_intensity FROM mi_band_activity WHERE timestamp_dt >= date('" +
                day.toString("yyyy-MM-ddT11:59:00") + "','-10 day') AND timestamp_dt <= '" +
                day.toString("yyyy-MM-ddT12:01:00") +  "' ORDER BY timestamp_dt ASC";

        // qDebug() << qry;
        if (m_conn && m_conn->isDatabaseUsed()) {
            KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(qry));

            if (curs) {
                if (curs->open() && curs->moveFirst()) {
                    QDate d; //Current day, sleepd detection runs from midday to midday
                    int light = 0; //minutes in light sleep
                    int deep = 0; //mintes in deep sleep
                    QDate curDate = curs->value(0).toDateTime().date();
                    d = curDate.addDays(-1);

                    QVariantMap pt;
                    int temp_sleep = 0;
                    bool in_sleep = false;

                    while (!curs->eof()) {

                        QVariant t = curs->value(0);
                        QVariant k = curs->value(1);
                        QVariant i = curs->value(2);

                        curDate = t.toDateTime().date();

                        if (curDate != d && t.toDateTime().time() > QTime(12, 00)) { //date change

                            //qDebug() << "Date changed:" << curDate << light << deep;
                            //Save current values
                            pt["x"] = QDateTime(d).toMSecsSinceEpoch() / 1000;
                            pt["y"] = light / 60.0;
                            pt["z"] = deep / 60.0;
                            result.append(pt);

                            //Reset values
                            d = curDate;
                            light = 0;
                            deep = 0;
                            in_sleep = false;
                        }

                        int activity = k.toInt();
                        int intensity = i.toInt();

                        if (activity == 123 || (in_sleep == false && activity)) {
                            //qDebug() << "Starting sleep tracking";
                            in_sleep = true;
                        }

                        if (activity == 124 || (in_sleep && ((activity & 112) != 112))) {
                            //qDebug() << "Stopping sleep tracking";

                            in_sleep = false;
                            //qDebug() << "Adding " << temp_sleep << " minutes of sleep";

                            if (temp_sleep > 10) { //add to deep
                                deep += temp_sleep;
                            } else {
                                light += temp_sleep;
                            }
                            temp_sleep = 0;
                        }

                        if (in_sleep && (activity == 112 || activity == 121 || activity == 122)) {
                            //qDebug() << "Data:" << t << k << i;

                            if (intensity > 0) { //Not deep so just add to light
                                //qDebug() << "Adding " << temp_sleep << " minutes of sleep";

                                if (temp_sleep > 10) { //add to deep
                                    deep += temp_sleep;
                                } else {
                                    light += temp_sleep;
                                }
                                temp_sleep = 0;
                                light++;
                            } else {
                                temp_sleep++;
                            }
                        }
                        curs->moveNext();
                    }
                }
                //qDebug() << result;
            }
            m_conn->deleteCursor(curs);
        } else {
            qDebug() << "Error executing query";
        }
#endif
    } else {
        if (type == DataSource::Heartrate) {
            qry = "SELECT min(timestamp_dt), avg(heartrate), min(timestamp) FROM mi_band_activity WHERE heartrate < 255 AND timestamp >= '" +
                    QString::number(sd.toMSecsSinceEpoch() / 1000) +  "' AND timestamp <= '" +
                    QString::number((sd.toMSecsSinceEpoch() / 1000) + 86400)  + "' GROUP BY timestamp / 300 ORDER BY min(timestamp) ASC";
        } else if (type == DataSource::Steps) {
            qry = "SELECT min(timestamp_dt), sum(steps), min(timestamp) FROM mi_band_activity WHERE timestamp >= '" +
                    QString::number(sd.toMSecsSinceEpoch() / 1000) +  "' AND timestamp <= '" +
                    QString::number((sd.toMSecsSinceEpoch() / 1000) + 86400) +  "' GROUP BY timestamp / 300 ORDER BY min(timestamp) ASC";
        } else if (type == DataSource::Intensity) {
            qry = "SELECT min(timestamp_dt), avg((raw_intensity / 255.0) * 100), min(timestamp) FROM mi_band_activity WHERE timestamp >= '" +
                    QString::number(sd.toMSecsSinceEpoch() / 1000) +  "' AND timestamp <= '" +
                    QString::number((sd.toMSecsSinceEpoch() / 1000) + 86400) +  "' GROUP BY timestamp / 300 ORDER BY min(timestamp) ASC";
        } else if (type == DataSource::StepSummary) {
            qry = "SELECT date(timestamp_dt), sum(steps) FROM mi_band_activity WHERE date(timestamp_dt) >= date('" +
                    day.toString("yyyy-MM-ddT00:00:00") + "','-10 day') AND timestamp_dt <= '" +
                    day.toString("yyyy-MM-ddT23:59:59") +  "' GROUP BY date(timestamp_dt) ORDER BY timestamp_dt ASC";
        } else if (type == DataSource::BatteryLog) {
            qry = "SELECT timestamp_dt, value FROM info_log WHERE date(timestamp_dt) >= date('" +
                    day.toString("yyyy-MM-ddT00:00:00") + "','-10 day') AND timestamp_dt <= '" +
                    day.toString("yyyy-MM-ddT23:59:59") +  "' AND key = '"+ QString::number((int)Amazfish::Info::INFO_BATTERY) +"' ORDER BY timestamp_dt ASC"; // 7 = AbstractDevice::INFO_BATTERY
        } else if (type == DataSource::HRV) {
            qry = "SELECT hrv_timestamp_dt, hrv_value FROM hrv WHERE hrv_timestamp_dt >= '" +
                    day.toString("yyyy-MM-ddT00:00:00") + "' AND hrv_timestamp_dt <= '" +
                    day.toString("yyyy-MM-ddT23:59:59") +  "' ORDER BY hrv_timestamp_dt ASC";
        } else if (type == DataSource::Spo2Normal) {
            qry = "SELECT max(spo2_timestamp_dt), avg(spo2_value) FROM spo2 WHERE date(spo2_timestamp_dt) >= date('" +
                    day.toString("yyyy-MM-ddT00:00:00") + "','-10 day') AND spo2_timestamp_dt <= '" +
                    day.toString("yyyy-MM-ddT23:59:59") +  "' GROUP BY date(spo2_timestamp_dt) ORDER BY date(spo2_timestamp_dt) ASC";
        } else if (type == DataSource::Spo2Sleep) {
            qry = "SELECT max(spo2sleep_timestamp_dt), avg(spo2sleep_value) FROM spo2sleep WHERE date(spo2sleep_timestamp_dt) >= date('" +
                    day.toString("yyyy-MM-ddT00:00:00") + "','-10 day') AND spo2sleep_timestamp_dt <= '" +
                    day.toString("yyyy-MM-ddT23:59:59") +  "' GROUP BY date(spo2sleep_timestamp_dt) ORDER BY date(spo2sleep_timestamp_dt) ASC";
        } else if (type == DataSource::BodyTemperature) {
            qry = "SELECT temperature_timestamp_dt, temperature_value FROM temperature WHERE temperature_timestamp_dt >= '" +
                    day.toString("yyyy-MM-ddT00:00:00") + "' AND temperature_timestamp_dt <= '" +
                    day.toString("yyyy-MM-ddT23:59:59") +  "' AND temperature_type = 'skin' AND temperature_location = 'wrist'" +
                    " ORDER BY temperature_timestamp_dt ASC";
        } else if (type == DataSource::StressAuto) {
            qry = "SELECT stress_timestamp_dt, stress_value FROM stress WHERE stress_type = 1 AND stress_timestamp_dt >= '" +
                    day.toString("yyyy-MM-ddT00:00:00") + "' AND stress_timestamp_dt <= '" +
                    day.toString("yyyy-MM-ddT23:59:59") +  "' AND stress_type = 1" +
                    " ORDER BY stress_timestamp_dt ASC";
        } else if (type == DataSource::StressSummary) {
            qry = "SELECT date(stress_timestamp_dt), sum(stress_value) / count(stress_value) FROM stress WHERE date(stress_timestamp_dt) >= date('" +
                    day.toString("yyyy-MM-ddT00:00:00") + "','-10 day') AND stress_timestamp_dt <= '" +
                    day.toString("yyyy-MM-ddT23:59:59") +  "' GROUP BY date(stress_timestamp_dt) ORDER BY date(stress_timestamp_dt) ASC";
        } else if (type == DataSource::StressManual) { //Latest Manual reading
            qry = "SELECT stress_timestamp_dt, stress_value FROM stress WHERE stress_type = 2"
                    " ORDER BY stress_timestamp_dt DESC LIMIT 1";
        } else if (type == DataSource::TrainingLoad) {
            qry = "SELECT sd.start_timestamp_dt, CAST(sm.value AS REAL) "
                    "FROM sports_data sd JOIN sports_meta sm ON sm.sport_id = sd.id "
                    "WHERE sm.key = 'currentWorkoutLoad' AND date(sd.start_timestamp_dt) = date('" +
                    day.toString("yyyy-MM-dd") + "') "
                    "ORDER BY sd.start_timestamp_dt ASC";
        }

        qDebug() << qry;
        if (m_conn && m_conn->isDatabaseUsed()) {
            KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(qry));

            if (curs) {
                if (curs->open() && curs->moveFirst()) {
                    while (!curs->eof()) {

                        QVariant d = curs->value(0);
                        QVariant h = curs->value(1);

                        QVariantMap pt;
                        pt["x"] = d.toDateTime().toMSecsSinceEpoch() / 1000;
                        pt["y"] = h.toDouble();
                        result.append(pt);

                        curs->moveNext();
                    }
                    //qDebug() << result;
                }
                m_conn->deleteCursor(curs);
            } else {
                qDebug() << "Error executing query";
            }
        }
    }
    return result;
}

QList<DataSource::SleepSession> DataSource::calculateSleep(const QDate &day)
{
    // qDebug() << "Calculating sleep for " << day;
    QDateTime startDate = QDateTime(day, QTime(12, 00)); //In localtime
    QDateTime endDate = startDate.addDays(1);

    if (m_conn && m_conn->isDatabaseUsed() && m_conn->containsTable("garmin_sleep") == true) {
        const qint64 start = startDate.toMSecsSinceEpoch() / 1000;
        const qint64 end = endDate.toMSecsSinceEpoch() / 1000;
        KDbCursor *check = m_conn->executeQuery(KDbEscapedString(
                "SELECT COUNT(*) FROM garmin_sleep WHERE timestamp >= %1 AND timestamp < %2")
                .arg(start).arg(end));
        int garminStages = 0;
        if (check) {
            if (check->open() && check->moveFirst()) {
                garminStages = check->value(0).toInt();
            }
            m_conn->deleteCursor(check);
        }
        if (garminStages > 0) {
            return calculateGarminSleep(day);
        }
    }

    QList<DataSource::SleepSession> sessions;

    long previousSample = 0;
    int previousKind = 0;
    QDateTime sleepStart;
    QDateTime sleepEnd;
    long lightSleepDuration = 0;
    long deepSleepDuration = 0;
    long durationSinceLastSleep = 0;

    QString qry = "SELECT timestamp, raw_kind, raw_intensity FROM mi_band_activity WHERE timestamp >= " +
            QString::number(startDate.toMSecsSinceEpoch() / 1000) + " AND timestamp <= '" +
            QString::number(endDate.toMSecsSinceEpoch() / 1000) +   "' ORDER BY timestamp ASC";

    // qDebug() << qry;

    if (m_conn && m_conn->isDatabaseUsed()) {
        KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(qry));

        if (curs) {
            if (curs->open() && curs->moveFirst()) {
                while (!curs->eof()) {

                    qint64 d = curs->value(0).toLongLong();
                    int k = kind(curs->value(1).toInt());
                    const bool previousWasSleep = isSleep(previousKind);

                    if (isSleep(k)) {
                        if (!sleepStart.isValid()) {
                            sleepStart.setMSecsSinceEpoch(d * 1000);
                            qDebug() << "Setting sleep start to " << d << sleepStart;
                        }
                        sleepEnd.setMSecsSinceEpoch(d * 1000);
                        durationSinceLastSleep = 0;
                    }
                    if (previousSample != 0) {
                        const long durationSinceLastSample = d - previousSample;

                        // Only count an interval as sleep if the previous sample
                        // was already a sleep stage. Otherwise the gap between
                        // the last activity and falling asleep would be counted
                        // as light sleep.
                        if (previousWasSleep) {
                            if (k == TYPE_LIGHT_SLEEP) {
                                lightSleepDuration += durationSinceLastSample;
                            } else if (k == TYPE_DEEP_SLEEP) {
                                deepSleepDuration += durationSinceLastSample;
                            }
                        }

                        if (!isSleep(k)) {
                            durationSinceLastSleep += durationSinceLastSample;
                            if (sleepStart.isValid() && durationSinceLastSleep > MAX_WAKE_PHASE_LENGTH) {
                                if (lightSleepDuration + deepSleepDuration > MIN_SESSION_LENGTH) {
                                    SleepSession ss;
                                    ss.sleepStart = sleepStart;
                                    ss.sleepEnd = sleepEnd;
                                    ss.lightSleepDuration = lightSleepDuration;
                                    ss.deepSleepDuration = deepSleepDuration;
                                    ss.remSleepDuration = 0;
                                    sessions << ss;
                                    // qDebug() << "Sleep Session" << ss.sleepStart << ss.sleepEnd << ss.lightSleepDuration << ss.deepSleepDuration;

                                }
                                sleepStart = QDateTime();
                                sleepEnd = QDateTime();
                                lightSleepDuration = 0;
                                deepSleepDuration = 0;
                                durationSinceLastSleep = 0;
                            }
                        }
                    }
                    previousSample = d;
                    previousKind = k;
                    curs->moveNext();
                }
                if (lightSleepDuration + deepSleepDuration > MIN_SESSION_LENGTH) {
                    SleepSession ss;
                    ss.sleepStart = sleepStart;
                    ss.sleepEnd = sleepEnd;
                    ss.lightSleepDuration = lightSleepDuration;
                    ss.deepSleepDuration = deepSleepDuration;
                    ss.remSleepDuration = 0;
                    sessions << ss;
                }
            }
            m_conn->deleteCursor(curs);
        } else {
            qDebug() << "Error executing query";
        }
    }
    
    return sessions;
    
}

QList<DataSource::SleepNight> DataSource::garminSleepNights(const QDate &day)
{
    QList<SleepNight> nights;

    if (!m_conn || !m_conn->isDatabaseUsed()
            || m_conn->containsTable("garmin_sleep") != true) {
        return nights;
    }

    const QDateTime startDate = QDateTime(day, QTime(12, 00));
    const QDateTime endDate = startDate.addDays(1);
    const qint64 start = startDate.toMSecsSinceEpoch() / 1000;
    const qint64 end = endDate.toMSecsSinceEpoch() / 1000;

    // The importer stores the sleep window from the watch (onset and wake
    // time). It is needed because Garmin timestamps a stage at the end of the
    // span it covers: the first span starts at the window onset.
    if (m_conn->containsTable("garmin_sleep_window") == true) {
        KDbCursor *windows = m_conn->executeQuery(KDbEscapedString(
                "SELECT start_ts, end_ts FROM garmin_sleep_window "
                "WHERE end_ts > %1 AND start_ts < %2 ORDER BY start_ts ASC")
                .arg(start).arg(end));
        if (windows) {
            if (windows->open() && windows->moveFirst()) {
                while (!windows->eof()) {
                    SleepNight night;
                    night.start = windows->value(0).toLongLong();
                    night.end = windows->value(1).toLongLong();
                    if (night.end > night.start) {
                        nights << night;
                    }
                    windows->moveNext();
                }
            }
            m_conn->deleteCursor(windows);
        }
    }

    QList<QPair<qint64, int>> rows;
    KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(
            "SELECT timestamp, stage FROM garmin_sleep "
            "WHERE timestamp >= %1 AND timestamp < %2 ORDER BY timestamp ASC")
            .arg(start).arg(end));
    if (!curs) {
        return nights;
    }
    if (curs->open() && curs->moveFirst()) {
        while (!curs->eof()) {
            rows << qMakePair(curs->value(0).toLongLong(), curs->value(1).toInt());
            curs->moveNext();
        }
    }
    m_conn->deleteCursor(curs);

    if (nights.isEmpty()) {
        // Night imported before the sleep window was stored: fall back to the
        // first stage change as the beginning. Its own span is unknown.
        if (rows.isEmpty()) {
            return nights;
        }
        SleepNight night;
        night.start = rows.first().first;
        night.end = rows.last().first;
        for (int i = 1; i < rows.size(); ++i) {
            SleepPhase phase;
            phase.start = rows.at(i - 1).first;
            phase.end = rows.at(i).first;
            phase.stage = rows.at(i).second;
            if (phase.end > phase.start) {
                night.phases << phase;
            }
        }
        nights << night;
        return nights;
    }

    for (SleepNight &night : nights) {
        qint64 previous = night.start;
        for (const QPair<qint64, int> &row : rows) {
            if (row.first < night.start || row.first > night.end) {
                continue;
            }
            if (row.first <= previous) {
                previous = row.first;
                continue;
            }
            SleepPhase phase;
            phase.start = previous;
            phase.end = row.first;
            phase.stage = row.second;
            night.phases << phase;
            previous = row.first;
        }
    }

    return nights;
}

QList<DataSource::SleepSession> DataSource::calculateGarminSleep(const QDate &day)
{
    QList<SleepSession> sessions;

    const QList<SleepNight> nights = garminSleepNights(day);
    for (const SleepNight &night : nights) {
        SleepSession session;
        session.sleepStart = QDateTime::fromMSecsSinceEpoch(night.start * 1000LL);
        session.sleepEnd = QDateTime::fromMSecsSinceEpoch(night.end * 1000LL);

        for (const SleepPhase &phase : night.phases) {
            const long duration = long(phase.end - phase.start);
            if (duration <= 0) {
                continue;
            }
            switch (phase.stage) {
            case 1: // AWAKE
                session.awakeSleepDuration += duration;
                break;
            case 2: // LIGHT
                session.lightSleepDuration += duration;
                break;
            case 3: // DEEP
                session.deepSleepDuration += duration;
                break;
            case 4: // REM
                session.remSleepDuration += duration;
                break;
            default: // UNMEASURABLE
                break;
            }
        }

        if (session.lightSleepDuration + session.deepSleepDuration
                + session.remSleepDuration > MIN_SESSION_LENGTH) {
            sessions << session;
        }
    }

    return sessions;
}

QDate DataSource::latestSleepDate()
{
    if (!m_conn || !m_conn->isDatabaseUsed()) {
        return QDate();
    }

    QString query;
    if (m_conn->containsTable("garmin_sleep") == true) {
        query = QStringLiteral("SELECT MAX(timestamp) FROM garmin_sleep");
    } else {
        query = QStringLiteral("SELECT MAX(timestamp) FROM mi_band_activity "
                               "WHERE raw_kind IN (9,11)");
    }

    KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(query));
    if (!curs) {
        return QDate();
    }

    QDate result;
    if (curs->open() && curs->moveFirst()) {
        const QVariant value = curs->value(0);
        if (!value.isNull()) {
            const qint64 timestamp = value.toLongLong();
            result = QDateTime::fromMSecsSinceEpoch(timestamp * 1000LL).date();
        }
    }
    m_conn->deleteCursor(curs);
    return result;
}

QVariantMap DataSource::sleepBreakdown(const QDate &day)
{
    QVariantMap breakdown;

    // Garmin files a night under the date of the wake-up, so the window that
    // ends on this date starts the previous afternoon.
    const QList<SleepSession> sessions = calculateSleep(day.addDays(-1));

    // Report the main night of the day. A nap is a separate session.
    const SleepSession *main = nullptr;
    long longest = -1;
    for (const SleepSession &session : sessions) {
        const long asleep = session.lightSleepDuration + session.deepSleepDuration
                + session.remSleepDuration;
        if (asleep > longest) {
            longest = asleep;
            main = &session;
        }
    }

    const long light = main ? main->lightSleepDuration : 0;
    const long deep = main ? main->deepSleepDuration : 0;
    const long rem = main ? main->remSleepDuration : 0;
    const long awake = main ? main->awakeSleepDuration : 0;

    breakdown["light"] = light / 3600.0;
    breakdown["deep"] = deep / 3600.0;
    breakdown["rem"] = rem / 3600.0;
    breakdown["awake"] = awake / 3600.0;
    breakdown["total"] = (light + deep + rem) / 3600.0;
    breakdown["inBed"] = (main && main->sleepStart.isValid())
            ? main->sleepStart.secsTo(main->sleepEnd) / 3600.0 : 0.0;
    breakdown["intervalStart"] = main ? main->sleepStart : QDateTime();
    breakdown["intervalEnd"] = main ? main->sleepEnd : QDateTime();
    return breakdown;
}

QVariantList DataSource::sleepPhases(const QDate &day)
{
    QVariantList result;

    const QList<SleepNight> nights = garminSleepNights(day.addDays(-1));
    if (nights.isEmpty()) {
        return result;
    }

    int main = 0;
    qint64 longest = -1;
    for (int i = 0; i < nights.size(); ++i) {
        qint64 asleep = 0;
        for (const SleepPhase &phase : nights.at(i).phases) {
            if (phase.stage >= 2 && phase.stage <= 4) {
                asleep += phase.end - phase.start;
            }
        }
        if (asleep > longest) {
            longest = asleep;
            main = i;
        }
    }

    for (const SleepPhase &phase : nights.at(main).phases) {
        QVariantMap entry;
        entry["start"] = phase.start;
        entry["end"] = phase.end;
        entry["stage"] = phase.stage;
        result << entry;
    }
    return result;
}

QVariantList DataSource::sleepHistory(int days)
{
    QVariantList result;

    if (days <= 0) {
        days = 7;
    }
    if (days > 30) {
        days = 30;
    }

    QDate anchor = latestSleepDate();
    if (!anchor.isValid()) {
        anchor = QDate::currentDate();
    }

    for (int i = days - 1; i >= 0; --i) {
        const QDate day = anchor.addDays(-i);
        const QVariantMap stats = sleepStats(day);
        const QVariantMap breakdown = sleepBreakdown(day);

        QVariantMap entry;
        entry["date"] = day;
        entry["score"] = stats.value("overallScore", -1);
        entry["light"] = breakdown.value("light", 0.0);
        entry["deep"] = breakdown.value("deep", 0.0);
        entry["rem"] = breakdown.value("rem", 0.0);
        entry["awake"] = breakdown.value("awake", 0.0);
        entry["total"] = breakdown.value("total", 0.0);
        result << entry;
    }

    return result;
}

QVariantMap DataSource::sleepStats(const QDate &day)
{
    QVariantMap stats;
    if (!m_conn || !m_conn->isDatabaseUsed()
            || m_conn->containsTable("garmin_sleep_stats") != true) {
        return stats;
    }
    KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(
            "SELECT overall_score, quality_score, recovery_score, average_stress "
            "FROM garmin_sleep_stats WHERE date(timestamp_dt) = date('%1') "
            "ORDER BY timestamp DESC LIMIT 1").arg(day.toString("yyyy-MM-dd")));
    if (!curs) {
        return stats;
    }
    if (curs->open() && curs->moveFirst()) {
        stats["overallScore"] = curs->value(0).toInt();
        stats["qualityScore"] = curs->value(1).toInt();
        stats["recoveryScore"] = curs->value(2).toInt();
        stats["averageStress"] = curs->value(3).toDouble();
    }
    m_conn->deleteCursor(curs);
    return stats;
}

namespace {

QVariant scalarQuery(KDbConnection *conn, const QString &query)
{
    if (!conn || !conn->isDatabaseUsed()) {
        return QVariant();
    }

    KDbCursor *curs = conn->executeQuery(KDbEscapedString(query));
    if (!curs) {
        return QVariant();
    }

    QVariant result;
    if (curs->open() && curs->moveFirst()) {
        result = curs->value(0);
    }
    conn->deleteCursor(curs);
    return result;
}

QString formatSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString garminDataPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/garmin";
}

} // namespace

QVariantMap DataSource::trainingLoadStats()
{
    QVariantMap stats;
    if (!m_conn || !m_conn->isDatabaseUsed()
            || m_conn->containsTable("sports_data") != true
            || m_conn->containsTable("sports_meta") != true) {
        return stats;
    }

    QMap<QDate, double> loads;
    KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(
            "SELECT date(sd.start_timestamp_dt), SUM(CAST(sm.value AS REAL)) "
            "FROM sports_data sd JOIN sports_meta sm ON sm.sport_id = sd.id "
            "WHERE sm.key = 'currentWorkoutLoad' "
            "GROUP BY date(sd.start_timestamp_dt) "
            "ORDER BY date(sd.start_timestamp_dt)"));
    if (curs) {
        if (curs->open() && curs->moveFirst()) {
            while (!curs->eof()) {
                QDate date = curs->value(0).toDate();
                if (!date.isValid()) {
                    date = QDate::fromString(curs->value(0).toString(), Qt::ISODate);
                }
                if (date.isValid()) {
                    loads[date] += curs->value(1).toDouble();
                }
                curs->moveNext();
            }
        }
        m_conn->deleteCursor(curs);
    }

    const QDate today = QDate::currentDate();
    double acute = 0.0;
    double chronic = 0.0;
    for (auto it = loads.constBegin(); it != loads.constEnd(); ++it) {
        const int daysAgo = it.key().daysTo(today);
        if (daysAgo >= 0 && daysAgo < 7) {
            acute += it.value();
        }
        if (daysAgo >= 0 && daysAgo < 28) {
            chronic += it.value();
        }
    }

    stats["acute"] = acute;
    stats["chronic"] = chronic;
    stats["ratio"] = chronic > 0.0 ? (acute / 7.0) / (chronic / 28.0) : 0.0;
    stats["activities"] = scalarQuery(m_conn,
            "SELECT COUNT(*) FROM sports_data");
    stats["lastLoad"] = scalarQuery(m_conn,
            "SELECT sm.value FROM sports_data sd "
            "JOIN sports_meta sm ON sm.sport_id = sd.id "
            "WHERE sm.key = 'currentWorkoutLoad' "
            "ORDER BY sd.start_timestamp_dt DESC LIMIT 1");
    stats["lastName"] = scalarQuery(m_conn,
            "SELECT name FROM sports_data "
            "ORDER BY start_timestamp_dt DESC LIMIT 1");
    return stats;
}

QVariantList DataSource::activityLaps(uint activityId)
{
    QVariantList laps;
    if (!m_conn || !m_conn->isDatabaseUsed()
            || m_conn->containsTable("garmin_activity_laps") != true) {
        return laps;
    }

    KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(
            "SELECT lap_index, timestamp_dt, elapsed, timer, distance, "
            "average_speed, max_speed, average_heart_rate, max_heart_rate, "
            "calories, ascent, descent "
            "FROM garmin_activity_laps WHERE sport_id = %1 ORDER BY lap_index ASC")
            .arg(activityId));
    if (!curs) {
        return laps;
    }
    if (curs->open() && curs->moveFirst()) {
        while (!curs->eof()) {
            QVariantMap lap;
            lap["index"] = curs->value(0).toInt();
            lap["time"] = curs->value(1).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
            lap["elapsed"] = curs->value(2).toDouble();
            lap["timer"] = curs->value(3).toDouble();
            lap["distance"] = curs->value(4).toDouble();
            lap["averageSpeed"] = curs->value(5).toDouble();
            lap["maxSpeed"] = curs->value(6).toDouble();
            lap["averageHeartRate"] = curs->value(7).toInt();
            lap["maxHeartRate"] = curs->value(8).toInt();
            lap["calories"] = curs->value(9).toInt();
            lap["ascent"] = curs->value(10).toInt();
            lap["descent"] = curs->value(11).toInt();
            if (lap.value("distance").toDouble() > 0.0) {
                double duration = lap.value("timer").toDouble();
                if (duration <= 0.0) {
                    duration = lap.value("elapsed").toDouble();
                }
                if (duration > 0.0) {
                    lap["pace"] = duration / 60.0
                            / (lap.value("distance").toDouble() / 1000.0);
                }
            }
            laps << lap;
            curs->moveNext();
        }
    }
    m_conn->deleteCursor(curs);
    return laps;
}

QVariantMap DataSource::latestSpo2()
{
    QVariantMap spo2;
    if (!m_conn || !m_conn->isDatabaseUsed()
            || m_conn->containsTable("spo2") != true) {
        return spo2;
    }
    KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(
            "SELECT spo2_timestamp_dt, spo2_value FROM spo2 "
            "ORDER BY spo2_timestamp_dt DESC LIMIT 1"));
    if (!curs) {
        return spo2;
    }
    if (curs->open() && curs->moveFirst()) {
        spo2["time"] = curs->value(0).toDateTime().toString("yyyy-MM-dd HH:mm:ss");
        spo2["value"] = curs->value(1).toInt();
    }
    m_conn->deleteCursor(curs);
    return spo2;
}

QVariantList DataSource::watchFiles()
{
    QVariantList files;

    QDir garminDir(garminDataPath());
    if (!garminDir.exists()) {
        return files;
    }

    QDirIterator it(garminDir.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        const QString relative = garminDir.relativeFilePath(path);
        QString type = relative.section('/', 0, -2);
        if (type.isEmpty()) {
            type = info.fileName().section('_', 0, 0);
        }

        QVariantMap entry;
        entry["type"] = type;
        entry["name"] = info.fileName();
        entry["path"] = path;
        entry["size"] = qint64(info.size());
        entry["sizeText"] = formatSize(info.size());
        entry["modified"] = info.lastModified().toString("yyyy-MM-dd HH:mm");
        entry["modifiedEpoch"] = info.lastModified().toMSecsSinceEpoch();
        files << entry;
    }

    std::sort(files.begin(), files.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("modifiedEpoch").toLongLong()
                > b.toMap().value("modifiedEpoch").toLongLong();
    });

    return files;
}

QVariantMap DataSource::watchSummary()
{
    QVariantMap summary;
    summary["databasePath"] = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + "/garminfish.kexi";
    summary["garminPath"] = garminDataPath();

    // FIT files on disk, grouped by the directory the daemon stores them in.
    int fitFiles = 0;
    int monitorFiles = 0;
    int sleepFiles = 0;
    int activityFiles = 0;
    qint64 fitBytes = 0;
    QDir garminDir(garminDataPath());
    if (garminDir.exists()) {
        QDirIterator it(garminDir.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo info(it.next());
            const QString relative = garminDir.relativeFilePath(info.filePath());
            const QString type = relative.section('/', 0, -2).toUpper();
            ++fitFiles;
            fitBytes += info.size();
            if (type == "MONITOR") {
                ++monitorFiles;
            } else if (type == "SLEEP") {
                ++sleepFiles;
            } else if (type == "ACTIVITY") {
                ++activityFiles;
            }
        }
    }

    summary["fitFiles"] = fitFiles;
    summary["monitorFiles"] = monitorFiles;
    summary["sleepFiles"] = sleepFiles;
    summary["activityFiles"] = activityFiles;
    summary["fitBytes"] = fitBytes;
    summary["fitSize"] = formatSize(fitBytes);

    if (m_conn && m_conn->isDatabaseUsed()) {
        if (m_conn->containsTable("mi_band_activity") == true) {
            summary["activitySamples"] = scalarQuery(m_conn,
                    "SELECT COUNT(*) FROM mi_band_activity");
            const QVariant lastSample = scalarQuery(m_conn,
                    "SELECT MAX(timestamp) FROM mi_band_activity");
            if (!lastSample.isNull()) {
                summary["lastSample"] = QDateTime::fromMSecsSinceEpoch(
                        lastSample.toLongLong() * 1000LL).toString("yyyy-MM-dd HH:mm");
            }
        }
        if (m_conn->containsTable("garmin_monitor") == true) {
            summary["monitorSamples"] = scalarQuery(m_conn,
                    "SELECT COUNT(*) FROM garmin_monitor");
        }
        if (m_conn->containsTable("garmin_sleep") == true) {
            summary["sleepStages"] = scalarQuery(m_conn,
                    "SELECT COUNT(*) FROM garmin_sleep");
            summary["sleepNights"] = scalarQuery(m_conn,
                    "SELECT COUNT(DISTINCT date(timestamp, 'unixepoch', 'localtime')) "
                    "FROM garmin_sleep");
        }
        if (m_conn->containsTable("sports_data") == true) {
            summary["activities"] = scalarQuery(m_conn,
                    "SELECT COUNT(*) FROM sports_data");
            summary["lastActivityName"] = scalarQuery(m_conn,
                    "SELECT name FROM sports_data ORDER BY start_timestamp_dt DESC LIMIT 1");
            summary["lastActivityLoad"] = scalarQuery(m_conn,
                    "SELECT sm.value FROM sports_data sd "
                    "JOIN sports_meta sm ON sm.sport_id = sd.id "
                    "WHERE sm.key = 'currentWorkoutLoad' "
                    "ORDER BY sd.start_timestamp_dt DESC LIMIT 1");
        }
        if (m_conn->containsTable("info_log") == true) {
            summary["battery"] = scalarQuery(m_conn,
                    "SELECT value FROM info_log WHERE key = 7 ORDER BY id DESC LIMIT 1");
        }
        if (m_conn->containsTable("garmin_body_energy") == true) {
            summary["bodyEnergy"] = scalarQuery(m_conn,
                    "SELECT value FROM garmin_body_energy ORDER BY timestamp DESC LIMIT 1");
        }

        const QDate today = QDate::currentDate();
        const qint64 dayStart = QDateTime(today, QTime(0, 0)).toMSecsSinceEpoch() / 1000;
        const qint64 dayEnd = QDateTime(today.addDays(1), QTime(0, 0)).toMSecsSinceEpoch() / 1000;
        summary["stepsToday"] = scalarQuery(m_conn,
                QStringLiteral("SELECT SUM(steps) FROM mi_band_activity "
                               "WHERE timestamp >= %1 AND timestamp < %2")
                .arg(dayStart).arg(dayEnd));
        summary["lastHeartRate"] = scalarQuery(m_conn,
                "SELECT heartrate FROM mi_band_activity WHERE heartrate > 0 "
                "ORDER BY timestamp DESC LIMIT 1");
        summary["lastHeartRateTime"] = scalarQuery(m_conn,
                "SELECT timestamp FROM mi_band_activity WHERE heartrate > 0 "
                "ORDER BY timestamp DESC LIMIT 1");
        if (!summary.value("lastHeartRateTime").isNull()) {
            summary["lastHeartRateTime"] = QDateTime::fromMSecsSinceEpoch(
                    summary.value("lastHeartRateTime").toLongLong() * 1000LL)
                    .toString("yyyy-MM-dd HH:mm");
        }
    }

    summary["latestSleep"] = latestSleepDate().isValid()
            ? latestSleepDate().toString("yyyy-MM-dd") : QString();

    const QFileInfo dbInfo(summary.value("databasePath").toString());
    summary["databaseSize"] = dbInfo.exists() ? formatSize(dbInfo.size()) : QStringLiteral("-");

    return summary;
}

QVariantMap DataSource::latestHrvSummary()
{
    QVariantMap summary;
    if (!m_conn || !m_conn->isDatabaseUsed()
            || m_conn->containsTable("garmin_hrv_summary") != true) {
        return summary;
    }

    KDbCursor *curs = m_conn->executeQuery(KDbEscapedString(
            "SELECT timestamp_dt, weekly_average, last_night_average, "
            "last_night_5min_high, baseline_low, baseline_balanced_low, "
            "baseline_balanced_high, status "
            "FROM garmin_hrv_summary ORDER BY timestamp DESC LIMIT 1"));
    if (!curs) {
        return summary;
    }

    if (curs->open() && curs->moveFirst()) {
        summary["date"] = curs->value(0).toDateTime().toString("yyyy-MM-dd");
        summary["weeklyAverage"] = curs->value(1).toDouble();
        summary["lastNightAverage"] = curs->value(2).toDouble();
        summary["lastNight5MinHigh"] = curs->value(3).toDouble();
        summary["baselineLow"] = curs->value(4).toDouble();
        summary["baselineBalancedLow"] = curs->value(5).toDouble();
        summary["baselineBalancedHigh"] = curs->value(6).toDouble();
        const int status = curs->value(7).toInt();
        QString statusText;
        switch (status) {
        case 1: statusText = tr("Poor"); break;
        case 2: statusText = tr("Low"); break;
        case 3: statusText = tr("Unbalanced"); break;
        case 4: statusText = tr("Balanced"); break;
        default: statusText = tr("No status"); break;
        }
        summary["status"] = statusText;
    }
    m_conn->deleteCursor(curs);
    return summary;
}

QVariantMap DataSource::inspectFitFile(const QString &path)
{
    return FitFileInspector::inspect(path);
}

QString DataSource::exportFitFile(const QString &path)
{
    const QFileInfo source(path);
    if (!source.exists() || !source.isFile()) {
        return QString();
    }

    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/Downloads";
    }
    if (!QDir().mkpath(directory)) {
        return QString();
    }

    QString destination = directory + "/" + source.fileName();
    if (QFile::exists(destination)) {
        destination = directory + "/"
                + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-")
                + source.fileName();
    }
    if (!QFile::copy(path, destination)) {
        return QString();
    }
    return destination;
}

bool DataSource::isSleep(int kind)
{
    int k = (int)kind & 0xf;
    return k == TYPE_DEEP_SLEEP || k == TYPE_LIGHT_SLEEP;
}
