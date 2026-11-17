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
#include <QString>
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

// PCM recording at the radio end, for offline analysis of what was actually
// sent to and received from the radio. Only while diagnostics run and only if
// they were started with record=true. Each stream keeps its most recent
// recordBytesPerStream bytes in a buffer allocated on the first recording and
// kept for the life of the process; the hook copies into it without locks.
enum RecordStream : int
{
    RecordRadioTx = 0, // 16-bit PCM datagrams as sent to the radio
    RecordRadioRx,     // 16-bit PCM datagrams as received from the radio
    RecordStreamCount
};

extern std::atomic_bool s_recording;

inline bool recording()
{
    return s_recording.load(std::memory_order_acquire);
}

// Hook: one datagram of 16-bit PCM (bytes must be whole frames).
void recordPcm16(RecordStream stream, const char *data, int bytes, int channels, int sampleRate);

constexpr qint64 recordBytesPerStream = 16 * 1024 * 1024;

// Control and report, MCP thread.
// record also records radio PCM (see above), discarding any earlier recording.
void start(int seconds, bool clear, bool record = false);
void stop();
QJsonObject status();
// reset clears everything recorded so far after reading it.
QJsonObject report(int maxAnomalies, bool reset);
// Stops recording and writes radio_tx.wav / radio_rx.wav with
// radio_tx_packets.csv / radio_rx_packets.csv (time of each datagram and its
// position in the WAV file) into dir.
QJsonObject saveRecording(const QString &dir);

constexpr int maxSeconds = 3600;
} // namespace McpAudioDiag
