#include "icomscopeframeassembler.h"

#include "logcategories.h"

namespace
{
quint64 pow10(int exponent)
{
    quint64 value = 1;
    for (int i = 0; i < exponent; ++i)
        value *= 10;
    return value;
}
}

bool IcomScopeFrameAssembler::parsePayload(scopeData &scope,
                                           const QByteArray &payload,
                                           uchar receiver,
                                           quint16 modelId,
                                           quint16 expectedLength,
                                           uchar &oldScopeMode,
                                           quint8 &nextSequence)
{
    if (payload.size() < 2)
        return false;

    bool ret = false;
    scope.valid = false;
    scope.receiver = receiver;

    const quint8 sequence = bcdToUChar(payload.at(0));
    const quint8 sequenceMax = bcdToUChar(payload.at(1));
    if (sequence == 0 || sequenceMax == 0 || sequence > sequenceMax) {
        scope.data.clear();
        nextSequence = 0;
        return false;
    }

    int freqLen = 5;
    if (modelId == 0xAC && (payload.size() == 491 || payload.size() == 16))
        freqLen = 6;

    if (sequence == 1) {
        const int requiredInfoLength = 4 + (freqLen * 2);
        if (payload.size() < requiredInfoLength) {
            scope.data.clear();
            nextSequence = 0;
            return false;
        }

        scope.mode = static_cast<uchar>(payload.at(2));
        if (scope.mode != oldScopeMode)
            oldScopeMode = scope.mode;

        scope.oor = bool(payload.at(3 + (freqLen * 2)));
        if (scope.oor) {
            scope.data = QByteArray(expectedLength, '\0');
            scope.valid = true;
            nextSequence = 0;
            return true;
        }

        scope.data.clear();
        nextSequence = (sequence < sequenceMax) ? quint8(sequence + 1) : 0;

        scope.startFreq = parseFrequencyHz(payload.mid(3, freqLen)) / 1000000.0;
        scope.endFreq = parseFrequencyHz(payload.mid(3 + freqLen, freqLen)) / 1000000.0;

        if (scope.mode == 0) {
            scope.startFreq -= scope.endFreq;
            scope.endFreq = scope.startFreq + 2 * scope.endFreq;
        }

        if (sequence == sequenceMax) {
            scope.data.append(payload.right(payload.length() - 4 - (freqLen * 2)));
            ret = true;
        }
    } else if (sequence < sequenceMax) {
        if (scope.startFreq <= 0.0 || scope.endFreq <= scope.startFreq || nextSequence != sequence) {
            qDebug(logRig()) << "Ignoring Icom scope section without preceding wave information"
                             << "receiver" << receiver
                             << "sequence" << sequence
                             << "of" << sequenceMax
                             << "expected" << nextSequence;
            scope.data.clear();
            nextSequence = 0;
            return false;
        }

        scope.data.insert(scope.data.length(), payload.right(payload.length() - 2));
        nextSequence = quint8(sequence + 1);
    } else {
        if (scope.startFreq <= 0.0 || scope.endFreq <= scope.startFreq || nextSequence != sequence) {
            qDebug(logRig()) << "Ignoring final Icom scope section without preceding wave information"
                             << "receiver" << receiver
                             << "sequence" << sequence
                             << "of" << sequenceMax
                             << "expected" << nextSequence;
            scope.data.clear();
            nextSequence = 0;
            return false;
        }

        scope.data.insert(scope.data.length(), payload.right(payload.length() - 2));
        ret = true;
        nextSequence = 0;
    }

    if (ret && !scope.oor && expectedLength > 0 && scope.data.size() != expectedLength) {
        qWarning(logRig()) << "Ignoring incomplete Icom scope frame"
                           << "receiver" << receiver
                           << "sequence" << sequence
                           << "of" << sequenceMax
                           << "bytes" << scope.data.size()
                           << "expected" << expectedLength;
        scope.data.clear();
        nextSequence = 0;
        ret = false;
    }

    scope.valid = ret;
    return ret;
}

quint8 IcomScopeFrameAssembler::bcdToUChar(char value)
{
    const quint8 in = static_cast<quint8>(value);
    return (in & 0x0f) + (((in & 0xf0) >> 4) * 10);
}

quint64 IcomScopeFrameAssembler::parseFrequencyHz(const QByteArray &data)
{
    quint64 value = 0;
    for (int i = 0; i < data.size() * 2; i += 2) {
        const quint8 byte = static_cast<quint8>(data.at(i / 2));
        value += (byte & 0x0f) * pow10(i);
        value += ((byte & 0xf0) >> 4) * pow10(i + 1);
    }
    return value;
}
