#ifndef CWSENDERCONTROLLER_H
#define CWSENDERCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QTimer>
#include "wfviewtypes.h"
#include "cachingqueue.h"
#include "rigcommander.h"
#include "cwsidetonegenerator.h"

#include <memory>

class CWSenderController : public QObject
{
    Q_OBJECT
    
    // Properties for QML binding
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(int wpm READ wpm WRITE setWpm NOTIFY wpmChanged)
    Q_PROPERTY(double dashRatio READ dashRatio WRITE setDashRatio NOTIFY dashRatioChanged)
    Q_PROPERTY(int pitch READ pitch WRITE setPitch NOTIFY pitchChanged)
    Q_PROPERTY(int pitchMin READ pitchMin NOTIFY pitchRangeChanged)
    Q_PROPERTY(int pitchMax READ pitchMax NOTIFY pitchRangeChanged)
    Q_PROPERTY(int breakInMode READ breakInMode WRITE setBreakInMode NOTIFY breakInModeChanged)
    Q_PROPERTY(bool cutNumbers READ cutNumbers WRITE setCutNumbers NOTIFY cutNumbersChanged)
    Q_PROPERTY(bool sendImmediate READ sendImmediate WRITE setSendImmediate NOTIFY sendImmediateChanged)
    Q_PROPERTY(bool sidetoneEnable READ sidetoneEnable WRITE setSidetoneEnable NOTIFY sidetoneEnableChanged)
    Q_PROPERTY(int sidetoneLevel READ sidetoneLevel WRITE setSidetoneLevel NOTIFY sidetoneLevelChanged)
    Q_PROPERTY(bool sidetoneAvailable READ sidetoneAvailable NOTIFY sidetoneAvailableChanged)
    Q_PROPERTY(int sequenceNumber READ sequenceNumber WRITE setSequenceNumber NOTIFY sequenceNumberChanged)
    Q_PROPERTY(int maxChars READ maxChars NOTIFY maxCharsChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool macroEditMode READ macroEditMode WRITE setMacroEditMode NOTIFY macroEditModeChanged)
    Q_PROPERTY(bool receiveVisible READ receiveVisible WRITE setReceiveVisible NOTIFY receiveVisibleChanged)
    Q_PROPERTY(bool canSendCW READ canSendCW NOTIFY canSendCWChanged)

    // CW decoder. Each control is only offered when the rig file defines its command.
    Q_PROPERTY(bool cwDecodeAvailable READ cwDecodeAvailable NOTIFY cwCapsChanged)
    Q_PROPERTY(bool cwDecodeActive READ cwDecodeActive NOTIFY cwDecodeActiveChanged)
    Q_PROPERTY(bool cwDecodeBusy READ cwDecodeBusy NOTIFY cwDecodeBusyChanged)
    Q_PROPERTY(bool cwDecodeThresholdAvailable READ cwDecodeThresholdAvailable NOTIFY cwCapsChanged)
    Q_PROPERTY(int cwDecodeThresholdMin READ cwDecodeThresholdMin NOTIFY cwCapsChanged)
    Q_PROPERTY(int cwDecodeThresholdMax READ cwDecodeThresholdMax NOTIFY cwCapsChanged)
    Q_PROPERTY(int cwDecodeThreshold READ cwDecodeThreshold WRITE setCwDecodeThreshold NOTIFY cwDecodeThresholdChanged)
    Q_PROPERTY(bool cwDecodeFilterAvailable READ cwDecodeFilterAvailable NOTIFY cwCapsChanged)
    Q_PROPERTY(int cwDecodeFilter READ cwDecodeFilter WRITE setCwDecodeFilter NOTIFY cwDecodeFilterChanged)
    Q_PROPERTY(bool cwAutoTuneAvailable READ cwAutoTuneAvailable NOTIFY cwCapsChanged)

public:
    explicit CWSenderController(QObject* parent = nullptr);
    ~CWSenderController();
    
    // Property getters
    bool visible() const { return m_visible; }
    int wpm() const { return m_wpm; }
    double dashRatio() const { return m_dashRatio; }
    int pitch() const { return m_pitch; }
    int pitchMin() const { return m_pitchMin; }
    int pitchMax() const { return m_pitchMax; }
    int breakInMode() const { return m_breakInMode; }
    bool cutNumbers() const { return m_cutNumbers; }
    bool sendImmediate() const { return m_sendImmediate; }
    bool sidetoneEnable() const { return m_sidetoneEnable; }
    int sidetoneLevel() const { return m_sidetoneLevel; }
    bool sidetoneAvailable() const { return m_sidetoneAvailable; }
    int sequenceNumber() const { return m_sequenceNumber; }
    int maxChars() const { return m_maxChars; }
    QString statusMessage() const { return m_statusMessage; }
    bool macroEditMode() const { return m_macroEditMode; }
    bool receiveVisible() const { return m_receiveVisible; }
    bool canSendCW() const { return m_canSendCW; }
    bool cwDecodeAvailable() const { return m_cwDecodeAvailable; }
    bool cwDecodeActive() const { return m_cwDecodeActive; }
    bool cwDecodeBusy() const { return m_cwDecodeBusy; }
    bool cwDecodeThresholdAvailable() const { return m_cwDecodeThresholdAvailable; }
    int cwDecodeThresholdMin() const { return m_cwDecodeThresholdMin; }
    int cwDecodeThresholdMax() const { return m_cwDecodeThresholdMax; }
    int cwDecodeThreshold() const { return m_cwDecodeThreshold; }
    bool cwDecodeFilterAvailable() const { return m_cwDecodeFilterAvailable; }
    int cwDecodeFilter() const { return m_cwDecodeFilter; }
    bool cwAutoTuneAvailable() const { return m_cwAutoTuneAvailable; }
    
    // Property setters
    void setVisible(bool visible);
    void setWpm(int wpm);
    void setDashRatio(double ratio);
    void setPitch(int pitch);
    void setBreakInMode(int mode);
    void setCutNumbers(bool val);
    void setSendImmediate(bool val);
    void setSidetoneEnable(bool val);
    void setSidetoneLevel(int val);
    // The sidetone is mixed into RX audio, so it needs a local RX audio path.
    void setSidetoneAvailable(bool available);
    std::shared_ptr<CwSidetoneGenerator> sidetoneGenerator() const { return m_tone; }
    void setSequenceNumber(int val);
    void setMacroEditMode(bool val);
    void setReceiveVisible(bool val);
    void setCwDecodeThreshold(int val);
    void setCwDecodeFilter(int val);
    void loadSettings(bool cutNumbers, bool sendImmediate, bool sidetoneEnabled, int sidetoneLevel, const QStringList& macros);
    
    // Macro management
    QStringList getMacroText() const;
    void setMacroText(const QStringList& macros);
    Q_INVOKABLE QString getMacro(int index) const;
    Q_INVOKABLE QString getMacroButtonText(int index) const;
    
    // Invokable methods for QML
    Q_INVOKABLE void sendText(const QString& text);
    Q_INVOKABLE void stopSending();
    Q_INVOKABLE void runMacro(int macroNumber);
    Q_INVOKABLE void editMacro(int macroNumber, const QString& newText);
    Q_INVOKABLE void textChanged(const QString& text);
    Q_INVOKABLE void appendReceiveText(const QString& text);
    Q_INVOKABLE void appendTranscriptText(const QString& text);
    // Turns the radio's CW decoder on or off, stepping through any commands it depends on.
    // cwDecodeActive follows the radio's confirmed state, not this request.
    Q_INVOKABLE void setCwDecode(bool on);
    // Asks the radio to tune onto the CW signal. Momentary, so there is no state to follow.
    Q_INVOKABLE void cwAutoTune();
    
public slots:
    void handleKeySpeed(quint8 wpm);
    void handleDashRatio(quint8 ratio);
    void handlePitch(quint16 pitch);
    void handleBreakInMode(quint8 b);
    void handleCurrentModeUpdate(rigMode_t mode);
    void handleSidetoneText(QString text);
    void handleStopSidetone();
    void receive(QString text);
    void receiveEnabled(bool en);
    void receiveRigCaps(rigCapabilities* caps);
    void handleCwDecodeEnable(bool on);
    void handleCwDecodeDisplay(bool on);
    void handleCwDecodeThreshold(quint8 val);
    void handleCwDecodeFilter(quint8 val);
    
signals:
    // Window signals
    void visibleChanged();
    
    // Property change signals
    void wpmChanged();
    void dashRatioChanged();
    void pitchChanged();
    void pitchRangeChanged();
    void breakInModeChanged();
    void cutNumbersChanged();
    void sendImmediateChanged();
    void sidetoneEnableChanged();
    void sidetoneLevelChanged();
    void sidetoneAvailableChanged();
    void sequenceNumberChanged();
    void statusMessageChanged();
    void macroEditModeChanged();
    void receiveVisibleChanged();
    void maxCharsChanged();
    void canSendCWChanged();
    void keyerSettingsChanged();
    void cwCapsChanged();
    void cwDecodeActiveChanged();
    void cwDecodeBusyChanged();
    void cwDecodeThresholdChanged();
    void cwDecodeFilterChanged();
    
    // Text update signals for QML
    void receiveTextAppended(QString text);
    void transcriptTextAppended(QString text);
    void textColorChanged(QString color);
    void clearTextInput();
    void macroButtonTextChanged(int index, QString text);
    
    // Radio command signals
    void sendCW(QString cwMessage);
    void stopCW();
    void setDashRatioSignal(quint8 ratio);
    void setPitchSignal(quint16 pitch);
    void setBreakInModeSignal(quint8 b);
    void getCWSettings();
    
private:
    void setStatusMessage(const QString& msg);
    void updateTextColor(const QString& text);
    QString processMacroText(const QString& macroText);

    // CW decoder sequencing
    struct CwDecodeStep {
        funcs func;
        bool value;
    };
    void queryCwDecodeState();
    void runCwDecodeStep();
    void cwDecodeStepTimeout();
    void finishCwDecodeSequence();
    void setCwDecodeBusy(bool busy);
    void updateCwDecodeActive();
    
    // UI state
    bool m_visible;
    int m_wpm;
    double m_dashRatio;
    int m_pitch;
    int m_pitchMin;
    int m_pitchMax;
    int m_breakInMode;
    bool m_cutNumbers;
    bool m_sendImmediate;
    bool m_sidetoneEnable;
    int m_sidetoneLevel;
    bool m_sidetoneAvailable;
    int m_sequenceNumber;
    int m_maxChars;
    QString m_statusMessage;
    bool m_macroEditMode;
    bool m_receiveVisible;
    bool m_canSendCW;

    // CW decoder state
    bool m_cwDecodeAvailable = false;
    bool m_cwDecodeHasDisplay = false; // the rig needs CW Decode Display on before the decoder works
    bool m_cwDecodeActive = false;
    bool m_cwDecodeBusy = false;
    bool m_cwDecodeEnableOn = false;
    bool m_cwDecodeDisplayOn = false;
    bool m_cwDecodeThresholdAvailable = false;
    int m_cwDecodeThresholdMin = 1;
    int m_cwDecodeThresholdMax = 30;
    int m_cwDecodeThreshold = 10;
    bool m_cwDecodeFilterAvailable = false;
    int m_cwDecodeFilter = 0;
    bool m_cwAutoTuneAvailable = false;
    QList<CwDecodeStep> m_cwDecodeSteps;
    int m_cwDecodeStepIndex = 0;
    int m_cwDecodeRetries = 0;
    QTimer m_cwDecodeTimer;
    
    // Macro storage
    QString m_macroText[11]; // 1-10 indexed
    
    // Radio state
    rigMode_t m_currentMode;
    
    // Infrastructure
    cachingQueue* m_queue;
    rigCapabilities* m_rigCaps;
    rigCommander* m_rig;
    bool m_queueSignalsConnected;
    
    // Sidetone
    std::shared_ptr<CwSidetoneGenerator> m_tone;
};

#endif // CWSENDERCONTROLLER_H
