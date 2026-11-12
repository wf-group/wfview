#include "audiorouting.h"

namespace
{
LocalAudioBackend backendFromAudioType(audioType type)
{
    switch (type) {
    case qtAudio:
        return LocalAudioBackend::Qt;
    case portAudio:
        return LocalAudioBackend::PortAudio;
    case rtAudio:
        return LocalAudioBackend::RtAudio;
    default:
        return LocalAudioBackend::Unsupported;
    }
}

QString transportName(RigTransport transport)
{
    switch (transport) {
    case RigTransport::Serial:
        return QStringLiteral("serial");
    case RigTransport::Network:
        return QStringLiteral("network");
    case RigTransport::WfSharePbx:
        return QStringLiteral("wfshare-pbx");
    case RigTransport::WfShareDirect:
        return QStringLiteral("wfshare-direct");
    }
    return QStringLiteral("unknown");
}

QString rxSourceName(RadioRxAudioSource source)
{
    switch (source) {
    case RadioRxAudioSource::None:
        return QStringLiteral("none");
    case RadioRxAudioSource::NativeNetwork:
        return QStringLiteral("native-network");
    case RadioRxAudioSource::LocalUsb:
        return QStringLiteral("local-usb");
    case RadioRxAudioSource::WfShare:
        return QStringLiteral("wfshare");
    }
    return QStringLiteral("unknown");
}

QString backendName(LocalAudioBackend backend)
{
    switch (backend) {
    case LocalAudioBackend::Qt:
        return QStringLiteral("qt");
    case LocalAudioBackend::PortAudio:
        return QStringLiteral("portaudio");
    case LocalAudioBackend::RtAudio:
        return QStringLiteral("rtaudio");
    case LocalAudioBackend::Unsupported:
        return QStringLiteral("unsupported");
    }
    return QStringLiteral("unknown");
}

QString txInputName(TxAudioInput input)
{
    switch (input) {
    case TxAudioInput::None:
        return QStringLiteral("none");
    case TxAudioInput::LocalInput:
        return QStringLiteral("local-input");
    case TxAudioInput::WfShare:
        return QStringLiteral("wfshare");
    case TxAudioInput::Tci:
        return QStringLiteral("tci");
    }
    return QStringLiteral("unknown");
}
}

AudioRouteState computeAudioRoute(const AudioRouteInputs &inputs)
{
    AudioRouteState route;

    if (inputs.enableWfShare)
        route.transport = inputs.wfShareDirectMode ? RigTransport::WfShareDirect : RigTransport::WfSharePbx;
    else if (inputs.enableNetwork)
        route.transport = RigTransport::Network;
    else
        route.transport = RigTransport::Serial;

    route.localBackend = backendFromAudioType(inputs.audioSystem);
    route.validLocalBackend = route.localBackend != LocalAudioBackend::Unsupported;

    if (inputs.enableWfShare)
        route.rxSource = RadioRxAudioSource::WfShare;
    else if (inputs.enableNetwork)
        route.rxSource = RadioRxAudioSource::NativeNetwork;
    else if (inputs.enableUsbAudio)
        route.rxSource = RadioRxAudioSource::LocalUsb;
    else
        route.rxSource = RadioRxAudioSource::None;

    route.usbRadioRxInputEnabled = inputs.connected &&
                                   route.validLocalBackend &&
                                   route.rxSource == RadioRxAudioSource::LocalUsb &&
                                   inputs.hasUsbRadioRxInput;

    route.localOutputEnabled = inputs.connected && route.validLocalBackend && inputs.hasLocalRxOutput;
    if (route.rxSource == RadioRxAudioSource::LocalUsb)
        route.localOutputEnabled = route.localOutputEnabled && route.usbRadioRxInputEnabled;
    else if (route.rxSource == RadioRxAudioSource::None)
        route.localOutputEnabled = false;

    route.tciRxSinkEnabled = inputs.connected &&
                             inputs.hasTciServer &&
                             route.rxSource != RadioRxAudioSource::None;

    const bool hasLocalTxDevice = inputs.connected && route.validLocalBackend && inputs.hasLocalTxInput;
    if (inputs.rigCanTransmit && hasLocalTxDevice)
        route.txInput = TxAudioInput::LocalInput;

    route.usbRadioTxOutputEnabled = route.txInput == TxAudioInput::LocalInput &&
                                    route.rxSource == RadioRxAudioSource::LocalUsb &&
                                    inputs.hasUsbRadioTxOutput;

    return route;
}

QString audioRouteDebugString(const AudioRouteState &route)
{
    return QStringLiteral("transport=%1 rx=%2 localBackend=%3 localOut=%4 tx=%5 usbRx=%6 usbTx=%7 tciRx=%8")
        .arg(transportName(route.transport),
             rxSourceName(route.rxSource),
             backendName(route.localBackend),
             route.localOutputEnabled ? QStringLiteral("on") : QStringLiteral("off"),
             txInputName(route.txInput),
             route.usbRadioRxInputEnabled ? QStringLiteral("on") : QStringLiteral("off"),
             route.usbRadioTxOutputEnabled ? QStringLiteral("on") : QStringLiteral("off"),
             route.tciRxSinkEnabled ? QStringLiteral("on") : QStringLiteral("off"));
}

TxAudioInput txSourceForPttOrigin(PttOrigin origin, const AudioRouteState &route)
{
    switch (origin) {
    case PttOrigin::Tci:
        return TxAudioInput::Tci;
    case PttOrigin::Wfview:
    case PttOrigin::ExternalCiv:
    case PttOrigin::RigCtl:
    case PttOrigin::Radio:
        return route.txInput;
    case PttOrigin::None:
        return TxAudioInput::None;
    }
    return TxAudioInput::None;
}

bool TxAudioSourceArbiter::accepts(TxAudioInput source) const
{
    return source != TxAudioInput::None &&
           (!session.isActive() || session.source == source);
}

bool TxAudioSourceArbiter::request(PttOrigin origin, TxAudioInput source)
{
    if (origin == PttOrigin::None || source == TxAudioInput::None)
        return false;

    if (session.isActive())
        return session.origin == origin && session.source == source;

    session.origin = origin;
    session.source = source;
    return true;
}

bool TxAudioSourceArbiter::release(PttOrigin origin)
{
    if (session.origin != origin)
        return false;

    reset();
    return true;
}

void TxAudioSourceArbiter::reset()
{
    session = TxAudioSession();
}
