#include "cwdecodercore.h"
#include "cwmultidecoder.h"
#include "cwwordjoiner.h"

#include <QtTest>
#include <cmath>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr float sampleRate = 8000.0f;
constexpr double pi = 3.14159265358979323846;

const std::map<char, const char *> &morseTable()
{
    static const std::map<char, const char *> table
        = {{'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},     {'F', "..-."},
           {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
           {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},
           {'S', "..."},   {'T', "-"},     {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
           {'Y', "-.--"},  {'Z', "--.."},  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
           {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}};
    return table;
}

// Normal noise from std::mt19937 (fully specified by the standard), so the
// tests produce the same audio on every compiler.
class Noise
{
public:
    explicit Noise(unsigned seed)
        : m_rng(seed)
    {
    }

    float next(float sigma)
    {
        const double u1 = (static_cast<double>(m_rng()) + 1.0) / 4294967296.0;
        const double u2 = static_cast<double>(m_rng()) / 4294967296.0;
        return static_cast<float>(sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * pi * u2));
    }

private:
    std::mt19937 m_rng;
};

// Keyed CW tone with 5 ms raised-cosine edges.
class CwSignal
{
public:
    void silence(double seconds)
    {
        element(seconds, false);
    }

    // Even keyer timing.
    void keyer(const std::string &text, float wpm, float pitchHz, float amplitude)
    {
        hand(text, wpm, pitchHz, amplitude, 0.0f, 3.0f, 3.0f, 1);
    }

    // Hand-keyed timing: element jitter, odd dash ratio, short letter gaps.
    void hand(const std::string &text,
              float wpm,
              float pitchHz,
              float amplitude,
              float jitter,
              float dashRatio,
              float letterGapDots,
              unsigned seed)
    {
        m_pitchHz = pitchHz;
        m_amplitude = amplitude;
        Noise rng(seed);
        const double dot = 1.2 / wpm;
        auto vary = [&](double length) { return length * std::max(0.4, 1.0 + rng.next(jitter)); };
        for (char c : text)
        {
            if (c == ' ')
            {
                element(vary((7.0 - letterGapDots) * dot), false);
                continue;
            }
            for (const char *p = morseTable().at(c); *p; ++p)
            {
                element(vary((*p == '.' ? 1.0 : dashRatio) * dot), true);
                element(vary(dot), false);
            }
            element(vary((letterGapDots - 1.0) * dot), false);
        }
    }

    std::vector<float> samples;

private:
    void element(double seconds, bool keyDown)
    {
        const int n = static_cast<int>(seconds * sampleRate);
        const int edge = static_cast<int>(0.005 * sampleRate);
        for (int i = 0; i < n; ++i)
        {
            double envelope = 1.0;
            if (i < edge)
                envelope = 0.5 - 0.5 * std::cos(pi * i / edge);
            else if (i > n - edge)
                envelope = 0.5 - 0.5 * std::cos(pi * (n - i) / edge);
            m_phase += 2.0 * pi * m_pitchHz / sampleRate;
            samples.push_back(keyDown ? static_cast<float>(m_amplitude * envelope * std::sin(m_phase)) : 0.0f);
        }
    }

    float m_pitchHz = 600.0f;
    float m_amplitude = 0.0f;
    double m_phase = 0.0;
};

std::vector<float> withNoise(std::vector<float> audio, float sigma, unsigned seed)
{
    Noise noise(seed);
    for (float &v : audio)
        v += noise.next(sigma);
    return audio;
}

// Feeds audio in 20 ms blocks, as the RX audio tap does, then 2 s of silence to flush.
std::string decode(CwDecoderCore &core, const std::vector<float> &audio)
{
    std::string out;
    const int block = static_cast<int>(sampleRate / 50);
    for (size_t i = 0; i + block <= audio.size(); i += block)
        core.feed(&audio[i], block, sampleRate, out);
    const std::vector<float> tail(static_cast<size_t>(sampleRate * 2), 0.0f);
    core.feed(tail.data(), static_cast<int>(tail.size()), sampleRate, out);
    return out;
}
} // namespace

class CwDecoderTest : public QObject
{
    Q_OBJECT

private slots:
    void machineEngineCopiesKeyerCw();
    void handKeyEngineCopiesKeyerCw();
    void handKeyEngineCopiesUnevenFist();
    void bothEnginesCopyHighPitch();
    void squelchKeepsNoiseSilent();
    void pitchTrackerFindsTone();
    void multiDecoderSeparatesStations();
    void multiDecoderIgnoresNoise();
    void wordJoinerJoinsSplitWord();
    void wordJoinerKeepsRealWords();
};

void CwDecoderTest::machineEngineCopiesKeyerCw()
{
    CwSignal signal;
    signal.silence(0.5);
    signal.keyer("CQ CQ DE W1AW W1AW K ", 20.0f, 600.0f, 0.5f);

    CwDecoderCore core;
    core.setFixedPitch(600.0f);
    const QString text = QString::fromStdString(decode(core, withNoise(signal.samples, 0.05f, 1)));

    QVERIFY2(text.contains("DE W1AW W1AW K"), qPrintable(text));
}

void CwDecoderTest::handKeyEngineCopiesKeyerCw()
{
    CwSignal signal;
    signal.silence(0.5);
    signal.keyer("CQ CQ DE W1AW W1AW K ", 20.0f, 600.0f, 0.5f);

    CwDecoderCore core;
    core.setEngine(CwDecoderCore::Engine::Hand);
    core.setFixedPitch(600.0f);
    const QString text = QString::fromStdString(decode(core, withNoise(signal.samples, 0.05f, 1)));

    QVERIFY2(text.contains("DE W1AW W1AW K"), qPrintable(text));
}

void CwDecoderTest::handKeyEngineCopiesUnevenFist()
{
    // 15% element jitter, long dashes and squeezed letter gaps.
    CwSignal signal;
    signal.silence(0.5);
    signal.hand("CQ CQ DE N9UV N9UV K ", 18.0f, 700.0f, 0.5f, 0.15f, 3.4f, 2.5f, 3);

    CwDecoderCore core;
    core.setEngine(CwDecoderCore::Engine::Hand);
    core.setTrackRange(200.0f, 1200.0f, -1.0f);
    const QString text = QString::fromStdString(decode(core, withNoise(signal.samples, 0.05f, 2)));

    QVERIFY2(text.contains("N9UV"), qPrintable(text));
    QVERIFY2(text.contains("DE"), qPrintable(text));
}

void CwDecoderTest::bothEnginesCopyHighPitch()
{
    // Fixed pitches up to the top of the multi-signal range (2000 Hz) must decode.
    CwSignal signal;
    signal.silence(0.5);
    signal.keyer("CQ CQ DE W1AW W1AW K ", 20.0f, 1900.0f, 0.5f);
    const std::vector<float> audio = withNoise(signal.samples, 0.05f, 1);

    CwDecoderCore machine;
    machine.setFixedPitch(1900.0f);
    const QString machineText = QString::fromStdString(decode(machine, audio));
    QVERIFY2(machineText.contains("DE W1AW W1AW K"), qPrintable(machineText));

    CwDecoderCore hand;
    hand.setEngine(CwDecoderCore::Engine::Hand);
    hand.setFixedPitch(1900.0f);
    const QString handText = QString::fromStdString(decode(hand, audio));
    QVERIFY2(handText.contains("DE W1AW W1AW K"), qPrintable(handText));
}

void CwDecoderTest::squelchKeepsNoiseSilent()
{
    const std::vector<float> noise = withNoise(std::vector<float>(static_cast<size_t>(sampleRate * 20), 0.0f), 0.3f, 4);

    CwDecoderCore machine;
    machine.setTrackRange(200.0f, 1200.0f, -1.0f);
    QCOMPARE(QString::fromStdString(decode(machine, noise)).trimmed(), QString());

    CwDecoderCore hand;
    hand.setEngine(CwDecoderCore::Engine::Hand);
    hand.setTrackRange(200.0f, 1200.0f, -1.0f);
    QCOMPARE(QString::fromStdString(decode(hand, noise)).trimmed(), QString());
}

void CwDecoderTest::pitchTrackerFindsTone()
{
    CwSignal signal;
    signal.keyer("TEST TEST TEST DE K1ABC ", 22.0f, 735.0f, 0.5f);

    CwDecoderCore core;
    core.setTrackRange(200.0f, 1200.0f, -1.0f);
    decode(core, withNoise(signal.samples, 0.05f, 5));

    QVERIFY2(std::fabs(core.stats().pitchHz - 735.0f) <= 10.0f, qPrintable(QString::number(core.stats().pitchHz)));
}

void CwDecoderTest::multiDecoderSeparatesStations()
{
    // Three overlapping stations, as with a wide filter on a busy band.
    CwSignal a;
    CwSignal b;
    CwSignal c;
    a.silence(0.5);
    a.hand("CQ TEST DE W1AW W1AW TEST ", 24.0f, 450.0f, 0.25f, 0.05f, 3.0f, 3.0f, 1);
    b.silence(3.0);
    b.hand("W1AW DE KC9OXP 5NN IN ", 28.0f, 600.0f, 0.15f, 0.05f, 3.0f, 3.0f, 2);
    c.silence(1.5);
    c.hand("TEST DE N9UV N9UV K ", 20.0f, 820.0f, 0.08f, 0.15f, 3.2f, 2.6f, 3);

    const size_t length
        = std::max({a.samples.size(), b.samples.size(), c.samples.size()}) + static_cast<size_t>(sampleRate * 2);
    a.samples.resize(length);
    b.samples.resize(length);
    c.samples.resize(length);
    std::vector<float> mix(length);
    for (size_t i = 0; i < length; ++i)
        mix[i] = a.samples[i] + b.samples[i] + c.samples[i];
    mix = withNoise(mix, 0.12f, 6);

    CwMultiDecoder multi;
    multi.setRange(200.0f, 2000.0f);
    const int block = static_cast<int>(sampleRate / 50);
    for (size_t i = 0; i + block <= mix.size(); i += block)
        multi.feed(&mix[i], block, sampleRate);

    QCOMPARE(static_cast<int>(multi.channels().size()), 3);
    const struct
    {
        float pitchHz;
        const char *call;
    } expected[] = {{450.0f, "W1AW"}, {600.0f, "KC9OXP"}, {820.0f, "N9UV"}};
    for (const auto &station : expected)
    {
        bool found = false;
        for (const auto &channel : multi.channels())
        {
            if (std::fabs(channel.pitchHz - station.pitchHz) > 15.0f)
                continue;
            found = true;
            QVERIFY2(QString::fromStdString(channel.text).contains(station.call), channel.text.c_str());
        }
        QVERIFY2(found, qPrintable(QString::number(station.pitchHz)));
    }
}

void CwDecoderTest::multiDecoderIgnoresNoise()
{
    const std::vector<float> noise
        = withNoise(std::vector<float>(static_cast<size_t>(sampleRate * 20), 0.0f), 0.12f, 7);

    CwMultiDecoder multi;
    multi.setRange(200.0f, 2000.0f);
    const int block = static_cast<int>(sampleRate / 50);
    for (size_t i = 0; i + block <= noise.size(); i += block)
        multi.feed(&noise[i], block, sampleRate);

    QCOMPARE(static_cast<int>(multi.channels().size()), 0);
}

void CwDecoderTest::wordJoinerJoinsSplitWord()
{
    // A hand sender's long gap inside "POWER" looks like a word gap.
    CwWordJoiner joiner;
    std::string out;
    joiner.push("P", false, out);
    joiner.push("OWER", true, out);
    joiner.flush(out);

    QCOMPARE(QString::fromStdString(out).trimmed(), QString("POWER"));
}

void CwDecoderTest::wordJoinerKeepsRealWords()
{
    CwWordJoiner joiner;
    std::string out;
    joiner.push("UR", false, out);
    joiner.push("RST", true, out);
    joiner.push("599", true, out);
    joiner.flush(out);

    QCOMPARE(QString::fromStdString(out).simplified(), QString("UR RST 599"));
}

int runCwDecoderTests(int argc, char **argv)
{
    CwDecoderTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_cwdecoder.moc"
