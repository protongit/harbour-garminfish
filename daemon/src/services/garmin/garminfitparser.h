#ifndef GARMINFITPARSER_H
#define GARMINFITPARSER_H

#include <QByteArray>
#include <QString>

class KDbConnection;

/**
 * Minimal FIT importer for Garmin daily activity and sleep data.
 *
 * It intentionally understands only the message types Amazfish needs:
 *  - FILE_ID / SPORT / SESSION / RECORD (activities)
 *  - MONITORING (55): steps, heart rate, intensity
 *  - STRESS_LEVEL (227): all-day stress
 *  - HRV_SUMMARY (370) / HRV_VALUE (371): HRV status
 *  - EVENT (21, event 74): sleep window start/stop
 *  - SLEEP_LEVEL (275): sleep stages, timestamped at the END of each stage
 *
 * Developer fields are skipped by their definition-provided size, so Garmin
 * activity files with custom fields (jump rope, strength, etc.) parse cleanly.
 */
class GarminFitParser
{
public:
    static const int ParserVersion = 7;

    static int importFile(const QByteArray &data,
                          KDbConnection *db,
                          uint deviceId,
                          uint userId,
                          const QString &sourceType);
};

#endif // GARMINFITPARSER_H
