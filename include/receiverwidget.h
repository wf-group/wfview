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
    Q_PROPERTY(uchar scopeModeValue READ getScopeMode WRITE setScopeModeUser NOTIFY scopeModeValueChanged)
    Q_PROPERTY(centerSpanData scopeSpanValue READ getScopeSpan WRITE setScopeSpanUser NOTIFY scopeSpanValueChanged)
    Q_PROPERTY(uchar scopeEdgeValue READ getScopeEdge WRITE setScopeEdgeUser NOTIFY scopeEdgeValueChanged)
    Q_PROPERTY(modeInfo modeValue READ getMode WRITE setModeUser NOTIFY modeValueChanged)
    Q_PROPERTY(uchar dataModeValue READ getDataMode WRITE setDataModeUser NOTIFY dataModeValueChanged)
    Q_PROPERTY(uchar filterValue READ getFilter WRITE setFilterUser NOTIFY filterValueChanged)
    Q_PROPERTY(uchar filterShapeValue READ getFilterShape WRITE setFilterShapeUser NOTIFY filterShapeValueChanged)
    Q_PROPERTY(uchar roofingValue READ getRoofing WRITE setRoofingUser NOTIFY roofingValueChanged)

    Q_PROPERTY(uchar speedValue READ getSpeed WRITE setSpeedUser NOTIFY speedValueChanged)
    Q_PROPERTY(WaterfallItem::Theme themeValue READ getTheme WRITE setThemeUser NOTIFY themeValueChanged)

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
    void setHold(bool h);

    //void setSplit(bool en) { splitButton->blockSignals(true);splitButton->setChecked(en); splitButton->blockSignals(false);}
    void setTracking(bool en) { tracking=en; }
    void setRef(int ref);
    void setRefLimits(int lower, int upper);
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

    // Each combobox needs a setXxx function and a setXxxUser. setXxxUser is used for settings FROM the comboxbox.
    void setScopeMode(uchar m);
    void setScopeModeUser(uchar m);
    void setScopeSpan(centerSpanData m);
    void setScopeSpanUser(centerSpanData m);
    void setScopeEdge(uchar m);
    void setScopeEdgeUser(uchar m);
    void setMode(modeInfo m);
    void setModeUser(modeInfo m);
    void setDataMode(uchar m);
    void setDataModeUser(uchar m);
    void setFilter(uchar m);
    void setFilterUser(uchar m);
    void setFilterShape(uchar m);
    void setFilterShapeUser(uchar m);
    void setRoofing(uchar m);
    void setRoofingUser(uchar m);

    void setSpeed(uchar m);
    void setSpeedUser(uchar m);
    void setTheme(WaterfallItem::Theme m);
    void setThemeUser(WaterfallItem::Theme m);

    uchar getScopeMode() { return currentScopeMode;}
    centerSpanData getScopeSpan() { return currentScopeSpan;}
    uchar getScopeEdge() { return currentScopeEdge;}
    modeInfo getMode() { return currentMode;}
    uchar getDataMode() { return currentMode.data;}
    uchar getFilter() { return currentMode.filter;}
    uchar getFilterShape() { return currentFilterShape;}
    uchar getRoofing() { return currentRoofing;}

    uchar getSpeed() { return currentSpeed;}
    WaterfallItem::Theme getTheme() { return currentTheme;}

    static void setSlider(QObject* root, const char* name, double mn, double mx, double v) {
        if (auto* s = root->findChild<QObject*>(name)) {
            s->setProperty("from", mn);
            s->setProperty("to", mx);
            s->setProperty("value", v);
        }
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
        if (auto* c = root->findChild<QObject*>(name)) {
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


private slots:
    void detachScope(bool state);
    void updatedMode(int index);
    void toFixedPressed();
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

    int currentRef = 0;
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

    // Current combobox contents to detect changes.
    uchar currentScopeMode=0xff;
    centerSpanData currentScopeSpan = centerSpanData();
    uchar currentScopeEdge=1;
    modeInfo currentMode = modeInfo();
    uchar currentFilterShape = 0xff;
    uchar currentRoofing = 0xff;

    uchar currentSpeed = 0;
    WaterfallItem::Theme currentTheme = WaterfallItem::Theme::Spectrum;

};

#endif // RECEIVERWIDGET_H
