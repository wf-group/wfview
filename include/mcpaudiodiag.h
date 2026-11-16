#pragma once

// Only built with qmake CONFIG+=mcp (defines WFVIEW_MCP). Reached from wfview
// code through MCP_AUDIO_DIAG() in mcphooks.h.
//
// Audio timing diagnostics, off until an MCP client turns them on with
// set_audio_diagnostics, and off again by themselves when their time runs out.
// While off, each hook costs one atomic load.
//
// Each stage is a point on an audio path. For each one this records how often
// it ran and the spread of intervals between runs, a value the hook passes
// (bytes, frames, buffer level or age), sample discontinuities between
// consecutive blocks, and sequence-number gaps. Unusual events (late runs,
// underruns, dropped audio, discontinuities, lost packets) go into a short
// list with their time, so they can be lined up with what was heard or
// recorded elsewhere.
//
// The hooks run on real-time audio and network threads, so they never take a
// lock or allocate: all state is atomics, and anomaly descriptions must be
// string literals.

#include <QJsonObject>
#include <QTime>
#include <QtGlobal>

#include <atomic>

namespace McpAudioDiag
{
enum Stage : int
{
    // Transmit, TCI client to radio
    TciChronoSent = 0, // TCI server sent a TX chrono frame (value: chrono number)
    TciTxFromClient,   // TX audio frame arrived from a TCI client (value: frames)
    TciTxPaced,        // TCI pacer released a block (value: frames still buffered)
    TciTxConverted,    // that block, converted to the radio's format (value: bytes)
    // Transmit, local microphone to radio
    MicTxForwarded,    // mic block routed towards the radio (value: bytes)
    // Transmit, any source: the last step before the radio
    RadioTxPacket,     // TX audio datagram sent to the radio (value: bytes)
    RadioTxAgeMs,      // gauge: ms from a block's creation to its sending
    // Receive
    RadioRxPacket,     // RX audio datagram from the radio (value: bytes)
    TciRxToClient,     // RX audio frame sent to TCI clients (value: floats)
    StageCount
};

extern std::atomic_bool s_active;

inline bool active()
{
    return s_active.load(std::memory_order_relaxed);
}

// Hooks, any thread. Each returns at once when diagnostics are off.

// The stage ran; records the interval since its last run, and value.
void event(Stage stage, qint64 value = 0);
// Records value only (no interval), for gauges such as RadioTxAgeMs.
void gauge(Stage stage, qint64 value);
// Something unusual. what must be a string literal.
void anomaly(Stage stage, const char *what, double value = 0.0);
// Compares the first sample of a block with the last sample of the previous
// block on the same stage (first channel only). A jump much larger than the
// steepest step inside either block is counted as a discontinuity.
void continuityInt16(Stage stage, const char *data, int bytes, int channels);
void continuityFloat(Stage stage, const float *samples, int count, int channels);
// A 16-bit sequence number that should go up by one each time.
void sequence16(Stage stage, quint16 seq);
// Records the age in ms of a block made at created (QTime::currentTime()) as
// a gauge; QTime wraps at midnight, which is allowed for.
void gaugeAgeMs(Stage stage, const QTime &created);

// wfview codec numbers that carry plain 16-bit PCM, which the continuity
// check can read; 0 for anything else (uLaw, Opus, ADPCM, 8-bit).
inline int pcm16Channels(quint8 codec)
{
    return codec == 0x04 ? 1 : (codec == 0x10 ? 2 : 0);
}

// Control and report, MCP thread.
void start(int seconds, bool clear);
void stop();
QJsonObject status();
// reset clears everything recorded so far after reading it.
QJsonObject report(int maxAnomalies, bool reset);

constexpr int maxSeconds = 3600;
} // namespace McpAudioDiag
