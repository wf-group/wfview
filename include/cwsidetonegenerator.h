#pragma once

#include <QMutex>
#include <QString>

#include <deque>

// Renders the CW Sender sidetone on demand. RxAudioProcessor calls mixInto()
// for every RX audio block, so the tone uses the RX stream's sample rate and
// plays on the user's selected RX output. Unlike a separate audio sink, there
// is no idle stream to underrun between messages.
//
// The control methods may be called from any thread; mixInto() is called from
// the RX audio converter thread.
class CwSidetoneGenerator
{
public:
    CwSidetoneGenerator() = default;

    void send(const QString& text);
    void stop();

    // Speed and dash ratio apply from the next element; pitch and level apply
    // from the next audio block.
    void setSpeed(int wpm);
    void setRatio(double ratio);
    void setFrequency(int hz);
    void setLevel(int percent);

    // samples are interleaved when channels == 2. The tone is added to every
    // channel and the result is clamped to [-1, 1].
    void mixInto(float* samples, int frames, int channels, float sampleRate);

private:
    struct Element
    {
        bool keyDown;
        double dits;
    };

    bool startNextElement();
    void queueCharacter(QChar ch);

    QMutex m_mutex;
    QString m_pending;              // characters not yet keyed
    std::deque<Element> m_elements; // elements of the character being keyed
    double m_remaining = 0.0;       // seconds left in the current element
    bool m_keyDown = false;
    double m_envelope = 0.0;
    double m_phase = 0.0;

    int m_wpm = 20;
    double m_ratio = 3.0;
    int m_frequency = 600;
    double m_level = 0.5;
};
