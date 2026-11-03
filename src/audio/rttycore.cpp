// rttycore.cpp - Qt-free RTTY (Baudot FSK) modem for wfview. See rttycore.h.

#include "rttycore.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{

constexpr double kTwoPi = 6.283185307179586476925286766559;

// Letters shift (same for ITA2 and US TTY). '\0' = no printable character.
const char kLetters[32] = {0,   'E', '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
                           'T', 'Z', 'L',  'W', 'H', 'Y', 'P', 'Q', 'O',  'B', 'G', 0,   'M', 'X', 'V', 0};

// Figures shift, US TTY (the usual amateur convention). 5 = BELL.
const char kFiguresUs[32] = {0,   '3', '\n', '-', ' ', '\a', '8', '7', '\r', '$', '4', '\'', ',', '!', ':', '(',
                             '5', '"', ')',  '2', '#', '6',  '0', '1', '9',  '?', '&', 0,    '.', '/', ';', 0};

// Figures shift, ITA2. 9 = WRU (ENQ, not printed), 11 = BELL.
const char kFiguresIta2[32] = {0,   '3', '\n', '-', ' ', '\'', '8', '7', '\r', 0,   '4', '\a', ',', 0,   ':', '(',
                               '5', '+', ')',  '2', 0,   '6',  '0', '1', '9',  '?', 0,   0,    '.', '/', '=', 0};

int findCode(const char *table, char c)
{
    for (int i = 0; i < 32; ++i)
    {
        if (i == RttyBaudot::kLtrs || i == RttyBaudot::kFigs)
            continue;
        if (table[i] == c && c != 0)
            return i;
    }
    return -1;
}

} // namespace

// ---------------------------------------------------------------------------
// RttyBaudot

char RttyBaudot::decode(uint8_t code, bool figs, bool ita2)
{
    code &= 31;
    if (!figs)
        return kLetters[code];
    return ita2 ? kFiguresIta2[code] : kFiguresUs[code];
}

std::vector<uint8_t> RttyBaudot::encode(const std::string &text, bool &figs, bool usos, bool ita2)
{
    std::vector<uint8_t> out;
    const char *figTable = ita2 ? kFiguresIta2 : kFiguresUs;
    for (char raw : text)
    {
        if (raw == '\r')
            continue;
        if (raw == '\n')
        {
            out.push_back(kCr);
            out.push_back(kLf);
            continue;
        }
        if (raw == '\t')
            raw = ' ';
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
        if (c == ' ')
        {
            out.push_back(kSpace);
            if (usos)
                figs = false; // receivers running USOS drop back to letters here
            continue;
        }
        const int l = findCode(kLetters, c);
        if (l >= 0)
        {
            if (figs)
            {
                out.push_back(kLtrs);
                figs = false;
            }
            out.push_back(static_cast<uint8_t>(l));
            continue;
        }
        const int f = findCode(figTable, c);
        if (f >= 0)
        {
            if (!figs)
            {
                out.push_back(kFigs);
                figs = true;
            }
            out.push_back(static_cast<uint8_t>(f));
        }
        // anything else has no Baudot code and is dropped
    }
    return out;
}

char RttyBaudot::Receiver::push(uint8_t code, bool usos, bool ita2)
{
    code &= 31;
    if (code == kLtrs)
    {
        m_figs = false;
        return 0;
    }
    if (code == kFigs)
    {
        m_figs = true;
        return 0;
    }
    if (code == kSpace)
    {
        if (usos)
            m_figs = false;
        return ' ';
    }
    if (code == kCr || code == 0)
        return 0;
    if (code == kLf)
        return '\n';
    const char c = decode(code, m_figs, ita2);
    return c == '\a' ? 0 : c;
}

// ---------------------------------------------------------------------------
// RttyModulator

void RttyModulator::setConfig(const RttyConfig &cfg)
{
    m_cfg = cfg;
}

void RttyModulator::queueText(const std::string &text)
{
    queueCodes(RttyBaudot::encode(text, m_figs, m_cfg.usos, m_cfg.ita2));
}

void RttyModulator::queueCodes(const std::vector<uint8_t> &codes)
{
    for (uint8_t c : codes)
    {
        Item it;
        it.isCode = true;
        it.code = c & 31;
        m_queue.push_back(it);
        if (it.code == RttyBaudot::kLtrs)
            m_figs = false;
        else if (it.code == RttyBaudot::kFigs)
            m_figs = true;
    }
}

void RttyModulator::queueMark(double seconds)
{
    if (seconds <= 0.0)
        return;
    Item it;
    it.isCode = false;
    it.markSec = seconds;
    m_queue.push_back(it);
}

void RttyModulator::clear()
{
    m_queue.clear();
    m_symbol.clear();
    m_symIndex = 0;
    m_timeLeft = 0.0;
    m_busy = false;
    m_symIsCode = false;
    m_codesSent = 0;
    m_figs = false;
}

void RttyModulator::loadCode(uint8_t code)
{
    m_symbol.clear();
    m_symbol.emplace_back(false, 1.0); // start bit
    for (int i = 0; i < 5; ++i)
        m_symbol.emplace_back(((code >> i) & 1) != 0, 1.0);
    m_symbol.emplace_back(true, m_cfg.stopBits);
    m_symIndex = 0;
}

void RttyModulator::nextElement()
{
    if (m_symIndex >= m_symbol.size())
    {
        if (m_symIsCode && m_busy)
            ++m_codesSent;
        m_symIsCode = false;
        if (!m_queue.empty())
        {
            const Item it = m_queue.front();
            m_queue.pop_front();
            m_busy = true;
            if (it.isCode)
            {
                loadCode(it.code);
                m_symIsCode = true;
            }
            else
            {
                m_symbol.assign(1, std::make_pair(true, it.markSec * m_cfg.baud));
                m_symIndex = 0;
            }
        }
        else
        {
            m_busy = false;
            if (m_diddle)
                loadCode(RttyBaudot::kLtrs);
            else
            {
                m_symbol.assign(1, std::make_pair(true, 1.0));
                m_symIndex = 0;
            }
        }
    }
    const auto &e = m_symbol[m_symIndex++];
    m_curMark = e.first;
    m_timeLeft += e.second * m_cfg.sampleRate / m_cfg.baud;
}

void RttyModulator::render(float *out, size_t n)
{
    const double wM = kTwoPi * m_cfg.markHz / m_cfg.sampleRate;
    const double wS = kTwoPi * m_cfg.spaceHz() / m_cfg.sampleRate;
    for (size_t i = 0; i < n; ++i)
    {
        while (m_timeLeft <= 0.0)
            nextElement();
        m_phase += m_curMark ? wM : wS;
        if (m_phase > kTwoPi)
            m_phase -= kTwoPi;
        out[i] = m_amp * static_cast<float>(std::sin(m_phase));
        m_timeLeft -= 1.0;
    }
}

// ---------------------------------------------------------------------------
// RttyDemodulator

std::complex<double> RttyDemodulator::Lpf::push(std::complex<double> x)
{
    const size_t n = taps.size();
    buf[pos] = x;
    std::complex<double> acc = 0.0;
    size_t j = pos;
    for (size_t k = 0; k < n; ++k)
    {
        acc += taps[k] * buf[j];
        j = (j == 0) ? n - 1 : j - 1;
    }
    pos = (pos + 1) % n;
    return acc;
}

RttyDemodulator::RttyDemodulator()
{
    rebuild();
}

void RttyDemodulator::setConfig(const RttyConfig &cfg)
{
    m_cfg = cfg;
    rebuild();
}

void RttyDemodulator::rebuild()
{
    m_dphM = kTwoPi * m_cfg.markHz / m_cfg.sampleRate;
    m_dphS = kTwoPi * m_cfg.spaceHz() / m_cfg.sampleRate;
    m_dec = std::max(1, static_cast<int>(std::lround(m_cfg.sampleRate / kTargetRate)));
    m_fsd = m_cfg.sampleRate / m_dec;
    m_bitLen = m_fsd / m_cfg.baud;

    int len = static_cast<int>(std::lround(kLpfLenBits * m_bitLen)) | 1;
    len = std::max(len, 9);
    const double fc = kLpfFactor * m_cfg.baud / m_fsd; // cycles per sample
    std::vector<double> taps(static_cast<size_t>(len));
    double sum = 0.0;
    const int mid = len / 2;
    for (int i = 0; i < len; ++i)
    {
        const int k = i - mid;
        const double sinc = (k == 0) ? 2.0 * fc : std::sin(kTwoPi * fc * k) / (0.5 * kTwoPi * k);
        const double w = 0.42 - 0.5 * std::cos(kTwoPi * i / (len - 1)) + 0.08 * std::cos(2.0 * kTwoPi * i / (len - 1));
        taps[static_cast<size_t>(i)] = sinc * w;
        sum += sinc * w;
    }
    for (double &t : taps)
        t /= sum;
    for (Lpf *f : {&m_lpfM, &m_lpfS})
    {
        f->taps = taps;
        f->buf.assign(taps.size(), 0.0);
        f->pos = 0;
    }
    reset();
}

void RttyDemodulator::reset()
{
    m_decCount = 0;
    m_accM = m_accS = 0.0;
    for (Lpf *f : {&m_lpfM, &m_lpfS})
    {
        std::fill(f->buf.begin(), f->buf.end(), std::complex<double>(0.0));
        f->pos = 0;
    }
    m_markEnv = m_spaceEnv = m_markNoise = m_spaceNoise = 0.0;
    m_lastV = 0.0;
    m_quality = 0.0;
    m_state = Uart::Hunt;
    m_prevMark = true;
    m_markRun = 0;
    m_rx.reset();
    m_stats = Stats();
}

std::string RttyDemodulator::process(const float *in, size_t n)
{
    std::string out;
    for (size_t i = 0; i < n; ++i)
    {
        const double x = in[i];
        m_accM += std::complex<double>(x * std::cos(m_phM), -x * std::sin(m_phM));
        m_accS += std::complex<double>(x * std::cos(m_phS), -x * std::sin(m_phS));
        m_phM += m_dphM;
        if (m_phM > kTwoPi)
            m_phM -= kTwoPi;
        m_phS += m_dphS;
        if (m_phS > kTwoPi)
            m_phS -= kTwoPi;
        if (++m_decCount >= m_dec)
        {
            const double scale = 2.0 / m_dec;
            const std::complex<double> ym = m_lpfM.push(m_accM * scale);
            const std::complex<double> ys = m_lpfS.push(m_accS * scale);
            m_accM = m_accS = 0.0;
            m_decCount = 0;
            step(std::abs(ym), std::abs(ys), out);
        }
    }
    return out;
}

static inline double decayAvg(double avg, double x, double weight)
{
    return avg + (x - avg) / std::max(1.0, weight);
}

void RttyDemodulator::step(double m, double s, std::string &out)
{
    // W7AY optimal ATC (as used by fldigi): track each tone's envelope and
    // noise floor separately so a fading or missing tone still slices right.
    const double bl = m_bitLen;
    m_markEnv = decayAvg(m_markEnv, m, m > m_markEnv ? bl / 4.0 : bl * 16.0);
    m_spaceEnv = decayAvg(m_spaceEnv, s, s > m_spaceEnv ? bl / 4.0 : bl * 16.0);
    m_markNoise = decayAvg(m_markNoise, m, m < m_markNoise ? bl / 4.0 : bl * 48.0);
    m_spaceNoise = decayAvg(m_spaceNoise, s, s < m_spaceNoise ? bl / 4.0 : bl * 48.0);
    const double noise = std::min(m_markNoise, m_spaceNoise);
    const double mc = std::clamp(m, noise, std::max(noise, m_markEnv));
    const double sc = std::clamp(s, noise, std::max(noise, m_spaceEnv));
    const double me = m_markEnv - noise;
    const double se = m_spaceEnv - noise;
    const double v = (mc - noise) * me - (sc - noise) * se - 0.25 * (me * me - se * se);
    m_lastV = v;

    m_stats.markLevel = m_markEnv;
    m_stats.spaceLevel = m_spaceEnv;
    m_stats.noiseLevel = noise;
    const double contrast = (m + s) > 1e-15 ? std::fabs(m - s) / (m + s) : 0.0;
    m_quality = decayAvg(m_quality, contrast, bl * 4.0);
    m_stats.quality = m_quality;
    if (m_squelch <= 0.0)
        m_stats.squelchOpen = true;
    else if (m_stats.squelchOpen)
        m_stats.squelchOpen = m_quality > m_squelch - 0.05;
    else
        m_stats.squelchOpen = m_quality > m_squelch;

    uart(v, out);
}

void RttyDemodulator::emitCode(uint8_t code, std::string &out)
{
    const char c = m_rx.push(code, m_cfg.usos, m_cfg.ita2);
    ++m_stats.chars;
    if (c != 0 && m_stats.squelchOpen)
        out.push_back(c);
}

void RttyDemodulator::uart(double v, std::string &out)
{
    const bool mark = v > 0.0;
    const double bl = m_bitLen;
    if (m_state == Uart::Hunt)
    {
        if (m_prevMark && !mark && m_markRun >= static_cast<int>(bl * 0.5))
        {
            m_state = Uart::Frame;
            m_t = 0.0;
            m_bitIndex = 0;
            m_acc = 0.0;
            m_accN = 0;
            m_code = 0;
        }
        m_markRun = mark ? m_markRun + 1 : 0;
        m_prevMark = mark;
        return;
    }

    // Frame: integrate the middle 60% of each bit and decide at its end.
    m_t += 1.0;
    const double p = m_t / bl;
    const double lo = m_bitIndex + 0.2;
    const double hi = m_bitIndex + 0.8;
    if (p >= lo && p < hi)
    {
        m_acc += v;
        ++m_accN;
        return;
    }
    if (p < hi)
        return;

    const bool bit = m_acc > 0.0;
    m_acc = 0.0;
    m_accN = 0;
    if (m_bitIndex == 0)
    {
        if (bit)
        { // start bit did not hold: noise, back to hunting
            m_state = Uart::Hunt;
            m_prevMark = true;
            m_markRun = 0;
            return;
        }
    }
    else if (m_bitIndex <= 5)
    {
        if (bit)
            m_code |= 1 << (m_bitIndex - 1);
    }
    else
    {
        m_state = Uart::Hunt;
        if (bit)
        {
            emitCode(static_cast<uint8_t>(m_code), out);
            m_prevMark = true;
            m_markRun = static_cast<int>(bl); // the stop bit counts as mark
        }
        else
        {
            ++m_stats.framingErrors;
            m_prevMark = false;
            m_markRun = 0;
        }
        return;
    }
    ++m_bitIndex;
}

// ---------------------------------------------------------------------------
// Tone pair finder

RttyTonePair rttyFindTonePair(
    const float *x, size_t n, double sampleRate, double shiftHz, double tolHz, double loHz, double hiHz)
{
    RttyTonePair r;
    const int seg = static_cast<int>(sampleRate / 5.0);
    if (seg < 16 || n < static_cast<size_t>(seg))
        return r;
    const double binHz = sampleRate / seg;
    const int lo = std::max(2, static_cast<int>(loHz / binHz));
    const int hi = std::min(seg / 2 - 2, static_cast<int>(std::min(hiHz, sampleRate / 2 - 100) / binHz));
    if (hi <= lo + 4)
        return r;

    std::vector<double> win(static_cast<size_t>(seg));
    for (int i = 0; i < seg; ++i)
        win[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(kTwoPi * i / (seg - 1));
    std::vector<double> P(static_cast<size_t>(hi + 2), 0.0);
    int segs = 0;
    for (size_t off = 0; off + static_cast<size_t>(seg) <= n; off += static_cast<size_t>(seg))
    {
        for (int k = lo - 1; k <= hi + 1; ++k)
        { // Goertzel per bin
            const double cw = 2.0 * std::cos(kTwoPi * k / seg);
            double s1 = 0.0, s2 = 0.0;
            for (int i = 0; i < seg; ++i)
            {
                const double s0 = x[off + static_cast<size_t>(i)] * win[static_cast<size_t>(i)] + cw * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            P[static_cast<size_t>(k)] += s1 * s1 + s2 * s2 - cw * s1 * s2;
        }
        ++segs;
    }
    std::vector<double> dB(P.size(), -300.0);
    for (size_t k = 0; k < P.size(); ++k)
        dB[k] = 10.0 * std::log10(P[k] / segs + 1e-30);
    std::vector<double> sorted(dB.begin() + lo, dB.begin() + hi);
    std::sort(sorted.begin(), sorted.end());
    const double floorDb = sorted[sorted.size() / 2];

    double best = -1e9;
    int bl = 0, bh = 0;
    const int dmin = std::max(1, static_cast<int>((shiftHz - tolHz) / binHz));
    const int dmax = static_cast<int>((shiftHz + tolHz) / binHz);
    for (int k = lo; k < hi; ++k)
    {
        if (!(dB[k] >= dB[k - 1] && dB[k] >= dB[k + 1]))
            continue;
        for (int d = dmin; d <= dmax && k + d < hi; ++d)
        {
            const int j = k + d;
            if (!(dB[j] >= dB[j - 1] && dB[j] >= dB[j + 1]))
                continue;
            const double v = std::min(dB[k], dB[j]);
            if (v > best)
            {
                best = v;
                bl = k;
                bh = j;
            }
        }
    }
    if (bl == 0)
        return r;
    auto refine = [&](int k)
    {
        const double a = dB[k - 1], b = dB[k], c = dB[k + 1], den = a - 2.0 * b + c;
        return (k + (std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0)) * binHz;
    };
    r.found = true;
    r.lowHz = refine(bl);
    r.highHz = refine(bh);
    r.levelDb = best - floorDb;
    return r;
}

// ---------------------------------------------------------------------------
// Crossed-ellipse scope

void RttyScope::Biquad::design(double sampleRate, double f0, double bw)
{
    // RBJ band-pass, 0 dB peak gain.
    const double w0 = kTwoPi * f0 / sampleRate;
    const double q = std::max(0.5, f0 / std::max(1.0, bw));
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = alpha / a0;
    b2 = -alpha / a0;
    a1 = -2.0 * std::cos(w0) / a0;
    a2 = (1.0 - alpha) / a0;
    x1 = x2 = y1 = y2 = 0.0;
}

double RttyScope::Biquad::run(double x)
{
    const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
}

void RttyScope::setConfig(double sampleRate, double markHz, double spaceHz, double baud, double shiftHz)
{
    if (sampleRate <= 0.0)
        return;
    // Wide enough to pass the keying, narrow enough to keep the other tone out.
    const double bw = std::clamp(std::min(1.5 * baud, 0.6 * shiftHz), 20.0, 400.0);
    for (Biquad &b : m_mark)
        b.design(sampleRate, markHz, bw);
    for (Biquad &b : m_space)
        b.design(sampleRate, spaceHz, bw);
    m_decay = std::exp(-1.0 / (0.7 * sampleRate)); // peak follower falls with ~0.7 s time constant
    m_peak = 1e-3;
}

void RttyScope::reset()
{
    for (Biquad *b : {&m_mark[0], &m_mark[1], &m_space[0], &m_space[1]})
        b->x1 = b->x2 = b->y1 = b->y2 = 0.0;
    m_peak = 1e-3;
    m_phase = 0;
}

void RttyScope::process(const float *in, size_t n, size_t step, std::vector<Point> &out)
{
    step = std::max<size_t>(1, step);
    for (size_t i = 0; i < n; ++i)
    {
        const double x = m_mark[1].run(m_mark[0].run(in[i]));
        const double y = m_space[1].run(m_space[0].run(in[i]));
        const double a = std::max(std::fabs(x), std::fabs(y));
        m_peak = std::max(a, m_peak * m_decay);
        if (++m_phase >= step)
        {
            m_phase = 0;
            const double s = 1.0 / (1.1 * std::max(m_peak, 1e-9));
            out.push_back({static_cast<float>(x * s), static_cast<float>(y * s)});
        }
    }
}
