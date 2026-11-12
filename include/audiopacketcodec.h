#ifndef AUDIOPACKETCODEC_H
#define AUDIOPACKETCODEC_H

#include "audioconverter.h"

#include <QDataStream>

namespace audioPacketCodec {

inline QByteArray encode(const audioPacket &packet)
{
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12);
    stream << quint32(0x57464150); // WFAP
    stream << quint8(1);
    stream << packet.seq;
    stream << quint32(packet.time.isValid() ? packet.time.msecsSinceStartOfDay() : 0);
    stream << packet.sent;
    stream << packet.data;
    stream.writeRawData(reinterpret_cast<const char *>(packet.guid), GUIDLEN);
    stream << packet.amplitudePeak;
    stream << packet.amplitudeRMS;
    stream << double(packet.volume);
    return out;
}

inline bool decode(const QByteArray &data, audioPacket *packet)
{
    if (packet == Q_NULLPTR) {
        return false;
    }

    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_5_12);

    quint32 magic = 0;
    quint8 version = 0;
    quint32 msecs = 0;
    double volume = 1.0;

    stream >> magic;
    stream >> version;
    if (magic != quint32(0x57464150) || version != quint8(1) || stream.status() != QDataStream::Ok) {
        return false;
    }

    stream >> packet->seq;
    stream >> msecs;
    stream >> packet->sent;
    stream >> packet->data;
    if (stream.readRawData(reinterpret_cast<char *>(packet->guid), GUIDLEN) != GUIDLEN) {
        return false;
    }
    stream >> packet->amplitudePeak;
    stream >> packet->amplitudeRMS;
    stream >> volume;

    if (stream.status() != QDataStream::Ok) {
        return false;
    }

    packet->time = QTime::fromMSecsSinceStartOfDay(int(msecs));
    packet->volume = qreal(volume);
    return true;
}

}

#endif // AUDIOPACKETCODEC_H
