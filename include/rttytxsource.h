#pragma once

// AFSK source for RTTY transmit.
//
//   RttyController (GUI thread): start(), queueText(), finish(), stop()
//   TxAudioProcessor::processAudio() (audio converter thread): render()
//
// While active() the rendered tones replace the microphone audio, so the
// radio must be in a mode that transmits the computer's audio (USB-D/LSB-D on
// Icom). The source follows whatever sample rate and channel count the TX
// path uses. All members are guarded by one mutex held only briefly.

#include "rttycore.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class RttyTxSource
{
public:
    // Tone, baud and Baudot settings. The sample rate is taken from render().
    void setConfig(const RttyConfig &cfg);
    void setLevel(float amplitude); // 0..1 of full scale
    void setDiddle(bool on); // send LTRS while waiting for text

    // Clear everything, then send prerollSec of mark and LTRS; output on.
    void start(double prerollSec);
    // Queue text; shift codes are inserted as needed. Unknown characters are dropped.
    void queueText(const std::string &text);
    // Queue tailSec of mark (after the last character, before PTT is released).
    void queueTail(double tailSec);
    // Output off immediately and drop anything queued.
    void stop();

    bool active() const
    {
        return m_active.load(std::memory_order_relaxed);
    }
    // Nothing queued and nothing part-way out.
    bool idle() const;
    // Characters whose last code has been sent since the previous call.
    std::string takeSent();
    // Frames rendered since start(): stays 0 if the TX audio path is not running.
    uint64_t renderedFrames() const
    {
        return m_frames.load(std::memory_order_relaxed);
    }

    // Converter thread: write frames * channels interleaved samples.
    void render(float *out, size_t frames, int channels, float sampleRate);

private:
    mutable std::mutex m_mutex;
    RttyModulator m_mod;
    RttyConfig m_cfg;
    bool m_figs = false;
    size_t m_queuedCodes = 0; // codes queued since start()
    std::deque<std::pair<size_t, char>> m_echo; // (code count after this char, char)
    std::vector<float> m_buf;
    std::atomic<bool> m_active {false};
    std::atomic<uint64_t> m_frames {0};
};
