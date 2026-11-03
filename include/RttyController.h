#pragma once

// RTTY (Baudot FSK) receive and AFSK transmit for the RTTY window.
//
//   RxAudioProcessor (converter thread)
//       -- rttyTapAudio(mono, sampleRate) [queued] -->
//   RttyDecoderWorker (own QThread, runs RttyDemodulator)
//       -- textDecoded / statsUpdated / tonesFound [queued] -->
//   RttyController (GUI thread, exposed to QML as MainController.rtty)
//
//   RttyController --> RttyTxSource <-- TxAudioProcessor (converter thread)
//   RttyController -- transmitRequested(bool) --> MainController::setTransmit()
//
// Transmit sends audio tones (AFSK) through wfview's TX audio path, so the
// radio must be in a data/SSB mode that takes the computer's audio
// (USB-D or LSB-D on Icom) and a TX audio input must be configured.

#include "rttycore.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVector>
#include <memory>
#include <vector>

class QSettings;
class QThread;
class RxAudioProcessor;
class RttyTxSource;
class TxAudioProcessor;

class RttyDecoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit RttyDecoderWorker(QObject *parent = nullptr);

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setParams(double baud, double shiftHz, double markHz, bool reverse, bool usos, bool ita2, double squelch);
    void resetDecoder();
    void findTones(double shiftHz);

signals:
    void textDecoded(QString text);
    // Levels in dB above the noise floor; quality 0..1.
    void statsUpdated(double quality, double markDb, double spaceDb, bool squelchOpen, double sampleRate);
    void tonesFound(bool found, double lowHz, double highHz, double levelDb);
    // Crossed-ellipse scope points, flat [x0, y0, x1, y1, ...] in -1..1, about 15 frames a second.
    void scopeUpdated(QVariantList xy);

private:
    void applyConfig();

    RttyConfig m_cfg;
    double m_squelch = 0.0;
    RttyDemodulator m_demod;
    RttyScope m_scope;
    std::vector<RttyScope::Point> m_scopePts;
    QElapsedTimer m_scopeTimer;
    std::vector<float> m_history; // last few seconds, for findTones()
    QElapsedTimer m_statsTimer;
};

class RttyController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool decoding READ decoding WRITE setDecoding NOTIFY decodingChanged)
    Q_PROPERTY(bool canTransmit READ canTransmit NOTIFY canTransmitChanged)
    Q_PROPERTY(bool transmitting READ transmitting NOTIFY transmittingChanged)
    Q_PROPERTY(bool holdTx READ holdTx WRITE setHoldTx NOTIFY holdTxChanged)

    Q_PROPERTY(double baud READ baud WRITE setBaud NOTIFY settingsChanged)
    Q_PROPERTY(int shift READ shift WRITE setShift NOTIFY settingsChanged)
    Q_PROPERTY(int markHz READ markHz WRITE setMarkHz NOTIFY settingsChanged)
    Q_PROPERTY(int spaceHz READ spaceHz NOTIFY settingsChanged)
    Q_PROPERTY(bool usb READ usb WRITE setUsb NOTIFY settingsChanged)
    Q_PROPERTY(bool reverse READ reverse WRITE setReverse NOTIFY settingsChanged)
    Q_PROPERTY(double stopBits READ stopBits WRITE setStopBits NOTIFY settingsChanged)
    Q_PROPERTY(bool usos READ usos WRITE setUsos NOTIFY settingsChanged)
    Q_PROPERTY(bool ita2 READ ita2 WRITE setIta2 NOTIFY settingsChanged)
    Q_PROPERTY(double squelch READ squelch WRITE setSquelch NOTIFY settingsChanged)
    Q_PROPERTY(bool diddle READ diddle WRITE setDiddle NOTIFY settingsChanged)
    Q_PROPERTY(double txLevel READ txLevel WRITE setTxLevel NOTIFY settingsChanged)
    Q_PROPERTY(QString myCall READ myCall WRITE setMyCall NOTIFY settingsChanged)
    Q_PROPERTY(QString call READ call WRITE setCall NOTIFY callChanged)
    Q_PROPERTY(QStringList macroNames READ macroNames NOTIFY macrosChanged)
    Q_PROPERTY(QStringList macroTexts READ macroTexts NOTIFY macrosChanged)

    Q_PROPERTY(double quality READ quality NOTIFY statsChanged)
    Q_PROPERTY(double markDb READ markDb NOTIFY statsChanged)
    Q_PROPERTY(double spaceDb READ spaceDb NOTIFY statsChanged)
    Q_PROPERTY(bool squelchOpen READ squelchOpen NOTIFY statsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    static constexpr int kMacroCount = 8;

    explicit RttyController(QObject *parent = nullptr);
    ~RttyController() override;

    void setRxAudioProcessor(RxAudioProcessor *rxProc);
    void setTxAudioProcessor(TxAudioProcessor *txProc);
    std::shared_ptr<RttyTxSource> txSource() const
    {
        return m_tx;
    }

    // Settings live in the [RTTY] group of wfview's settings.
    void loadSettings(QSettings *settings);

    // From MainController.
    void setCanTransmit(bool on);
    void setRadioTransmitting(bool on);

    bool visible() const
    {
        return m_visible;
    }
    bool decoding() const
    {
        return m_decoding;
    }
    bool canTransmit() const
    {
        return m_canTransmit;
    }
    bool transmitting() const
    {
        return m_keyed;
    }
    bool holdTx() const
    {
        return m_holdTx;
    }
    double baud() const
    {
        return m_baud;
    }
    int shift() const
    {
        return m_shift;
    }
    int markHz() const
    {
        return m_markHz;
    }
    int spaceHz() const;
    bool usb() const
    {
        return m_usb;
    }
    bool reverse() const
    {
        return m_reverse;
    }
    double stopBits() const
    {
        return m_stopBits;
    }
    bool usos() const
    {
        return m_usos;
    }
    bool ita2() const
    {
        return m_ita2;
    }
    double squelch() const
    {
        return m_squelch;
    }
    bool diddle() const
    {
        return m_diddle;
    }
    double txLevel() const
    {
        return m_txLevel;
    }
    QString myCall() const
    {
        return m_myCall;
    }
    QString call() const
    {
        return m_call;
    }
    QStringList macroNames() const
    {
        return m_macroNames;
    }
    QStringList macroTexts() const
    {
        return m_macroTexts;
    }
    double quality() const
    {
        return m_quality;
    }
    double markDb() const
    {
        return m_markDb;
    }
    double spaceDb() const
    {
        return m_spaceDb;
    }
    bool squelchOpen() const
    {
        return m_squelchOpen;
    }
    QString status() const
    {
        return m_status;
    }

    void setVisible(bool on);
    void setDecoding(bool on);
    void setHoldTx(bool on);
    void setBaud(double baud);
    void setShift(int hz);
    void setMarkHz(int hz);
    void setUsb(bool on);
    void setReverse(bool on);
    void setStopBits(double bits);
    void setUsos(bool on);
    void setIta2(bool on);
    void setSquelch(double q);
    void setDiddle(bool on);
    void setTxLevel(double level);
    void setMyCall(const QString &c);
    void setCall(const QString &c);

    // Queue text to send; keys the radio if needed. {MYCALL} and {CALL} are expanded.
    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE void sendMacro(int index);
    Q_INVOKABLE void setMacro(int index, const QString &name, const QString &text);
    // Key up and wait for text (idle mark or diddles) until finishTx().
    Q_INVOKABLE void startTx();
    // Unkey once everything queued has been sent.
    Q_INVOKABLE void finishTx();
    // Unkey now and drop anything queued.
    Q_INVOKABLE void abortTx();
    // Find the strongest tone pair in the last few seconds and tune to it.
    Q_INVOKABLE void autoTune();
    Q_INVOKABLE void resync();

signals:
    void transmitRequested(bool on);
    // Text for the receive pane: decoded characters, [TX] echo and notices.
    void textAppended(QString text);

    void visibleChanged();
    void decodingChanged();
    void canTransmitChanged();
    void transmittingChanged();
    void holdTxChanged();
    void settingsChanged();
    void callChanged();
    void macrosChanged();
    void statsChanged();
    void statusChanged();
    // Points for the tuning scope (see RttyDecoderWorker::scopeUpdated).
    void scopePoints(QVariantList xy);

private slots:
    void onTextDecoded(const QString &text);
    void onStatsUpdated(double quality, double markDb, double spaceDb, bool squelchOpen, double sampleRate);
    void onTonesFound(bool found, double lowHz, double highHz, double levelDb);
    void onScopeUpdated(const QVariantList &xy);
    void pollTx();

private:
    RttyConfig config() const;
    void applyConfig();
    void save(const char *key, const QVariant &value);
    void saveMacros();
    void keyUp();
    void release(const QString &why);
    void connectTap();
    void disconnectTap();
    void setStatus(const QString &s);
    QString expand(const QString &text) const;

    QPointer<RxAudioProcessor> m_rxProc;
    QPointer<TxAudioProcessor> m_txProc;
    QSettings *m_settings = nullptr;
    QThread *m_thread = nullptr;
    RttyDecoderWorker *m_worker = nullptr;
    QMetaObject::Connection m_tapConn;
    std::shared_ptr<RttyTxSource> m_tx;

    bool m_visible = false;
    bool m_decoding = false;
    bool m_canTransmit = false;
    bool m_holdTx = false;

    double m_baud = 45.45;
    int m_shift = 170;
    int m_markHz = 2295;
    bool m_usb = true;
    bool m_reverse = false;
    double m_stopBits = 1.5;
    bool m_usos = true;
    bool m_ita2 = false;
    double m_squelch = 0.5;
    bool m_diddle = false;
    double m_txLevel = 0.5;
    QString m_myCall;
    QString m_call;
    QStringList m_macroNames;
    QStringList m_macroTexts;

    double m_quality = 0.0;
    double m_markDb = 0.0;
    double m_spaceDb = 0.0;
    bool m_squelchOpen = false;
    double m_rxRate = 0.0;
    QString m_status;

    // transmit state
    bool m_keyed = false;
    bool m_autoRelease = false;
    bool m_tailQueued = false;
    bool m_radioTx = false;
    bool m_radioWasTx = false;
    QElapsedTimer m_keyedTimer;
    QTimer m_txPoll;
    bool m_atLineStart = true;
};
