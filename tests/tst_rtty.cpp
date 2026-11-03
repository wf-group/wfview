#include "rttycore.h"
#include "rttytxsource.h"

#include <QtTest>
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace
{
const char *const kText = "CQ CQ CQ DE W1AW W1AW K\nTHE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789\n"
                          "UR RST 599 5NN ?/.,-() TU 73 SK\n";

size_t editDistance(const std::string &a, const std::string &b)
{
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i)
    {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

RttyConfig makeConfig(double sampleRate, double baud, double shift, double mark = 2125.0)
{
    RttyConfig c;
    c.sampleRate = sampleRate;
    c.baud = baud;
    c.shiftHz = shift;
    c.markHz = mark;
    return c;
}

// Audio for 'text' with a mark lead-in and tail; optional white noise at
// snrDb in a 2.5 kHz bandwidth (std::mt19937 so every compiler agrees).
std::vector<float> synth(const RttyConfig &c, const std::string &text, double snrDb, unsigned seed)
{
    RttyModulator mod;
    mod.setConfig(c);
    mod.setAmplitude(0.5f);
    mod.queueMark(0.4);
    mod.queueCodes({RttyBaudot::kLtrs});
    mod.queueText(text);
    mod.queueMark(0.3);
    std::vector<float> out;
    std::vector<float> buf(512);
    while (!mod.idle())
    {
        mod.render(buf.data(), buf.size());
        out.insert(out.end(), buf.begin(), buf.end());
    }
    if (snrDb < 100.0)
    {
        std::mt19937 rng(seed);
        const double sigma = std::sqrt(0.125 / std::pow(10.0, snrDb / 10.0) * (c.sampleRate / 2.0) / 2500.0);
        std::normal_distribution<double> nd(0.0, sigma);
        for (float &s : out)
            s += static_cast<float>(nd(rng));
    }
    return out;
}

std::string decode(const RttyConfig &c, const std::vector<float> &audio, double squelch = 0.0)
{
    RttyDemodulator d;
    d.setConfig(c);
    d.setSquelch(squelch);
    std::string got;
    for (size_t i = 0; i < audio.size(); i += 480)
        got += d.process(audio.data() + i, std::min<size_t>(480, audio.size() - i));
    return got;
}
} // namespace

class RttyTest : public QObject
{
    Q_OBJECT

private slots:
    void baudotRoundTrip();
    void usosResendsFigures();
    void loopbackFormats_data();
    void loopbackFormats();
    void copiesInNoise();
    void squelchKeepsNoiseQuiet();
    void findsTonePair();
    void txSourceEchoesAndDecodes();
    void scopeDrawsMarkAndSpaceOnTheirAxes();
};

void RttyTest::baudotRoundTrip()
{
    bool figs = false;
    const std::string t = "RYRY 123 ABC/45 ?";
    const std::vector<uint8_t> codes = RttyBaudot::encode(t, figs, true, false);
    RttyBaudot::Receiver rx;
    std::string back;
    for (uint8_t c : codes)
        if (const char ch = rx.push(c, true, false))
            back += ch;
    QCOMPARE(QString::fromStdString(back), QString::fromStdString(t));
    QCOMPARE(RttyBaudot::decode(9, true, false), '$'); // US TTY D
    QCOMPARE(RttyBaudot::decode(17, true, true), '+'); // ITA2 Z
}

void RttyTest::usosResendsFigures()
{
    bool figs = false;
    QCOMPARE(RttyBaudot::encode("1 2", figs, true, false).size(), size_t(5)); // FIGS 1 SP FIGS 2
    figs = false;
    QCOMPARE(RttyBaudot::encode("1 2", figs, false, false).size(), size_t(4)); // FIGS 1 SP 2
}

void RttyTest::loopbackFormats_data()
{
    QTest::addColumn<double>("rate");
    QTest::addColumn<double>("baud");
    QTest::addColumn<double>("shift");
    QTest::addColumn<double>("stop");
    QTest::newRow("48k 45.45/170 1.5") << 48000.0 << 45.45 << 170.0 << 1.5;
    QTest::newRow("48k 50/170 1") << 48000.0 << 50.0 << 170.0 << 1.0;
    QTest::newRow("48k 75/425 2") << 48000.0 << 75.0 << 425.0 << 2.0;
    QTest::newRow("48k 100/850 1.5") << 48000.0 << 100.0 << 850.0 << 1.5;
    QTest::newRow("8k 45.45/170 1.5") << 8000.0 << 45.45 << 170.0 << 1.5;
    QTest::newRow("16k 45.45/200 1") << 16000.0 << 45.45 << 200.0 << 1.0;
}

void RttyTest::loopbackFormats()
{
    QFETCH(double, rate);
    QFETCH(double, baud);
    QFETCH(double, shift);
    QFETCH(double, stop);
    RttyConfig c = makeConfig(rate, baud, shift);
    c.stopBits = stop;
    QCOMPARE(QString::fromStdString(decode(c, synth(c, kText, 200.0, 1))), QString::fromLatin1(kText));
}

void RttyTest::copiesInNoise()
{
    const RttyConfig c = makeConfig(48000.0, 45.45, 170.0);
    const std::string text = std::string(kText) + kText;
    for (unsigned seed = 10; seed < 13; ++seed)
    {
        const std::string got = decode(c, synth(c, text, 6.0, seed));
        const double cer = static_cast<double>(editDistance(text, got)) / text.size();
        QVERIFY2(cer < 0.02, qPrintable(QStringLiteral("CER %1 at +6 dB").arg(cer)));
    }
}

void RttyTest::squelchKeepsNoiseQuiet()
{
    const RttyConfig c = makeConfig(48000.0, 45.45, 170.0);
    std::mt19937 rng(5);
    std::normal_distribution<double> nd(0.0, 0.1);
    std::vector<float> noise(48000 * 5);
    for (float &s : noise)
        s = static_cast<float>(nd(rng));
    QVERIFY(decode(c, noise, 0.5).size() < 8);
    // ...and opens for a signal at 0 dB.
    const std::string got = decode(c, synth(c, kText, 0.0, 20), 0.5);
    QVERIFY(static_cast<double>(editDistance(kText, got)) / std::string(kText).size() < 0.05);
}

void RttyTest::findsTonePair()
{
    RttyConfig c = makeConfig(48000.0, 45.45, 175.0, 1587.0);
    c.reverse = true; // space 1412 Hz below mark
    const std::vector<float> a = synth(c, kText, 15.0, 3);
    const RttyTonePair p = rttyFindTonePair(a.data(), a.size(), c.sampleRate, 170.0);
    QVERIFY(p.found);
    QVERIFY2(std::fabs(p.lowHz - 1412.0) < 5.0, qPrintable(QString::number(p.lowHz)));
    QVERIFY2(std::fabs(p.highHz - 1587.0) < 5.0, qPrintable(QString::number(p.highHz)));
    QVERIFY(p.levelDb > 10.0);
}

void RttyTest::txSourceEchoesAndDecodes()
{
    // The TX source as TxAudioProcessor drives it: stereo, 48 kHz, small blocks.
    RttyConfig c = makeConfig(48000.0, 45.45, 170.0, 2295.0);
    c.reverse = true;
    RttyTxSource tx;
    tx.setConfig(c);
    tx.setLevel(0.5f);
    QVERIFY(!tx.active());
    tx.start(0.3);
    QVERIFY(tx.active());
    tx.queueText("CQ DE TEST 599\n");
    tx.queueTail(0.15);

    std::vector<float> left;
    std::vector<float> block(2 * 960);
    std::string echoed;
    for (int i = 0; i < 400 && !tx.idle(); ++i)
    {
        tx.render(block.data(), 960, 2, 48000.0f);
        for (size_t f = 0; f < 960; ++f)
        {
            QCOMPARE(block[2 * f], block[2 * f + 1]); // both channels carry the tones
            left.push_back(block[2 * f]);
        }
        echoed += tx.takeSent();
    }
    QVERIFY(tx.idle());
    QVERIFY(tx.renderedFrames() > 0);
    QCOMPARE(QString::fromStdString(echoed), QStringLiteral("CQ DE TEST 599\n"));
    QCOMPARE(QString::fromStdString(decode(c, left)), QStringLiteral("CQ DE TEST 599\n"));

    tx.stop();
    QVERIFY(!tx.active());
}

void RttyTest::scopeDrawsMarkAndSpaceOnTheirAxes()
{
    const double sr = 48000.0, mark = 2295.0, space = 2125.0;
    for (int which = 0; which < 2; ++which)
    {
        RttyScope scope;
        scope.setConfig(sr, mark, space, 45.45, 170.0);
        const double f = which == 0 ? mark : space;
        std::vector<float> tone(48000);
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * f * i / sr));
        std::vector<RttyScope::Point> pts;
        scope.process(tone.data(), tone.size(), 7, pts);
        QVERIFY(pts.size() > 6000);
        double sx = 0, sy = 0, peak = 0;
        for (size_t i = pts.size() / 2; i < pts.size(); ++i)
        { // after the filters settle
            sx += std::fabs(pts[i].x);
            sy += std::fabs(pts[i].y);
            peak = std::max(peak, static_cast<double>(std::max(std::fabs(pts[i].x), std::fabs(pts[i].y))));
        }
        const double ratio = which == 0 ? sy / sx : sx / sy;
        QVERIFY2(ratio < 0.25, qPrintable(QStringLiteral("cross-axis ratio %1").arg(ratio)));
        QVERIFY(peak <= 1.0);
        QVERIFY(peak > 0.7); // auto-scaled to fill most of the screen
    }
}

int runRttyTests(int argc, char **argv)
{
    RttyTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_rtty.moc"
