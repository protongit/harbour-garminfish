#ifndef INSTINCT2SOLAR_DEVICE__H
#define INSTINCT2SOLAR_DEVICE__H

#include <QObject>
#include "garmindevice.h"

class GarminInstinct2SolarDevice : public GarminDevice
{
    Q_OBJECT
public:
    explicit GarminInstinct2SolarDevice(const QString &pairedName, QObject *parent = 0);

    Amazfish::Features supportedFeatures() const override;
    Amazfish::DataTypes supportedDataTypes() const override;
    QString deviceType() const override;
};

#endif // INSTINCT2SOLAR_DEVICE__H
