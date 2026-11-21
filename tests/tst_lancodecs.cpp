#include "lancodecs.h"

#include <QtTest>

class LanCodecsTest : public QObject
{
    Q_OBJECT

private slots:
    void oemCodecsComeFirst();
    void kenwoodCodecs();
    void yaesuHasNo8BitPcm();
    void sanitizeReplacesUnsupported();
    void sanitizeKenwoodTxFollowsRx();
    void kenwoodVoipModes();
};

void LanCodecsTest::oemCodecsComeFirst()
{
    for (manufacturersType_t manufacturer : {manufIcom, manufKenwood, manufYaesu}) {
        for (bool tx : {false, true}) {
            bool seenServerOnly = false;
            for (const lanCodecInfo &info : lanCodecs(manufacturer, tx)) {
                if (!info.oem)
                    seenServerOnly = true;
                else
                    QVERIFY2(!seenServerOnly, "an OEM codec is listed after a wfview server codec");
            }
        }
    }
}

void LanCodecsTest::kenwoodCodecs()
{
    const QList<lanCodecInfo> codecs = lanCodecs(manufKenwood, false);
    QCOMPARE(codecs.size(), 3);
    QCOMPARE(codecs.at(0).codec, lanCodecPcm16Bit1ch);
    QVERIFY(codecs.at(0).oem);
    QCOMPARE(codecs.at(1).codec, lanCodecAdpcm1ch);
    QVERIFY(codecs.at(1).oem);
    QCOMPARE(codecs.at(2).codec, lanCodecOpus1ch);
    QVERIFY(!codecs.at(2).oem);
    QVERIFY(!lanCodecSupported(manufKenwood, lanCodecULaw1ch, false));
}

void LanCodecsTest::yaesuHasNo8BitPcm()
{
    QVERIFY(!lanCodecSupported(manufYaesu, lanCodecPcm8Bit1ch, false));
    QVERIFY(!lanCodecSupported(manufYaesu, lanCodecPcm8Bit2ch, false));
    QVERIFY(!lanCodecSupported(manufYaesu, lanCodecPcm8Bit1ch, true));
    QVERIFY(lanCodecSupported(manufYaesu, lanCodecAdpcm1ch, false));
    QVERIFY(lanCodecSupported(manufIcom, lanCodecPcm8Bit2ch, false));
}

void LanCodecsTest::sanitizeReplacesUnsupported()
{
    quint8 rx = lanCodecPcm8Bit2ch;
    quint8 tx = lanCodecOpus2ch; // TX is mono only.
    QVERIFY(lanSanitizeCodecs(manufYaesu, &rx, &tx));
    QCOMPARE(rx, lanCodecDefault);
    QCOMPARE(tx, lanCodecDefault);

    rx = lanCodecOpus2ch;
    tx = lanCodecAdpcm1ch;
    QVERIFY(!lanSanitizeCodecs(manufIcom, &rx, &tx));
    QCOMPARE(rx, lanCodecOpus2ch);
    QCOMPARE(tx, lanCodecAdpcm1ch);
}

void LanCodecsTest::sanitizeKenwoodTxFollowsRx()
{
    quint8 rx = lanCodecAdpcm1ch;
    quint8 tx = lanCodecPcm16Bit1ch;
    QVERIFY(lanSanitizeCodecs(manufKenwood, &rx, &tx));
    QCOMPARE(rx, lanCodecAdpcm1ch);
    QCOMPARE(tx, lanCodecAdpcm1ch);

    rx = lanCodecOpus2ch;
    tx = lanCodecOpus1ch;
    QVERIFY(lanSanitizeCodecs(manufKenwood, &rx, &tx));
    QCOMPARE(rx, lanCodecDefault);
    QCOMPARE(tx, lanCodecDefault);
}

void LanCodecsTest::kenwoodVoipModes()
{
    QCOMPARE(kenwoodVoipModeForCodec(lanCodecPcm16Bit1ch), quint8(1));
    QCOMPARE(kenwoodVoipModeForCodec(lanCodecAdpcm1ch), quint8(2));
    QCOMPARE(kenwoodVoipModeForCodec(lanCodecOpus1ch), quint8(3));
    QCOMPARE(kenwoodVoipModeForCodec(lanCodecULaw1ch), quint8(1));
}

int runLanCodecsTests(int argc, char **argv)
{
    LanCodecsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_lancodecs.moc"
