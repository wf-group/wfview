#ifndef ICOMSCOPEFRAMEASSEMBLER_H
#define ICOMSCOPEFRAMEASSEMBLER_H

#include "wfviewtypes.h"

class IcomScopeFrameAssembler
{
public:
    static bool parsePayload(scopeData &scope,
                             const QByteArray &payload,
                             uchar receiver,
                             quint16 modelId,
                             quint16 expectedLength,
                             uchar &oldScopeMode,
                             quint8 &nextSequence);

private:
    static quint8 bcdToUChar(char value);
    static quint64 parseFrequencyHz(const QByteArray &data);
};

#endif // ICOMSCOPEFRAMEASSEMBLER_H
