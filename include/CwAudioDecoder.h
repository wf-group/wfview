#pragma once

// Software CW (Morse) decoder for the CW Sender window. Works with any radio:
// it decodes the receive audio rather than relying on a decoder in the rig.
//
//   RxAudioProcessor (converter thread)
//       -- cwTapAudio(mono, sampleRate) [queued] -->
//   CwAudioDecoderWorker (own QThread, runs CwDecoderCore / CwMultiDecoder)
//       -- textDecoded / statsUpdated / multiUpdated [queued] -->
//   CwAudioDecoder (GUI thread, exposed to QML as CWSenderController.audioDecoder)
//
// The tap costs nothing while decoding is off.

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVector>
#include <memory>

class CwDecoderCore;
class CwMultiDecoder;
class QThread;
class RxAudioProcessor;

class CwAudioDecoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit CwAudioDecoderWorker(QObject *parent = nullptr);
    ~CwAudioDecoderWorker() override;

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    // mode: see CwAudioDecoder::PitchMode. radioHz <= 0 = not read from the radio yet.
    void setPitchMode(int mode, float fixedHz, float radioHz);
    void setSpeedWpm(float wpm); // <= 0 = auto
    void setSquelch(bool on);
    void setEngine(int engine); // see CwAudioDecoder::Engine
    void setMulti(bool on);
    void resetDecoder();

signals:
    void textDecoded(QString text);
    // List of QVariantMap {id, pitch, wpm, snr, active, text}.
    void multiUpdated(QVariantList channels);
    void statsUpdated(float pitchHz, float speedWpm, float snrDb, bool locked, bool squelchOpen, float sampleRate);

private:
    std::unique_ptr<CwDecoderCore> m_core;
    std::unique_ptr<CwMultiDecoder> m_multi;
    QElapsedTimer m_statsTimer;
    QElapsedTimer m_multiTimer;
};

class CwAudioDecoder : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool squelch READ squelch WRITE setSquelch NOTIFY squelchChanged)
    Q_PROPERTY(int engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(bool multiSignal READ multiSignal WRITE setMultiSignal NOTIFY multiSignalChanged)
    Q_PROPERTY(int pitchMode READ pitchMode WRITE setPitchMode NOTIFY pitchModeChanged)
    Q_PROPERTY(int fixedPitch READ fixedPitch WRITE setFixedPitch NOTIFY fixedPitchChanged)
    Q_PROPERTY(int pitchMin READ pitchMin CONSTANT)
    Q_PROPERTY(int pitchMax READ pitchMax CONSTANT)
    Q_PROPERTY(bool fixedSpeed READ fixedSpeed WRITE setFixedSpeed NOTIFY fixedSpeedChanged)
    Q_PROPERTY(int speedWpm READ speedWpm WRITE setSpeedWpm NOTIFY speedWpmChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)

public:
    enum PitchMode
    {
        PitchFollowRadio = 0, // track the strongest tone near the radio's CW pitch
        PitchAuto = 1, // track the strongest steady tone, 200-1200 Hz
        PitchFixed = 2 // decode exactly at fixedPitch
    };
    Q_ENUM(PitchMode)

    enum Engine
    {
        EngineMachine = 0, // ggmorse: keyer / computer-sent CW
        EngineHandKey = 1 // learns each sender's own timing
    };
    Q_ENUM(Engine)

    explicit CwAudioDecoder(QObject *parent = nullptr);
    ~CwAudioDecoder() override;

    void setRxAudioProcessor(RxAudioProcessor *rxProc);

    bool enabled() const
    {
        return m_enabled;
    }
    bool squelch() const
    {
        return m_squelch;
    }
    int engine() const
    {
        return m_engine;
    }
    bool multiSignal() const
    {
        return m_multiSignal;
    }
    int pitchMode() const
    {
        return m_pitchMode;
    }
    int fixedPitch() const
    {
        return m_fixedPitch;
    }
    int pitchMin() const;
    int pitchMax() const;
    bool fixedSpeed() const
    {
        return m_fixedSpeed;
    }
    int speedWpm() const
    {
        return m_speedWpm;
    }
    QString status() const
    {
        return m_status;
    }
    QVariantList channels() const
    {
        return m_channels;
    }

    void setEnabled(bool on);
    void setSquelch(bool on);
    void setEngine(int engine);
    void setMultiSignal(bool on);
    void setPitchMode(int mode);
    void setFixedPitch(int hz);
    void setFixedSpeed(bool on);
    void setSpeedWpm(int wpm);

    // Restart pitch/speed tracking, e.g. after changing stations.
    Q_INVOKABLE void resync();
    // Make the station at this pitch (from the multi-signal list) the main decode.
    Q_INVOKABLE void decodeChannel(int pitchHz);

public slots:
    // CW pitch reported by the radio, in Hz.
    void setRadioPitch(int hz);
    // Text being sent, shown inline as a "[TX] ..." line.
    void appendSent(const QString &text);

signals:
    // Text for the decode pane: decoded characters, [TX] lines and notices.
    void textAppended(QString text);
    // Emitted every few seconds in follow-radio mode so the radio's CW pitch is re-read.
    void radioPitchRequested();

    void enabledChanged();
    void squelchChanged();
    void engineChanged();
    void multiSignalChanged();
    void pitchModeChanged();
    void fixedPitchChanged();
    void fixedSpeedChanged();
    void speedWpmChanged();
    void statusChanged();
    void channelsChanged();

private slots:
    void onTextDecoded(const QString &text);
    void onStatsUpdated(float pitchHz, float speedWpm, float snrDb, bool locked, bool squelchOpen, float sampleRate);
    void onMultiUpdated(const QVariantList &channels);

private:
    void connectTap();
    void disconnectTap();
    void applyDecodeSettings();
    void appendText(const QString &text);
    void setStatus(const QString &status);

    QPointer<RxAudioProcessor> m_rxProc;
    QThread *m_thread = nullptr;
    CwAudioDecoderWorker *m_worker = nullptr;
    QMetaObject::Connection m_tapConn;

    bool m_enabled = false;
    bool m_squelch = true;
    int m_engine = EngineMachine;
    bool m_multiSignal = false;
    int m_pitchMode = PitchFollowRadio;
    int m_fixedPitch = 600;
    bool m_fixedSpeed = false;
    int m_speedWpm = 20;
    int m_radioPitchHz = 0;
    QString m_status;
    QVariantList m_channels;

    QTimer m_pitchPoll;
    QTimer m_txTimer;
    QString m_txPending;
    bool m_atLineStart = true;
};
