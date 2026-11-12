#include "cwsidetonegenerator.h"

#include <QHash>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
// PARIS timing: one dit lasts 1.2 seconds at 1 WPM.
constexpr double ditSecondsAtOneWpm = 1.2;
// Rise and fall time of each element, to avoid key clicks.
constexpr double rampSeconds = 0.005;
constexpr double twoPi = 6.283185307179586;
// The sidetone maximum is lowered by 3 dB (10^(-3/20)) from full scale, because
// a full-scale tone distorts once it is mixed with RX audio. RX audio is not
// scaled.
constexpr double maxLevel = 0.7079457843841379;

// Characters match the Icom table; unknown characters are sent as '?'.
const QHash<QChar, const char*>& cwTable()
{
    static const QHash<QChar, const char*> table = {
        {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
        {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
        {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},
        {'F', "..-."},  {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},
        {'K', "-.-"},   {'L', ".-.."},  {'M', "--"},    {'N', "-."},    {'O', "---"},
        {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
        {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},  {'Y', "-.--"},
        {'Z', "--.."},
        {'/', "-..-."}, {'?', "..--.."}, {'.', ".-.-.-"}, {'-', "-....-"}, {',', "--..--"},
        {':', "---..."}, {'\'', ".----."}, {'(', "-.--.-"}, {')', "-.--.-"}, {'=', "-...-"},
        {'+', ".-.-."}, {'"', ".-..-."},
        {' ', " "},
    };
    return table;
}
}

void CwSidetoneGenerator::send(const QString& text)
{
    QString cleaned;
    cleaned.reserve(text.size());
    for (const QChar ch : text)
        cleaned.append(ch.isSpace() ? QChar(' ') : ch.toUpper());

    QMutexLocker locker(&m_mutex);
    m_pending.append(cleaned);
}

void CwSidetoneGenerator::stop()
{
    QMutexLocker locker(&m_mutex);
    m_pending.clear();
    m_elements.clear();
    m_remaining = 0.0;
    // The envelope ramps down in mixInto(), so stopping mid-tone does not click.
    m_keyDown = false;
}

void CwSidetoneGenerator::setSpeed(int wpm)
{
    QMutexLocker locker(&m_mutex);
    m_wpm = std::max(1, wpm);
}

void CwSidetoneGenerator::setRatio(double ratio)
{
    QMutexLocker locker(&m_mutex);
    m_ratio = std::max(1.0, ratio);
}

void CwSidetoneGenerator::setFrequency(int hz)
{
    QMutexLocker locker(&m_mutex);
    m_frequency = std::max(0, hz);
}

void CwSidetoneGenerator::setLevel(int percent)
{
    QMutexLocker locker(&m_mutex);
    m_level = std::clamp(percent, 0, 100) / 100.0 * maxLevel;
}

void CwSidetoneGenerator::mixInto(float* samples, int frames, int channels, float sampleRate)
{
    if (!samples || frames <= 0 || channels < 1 || sampleRate <= 0.0f)
        return;

    QMutexLocker locker(&m_mutex);

    if (!m_keyDown && m_envelope <= 0.0 && m_remaining <= 0.0 && m_elements.empty() && m_pending.isEmpty())
        return;

    const double secondsPerSample = 1.0 / sampleRate;
    const double rampStep = secondsPerSample / rampSeconds;
    const double phaseStep = twoPi * m_frequency / sampleRate;

    for (int f = 0; f < frames; ++f) {
        while (m_remaining <= 0.0) {
            if (!startNextElement())
                break;
        }

        if (m_keyDown)
            m_envelope = std::min(1.0, m_envelope + rampStep);
        else
            m_envelope = std::max(0.0, m_envelope - rampStep);

        if (m_envelope > 0.0) {
            const float value = static_cast<float>(std::sin(m_phase) * m_envelope * m_level);
            float* frame = samples + static_cast<qsizetype>(f) * channels;
            for (int ch = 0; ch < channels; ++ch)
                frame[ch] = std::clamp(frame[ch] + value, -1.0f, 1.0f);

            m_phase += phaseStep;
            if (m_phase >= twoPi)
                m_phase -= twoPi;
        } else {
            m_phase = 0.0;
        }

        if (m_remaining > 0.0)
            m_remaining -= secondsPerSample;
    }
}

// Caller holds m_mutex. Returns false when nothing is left to key.
bool CwSidetoneGenerator::startNextElement()
{
    while (m_elements.empty()) {
        if (m_pending.isEmpty()) {
            m_keyDown = false;
            m_remaining = 0.0;
            return false;
        }
        queueCharacter(m_pending.front());
        m_pending.remove(0, 1);
    }

    const Element element = m_elements.front();
    m_elements.pop_front();
    m_keyDown = element.keyDown;
    // Adding keeps the sub-sample overshoot of the previous element.
    m_remaining += element.dits * ditSecondsAtOneWpm / m_wpm;
    return true;
}

// Caller holds m_mutex.
void CwSidetoneGenerator::queueCharacter(QChar ch)
{
    const auto& table = cwTable();
    auto it = table.constFind(ch);
    if (it == table.constEnd())
        it = table.constFind('?');

    const char* morse = it.value();
    const size_t length = std::strlen(morse);
    for (size_t i = 0; i < length; ++i) {
        const char symbol = morse[i];
        if (symbol == '-')
            m_elements.push_back({true, m_ratio});
        else if (symbol == '.')
            m_elements.push_back({true, 1.0});
        else
            m_elements.push_back({false, 1.0}); // word space

        // One dit between elements, three after the character.
        const bool last = (i == length - 1);
        m_elements.push_back({false, last ? 3.0 : 1.0});
    }
}
