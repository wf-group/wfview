#include "radiotransportframe.h"

#include <QtEndian>
#include <climits>
#include <cstring>

namespace {

constexpr char frameMagic[4] = {'W', 'F', 'R', 'T'};
constexpr quint8 frameVersion = 1;
constexpr int headerSize = 14;

quint8 channelToByte(RadioTransportChannel channel)
{
    return quint8(channel);
}

RadioTransportChannel channelFromByte(quint8 value)
{
    switch (RadioTransportChannel(value)) {
    case RadioTransportChannel::Control:
    case RadioTransportChannel::Civ:
    case RadioTransportChannel::Cat:
    case RadioTransportChannel::AudioRx:
    case RadioTransportChannel::AudioTx:
    case RadioTransportChannel::Scope:
    case RadioTransportChannel::Discovery:
    case RadioTransportChannel::Unknown:
        return RadioTransportChannel(value);
    }

    return RadioTransportChannel::Unknown;
}

}

QByteArray radioTransportFrame::encode(const Frame &frame)
{
    QByteArray out;
    out.resize(headerSize + frame.payload.size());

    char *data = out.data();
    memcpy(data, frameMagic, sizeof(frameMagic));
    data[4] = char(frameVersion);
    data[5] = char(channelToByte(frame.channel));
    qToBigEndian<quint32>(frame.sequence, data + 6);
    qToBigEndian<quint32>(quint32(frame.payload.size()), data + 10);

    if (!frame.payload.isEmpty()) {
        memcpy(data + headerSize, frame.payload.constData(), size_t(frame.payload.size()));
    }

    return out;
}

radioTransportFrame::DecodeResult radioTransportFrame::decode(const QByteArray &data, Frame *frame, int *bytesUsed)
{
    if (bytesUsed != nullptr) {
        *bytesUsed = 0;
    }

    if (data.size() < headerSize) {
        return DecodeIncomplete;
    }

    if (memcmp(data.constData(), frameMagic, sizeof(frameMagic)) != 0 || quint8(data[4]) != frameVersion) {
        return DecodeInvalid;
    }

    const quint32 payloadSize = qFromBigEndian<quint32>(data.constData() + 10);
    if (payloadSize > quint32(INT_MAX - headerSize)) {
        return DecodeInvalid;
    }

    const int totalSize = headerSize + int(payloadSize);
    if (data.size() < totalSize) {
        return DecodeIncomplete;
    }

    if (frame != nullptr) {
        frame->channel = channelFromByte(quint8(data[5]));
        frame->sequence = qFromBigEndian<quint32>(data.constData() + 6);
        frame->payload = data.mid(headerSize, int(payloadSize));
    }

    if (bytesUsed != nullptr) {
        *bytesUsed = totalSize;
    }

    return DecodeOk;
}
