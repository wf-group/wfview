#ifndef CWSENDERCONTROLLER_H
#define CWSENDERCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include "wfviewtypes.h"
#include "cachingqueue.h"
#include "rigcommander.h"
#include "cwsidetone.h"

class CWSenderController : public QObject
{
    Q_OBJECT
    
    // Properties for QML binding
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(int wpm READ wpm WRITE setWpm NOTIFY wpmChanged)
    Q_PROPERTY(double dashRatio READ dashRatio WRITE setDashRatio NOTIFY dashRatioChanged)
    Q_PROPERTY(int pitch READ pitch WRITE setPitch NOTIFY pitchChanged)
    Q_PROPERTY(int breakInMode READ breakInMode WRITE setBreakInMode NOTIFY breakInModeChanged)
    Q_PROPERTY(bool cutNumbers READ cutNumbers WRITE setCutNumbers NOTIFY cutNumbersChanged)
    Q_PROPERTY(bool sendImmediate READ sendImmediate WRITE setSendImmediate NOTIFY sendImmediateChanged)
    Q_PROPERTY(bool sidetoneEnable READ sidetoneEnable WRITE setSidetoneEnable NOTIFY sidetoneEnableChanged)
    Q_PROPERTY(int sidetoneLevel READ sidetoneLevel WRITE setSidetoneLevel NOTIFY sidetoneLevelChanged)
    Q_PROPERTY(int sequenceNumber READ sequenceNumber WRITE setSequenceNumber NOTIFY sequenceNumberChanged)
    Q_PROPERTY(int maxChars READ maxChars NOTIFY maxCharsChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool macroEditMode READ macroEditMode WRITE setMacroEditMode NOTIFY macroEditModeChanged)
    Q_PROPERTY(bool receiveVisible READ receiveVisible WRITE setReceiveVisible NOTIFY receiveVisibleChanged)
    Q_PROPERTY(bool canSendCW READ canSendCW NOTIFY canSendCWChanged)

public:
    explicit CWSenderController(QObject* parent = nullptr);
    ~CWSenderController();
    
    // Property getters
    bool visible() const { return m_visible; }
    int wpm() const { return m_wpm; }
    double dashRatio() const { return m_dashRatio; }
    int pitch() const { return m_pitch; }
    int breakInMode() const { return m_breakInMode; }
    bool cutNumbers() const { return m_cutNumbers; }
    bool sendImmediate() const { return m_sendImmediate; }
    bool sidetoneEnable() const { return m_sidetoneEnable; }
    int sidetoneLevel() const { return m_sidetoneLevel; }
    int sequenceNumber() const { return m_sequenceNumber; }
    int maxChars() const { return m_maxChars; }
    QString statusMessage() const { return m_statusMessage; }
    bool macroEditMode() const { return m_macroEditMode; }
    bool receiveVisible() const { return m_receiveVisible; }
    bool canSendCW() const { return m_canSendCW; }
    
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
    void setSequenceNumber(int val);
    void setMacroEditMode(bool val);
    void setReceiveVisible(bool val);
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
    
signals:
    // Window signals
    void visibleChanged();
    
    // Property change signals
    void wpmChanged();
    void dashRatioChanged();
    void pitchChanged();
    void breakInModeChanged();
    void cutNumbersChanged();
    void sendImmediateChanged();
    void sidetoneEnableChanged();
    void sidetoneLevelChanged();
    void sequenceNumberChanged();
    void statusMessageChanged();
    void macroEditModeChanged();
    void receiveVisibleChanged();
    void maxCharsChanged();
    void canSendCWChanged();
    void keyerSettingsChanged();
    
    // Text update signals for QML
    void receiveTextAppended(QString text);
    void transcriptTextAppended(QString text);
    void textColorChanged(QString color);
    void clearTextInput();
    void macroButtonTextChanged(int index, QString text);
    
    // Radio command signals
    void sendCW(QString cwMessage);
    void stopCW();
    void setKeySpeed(quint8 wpm);
    void setDashRatioSignal(quint8 ratio);
    void setPitchSignal(quint16 pitch);
    void setBreakInModeSignal(quint8 b);
    void setLevel(int level);
    void getCWSettings();
    void initTone();
    
private:
    void setStatusMessage(const QString& msg);
    void updateTextColor(const QString& text);
    void setupTone();
    void teardownTone();
    QString processMacroText(const QString& macroText);
    
    // UI state
    bool m_visible;
    int m_wpm;
    double m_dashRatio;
    int m_pitch;
    int m_breakInMode;
    bool m_cutNumbers;
    bool m_sendImmediate;
    bool m_sidetoneEnable;
    int m_sidetoneLevel;
    int m_sequenceNumber;
    int m_maxChars;
    QString m_statusMessage;
    bool m_macroEditMode;
    bool m_receiveVisible;
    bool m_canSendCW;
    
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
    cwSidetone* m_tone;
    QThread* m_toneThread;
};

#endif // CWSENDERCONTROLLER_H
