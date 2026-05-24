#ifndef RECEIVERCONTROLLER_H
#define RECEIVERCONTROLLER_H

#include <QObject>


#include "spectrumitem.h"
#include "waterfallitem.h"
#include "freqctrlquick.h"
#include "cachingqueue.h"

#include "wfviewtypes.h"

class ReceiverController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool active READ getActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString title READ getTitle WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(quint64 frequencyA READ getFrequencyA WRITE setFrequencyA NOTIFY frequencyAChanged)
    Q_PROPERTY(quint64 frequencyB READ getFrequencyB WRITE setFrequencyB NOTIFY frequencyBChanged)
    Q_PROPERTY(uchar scopeMode READ getScopeMode WRITE setScopeMode NOTIFY scopeModeChanged)
    Q_PROPERTY(uchar scopeSpan READ getScopeSpan WRITE setScopeSpan NOTIFY scopeSpanChanged)
    Q_PROPERTY(uchar scopeEdge READ getScopeEdge WRITE setScopeEdge NOTIFY scopeEdgeChanged)
    Q_PROPERTY(uchar toFixed READ getToFixedEdge WRITE toFixedEdge)
    Q_PROPERTY(rigMode_t mode READ getMode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(uchar dataMode READ getDataMode WRITE setDataMode NOTIFY dataModeChanged)
    Q_PROPERTY(uchar filter READ getFilter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(uchar filterShape READ getFilterShape WRITE setFilterShape NOTIFY filterShapeChanged)
    Q_PROPERTY(uchar roofing READ getRoofing WRITE setRoofing NOTIFY roofingChanged)

    Q_PROPERTY(uchar speed READ getSpeed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(WaterfallItem::Theme theme READ getTheme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool hold READ getHold WRITE setHold NOTIFY holdChanged)

    Q_PROPERTY(int ref READ getRef WRITE setRef NOTIFY refChanged)
    // Length and ceiling/floor update the scope directly

    Q_PROPERTY(ushort rfGain READ getRfGain WRITE setRfGain NOTIFY rfGainChanged)
    Q_PROPERTY(ushort afGain READ getAfGain WRITE setAfGain NOTIFY afGainChanged)
    Q_PROPERTY(ushort squelch READ getSquelch WRITE setSquelch NOTIFY squelchChanged)
    Q_PROPERTY(uchar attenuator READ getAttenuator WRITE setAttenuator NOTIFY attenuatorChanged)
    Q_PROPERTY(uchar nr READ getNr WRITE setNr NOTIFY nrChanged)
    Q_PROPERTY(uchar nb READ getNb WRITE setNb NOTIFY nbChanged)
    Q_PROPERTY(uchar ipPlus READ getIpPlus WRITE setIpPlus NOTIFY ipPlusChanged)
    Q_PROPERTY(uchar mn READ getMn WRITE setMn NOTIFY mnChanged)
    Q_PROPERTY(uchar ds READ getDs WRITE setDs NOTIFY dsChanged)
    Q_PROPERTY(ushort nrLevel READ getNrLevel WRITE setNrLevel NOTIFY nrLevelChanged)
    Q_PROPERTY(ushort nbLevel READ getNbLevel WRITE setNbLevel NOTIFY nbLevelChanged)
    Q_PROPERTY(uchar preamp READ getPreamp WRITE setPreamp NOTIFY preampChanged)
    Q_PROPERTY(uchar antenna READ getAntenna WRITE setAntenna NOTIFY antennaChanged)
    Q_PROPERTY(bool rxAntenna READ getRxAntenna WRITE setRxAntenna NOTIFY rxAntennaChanged)

    Q_PROPERTY(int pbtInner READ getPbtInner WRITE setPbtInner NOTIFY pbtInnerChanged)
    Q_PROPERTY(int pbtOuter READ getPbtOuter WRITE setPbtOuter NOTIFY pbtOuterChanged)
    Q_PROPERTY(int ifShift READ getIfShift WRITE setIfShift NOTIFY ifShiftChanged)
    Q_PROPERTY(int filterWidth READ getFilterWidth WRITE setFilterWidth NOTIFY filterWidthChanged)
    Q_PROPERTY(colorPrefsType colors READ getColors WRITE setColors NOTIFY colorsChanged)

    Q_PROPERTY(QVariantMap uiSpecs READ getUiSpecs NOTIFY uiSpecsChanged)

    // Spectrum items
    Q_PROPERTY(double passbandLow READ getPassbandLow NOTIFY passbandChanged)
    Q_PROPERTY(double passbandHigh READ getPassbandHigh NOTIFY passbandChanged)
    Q_PROPERTY(double pbtLow READ getPbtLow NOTIFY pbtChanged)
    Q_PROPERTY(double pbtHigh READ getPbtHigh NOTIFY pbtChanged)

    Q_PROPERTY(QString drawerTitle READ getDrawerTitle NOTIFY drawerTitleChanged)

    Q_PROPERTY(QVariantMap freqDisplay READ getFreqDisplay WRITE setFreqDisplay NOTIFY freqDisplayChanged)
    Q_PROPERTY(QVector<bandType> activeBands READ getActiveBands NOTIFY activeBandsChanged)

    Q_PROPERTY(meter_t meterType READ meterType WRITE setMeterType NOTIFY meterTypeChanged)
    Q_PROPERTY(double meter READ meter WRITE setMeter NOTIFY meterChanged)

    Q_PROPERTY(availableBands band READ getBand WRITE setBand NOTIFY bandChanged)
    Q_PROPERTY(uchar bsrReg READ getBsrReg WRITE setBsrReg NOTIFY bsrRegChanged)
    Q_PROPERTY(bool vfoBSelected READ vfoBSelected NOTIFY vfoBSelectedChanged)
    Q_PROPERTY(bool memoryMode READ memoryMode NOTIFY memoryModeChanged)
    Q_PROPERTY(bool satelliteMode READ satelliteMode NOTIFY satelliteModeChanged)
    Q_PROPERTY(bool splitEnabled READ splitEnabled NOTIFY splitEnabledChanged)

public:
    explicit ReceiverController(int rxIndex = 0, QString region="", QObject *parent = nullptr);
    ~ReceiverController() override = default;

    #define UIFLAG(n) (quint64(1) << (n))

    enum UiFlag {
        ShowNothing        = 0,
        ShowVFOA           = UIFLAG(0),
        ShowVFOB           = UIFLAG(1),
        ShowIfShift        = UIFLAG(2),
        ShowPbt            = UIFLAG(3),
        ShowFilter         = UIFLAG(4),
        ShowTxPanel        = UIFLAG(5),
        ShowVFOButton      = UIFLAG(6),
        ShowSwapABButton   = UIFLAG(7),
        ShowEqualsABButton = UIFLAG(8),
        ShowVMButton       = UIFLAG(9),
        ShowSatButton      = UIFLAG(10),
        ShowSplitButton    = UIFLAG(11)
    };

    Q_DECLARE_FLAGS(UiFlags, UiFlag)
    Q_FLAG(UiFlags)
    Q_PROPERTY(UiFlags uiFlags READ uiFlags NOTIFY uiFlagsChanged)
    UiFlags uiFlags() const { return flags; }

    // getters
    QVariantMap getUiSpecs() const { return uiSpecs; }
    QVariantMap getFreqDisplay() const { return freqDisplay; }
    int getReceiver() const { return receiver; }
    bool getActive() const { return active; }
    QString getTitle() const { return title; }
    quint64 getFrequencyA() const { return frequencyA;}
    quint64 getFrequencyB() const { return frequencyB;}
    uchar getScopeMode() { return scopeMode;}
    uchar getScopeSpan() { return scopeSpan.reg;}
    uchar getScopeEdge() { return scopeEdge;}
    uchar getToFixedEdge() const { return scopeEdge; }
    rigMode_t getMode() { return mode.mk;}
    uchar getDataMode() { return mode.data;}
    uchar getFilter() { return mode.filter;}
    uchar getFilterShape() { return filterShape;}
    uchar getRoofing() { return roofing;}
    void toFixedEdge(uchar val);
    availableBands getBand() {return band.band;}
    uchar getBsrReg() {return bsr.reg;}
    void receiveBSR(bandStackType& bsr);
    bool vfoBSelected() const { return m_vfoBSelected; }
    bool memoryMode() const { return m_memoryMode; }
    bool satelliteMode() const { return m_satelliteMode; }
    bool splitEnabled() const { return m_splitEnabled; }

    uchar getSpeed() { return speed;}
    WaterfallItem::Theme getTheme() { return theme;}


    bool getHold() {return hold;}

    int getRef() { return ref;}
    ushort getRfGain() { return rfGain;}
    ushort getAfGain() { return afGain;}
    ushort getSquelch() { return squelch;}
    uchar getAttenuator() { return attenuator;}
    uchar getPreamp() { return preamp;}
    uchar getNr() { return nr;}
    uchar getNb() { return nb;}
    ushort getNrLevel() { return nrLevel;}
    ushort getNbLevel() { return nbLevel;}
    bool getIpPlus() { return ipPlus;}
    bool getMn() { return mn;}
    bool getDs() { return ds;}
    uchar getAntenna() { return antenna.antenna;}
    bool getRxAntenna() { return antenna.rx;}
    int getPbtInner() { return pbtInner;}
    int getPbtOuter() { return pbtOuter;}
    int getIfShift() { return ifShift;}
    int getFilterWidth() { return filterWidth;}
    QString getDrawerTitle() { return drawerTitle;}
    colorPrefsType getColors()     const { return colors; }

    double getPassbandLow() const { return passbandLow; }
    double getPassbandHigh() const { return passbandHigh; }
    double getPbtLow() const { return pbtLow; }
    double getPbtHigh() const { return pbtHigh; }

    void setPassbandWidth(int width) {
        double pb = double(width / 1000000.0);

        if (passbandWidth != pb) {
            passbandWidth = pb;
            updatePassband();
        }
    }

    void setFreqDisplay(QVariantMap m) {
        if (freqDisplay == m) return;
        freqDisplay = std::move(m);
        emit freqDisplayChanged();
    }

    void setBandIndicators(bool show, QString region, std::vector<bandType>* bands);

    QVector<bandType> getActiveBands() {return activeBands;}

    meter_t meterType() const { return m_meterType; }
    void setMeterType(meter_t t);

    double meter() const { return m_meter; }
    void setMeter(double v);

    Q_INVOKABLE void onWheelTune(int angleDeltaY, int modifiers);
    Q_INVOKABLE void tuneSteps(int steps, int modifiers = 0, bool uniqueQueue = false);
    Q_INVOKABLE void resizePassband(double lowFreqMHz, double highFreqMHz);
    Q_INVOKABLE void dragPbt(int action, double deltaMHz);
    Q_INVOKABLE void resetPbt();
    Q_INVOKABLE void storeBsr();
    Q_INVOKABLE void selectVfoB(bool enabled);
    Q_INVOKABLE void swapVfoAB();
    Q_INVOKABLE void equalizeVfoAB();

public slots:

    // Receive mode from Radio.
    void receiveMode(modeInfo m, uchar vfo);

    // setters
    void setTitle(QString t);
    void setActive(bool a);
    void setScopeEdge(uchar m, bool u=true);
    void setScopeMode(uchar m, bool u=true);
    void setScopeSpan(uchar m, bool u=true);
    void setMode(uchar m, bool u=true);
    void setDataMode(uchar m, bool u=true);
    void setFilter(uchar m, bool u=true);
    void setFilterShape(uchar m, bool u=true);
    void setRoofing(uchar m, bool u=true);

    void setSpeed(uchar m, bool u=true);
    void setTheme(WaterfallItem::Theme m, bool u=true);
    void setHold(bool v, bool u=true);
    void setRef(int v, bool u=true);

    // Although these are 0-255 at most, we use uchar for 2 digit conversion (0-99) so use ushort instead.
    // We could use char for 2 digit and uchar for 0-255 but that would require singificant rewriting.
    void setRfGain(ushort v, bool u=true);
    void setAfGain(ushort v, bool u=true);
    void setSquelch(ushort v, bool u=true);
    void setAttenuator(uchar v, bool u=true);
    void setPreamp(uchar v, bool u=true);
    void setNb(uchar v, bool u=true);
    void setNr(uchar v, bool u=true);
    void setIpPlus(bool v, bool u=true);
    void setMn(bool v, bool u=true);
    void setDs(bool v, bool u=true);
    void setNbLevel(ushort v, bool u=true);
    void setNrLevel(ushort v, bool u=true);
    void setAntenna(uchar v, bool u=true);
    void setRxAntenna(bool v, bool u=true);

    void setPbtInner(int v, bool u=true);
    void setPbtOuter(int v, bool u=true);
    void setIfShift(int v, bool u=true);
    void setFilterWidth(int v, bool u=true);

    void setScopeData(const scopeData &d);
    void setClusterSpots(const QList<spotData> &spots);
    void clearClusterSpots();

    void setFrequencyA(quint64 f, bool u=true);
    void setFrequencyB(quint64 f, bool u=true);

    void setBand(availableBands b, bool u=true);
    void setMemoryMode(bool enabled, bool u=true);
    void setSatelliteMode(bool enabled, bool u=true);
    void setSplitEnabled(bool enabled, bool u=true);

    void setColors (colorPrefsType c)
    {
        colors = c;
        emit colorsChanged();
    }

    void setFreqLock(bool lock) { m_freqLock = lock;}
    bool freqLock() { return m_freqLock;}

    void receiveMeter(meter_t meter, double level);

    void receiveStepSize(quint64 s) { stepSize = s;}

    void setBsrReg(uchar r) {
        if (bsr.reg != r)
        {
            bsr.reg = r;
            emit bsrRegChanged();
        }
    }
private slots:
    void receiveRigCaps(rigCapabilities* caps);

signals:
    //void requestDetach();
    void freqDisplayChanged();
    void uiFlagsChanged();
    void scopeUpdated(const scopeData &d);
    void clusterSpotsUpdated(const QVector<spotData> &spots);
    void uiSpecsChanged();

    void activeChanged();
    void titleChanged();
    void colorsChanged();
    void scopeSpanChanged();
    void scopeEdgeChanged();
    void scopeModeChanged();
    void holdChanged();

    void modeChanged();
    void dataModeChanged();

    void filterChanged();
    void filterShapeChanged();
    void roofingChanged();
    void filterWidthChanged();

    void refChanged();
    void rfGainChanged();
    void afGainChanged();
    void squelchChanged();
    void attenuatorChanged();
    void preampChanged();
    void nrChanged();
    void nbChanged();
    void nrLevelChanged();
    void nbLevelChanged();
    void ipPlusChanged();
    void mnChanged();
    void dsChanged();
    void antennaChanged();
    void rxAntennaChanged();


    void speedChanged();
    void themeChanged();

    void pbtInnerChanged();
    void pbtOuterChanged();
    void pbtChanged();
    void ifShiftChanged();

    void passbandChanged();
    void frequencyAChanged();
    void frequencyBChanged();
    void drawerTitleChanged();

    void activeBandsChanged();

    void meterTypeChanged();
    void meterChanged();

    void bandChanged();
    void bsrRegChanged();
    void vfoBSelectedChanged();
    void memoryModeChanged();
    void satelliteModeChanged();
    void splitEnabledChanged();

private:
    void buildUiSpecs();
    void updatePassband();
    void updatePbt();
    void updatePassbandModeParameters(const modeInfo &m);
    int pbtDefaultValue(const funcType &func) const;
    double pbtOffsetMHz(int value, int defaultValue) const;
    QVariantMap uiSpecs;
    quint64 roundFrequency(quint64 frequency, unsigned int tsHz);
    quint64 roundFrequency(quint64 frequency, int steps, unsigned int tsHz);

    uchar receiver = 0;
    bool scopeReceived = false;
    bool m_vfoBSelected = false;
    bool m_memoryMode = false;
    bool m_satelliteMode = false;
    bool m_splitEnabled = false;

    qreal scrollWheelOffsetAccumulated = 0.0;


    quint64 frequencyA;
    quint64 frequencyB;
    bool active=false;
    QString title = "Main";
    uchar scopeMode=0xff;
    centerSpanData scopeSpan = centerSpanData();
    uchar scopeEdge=1;
    modeInfo mode = modeInfo();
    modeInfo unselectedMode = modeInfo();
    uchar filterShape = 0xff;
    uchar roofing = 0xff;

    uchar speed = 0;
    WaterfallItem::Theme theme = WaterfallItem::Theme::Spectrum;

    // Buttons
    bool hold = false;

    // Sliders
    int ref = 0;
    int pbtInner = 0;
    int pbtOuter = 0;
    int ifShift = 0;
    int filterWidth = 0;

    // Spectrum
    double passbandLow = 0;
    double passbandHigh = 0;
    double passbandWidth = 0;
    double passbandCenterFrequency = 0.0;
    double pbtLow = 0;
    double pbtHigh = 0;

    UiFlags flags {}; // Everything off until I tell you otherwise!
    scopeData lastScope; // Contains the previous scope data.
    colorPrefsType colors;
    cachingQueue* queue=nullptr;
    rigCapabilities* rigCaps=nullptr;

    QString drawerTitle = "Receiver settings";

    QVariantMap freqDisplay;

    bool m_freqLock=false;
    QString region = "";
    QVector<bandType> activeBands;
    // These need to be settable
    bool tuningFloorZeros=false;
    quint64 stepSize = 100;

    meter_t m_meterType = meterS;
    double m_meter = 0.0;

    ushort rfGain = 0;
    ushort afGain = 0;
    ushort squelch = 0;
    uchar attenuator = 0;
    uchar preamp = 0;
    uchar nr = 0;
    uchar nb = 0;
    ushort nrLevel = 0;
    ushort nbLevel = 0;
    bool ipPlus = false;
    bool mn = false;
    bool ds = false;
    antennaInfo antenna;
    bandType band;
    bandStackType bsr;
    bool awaitingBSR = false;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ReceiverController::UiFlags)

#endif // RECEIVERCONTROLLER_H
