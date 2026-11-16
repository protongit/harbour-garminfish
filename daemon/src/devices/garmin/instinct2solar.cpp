#include "instinct2solar.h"

GarminInstinct2SolarDevice::GarminInstinct2SolarDevice(const QString &pairedName, QObject *parent)
    : GarminDevice(pairedName, parent)
{
}

Amazfish::Features GarminInstinct2SolarDevice::supportedFeatures() const
{
    return Amazfish::Feature::FEATURE_NONE
        | Amazfish::Feature::FEATURE_HRM
        | Amazfish::Feature::FEATURE_STEPS
        | Amazfish::Feature::FEATURE_ALERT
        | Amazfish::Feature::FEATURE_SPO2
        | Amazfish::Feature::FEATURE_WEATHER
        | Amazfish::Feature::FEATURE_ACTIVITY
        | Amazfish::Feature::FEATURE_MUSIC_CONTROL;
}

Amazfish::DataTypes GarminInstinct2SolarDevice::supportedDataTypes() const
{
    return Amazfish::DataType::TYPE_SPO2
        | Amazfish::DataType::TYPE_HRV
        | Amazfish::DataType::TYPE_HEART_RATE
        | Amazfish::DataType::TYPE_ACTIVITY
        | Amazfish::DataType::TYPE_STRESS
        | Amazfish::DataType::TYPE_SLEEP;
}

QString GarminInstinct2SolarDevice::deviceType() const
{
    return "Garmin Instinct 2 Solar";
}
