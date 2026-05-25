#ifndef RIGCTLCOMPAT_H
#define RIGCTLCOMPAT_H

#include <QList>
#include <QString>
#include <QStringList>

namespace rigctlcompat
{

struct ModeBandwidths {
    int normal;
    int narrow;
    int wide;
};

int hamlibAntennaIndex(int rigNumber, const QList<int> &rigNumbers);
quint8 rigAntennaNumber(int hamlibIndex, const QList<int> &rigNumbers);
quint32 antennaMask(const QList<int> &rigNumbers);
QString antennaName(int hamlibIndex);
quint8 antennaIndexFromName(const QString &name);
bool isVfoName(const QString &name);
ModeBandwidths modeBandwidths(const QString &modeName);
QStringList modeBandwidthResponse(const QString &modeName);

}

#endif
