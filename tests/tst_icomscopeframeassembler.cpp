#include <QtTest>

#include "icomscopeframeassembler.h"

class IcomScopeFrameAssemblerTest : public QObject
{
    Q_OBJECT

private slots:
    void acceptsCompleteOrderedUsbFrame();
    void rejectsMidFrameStart();
    void rejectsOutOfOrderSection();
    void rejectsIncompleteFinalFrame();
    void keepsReceiversIndependent();
};

namespace
{
QByteArray bcdFrequency(quint64 hz)
{
    QByteArray out;
    for (int i = 0; i < 5; ++i) {
        uchar value = hz % 10;
        hz /= 10;
        value |= (hz % 10) << 4;
        hz /= 10;
        out.append(char(value));
    }
    return out;
}

QByteArray waveInfo(quint8 sequenceMax = 3)
{
    QByteArray payload;
    payload.append(char(0x01));
    payload.append(char(sequenceMax));
    payload.append(char(0x01)); // fixed mode
    payload.append(bcdFrequency(14000000));
    payload.append(bcdFrequency(14350000));
    payload.append(char(0x00)); // in range
    return payload;
}

QByteArray waveSection(quint8 sequence, quint8 sequenceMax, const QByteArray &data)
{
    QByteArray payload;
    payload.append(char(sequence));
    payload.append(char(sequenceMax));
    payload.append(data);
    return payload;
}

bool parse(scopeData &scope, const QByteArray &payload, quint8 &nextSequence, uchar receiver = 0)
{
    static uchar oldMode = 0xff;
    return IcomScopeFrameAssembler::parsePayload(scope, payload, receiver, 0x98, 6, oldMode, nextSequence);
}
}

void IcomScopeFrameAssemblerTest::acceptsCompleteOrderedUsbFrame()
{
    scopeData scope;
    quint8 nextSequence = 0;

    QVERIFY(!parse(scope, waveInfo(), nextSequence));
    QCOMPARE(nextSequence, quint8(2));
    QVERIFY(!parse(scope, waveSection(2, 3, QByteArray("abc", 3)), nextSequence));
    QCOMPARE(nextSequence, quint8(3));
    QVERIFY(parse(scope, waveSection(3, 3, QByteArray("def", 3)), nextSequence));

    QVERIFY(scope.valid);
    QCOMPARE(scope.data, QByteArray("abcdef", 6));
    QCOMPARE(nextSequence, quint8(0));
    QCOMPARE(scope.startFreq, 14.0);
    QCOMPARE(scope.endFreq, 14.35);
}

void IcomScopeFrameAssemblerTest::rejectsMidFrameStart()
{
    scopeData scope;
    quint8 nextSequence = 0;

    QVERIFY(!parse(scope, waveSection(2, 3, QByteArray("abc", 3)), nextSequence));
    QVERIFY(scope.data.isEmpty());
    QCOMPARE(nextSequence, quint8(0));
    QVERIFY(!scope.valid);
}

void IcomScopeFrameAssemblerTest::rejectsOutOfOrderSection()
{
    scopeData scope;
    quint8 nextSequence = 0;

    QVERIFY(!parse(scope, waveInfo(), nextSequence));
    QVERIFY(!parse(scope, waveSection(3, 3, QByteArray("def", 3)), nextSequence));

    QVERIFY(scope.data.isEmpty());
    QCOMPARE(nextSequence, quint8(0));
    QVERIFY(!scope.valid);
}

void IcomScopeFrameAssemblerTest::rejectsIncompleteFinalFrame()
{
    scopeData scope;
    quint8 nextSequence = 0;

    QVERIFY(!parse(scope, waveInfo(), nextSequence));
    QVERIFY(!parse(scope, waveSection(2, 3, QByteArray("abc", 3)), nextSequence));
    QVERIFY(!parse(scope, waveSection(3, 3, QByteArray("d", 1)), nextSequence));

    QVERIFY(scope.data.isEmpty());
    QCOMPARE(nextSequence, quint8(0));
    QVERIFY(!scope.valid);
}

void IcomScopeFrameAssemblerTest::keepsReceiversIndependent()
{
    scopeData mainScope;
    scopeData subScope;
    quint8 mainNext = 0;
    quint8 subNext = 0;

    QVERIFY(!parse(mainScope, waveInfo(), mainNext, 0));
    QVERIFY(!parse(subScope, waveInfo(), subNext, 1));
    QVERIFY(!parse(mainScope, waveSection(2, 3, QByteArray("abc", 3)), mainNext, 0));
    QVERIFY(!parse(subScope, waveSection(2, 3, QByteArray("123", 3)), subNext, 1));
    QVERIFY(parse(mainScope, waveSection(3, 3, QByteArray("def", 3)), mainNext, 0));
    QVERIFY(parse(subScope, waveSection(3, 3, QByteArray("456", 3)), subNext, 1));

    QCOMPARE(mainScope.data, QByteArray("abcdef", 6));
    QCOMPARE(subScope.data, QByteArray("123456", 6));
    QCOMPARE(mainScope.receiver, uchar(0));
    QCOMPARE(subScope.receiver, uchar(1));
}

int runIcomScopeFrameAssemblerTests(int argc, char **argv)
{
    IcomScopeFrameAssemblerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_icomscopeframeassembler.moc"
