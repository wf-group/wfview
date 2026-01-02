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
    Q_PROPERTY(colorPrefsType colors READ getColors WRITE setColors NOTIFY colorsChanged)

    Q_PROPERTY(QVariantMap uiSpecs READ getUiSpecs NOTIFY uiSpecsChanged)

    // Spectrum items
    Q_PROPERTY(double passbandLow READ getPassbandLow NOTIFY passbandChanged)
    Q_PROPERTY(double passbandHigh READ getPassbandHigh NOTIFY passbandChanged)

    Q_PROPERTY(QString drawerTitle READ getDrawerTitle NOTIFY drawerTitleChanged)

    Q_PROPERTY(QVariantMap freqDisplay READ getFreqDisplay WRITE setFreqDisplay NOTIFY freqDisplayChanged)

    Q_PROPERTY(QVector<bandType> activeBands READ getActiveBands NOTIFY activeBandsChanged)

public:
    explicit ReceiverController(int rxIndex = 0, QObject *parent = nullptr);
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
    centerSpanData getScopeSpan() { return scopeSpan;}
    uchar getScopeEdge() { return scopeEdge;}
    modeInfo getMode() { return mode;}
    uchar getDataMode() { return mode.data;}
    uchar getFilter() { return mode.filter;}
    uchar getFilterShape() { return filterShape;}
    uchar getRoofing() { return roofing;}
    void toFixedEdge(uchar val);

    uchar getSpeed() { return speed;}
    WaterfallItem::Theme getTheme() { return theme;}


    bool getHold() {return hold;}

    int getRef() { return ref;}
    int getPbtInner() { return pbtInner;}
    int getPbtOuter() { return pbtOuter;}
    int getIfShift() { return ifShift;}
    int getFilterWidth() { return filterWidth;}
    QString getDrawerTitle() { return drawerTitle;}
    colorPrefsType getColors()     const { return colors; }

    double getPassbandLow() const { return passbandLow; }
    double getPassbandHigh() const { return passbandHigh; }

    void setPassbandWidth(int width) {
        double pb = double(width / 1000000.0);

        if (passbandWidth != pb) {
            passbandWidth = pb;
        }
        // Don't emit as it will be handled later.
    }

    void setFreqDisplay(QVariantMap m) {
        if (freqDisplay == m) return;
        freqDisplay = std::move(m);
        emit freqDisplayChanged();
    }

    void setBandIndicators(bool show, QString region, std::vector<bandType>* bands);

    QVector<bandType> getActiveBands() {return activeBands;}

public slots:

    // Receive mode from Radio.
    void receiveMode(modeInfo m, uchar vfo);

    // setters
    void setTitle(QString t);
    void setActive(bool a);
    void setScopeEdge(uchar m, bool u=true);
    void setScopeMode(uchar m, bool u=true);
    void setScopeSpan(centerSpanData m, bool u=true);
    void setMode(modeInfo m, bool u=true);
    void setDataMode(uchar m, bool u=true);
    void setFilter(uchar m, bool u=true);
    void setFilterShape(uchar m, bool u=true);
    void setRoofing(uchar m, bool u=true);

    void setSpeed(uchar m, bool u=true);
    void setTheme(WaterfallItem::Theme m, bool u=true);
    void setHold(bool v, bool u=true);
    void setRef(int v, bool u=true);
    void setPbtInner(int v, bool u=true);
    void setPbtOuter(int v, bool u=true);
    void setIfShift(int v, bool u=true);
    void setFilterWidth(int v, bool u=true);

    void setScopeData(const scopeData &d);

    void setFrequencyA(quint64 f, bool u=true);
    void setFrequencyB(quint64 f, bool u=true);

    void setColors (colorPrefsType c)
    {
        colors = c;
        emit colorsChanged();
    }

    void setFreqLock(bool lock) { m_freqLock = lock;}
    bool freqLock() { return m_freqLock;}

private slots:
    void receiveRigCaps(rigCapabilities* caps);

signals:
    //void requestDetach();
    void freqDisplayChanged();
    void uiFlagsChanged();
    void scopeUpdated(const scopeData &d);
    void uiSpecsChanged();

    void activeChanged();
    void titleChanged();
    void colorsChanged();
    void scopeSpanValueChanged();
    void scopeEdgeValueChanged();
    void scopeModeValueChanged();
    void holdChanged();

    void modeValueChanged();
    void dataModeValueChanged();

    void filterValueChanged();
    void filterShapeValueChanged();
    void roofingValueChanged();
    void filterWidthValueChanged();

    void refValueChanged();
    void speedValueChanged();
    void themeValueChanged();

    void pbtInnerValueChanged();
    void pbtOuterValueChanged();
    void ifShiftValueChanged();

    void passbandChanged();
    void frequencyAChanged();
    void frequencyBChanged();
    void drawerTitleChanged();

    void activeBandsChanged();
private:
    void buildUiSpecs();
    quint64 roundFrequency(quint64 frequency, unsigned int tsHz);
    quint64 roundFrequency(quint64 frequency, int steps, unsigned int tsHz);


    uchar receiver = 0;

    QVariantMap uiSpecs;
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

    UiFlags flags {}; // Everything off until I tell you otherwise!
    scopeData lastScope; // Contains the previous scope data.
    colorPrefsType colors;
    cachingQueue* queue=nullptr;
    rigCapabilities* rigCaps=nullptr;

    QString drawerTitle = "Receiver settings";

    QVariantMap freqDisplay;

    bool m_freqLock=false;
    QString currentRegion = "";
    QVector<bandType> activeBands;
    // These need to be settable
    bool tuningFloorZeros=false;
    quint64 stepSize = 100;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ReceiverController::UiFlags)

#endif // RECEIVERCONTROLLER_H
