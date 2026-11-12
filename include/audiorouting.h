#ifndef AUDIOROUTING_H
#define AUDIOROUTING_H

#include "wfviewtypes.h"

#include <QString>

enum class RigTransport {
    Serial,
    Network,
    WfSharePbx,
    WfShareDirect
};

enum class RadioRxAudioSource {
    None,
    NativeNetwork,
    LocalUsb,
    WfShare
};

enum class LocalAudioBackend {
    Qt,
    PortAudio,
    RtAudio,
    Unsupported
};

enum class TxAudioInput {
    None,
    LocalInput,
    WfShare,
    Tci
};

enum class PttOrigin {
    None,
    Radio,
    Wfview,
    ExternalCiv,
    RigCtl,
    Tci
};

struct TxAudioSession {
    PttOrigin origin = PttOrigin::None;
    TxAudioInput source = TxAudioInput::None;

    bool isActive() const
    {
        return origin != PttOrigin::None && source != TxAudioInput::None;
    }
};

struct AudioRouteInputs {
    bool connected = false;
    bool rigCanTransmit = false;
    bool enableNetwork = false;
    bool enableWfShare = false;
    bool wfShareDirectMode = false;
    bool enableUsbAudio = false;
    bool hasLocalRxOutput = false;
    bool hasLocalTxInput = false;
    bool hasUsbRadioRxInput = false;
    bool hasUsbRadioTxOutput = false;
    bool hasTciServer = false;
    audioType audioSystem = qtAudio;
};

struct AudioRouteState {
    RigTransport transport = RigTransport::Serial;
    RadioRxAudioSource rxSource = RadioRxAudioSource::None;
    LocalAudioBackend localBackend = LocalAudioBackend::Unsupported;
    TxAudioInput txInput = TxAudioInput::None;

    bool localOutputEnabled = false;
    bool usbRadioRxInputEnabled = false;
    bool usbRadioTxOutputEnabled = false;
    bool tciRxSinkEnabled = false;
    bool validLocalBackend = false;

    bool operator==(const AudioRouteState &other) const
    {
        return transport == other.transport &&
               rxSource == other.rxSource &&
               localBackend == other.localBackend &&
               txInput == other.txInput &&
               localOutputEnabled == other.localOutputEnabled &&
               usbRadioRxInputEnabled == other.usbRadioRxInputEnabled &&
               usbRadioTxOutputEnabled == other.usbRadioTxOutputEnabled &&
               tciRxSinkEnabled == other.tciRxSinkEnabled &&
               validLocalBackend == other.validLocalBackend;
    }

    bool operator!=(const AudioRouteState &other) const
    {
        return !(*this == other);
    }
};

AudioRouteState computeAudioRoute(const AudioRouteInputs &inputs);
QString audioRouteDebugString(const AudioRouteState &route);
TxAudioInput txSourceForPttOrigin(PttOrigin origin, const AudioRouteState &route);

class TxAudioSourceArbiter
{
public:
    TxAudioInput activeSource() const { return session.source; }
    PttOrigin activeOrigin() const { return session.origin; }
    TxAudioSession activeSession() const { return session; }
    bool isActive() const { return session.isActive(); }
    bool accepts(TxAudioInput source) const;
    bool request(PttOrigin origin, TxAudioInput source);
    bool release(PttOrigin origin);
    void reset();

private:
    TxAudioSession session;
};

#endif // AUDIOROUTING_H
