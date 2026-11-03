// rttycore.h - Qt-free RTTY (Baudot FSK) modem for wfview.
//
//   RttyBaudot       ITA2 / US-TTY code tables, LTRS/FIGS handling, USOS.
//   RttyModulator    text -> phase-continuous AFSK audio (start bit, 5 data
//                    bits LSB first, 1 / 1.5 / 2 stop bits).
//   RttyDemodulator  audio -> text: complex mix at mark and space, decimate,
//                    low-pass, W7AY "optimal" ATC slicer, UART with mid-bit
//                    integration, framing check and squelch.
//
// Tone convention: markHz is the mark tone. With reverse == false the space
// tone is markHz + shiftHz (the usual LSB AFSK pair 2125 / 2295 Hz, which puts
// mark on the higher RF frequency). With reverse == true it is markHz - shiftHz.
// The caller flips reverse for USB so the RF pair stays the same.

#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct RttyConfig
{
    double sampleRate = 48000.0;
    double baud = 45.45; // 45.45, 50, 75, 100 ...
    double shiftHz = 170.0; // 170, 200, 425, 850 ...
    double markHz = 2125.0;
    bool reverse = false; // space below mark instead of above
    double stopBits = 1.5; // 1, 1.5 or 2
    bool usos = true; // unshift on space (RX and TX)
    bool ita2 = false; // false = US TTY figures (ham default), true = ITA2

    double spaceHz() const
    {
        return reverse ? markHz - shiftHz : markHz + shiftHz;
    }
};

class RttyBaudot
{
public:
    static constexpr uint8_t kLtrs = 31;
    static constexpr uint8_t kFigs = 27;
    static constexpr uint8_t kSpace = 4;
    static constexpr uint8_t kCr = 8;
    static constexpr uint8_t kLf = 2;

    // Character for a code in a given shift; 0 if none.
    static char decode(uint8_t code, bool figs, bool ita2);

    // Encode text into Baudot codes, inserting LTRS/FIGS as needed. Lower case
    // is folded to upper case; unknown characters are dropped; '\n' becomes
    // CR LF. 'figs' is the transmitter shift state and is updated.
    static std::vector<uint8_t> encode(const std::string &text, bool &figs, bool usos, bool ita2);

    // Receive-side shift state machine. Returns the character to print (0 for
    // none). CR is swallowed, LF prints '\n'.
    class Receiver
    {
    public:
        char push(uint8_t code, bool usos, bool ita2);
        void reset()
        {
            m_figs = false;
        }
        bool figs() const
        {
            return m_figs;
        }

    private:
        bool m_figs = false;
    };
};

class RttyModulator
{
public:
    void setConfig(const RttyConfig &cfg);
    const RttyConfig &config() const
    {
        return m_cfg;
    }
    void setAmplitude(float a)
    {
        m_amp = a;
    }
    void setDiddle(bool on)
    {
        m_diddle = on;
    } // send LTRS while the buffer is empty

    // Queue text to send. Shift state carries across calls.
    void queueText(const std::string &text);
    void queueCodes(const std::vector<uint8_t> &codes);
    // Steady mark for 'seconds' (pre-roll before the first character, tail).
    void queueMark(double seconds);
    void clear();

    // Codes still waiting (not counting the one being sent).
    size_t pending() const
    {
        return m_queue.size();
    }
    // True when nothing is queued and no character is part-way out.
    bool idle() const
    {
        return m_queue.empty() && !m_busy;
    }
    // Baudot codes fully sent since the last clear(); for echoing sent text.
    size_t codesSent() const
    {
        return m_codesSent;
    }

    // Render n samples. While idle it sends mark (or LTRS diddles).
    void render(float *out, size_t n);

private:
    struct Item
    {
        bool isCode = true;
        uint8_t code = 0;
        double markSec = 0.0;
    };
    void nextElement();
    void loadCode(uint8_t code);

    RttyConfig m_cfg;
    float m_amp = 0.5f;
    bool m_diddle = false;
    bool m_figs = false;
    std::deque<Item> m_queue;
    double m_phase = 0.0;
    std::vector<std::pair<bool, double>> m_symbol; // (isMark, bits) of the current item
    size_t m_symIndex = 0;
    double m_timeLeft = 0.0; // samples left in the current element
    bool m_curMark = true;
    bool m_busy = false; // the current element comes from a queued item
    bool m_symIsCode = false;
    size_t m_codesSent = 0;
};

class RttyDemodulator
{
public:
    struct Stats
    {
        double quality = 0.0; // 0..1, tone contrast |m-s|/(m+s) averaged over ~4 bits
        double markLevel = 0.0;
        double spaceLevel = 0.0;
        double noiseLevel = 0.0;
        size_t chars = 0;
        size_t framingErrors = 0;
        bool squelchOpen = false;
    };

    RttyDemodulator();
    void setConfig(const RttyConfig &cfg);
    const RttyConfig &config() const
    {
        return m_cfg;
    }
    // Squelch: characters are only printed while quality is above this
    // (0..1). 0 disables the squelch. Noise alone sits near kNoiseQuality.
    void setSquelch(double q)
    {
        m_squelch = q;
    }
    void reset();

    // Feed mono audio; returns any text decoded from it.
    std::string process(const float *in, size_t n);

    Stats stats() const
    {
        return m_stats;
    }
    // Slicer output history at the decimated rate (for a tuning scope); the
    // most recent value is v (positive = mark).
    double lastSlicer() const
    {
        return m_lastV;
    }

    // Tuning knobs (exposed for tests).
    static constexpr double kTargetRate = 3000.0; // decimated rate, Hz
    static constexpr double kLpfFactor = 0.75; // low-pass cutoff = factor * baud
    static constexpr double kLpfLenBits = 2.0; // FIR length in bit periods

private:
    struct Lpf
    {
        std::vector<double> taps;
        std::vector<std::complex<double>> buf;
        size_t pos = 0;
        std::complex<double> push(std::complex<double> x);
    };

    void rebuild();
    void step(double m, double s, std::string &out);
    void uart(double v, std::string &out);
    void emitCode(uint8_t code, std::string &out);

    RttyConfig m_cfg;
    double m_squelch = 0.0;
    double m_quality = 0.0;

    // mixers
    double m_phM = 0.0, m_phS = 0.0, m_dphM = 0.0, m_dphS = 0.0;
    // integrate-and-dump decimator per tone
    int m_dec = 1;
    int m_decCount = 0;
    std::complex<double> m_accM, m_accS;
    double m_fsd = 0.0; // decimated rate
    Lpf m_lpfM, m_lpfS;

    // ATC
    double m_bitLen = 0.0; // decimated samples per bit
    double m_markEnv = 0.0, m_spaceEnv = 0.0, m_markNoise = 0.0, m_spaceNoise = 0.0;
    double m_lastV = 0.0;

    // UART
    enum class Uart
    {
        Hunt,
        Frame
    };
    Uart m_state = Uart::Hunt;
    bool m_prevMark = true;
    double m_t = 0.0; // decimated samples since the start edge
    int m_bitIndex = 0; // 0 = start, 1..5 data, 6 = stop
    double m_acc = 0.0;
    int m_accN = 0;
    int m_code = 0;
    int m_markRun = 0;

    RttyBaudot::Receiver m_rx;
    Stats m_stats;
};

// Strongest pair of tones about shiftHz apart (+- tolHz) between loHz and
// hiHz, from an averaged 5 Hz-resolution spectrum. Used by auto-tune: the
// caller decides which tone is mark from the sideband in use.
struct RttyTonePair
{
    bool found = false;
    double lowHz = 0.0;
    double highHz = 0.0;
    double levelDb = 0.0; // weaker tone above the median spectrum level
};

RttyTonePair rttyFindTonePair(const float *x,
                              size_t n,
                              double sampleRate,
                              double shiftHz,
                              double tolHz = 25.0,
                              double loHz = 250.0,
                              double hiHz = 3200.0);

// Crossed-ellipse tuning scope (as in MMTTY): X = audio through a narrow
// band-pass on the mark tone, Y = the same on the space tone. A mark tone
// draws a horizontal line, a space tone a vertical one, so a well-tuned
// signal makes a "+"; mistuning tilts the lines into ellipses and noise
// makes a blob. Output points are scaled to about -1..1 by a slow peak
// follower so the picture size does not depend on the audio level.
class RttyScope
{
public:
    struct Point
    {
        float x;
        float y;
    };

    // Filter bandwidth follows baud and shift (narrower than the shift).
    void setConfig(double sampleRate, double markHz, double spaceHz, double baud, double shiftHz);
    // Process audio; appends every 'step'-th filtered sample as a point.
    void process(const float *in, size_t n, size_t step, std::vector<Point> &out);
    void reset();

private:
    struct Biquad
    {
        double b0 = 0, b2 = 0, a1 = 0, a2 = 0; // b1 = 0 for this band-pass
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void design(double sampleRate, double f0, double bw);
        double run(double x);
    };

    Biquad m_mark[2], m_space[2]; // two sections each for cleaner lines
    double m_peak = 1e-3;
    double m_decay = 0.9999;
    size_t m_phase = 0;
};
