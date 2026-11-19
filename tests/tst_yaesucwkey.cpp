// The wfview-only CW key messages on the Yaesu CAT socket. The client and the
// station may be different wfview versions, so the layout must not change.

#include "packettypes.h"

#include <QtTest>
#include <algorithm>
#include <cstddef>
#include <cstring>

class YaesuCwKeyTest : public QObject
{
    Q_OBJECT

private slots:
    void messageIds();
    void keyFrameLayout();
    void keyStateFrameLayout();
    void keyFrameRoundTrip();
};

void YaesuCwKeyTest::messageIds()
{
    QCOMPARE(int(C2R_CwKey), 0x1c0);
    QCOMPARE(int(R2C_CwKeyState), 0x1c1);
    QCOMPARE(int(CwKeyQuery), 0);
    QCOMPARE(int(CwKeySend), 1);
    QCOMPARE(int(CwKeyStop), 2);
    QCOMPARE(yaesuCwKeyMaxText, 250);
}

void YaesuCwKeyTest::keyFrameLayout()
{
    QCOMPARE(sizeof(yaesuFrameHeader), size_t(6));
    QCOMPARE(offsetof(yaesuC2R_CwKeyFrame, session), size_t(6));
    QCOMPARE(offsetof(yaesuC2R_CwKeyFrame, op), size_t(10));
    QCOMPARE(offsetof(yaesuC2R_CwKeyFrame, wpm), size_t(11));
    QCOMPARE(offsetof(yaesuC2R_CwKeyFrame, dashRatio), size_t(12));
    QCOMPARE(offsetof(yaesuC2R_CwKeyFrame, textLen), size_t(13));
    QCOMPARE(offsetof(yaesuC2R_CwKeyFrame, text), size_t(14));
    QCOMPARE(sizeof(yaesuC2R_CwKeyFrame), size_t(14 + yaesuCwKeyMaxText));
}

void YaesuCwKeyTest::keyStateFrameLayout()
{
    QCOMPARE(offsetof(yaesuR2C_CwKeyStateFrame, available), size_t(6));
    QCOMPARE(sizeof(yaesuR2C_CwKeyStateFrame), size_t(7));
}

// Built the way the client sends it (only the used text bytes go on the wire),
// and read back the way the station reads it.
void YaesuCwKeyTest::keyFrameRoundTrip()
{
    const QByteArray text("CQ CQ DE N0RC");
    yaesuC2R_CwKeyFrame f;
    std::memset(&f, 0, sizeof(f));
    f.hdr.device = UsbCat;
    f.hdr.msgtype = C2R_CwKey;
    f.session = 0x12345678;
    f.op = CwKeySend;
    f.wpm = 25;
    f.dashRatio = 30;
    f.textLen = quint8(text.size());
    std::memcpy(f.text, text.constData(), size_t(text.size()));
    const int len = int(offsetof(yaesuC2R_CwKeyFrame, text)) + text.size();
    f.hdr.len = quint16(len - int(sizeof(f.hdr)));
    const QByteArray wire(reinterpret_cast<const char *>(&f), len);

    QCOMPARE(wire.size(), 14 + text.size());
    const int headerSize = int(offsetof(yaesuC2R_CwKeyFrame, text));
    QVERIFY(wire.size() >= headerSize);
    const auto *r = reinterpret_cast<const yaesuC2R_CwKeyFrame *>(wire.constData());
    QCOMPARE(r->hdr.msgtype, C2R_CwKey);
    QCOMPARE(r->hdr.len, quint16(len - 6));
    QCOMPARE(r->session, quint32(0x12345678));
    QCOMPARE(int(r->op), int(CwKeySend));
    QCOMPARE(int(r->wpm), 25);
    QCOMPARE(r->dashRatio / 10.0, 3.0);
    const int textLen = std::min({int(r->textLen), yaesuCwKeyMaxText, int(wire.size()) - headerSize});
    QCOMPARE(QByteArray(r->text, textLen), text);
}

int runYaesuCwKeyTests(int argc, char **argv)
{
    YaesuCwKeyTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_yaesucwkey.moc"
