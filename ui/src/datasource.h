#ifndef DATASOURCE_H
#define DATASOURCE_H

#include <QObject>
#include <QVariant>
#include <QDate>

#include <KDb3/KDbConnection>

class DataSource: public QObject
{
    Q_OBJECT

public:
    enum Type {
        Heartrate = 1,
        Steps = 2,
        Sleep = 3,
        Intensity = 4,
        StepSummary = 5,
        SleepSummary = 6,
        BatteryLog = 7,
        HRV = 8,
        Spo2Normal = 9,
        Spo2Sleep = 10,
        BodyTemperature = 11,
        StressAuto = 12,
        StressManual = 13,
        StressSummary = 14,
        TrainingLoad = 15,
        TrainingLoadSummary = 16
    };
    Q_ENUM(Type)

    DataSource();
    void setConnection(KDbConnection *conn);

    Q_INVOKABLE QVariant data(const DataSource::Type type, const QDate  &day);
    Q_INVOKABLE QDate latestSleepDate();
    Q_INVOKABLE QVariantMap sleepBreakdown(const QDate &day);
    Q_INVOKABLE QVariantMap sleepStats(const QDate &day);
    Q_INVOKABLE QVariantList sleepPhases(const QDate &day);
    Q_INVOKABLE QVariantList sleepHistory(int days = 7);
    Q_INVOKABLE QVariantMap trainingLoadStats();
    Q_INVOKABLE QVariantList activityLaps(uint activityId);
    Q_INVOKABLE QVariantMap latestSpo2();
    Q_INVOKABLE QVariantList watchFiles();
    Q_INVOKABLE QVariantMap watchSummary();
    Q_INVOKABLE QVariantMap latestHrvSummary();
    Q_INVOKABLE QVariantMap inspectFitFile(const QString &path);
    Q_INVOKABLE QString exportFitFile(const QString &path);

private:
    KDbConnection *m_conn = nullptr;
    
    struct SleepSession {
        QDateTime sleepStart;
        QDateTime sleepEnd;
        long lightSleepDuration = 0;
        long deepSleepDuration = 0;
        long remSleepDuration = 0;
        long awakeSleepDuration = 0;
    };

    struct SleepPhase {
        qint64 start = 0; // Unix seconds
        qint64 end = 0;   // Unix seconds
        int stage = 0;    // FIT sleep level: 0 unmeasurable, 1 awake, 2 light, 3 deep, 4 REM
    };

    struct SleepNight {
        qint64 start = 0;
        qint64 end = 0;
        QList<SleepPhase> phases;
    };

    QList<SleepSession> calculateSleep(const QDate &day);
    QList<SleepSession> calculateGarminSleep(const QDate &day);
    QList<SleepNight> garminSleepNights(const QDate &day);
    bool isSleep(int kind);
};

#endif // DATASOURCE_H
