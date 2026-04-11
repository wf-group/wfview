#ifndef AUDIOHANDLERRTINPUT_H
#define AUDIOHANDLERRTINPUT_H

#include "audiohandlerbase.h"

#ifndef Q_OS_LINUX
#include "RtAudio.h"
#else
#include "rtaudio/RtAudio.h"
#endif

#include <memory>
#include "bytering.h"


class audioHandlerRtInput : public audioHandlerBase {
    Q_OBJECT

public:
    explicit audioHandlerRtInput(QObject* parent=nullptr);
    ~audioHandlerRtInput() override { dispose(); } // ensure close on destruction
    QString role() const override { return QStringLiteral("Input"); }

protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    virtual QAudioFormat getNativeFormat() override;
    virtual bool isFormatSupported(QAudioFormat f) override;

private slots:
    void onReadyRead();
    void onConverted(audioPacket pkt);

private:
    static int rtCallback(void* out, void* in, unsigned nFrames, double, RtAudioStreamStatus st, void* u);
    void handleCallbackInput(const void* in, unsigned nFrames, RtAudioStreamStatus st);

private:
#if defined(Q_OS_LINUX)
    RtAudio rtaudio{RtAudio::Api::LINUX_ALSA};
#elif defined(Q_OS_WIN)
    RtAudio rtaudio{RtAudio::Api::WINDOWS_WASAPI};
#elif defined(Q_OS_MACOS)
    RtAudio rtaudio{RtAudio::Api::MACOSX_CORE};
#else
    RtAudio rtaudio;
#endif
    RtAudioFormat fmt{RTAUDIO_SINT16};
    unsigned bytesPerSample{2};
    unsigned bytesPerFrame{2};
    std::unique_ptr<ByteRing> inRB;
    RtAudio::StreamParameters aParams;
};

#endif // AUDIOHANDLERRTINPUT_H
