#ifndef FITFILEINSPECTOR_H
#define FITFILEINSPECTOR_H

#include <QString>
#include <QVariantMap>

class FitFileInspector
{
public:
    static QVariantMap inspect(const QString &path);
};

#endif // FITFILEINSPECTOR_H
