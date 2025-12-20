#ifndef RECEIVERWIDGET_H
#define RECEIVERWIDGET_H

#include <QWidget>
#include <QQuickWidget>
#include <QQuickStyle>
#include <QQmlContext>
#include <QMutex>
#include <QMutexLocker>
#include <QSplitter>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QSlider>
#include <QSpacerItem>
#include <QElapsedTimer>
#include <QTimer>
#include <QToolTip>
#include <QDateTime>
#include <QQuickWindow>
#include <QQmlEngine>
#include <qcustomplot.h>
#include "freqctrlquick.h"
#include "cluster.h"
#include "wfviewtypes.h"
#include "colorprefs.h"
#include "rigidentities.h"
#include "cachingqueue.h"
#include "paletteproxy.h"

#include "spectrumitem.h"
#include "waterfallitem.h"

enum scopeTypes {
    scopeSpectrum=0,
    scopeWaterfall,
    scopeNone
};

struct bandIndicator {
    QCPItemLine* line;
    QCPItemText* text;
};

class receiverWidget : public QGroupBox
{
    Q_OBJECT
    Q_PROPERTY(uchar scopeModeValue READ getScopeMode WRITE setScopeMode NOTIFY scopeModeValueChanged)
    Q_PROPERTY(centerSpanData scopeSpanValue READ getScopeSpan WRITE setScopeSpan NOTIFY scopeSpanValueChanged)
    Q_PROPERTY(uchar scopeEdgeValue READ getScopeEdge WRITE setScopeEdge NOTIFY scopeEdgeValueChanged)
    Q_PROPERTY(uchar toFixed WRITE toFixedEdge)
    Q_PROPERTY(modeInfo modeValue READ getMode WRITE setMode NOTIFY modeValueChanged)
    Q_PROPERTY(uchar dataModeValue READ getDataMode WRITE setDataMode NOTIFY dataModeValueChanged)
    Q_PROPERTY(uchar filterValue READ getFilter WRITE setFilter NOTIFY filterValueChanged)
    Q_PROPERTY(uchar filterShapeValue READ getFilterShape WRITE setFilterShape NOTIFY filterShapeValueChanged)
    Q_PROPERTY(uchar roofingValue READ getRoofing WRITE setRoofing NOTIFY roofingValueChanged)

    Q_PROPERTY(uchar speedValue READ getSpeed WRITE setSpeed NOTIFY speedValueChanged)
    Q_PROPERTY(WaterfallItem::Theme themeValue READ getTheme WRITE setTheme NOTIFY themeValueChanged)
    Q_PROPERTY(bool hold READ getHold WRITE setHold NOTIFY holdChanged)

    Q_PROPERTY(int refValue READ getRef WRITE setRef NOTIFY refValueChanged)
    // Length and ceiling/floor update the scope directly
    Q_PROPERTY(int pbtInnerValue READ getPbtInner WRITE setPbtInner NOTIFY pbtInnerValueChanged)
    Q_PROPERTY(int pbtOuterValue READ getPbtOuter WRITE setPbtOuter NOTIFY pbtOuterValueChanged)
    Q_PROPERTY(int ifShiftValue READ getIfShift WRITE setIfShift NOTIFY ifShiftValueChanged)
    Q_PROPERTY(int filterWidthValue READ getFilterWidth WRITE setFilterWidth NOTIFY filterWidthValueChanged)



public:
    explicit receiverWidget(bool scope, uchar receiver = 0, uchar vfo = 1, QWidget *parent = nullptr);
    ~receiverWidget();

    bool prepareWf(uint wfLength);
    void prepareScope(uint ampMap, uint spectWidth);
    void changeWfLength(uint wf);
    void setRange(int floor, int ceiling);
    void wfInterpolate(bool en) { if (waterfall) waterfall->setSmooth(en); } ;
    void wfAntiAliased(bool en) {} ;
    //void wfTheme(int num);
    //void setUnderlayMode(underlay_t un) { underlayMode = un; clearPeaks(); }
    void overflow(bool en) { if (spectrum) spectrum->setOverflow(en); }
    void resizePlasmaBuffer(int size );
    void colorPreset(colorPrefsType *p );

    void setCenterFreq (double hz) { passbandCenterFrequency = hz; }
    double getCenterFreq () { return passbandCenterFrequency; }

    void setPassbandWidth(double hz) { passbandWidth = hz;}
    //double getPassbandWidth() { configFilterWidth->setValue(passbandWidth*1E6); return passbandWidth; }

    void setIdentity(QString name) {this->setTitle(name); }
    uchar getReceiver() { return receiver; }

    void setTuningFloorZeros(bool tf) {this->tuningFloorZeros = tf; }
    void setClickDragTuning(bool cg) { this->clickDragTuning = cg; }

    //void setSatMode(bool sm) { this->satMode = sm; satelliteButton->blockSignals(true); satelliteButton->setChecked(sm); satelliteButton->blockSignals(false); }

    void setScrollSpeedXY(int clicksX, int clicksY) { this->scrollXperClick = clicksX; this->scrollYperClick = clicksY;}

    void displayScope(bool en);

    void receiveCwPitch(quint16 p);
    quint16 getCwPitch() { return cwPitch;}
    void receivePassband(quint16 pass);

    double getPBTInner () { return PBTInner;}
    void setPBTInner (uchar val);

    double getPBTOuter () { return PBTOuter;}
    void setPBTOuter (uchar val);

    void setIFShift (uchar val);

    quint16 getStepSize () { return stepSize;}
    void setStepSize (quint64 hz) { stepSize = hz;}

    freqt getFrequency () { return freq;}
    void setFrequencyLocally (freqt f,uchar vfo=0);
    void setFrequency (freqt f,uchar vfo=0);
    void updateInfo();
    uchar getNumVFO () { return numVFO;}

    void receiveMode (modeInfo m, uchar vfo=0);

    void setEdge(uchar index);

    //void setSplit(bool en) { splitButton->blockSignals(true);splitButton->setChecked(en); splitButton->blockSignals(false);}
    void setTracking(bool en) { tracking=en; }

    void setFreqLock( bool en) { freqLock = en; }
    //void setScopeEnabled(bool en) { this->configScopeEnabled->setEnabled(en);};

    void setBandIndicators(bool show, QString region, std::vector<bandType>* bands);
    void setUnit(FctlUnit unit);
    void setSeparators(QChar group, QChar decimal);

    void selected(bool);
    bool isSelected() {return isActive;}
    //void showScope(bool en) { this->splitter->setVisible(en); }
    //bool isScopeEnabled() { return this->configScopeEnabled->isEnabled();};

    void displaySettings(int NumDigits, qint64 Minf, qint64 Maxf, int MinStep,FctlUnit unit,std::vector<bandType>* bands = Q_NULLPTR);

    void updateBSR(std::vector<bandType>* bands);
    QImage getSpectrumImage();
    QImage getWaterfallImage();
    bandType getCurrentBand();

    // Functions to allow updates of QML items. u=true by default (settings from elsewhere must set u=false)
    void toFixedEdge(uchar val);

    void setScopeMode(uchar m, bool u=true);
    uchar getScopeMode() { return currentScopeMode;}
    void setScopeSpan(centerSpanData m, bool u=true);
    centerSpanData getScopeSpan() { return currentScopeSpan;}
    void setScopeEdge(uchar m, bool u=true);
    uchar getScopeEdge() { return currentScopeEdge;}
    void setMode(modeInfo m, bool u=true);
    modeInfo getMode() { return currentMode;}
    void setDataMode(uchar m, bool u=true);
    uchar getDataMode() { return currentMode.data;}
    void setFilter(uchar m, bool u=true);
    uchar getFilter() { return currentMode.filter;}
    void setFilterShape(uchar m, bool u=true);
    uchar getFilterShape() { return currentFilterShape;}
    void setRoofing(uchar m, bool u=true);
    uchar getRoofing() { return currentRoofing;}

    void setSpeed(uchar m, bool u=true);
    uchar getSpeed() { return currentSpeed;}
    void setTheme(WaterfallItem::Theme m, bool u=true);
    WaterfallItem::Theme getTheme() { return currentTheme;}

    void setHold(bool v, bool u=true);
    bool getHold() {return currentHold;}

    void setRef(int v, bool u=true);
    int getRef() { return currentRef;}
    void setPbtInner(int v, bool u=true);
    int getPbtInner() { return currentPbtInner;}
    void setPbtOuter(int v, bool u=true);
    int getPbtOuter() { return currentPbtOuter;}
    void setIfShift(int v, bool u=true);
    int getIfShift() { return currentIfShift;}
    void setFilterWidth(int v, bool u=true);
    int getFilterWidth() { return currentFilterWidth;}

    static void setSlider(QObject* root, const char* name, int mn, int mx, int ss, int v) {
        if (!root) { qWarning() << "setSlider: root is null"; return; }

        QObject* s = root->findChild<QObject*>(name, Qt::FindChildrenRecursively);
        if (!s) {
            qWarning() << "setSlider: not found:" << name << "(did you set objectName in QML?)";
            return;
        }

        // Slider props are real (double), not int
        const bool ok1 = s->setProperty("from",     double(mn));
        const bool ok2 = s->setProperty("to",       double(mx));
        const bool ok3 = s->setProperty("stepSize", double(ss));
        const bool ok4 = s->setProperty("value",    double(v));

        qInfo() << "setSlider:" << name << "writes:" << ok1 << ok2 << ok3 << ok4;
    }

    /* Don't set a valid default as we should receive the necessary value from rig */
    static void setCombo(QObject* root, const char* name, const QVariantList& model, int current = 0)
    {
        if (auto* c = root->findChild<QObject*>(name)) {
            c->setProperty("model", model);
            c->setProperty("currentIndex", current);
        }
    }

    static void setProperty(QObject* root, const char* name, const char* prop, bool value)
    {
        if (!root) { qWarning() << "setProperty: root is null"; return; }
        if (auto* c = root->findChild<QObject*>(name)) {
            if (!c) {
                qWarning() << "setProperty: not found:" << name << "(did you set objectName in QML?)";
                return;
            }
            c->setProperty(prop, value);
        }
    }


public slots: // Can be called directly or updated via signal/slot
    // public is required when being called from Qt Quick
    void receiveSpots(uchar receiver, QVector<spotData> spots);
    void memoryMode(bool en);
    void updateScope(const scopeData &data);
    void setFreqFromScope(double f);
    void detachScopeFromQml(bool state);
    QVariantMap customEdgeFreqDialogParams() const;
    void setCustomEdgeFreqs(quint64 startKHz, quint64 endKHz);

signals:    
    void frequencyRange(uchar receiver, double start, double end);
    void updateScopeMode(uchar index);
    void updateSpan(centerSpanData s);
    void showStatusBarText(QString text);
    void updateSettings(uchar receiver, int value, quint16 len, int floor, int ceiling);
    void elapsedTime(uchar receiver, qint64 ns);
    void dataChanged(modeInfo m);
    void bandChanged(uchar receiver, bandType b);
    void spectrumTime(double time);
    void waterfallTime(double time);
    void sendScopeImage(uchar receiver);
    void sendTrack(int f);



    // Comboboxes
    void scopeModeValueChanged();
    void scopeSpanValueChanged();
    void scopeEdgeValueChanged();
    void modeValueChanged();
    void dataModeValueChanged();
    void filterValueChanged();
    void filterShapeValueChanged();
    void roofingValueChanged();
    void speedValueChanged();
    void themeValueChanged();

    // Buttons (checkable)
    void holdChanged();

    // Sliders
    void refValueChanged();
    void pbtInnerValueChanged();
    void pbtOuterValueChanged();
    void ifShiftValueChanged();
    void filterWidthValueChanged();

private slots:
    void detachScope(bool state);
    void updatedMode(int index);
    void customSpanPressed();
    void configPressed();
    void freqNoteLockTimerSlot();


    // Mouse interaction with scope/waterfall
    void scopeClick(QMouseEvent *);
    void scopeMouseRelease(QMouseEvent *);
    void scopeMouseMove(QMouseEvent *);
    void doubleClick(QMouseEvent *); // used for both scope and wf
    void waterfallClick(QMouseEvent *);
    void scroll(QWheelEvent *);

    void clearPeaks();
    void newFrequency(qint64 freq,uchar i=0);
    void receiveTrack(int f);

    void onHoverSpotChanged(const spotData &spot, QPointF itemPos, bool active);
    void onWheelTuneRequested(int steps, Qt::KeyboardModifiers mods);

private:
    void clearPlasma();
    void computePlasma();
    void showHideControls(uchar mode);
    void showBandIndicators(bool en);
    void vfoSwap();

    quint64 roundFrequency(quint64 frequency, unsigned int tsHz);
    quint64 roundFrequency(quint64 frequency, int steps, unsigned int tsHz);

    QString defaultStyleSheet;

    QMutex mutex;
    QWidget* originalParent = Q_NULLPTR;
    QLabel* windowLabel = Q_NULLPTR;
    //QCustomPlot* spectrum = Q_NULLPTR;
    //QCustomPlot* waterfall = Q_NULLPTR;
    QHBoxLayout* mainLayout;
    QVBoxLayout* layout;
    QVBoxLayout* rhsLayout;
    QHBoxLayout* displayLayout;
    QHBoxLayout* controlLayout;
    /*
    QLinearGradient spectrumGradient;
    QLinearGradient underlayGradient;
    QSpacerItem* displayLSpacer;
    QPushButton* vfoSelectButton;
    QSpacerItem* displayCSpacer;
    QPushButton* vfoSwapButton;
    QPushButton* vfoEqualsButton;
    QPushButton* vfoMemoryButton;
    QPushButton* satelliteButton;
    QSpacerItem* displayMSpacer;
    QPushButton* splitButton;
    QSpacerItem* displayRSpacer;
    QGroupBox* group;
    QSplitter* splitter;
    QPushButton* detachButton;
    QLabel* scopeModeLabel;
    QComboBox* scopeModeCombo;
    QLabel* spanLabel;
    QComboBox* spanCombo;
    QLabel* edgeLabel;
    QComboBox* edgeCombo;
    QPushButton* edgeButton;
    QPushButton* toFixedButton;
    QPushButton* clearPeaksButton;
    QLabel* themeLabel;
    QComboBox* modeCombo;
    QComboBox* dataCombo;
    QComboBox* filterCombo;
    QComboBox* filterShapeCombo;
    QComboBox* roofingCombo;
    QComboBox* antennaCombo;
    QPushButton* holdButton;
    QSlider* dummySlider;

    // Config screen

    QSpacerItem* rhsTopSpacer;
    QSpacerItem* rhsBottomSpacer;
    QGroupBox* configGroup;
    QFormLayout* configLayout;
    QHBoxLayout* configIfLayout;
    QSlider* configRef;
    QSlider* configLength;
    QSlider* configBottom;
    QSlider* configTop;
    QComboBox* configSpeed;
    QComboBox* configTheme;
    QSlider* configPbtInner;
    QSlider* configPbtOuter;
    QSlider* configIfShift;
    QPushButton* configResetIf;
    QSlider* configFilterWidth;
    QCheckBox* configScopeEnabled;
    QCheckBox* rxCheckBox;
    QPushButton* confButton;
    QSpacerItem* controlSpacer;
    QSpacerItem* midSpacer;
    */

    // These parameters relate to scroll wheel response:
    int scrollYperClick = 24;
    int scrollXperClick = 24;
    qreal scrollWheelOffsetAccumulated=0;
    QTimer *frequencyNotificationLockoutTimer = NULL;
    bool allowAcceptFreqData = true;
    void tempLockAcceptFreqData();

    colorPrefsType colors;
    freqt freq;
    modeInfo mode;
    modeInfo unselectedMode;
    freqt unselectedFreq;
    bool scopePrepared=false;
    quint16 spectWidth=689; // Default was 689
    quint16 maxAmp=200;
    quint16 wfLength;
    quint16 wfLengthMax;

    double lowerFreq=0.0;
    double upperFreq=0.0;

    /*
    // Spectrum items;
    QCPItemLine * freqIndicatorLine;
    QCPItemRect* passbandIndicator;
    QCPItemRect* pbtIndicator;
    QCPItemText* oorIndicator;
    QCPItemText* ovfIndicator;
    QCPItemText* redrawSpeed;
    QByteArray spectrumPeaks;
    QVector <double> spectrumPlasmaLine;
    QVector <QByteArray> spectrumPlasma;
    QVector<bandIndicator> bandIndicators;
    unsigned int spectrumPlasmaSizeCurrent = 64;
    unsigned int spectrumPlasmaSizeMax = 128;
    unsigned int spectrumPlasmaPosition = 0;

    underlay_t underlayMode = underlayNone;
    QMutex plasmaMutex;
    */

    double plotFloor = 0;
    double plotCeiling = 160;
    double wfFloor = 0;
    double wfCeiling = 160;
    double oldPlotFloor = -1;
    double oldPlotCeiling = 999;
    double mousePressFreq = 0.0;
    double mouseReleaseFreq = 0.0;

    passbandActions passbandAction = passbandStatic;

    double PBTInner = 1.0; // Invalid value to start.
    double PBTOuter = 1.0; // Invalid value to start.
    double passbandWidth = 0.0;
    double passbandCenterFrequency = 0.0;
    double pbtDefault = 0.0;
    quint16 cwPitch = 600;
    quint64 stepSize = 100;
    uchar ifShift = 0;
    int refLower=0;
    int refUpper=0;

    // Waterfall items;
    /*
    QCPColorMap * colorMap = Q_NULLPTR;
    QCPColorMapData * colorMapData = Q_NULLPTR;
    QCPColorScale * colorScale = Q_NULLPTR;
    QVector <QByteArray> wfimage;
    */

    cachingQueue* queue;
    uchar receiver=0;
    uchar rxcmd=0;
    double startFrequency;
    QMap<QString, spotData*> clusterSpots;

    // Frequency display defaults
    int freqDigits=4;
    qint64 freqMin=0;
    qint64 freqMax=0;
    qint64 freqMinStep=0;
    FctlUnit freqUnit = FctlUnit::FCTL_UNIT_NONE;

    // Assorted settings
    bool tuningFloorZeros=false;
    bool clickDragTuning=false;
    bool isActive;
    uchar numVFO=1;
    uchar selectedVFO=0;
    bool hasScope=true;
    QString currentRegion="1";
    bool bandIndicatorsVisible=false;
    rigCapabilities* rigCaps=Q_NULLPTR;
    bandType currentBand;
    QElapsedTimer lastData;
    bool satMode = false;
    bool memMode = false;
    bool tracking = false;
    qint64 freqOffset = 0;
    double minFreqMhz = 0.0;
    double maxFreqMhz = 0.0;
    bool freqLock = false;
    bool scopeReceived = false;

    QQuickWidget *scopeQuick = nullptr;   // the QQuickWidget hosting scope.qml
    QWidget      *scopeOrigParent  = nullptr;  // original parent widget
    QLayout      *scopeOrigLayout  = nullptr;  // original layout
    int           scopeOrigIndex   = -1;       // index in that layout
    QWidget      *scopeFloatWindow = nullptr;  // floating window when detached

    SpectrumItem *spectrum = nullptr;
    WaterfallItem *waterfall = nullptr;
    FreqCtrlQuick *freqDisplayA = nullptr;
    FreqCtrlQuick *freqDisplayB = nullptr;

    qint64 lastSpectrumNs  = 0;
    qint64 lastWaterfallNs = 0;
    QVector<bandType> activeBands;

    // Current QML settings to allow us to detect any changes
    // Comboboxes
    uchar currentScopeMode=0xff;
    centerSpanData currentScopeSpan = centerSpanData();
    uchar currentScopeEdge=1;
    modeInfo currentMode = modeInfo();
    uchar currentFilterShape = 0xff;
    uchar currentRoofing = 0xff;

    uchar currentSpeed = 0;
    WaterfallItem::Theme currentTheme = WaterfallItem::Theme::Spectrum;

    // Buttons
    bool currentHold = false;

    // Sliders
    int currentRef = 0;
    int currentPbtInner = 0;
    int currentPbtOuter = 0;
    int currentIfShift = 0;
    int currentFilterWidth = 0;

};

#endif // RECEIVERWIDGET_H
