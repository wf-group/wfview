#ifndef AUDIOHANDLERTCIINPUT_H
#define AUDIOHANDLERTCIINPUT_H

#include "audiohandlerbase.h"

#include <QElapsedTimer>
#include <QTimer>


class audioHandlerTciInput : public audioHandlerBase {
    Q_OBJECT

public:
    explicit audioHandlerTciInput(QObject* parent = nullptr) : audioHandlerBase(parent) {}
    ~audioHandlerTciInput() override { dispose(); } // ensure close on destruction
    QString role() const override { return QStringLiteral("Input"); }

    // TCI clients send transmit audio in lumps (one per TX chrono, ~43 ms) and
    // their timing jitters. Audio is collected here and passed on one block at
    // a time on a steady clock, the way a sound card input would deliver it.
    // 150 ms covered a 43 ms lump plus the worst chrono-to-audio jitter seen
    // in testing (about 45 ms) with about 70 ms to spare; 80 ms underran.
    static constexpr int prebufferMs = 150;  // collected before output starts
    static constexpr int maxBufferMs = 400;  // oldest audio is dropped beyond this

protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    virtual QAudioFormat getNativeFormat() override;
    virtual bool isFormatSupported(QAudioFormat f) override;

private slots:
    void receiveTCIAudio(const QByteArray data);
    void pace();
    void onConverted(audioPacket pkt);

private:
    void logPacingStats(bool force);

    static constexpr quint64 maxCatchUpBlocks = 3;

    QByteArray pending;
    QTimer *paceTimer = nullptr;
    QElapsedTimer paceClock;
    quint64 blocksSent = 0;
    bool priming = true;
    int bytesPerFrame = 8;
    int bytesPerBlock = 0;
    int blockMs = 20;
    int prebufferBytes = 0;
    int maxBufferBytes = 0;
    quint64 underruns = 0;
    quint64 droppedBytes = 0;
    quint64 lateResyncs = 0;
    QElapsedTimer statsLog;

};

#endif // AUDIOHANDLERTCIINPUT_H
