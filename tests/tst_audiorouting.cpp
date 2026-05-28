#include <QtTest>

#include "audiorouting.h"

class AudioRoutingTest : public QObject
{
    Q_OBJECT

private slots:
    void serialWithoutUsbHasNoLocalAudio();
    void serialUsbAudioEnablesLocalOutputAndTxWhenConfigured();
    void networkUsesNativeRxAndLocalTxDevices();
    void wfShareDirectUsesWfShareTransport();
    void tciServerIsAdditionalRxSink();
    void tciServerDoesNotRequireLocalOutput();
    void txAudioArbiterAllowsOnlyOneActiveSource();
    void txAudioArbiterRequiresMatchingRelease();
    void txAudioArbiterAllowsSameSourceOnlyForActiveSession();
    void txAudioArbiterCoversPttOriginTransitions();
    void txSourcePolicyMapsPttOrigins();
};

namespace
{
AudioRouteInputs baseInputs()
{
    AudioRouteInputs inputs;
    inputs.connected = true;
    inputs.rigCanTransmit = true;
    inputs.audioSystem = qtAudio;
    inputs.hasLocalRxOutput = true;
    inputs.hasLocalTxInput = true;
    inputs.hasUsbRadioRxInput = true;
    inputs.hasUsbRadioTxOutput = true;
    return inputs;
}
}

void AudioRoutingTest::serialWithoutUsbHasNoLocalAudio()
{
    const AudioRouteInputs inputs = baseInputs();
    const AudioRouteState route = computeAudioRoute(inputs);

    QCOMPARE(route.transport, RigTransport::Serial);
    QCOMPARE(route.rxSource, RadioRxAudioSource::None);
    QVERIFY(!route.localOutputEnabled);
    QCOMPARE(route.txInput, TxAudioInput::LocalInput);
    QVERIFY(!route.usbRadioTxOutputEnabled);
}

void AudioRoutingTest::serialUsbAudioEnablesLocalOutputAndTxWhenConfigured()
{
    AudioRouteInputs inputs = baseInputs();
    inputs.enableUsbAudio = true;

    const AudioRouteState route = computeAudioRoute(inputs);

    QCOMPARE(route.transport, RigTransport::Serial);
    QCOMPARE(route.rxSource, RadioRxAudioSource::LocalUsb);
    QVERIFY(route.localOutputEnabled);
    QVERIFY(route.usbRadioRxInputEnabled);
    QCOMPARE(route.txInput, TxAudioInput::LocalInput);
    QVERIFY(route.usbRadioTxOutputEnabled);
}

void AudioRoutingTest::networkUsesNativeRxAndLocalTxDevices()
{
    AudioRouteInputs inputs = baseInputs();
    inputs.enableNetwork = true;

    const AudioRouteState route = computeAudioRoute(inputs);

    QCOMPARE(route.transport, RigTransport::Network);
    QCOMPARE(route.rxSource, RadioRxAudioSource::NativeNetwork);
    QVERIFY(route.localOutputEnabled);
    QCOMPARE(route.txInput, TxAudioInput::LocalInput);
    QVERIFY(!route.usbRadioRxInputEnabled);
}

void AudioRoutingTest::wfShareDirectUsesWfShareTransport()
{
    AudioRouteInputs inputs = baseInputs();
    inputs.enableWfShare = true;
    inputs.wfShareDirectMode = true;

    const AudioRouteState route = computeAudioRoute(inputs);

    QCOMPARE(route.transport, RigTransport::WfShareDirect);
    QCOMPARE(route.rxSource, RadioRxAudioSource::WfShare);
    QVERIFY(route.localOutputEnabled);
    QCOMPARE(route.txInput, TxAudioInput::LocalInput);
}

void AudioRoutingTest::tciServerIsAdditionalRxSink()
{
    AudioRouteInputs inputs = baseInputs();
    inputs.enableNetwork = true;
    inputs.hasTciServer = true;

    const AudioRouteState route = computeAudioRoute(inputs);

    QCOMPARE(route.rxSource, RadioRxAudioSource::NativeNetwork);
    QVERIFY(route.localOutputEnabled);
    QVERIFY(route.tciRxSinkEnabled);
}

void AudioRoutingTest::tciServerDoesNotRequireLocalOutput()
{
    AudioRouteInputs inputs = baseInputs();
    inputs.enableUsbAudio = true;
    inputs.hasTciServer = true;
    inputs.hasLocalRxOutput = false;

    const AudioRouteState route = computeAudioRoute(inputs);

    QVERIFY(route.validLocalBackend);
    QVERIFY(!route.localOutputEnabled);
    QVERIFY(route.usbRadioRxInputEnabled);
    QVERIFY(route.tciRxSinkEnabled);
}

void AudioRoutingTest::txAudioArbiterAllowsOnlyOneActiveSource()
{
    TxAudioSourceArbiter arbiter;

    QVERIFY(!arbiter.isActive());
    QVERIFY(!arbiter.request(PttOrigin::None, TxAudioInput::Tci));
    QVERIFY(!arbiter.request(PttOrigin::Tci, TxAudioInput::None));
    QVERIFY(arbiter.request(PttOrigin::Tci, TxAudioInput::Tci));
    QCOMPARE(arbiter.activeSource(), TxAudioInput::Tci);
    QCOMPARE(arbiter.activeOrigin(), PttOrigin::Tci);
    QVERIFY(arbiter.request(PttOrigin::Tci, TxAudioInput::Tci));
    QVERIFY(!arbiter.request(PttOrigin::Wfview, TxAudioInput::LocalInput));
    QCOMPARE(arbiter.activeSource(), TxAudioInput::Tci);
    QCOMPARE(arbiter.activeOrigin(), PttOrigin::Tci);
}

void AudioRoutingTest::txAudioArbiterRequiresMatchingRelease()
{
    TxAudioSourceArbiter arbiter;

    QVERIFY(arbiter.request(PttOrigin::ExternalCiv, TxAudioInput::LocalInput));
    QVERIFY(!arbiter.release(PttOrigin::Tci));
    QCOMPARE(arbiter.activeOrigin(), PttOrigin::ExternalCiv);
    QCOMPARE(arbiter.activeSource(), TxAudioInput::LocalInput);
    QVERIFY(arbiter.release(PttOrigin::ExternalCiv));
    QVERIFY(!arbiter.isActive());
    QVERIFY(arbiter.request(PttOrigin::Wfview, TxAudioInput::LocalInput));
}

void AudioRoutingTest::txAudioArbiterAllowsSameSourceOnlyForActiveSession()
{
    TxAudioSourceArbiter arbiter;

    QVERIFY(arbiter.request(PttOrigin::ExternalCiv, TxAudioInput::LocalInput));
    QVERIFY(arbiter.accepts(TxAudioInput::LocalInput));
    QVERIFY(!arbiter.request(PttOrigin::Wfview, TxAudioInput::LocalInput));
    QVERIFY(!arbiter.accepts(TxAudioInput::Tci));
    QCOMPARE(arbiter.activeSource(), TxAudioInput::LocalInput);
    QCOMPARE(arbiter.activeOrigin(), PttOrigin::ExternalCiv);
    arbiter.reset();
    QVERIFY(arbiter.accepts(TxAudioInput::Tci));
}

void AudioRoutingTest::txAudioArbiterCoversPttOriginTransitions()
{
    TxAudioSourceArbiter arbiter;

    QVERIFY(arbiter.request(PttOrigin::Wfview, TxAudioInput::LocalInput));
    QVERIFY(!arbiter.request(PttOrigin::RigCtl, TxAudioInput::LocalInput));
    QVERIFY(!arbiter.request(PttOrigin::Tci, TxAudioInput::Tci));
    QCOMPARE(arbiter.activeOrigin(), PttOrigin::Wfview);
    QVERIFY(arbiter.release(PttOrigin::Wfview));

    QVERIFY(arbiter.request(PttOrigin::RigCtl, TxAudioInput::LocalInput));
    QVERIFY(!arbiter.release(PttOrigin::ExternalCiv));
    QCOMPARE(arbiter.activeOrigin(), PttOrigin::RigCtl);
    QVERIFY(arbiter.release(PttOrigin::RigCtl));

    QVERIFY(arbiter.request(PttOrigin::Tci, TxAudioInput::Tci));
    QVERIFY(!arbiter.request(PttOrigin::ExternalCiv, TxAudioInput::LocalInput));
    arbiter.reset();
    QVERIFY(!arbiter.isActive());
}

void AudioRoutingTest::txSourcePolicyMapsPttOrigins()
{
    AudioRouteState route;
    route.txInput = TxAudioInput::LocalInput;

    QCOMPARE(txSourceForPttOrigin(PttOrigin::Tci, route), TxAudioInput::Tci);
    QCOMPARE(txSourceForPttOrigin(PttOrigin::Wfview, route), TxAudioInput::LocalInput);
    QCOMPARE(txSourceForPttOrigin(PttOrigin::RigCtl, route), TxAudioInput::LocalInput);
    QCOMPARE(txSourceForPttOrigin(PttOrigin::ExternalCiv, route), TxAudioInput::LocalInput);
    QCOMPARE(txSourceForPttOrigin(PttOrigin::Radio, route), TxAudioInput::LocalInput);

    route.txInput = TxAudioInput::None;
    QCOMPARE(txSourceForPttOrigin(PttOrigin::Wfview, route), TxAudioInput::None);
}

int runAudioRoutingTests(int argc, char **argv)
{
    AudioRoutingTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_audiorouting.moc"
