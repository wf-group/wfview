#ifndef RIGCTLCOMPAT_H
#define RIGCTLCOMPAT_H

#include <QList>
#include <QString>
#include <QStringList>

namespace rigctlcompat
{

// AGC entry from the rig file, matched to Hamlib's agc_level_e by name.
struct AgcEntry {
    int num;
    QString name;
    bool setOnly;
    bool readOnly;
};

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
int hamlibAgcFromName(const QString &name);
int rigAgcFromHamlib(int hamlibAgc, const QList<AgcEntry> &entries);
int hamlibAgcFromRig(int rigAgc, const QList<AgcEntry> &entries);
ModeBandwidths modeBandwidths(const QString &modeName);
QStringList modeBandwidthResponse(const QString &modeName);

}

#endif
