#ifndef RADIOTRANSPORTFRAME_H
#define RADIOTRANSPORTFRAME_H

#include <QByteArray>

#include "radiotransport.h"

namespace radioTransportFrame {

enum DecodeResult {
    DecodeOk,
    DecodeIncomplete,
    DecodeInvalid
};

struct Frame {
    RadioTransportChannel channel = RadioTransportChannel::Unknown;
    quint32 sequence = 0;
    QByteArray payload;
};

QByteArray encode(const Frame &frame);
DecodeResult decode(const QByteArray &data, Frame *frame, int *bytesUsed = nullptr);

}

#endif // RADIOTRANSPORTFRAME_H
