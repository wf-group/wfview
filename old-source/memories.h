#ifndef MEMORIES_H
#define MEMORIES_H

#include <QWidget>
#include <QItemDelegate>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QPushButton>
#include <QFileDialog>
#include <QTimer>
#include <QMessageBox>
#include <QStatusBar>
#include <QProgressBar>

#include "tablewidget.h"
#include "wfviewtypes.h"
#include "rigidentities.h"
#include "cachingqueue.h"

#define MEMORY_TIMEOUT 1000
#define MEMORY_SLOWLOAD 500
#define MEMORY_SATGROUP 200
#define MEMORY_SHORTGROUP 100

namespace Ui {
class memories;
}

class memories : public QWidget
{
    Q_OBJECT

public:
    explicit memories(bool isAdmin, bool slowLoad=false,QWidget *parent = nullptr);
    ~memories();
    void populate();
    void setRegion(QString reg) { region=reg; }
signals:
    void memoryMode(bool en);

private slots:
    void on_table_cellChanged(int row, int col);
    void on_group_currentIndexChanged(int index);
    void on_modeButton_clicked(bool on);
    void on_csvImport_clicked();
    void on_csvExport_clicked();
    void on_scanButton_toggled(bool scan);
    void on_disableEditing_toggled(bool dis);
    bool readCSVRow (QTextStream &in, QStringList *row);


    void receiveMemory(memoryType mem);
    void receiveMemoryName(memoryTagType tag);
    void receiveMemorySplit(memorySplitType s);
    void rowAdded(int row, memoryType mem=memoryType());
    void rowDeleted(quint32 mem);
    void menuAction(QAction* action, quint32 mem);
    void timeout();

private:
    bool startup=true;
    cachingQueue* queue;
    quint32 groupMemories=0;
    quint32 lastMemoryRequested=0;
    QTimer timeoutTimer;
    int timeoutCount=0;
    int retries=0;
    int visibleColumns=1;
    bool slowLoad=false;
    bool extendedView = false;
    funcs writeCommand = funcMemoryContents;
    funcs selectCommand = funcMemoryMode;

    QString region;

    bool checkASCII(QString str);

    QStandardItemModel* createModel(QStandardItemModel* model, QStringList strings);

    QStringList clar;
    QStringList split;
    QStringList scan;
    QStringList skip;
    QStringList vfos;
    QStringList duplexModes;
    QStringList modes;
    QStringList dataModes;
    QStringList filters;
    QStringList tones;
    QStringList toneModes;
    QStringList dtcs;
    QStringList dtcsp;
    QStringList dsql;
    QStringList dvsql;
    QStringList tuningSteps;
    QStringList preamps;
    QStringList attenuators;
    QStringList antennas;
    QStringList ipplus;
    QStringList p25Sql;
    QStringList dPmrSql;
    QStringList dPmrSCRM;
    QStringList nxdnSql;
    QStringList nxdnEnc;
    QStringList dcrSql;
    QStringList dcrEnc;


    /*
        columnFrequencyB,
        columnModeB,
        columnFilterB,
        columnDataB,
        columnToneModeB,
        columnDSQLB,
        columnToneB,
        columnTSQLB,
        columnDTCSPolarityB,
        columnDTCSB,
        columnDVSquelchB,
        columnURB,
        columnR1B,
        columnR2B,
    */
    QStandardItemModel* clarRXModel = nullptr;
    QStandardItemModel* clarTXModel = nullptr;
    QStandardItemModel* splitModel = nullptr;
    QStandardItemModel* skipModel = nullptr;
    QStandardItemModel* scanModel = nullptr;
    QStandardItemModel* filterModel = nullptr;
    QStandardItemModel* vfoModel = nullptr;
    QStandardItemModel* dataModel = nullptr;
    QStandardItemModel* modesModel = nullptr;
    QStandardItemModel* duplexModel = nullptr;
    QStandardItemModel* toneModesModel = nullptr;
    QStandardItemModel* dsqlModel = nullptr;
    QStandardItemModel* tonesModel = nullptr;
    QStandardItemModel* tsqlModel = nullptr;
    QStandardItemModel* dtcspModel = nullptr;
    QStandardItemModel* dtcsModel = nullptr;
    QStandardItemModel* dvsqlModel = nullptr;
    QStandardItemModel* tuningStepsModel = nullptr;
    QStandardItemModel* preampsModel = nullptr;
    QStandardItemModel* attenuatorsModel = nullptr;
    QStandardItemModel* antennasModel = nullptr;
    QStandardItemModel* ipplusModel = nullptr;
    QStandardItemModel* p25SqlModel = nullptr;
    QStandardItemModel* dPmrSqlModel = nullptr;
    QStandardItemModel* dPmrSCRMModel = nullptr;
    QStandardItemModel* nxdnSqlModel = nullptr;
    QStandardItemModel* nxdnEncModel = nullptr;
    QStandardItemModel* dcrSqlModel = nullptr;
    QStandardItemModel* dcrEncModel = nullptr;

    QStandardItemModel* vfoModelB = nullptr;
    QStandardItemModel* modesModelB = nullptr;
    QStandardItemModel* filterModelB = nullptr;
    QStandardItemModel* dataModelB = nullptr;
    QStandardItemModel* toneModesModelB = nullptr;
    QStandardItemModel* dsqlModelB = nullptr;
    QStandardItemModel* tonesModelB = nullptr;
    QStandardItemModel* tsqlModelB = nullptr;
    QStandardItemModel* dtcspModelB = nullptr;
    QStandardItemModel* duplexModelB = nullptr;
    QStandardItemModel* dtcsModelB = nullptr;
    QStandardItemModel* dvsqlModelB = nullptr;

    tableEditor* numEditor = nullptr;
    tableCombobox* clarRXList = nullptr;
    tableCombobox* clarTXList = nullptr;
    tableCombobox* splitList = nullptr;
    tableCombobox* scanList = nullptr;
    tableCombobox* skipList = nullptr;
    tableCombobox* vfoList = nullptr;
    tableEditor* nameEditor = nullptr;
    tableEditor* freqEditor = nullptr;
    tableEditor* clarEditor = nullptr;
    tableCombobox* filterList = nullptr;
    tableCombobox* dataList = nullptr;
    tableCombobox* duplexList = nullptr;
    tableCombobox* toneModesList = nullptr;
    tableCombobox* dsqlList = nullptr;
    tableCombobox* tonesList = nullptr;
    tableCombobox* tsqlList = nullptr;
    tableCombobox* dtcsList = nullptr;
    tableCombobox* dtcspList = nullptr;
    tableCombobox* modesList = nullptr;
    tableEditor* offsetEditor = nullptr;
    tableCombobox* dvsqlList = nullptr;
    tableEditor* urEditor = nullptr;
    tableEditor* r1Editor = nullptr;
    tableEditor* r2Editor = nullptr;
    tableCombobox* tuningStepsList = nullptr;
    tableEditor* tuningStepEditor = nullptr;
    tableCombobox* preampsList = nullptr;
    tableCombobox* attenuatorsList = nullptr;
    tableCombobox* antennasList = nullptr;
    tableCombobox* ipplusList = nullptr;

    tableCombobox* p25SqlList = nullptr;
    tableEditor* p25NacEditor = nullptr;
    tableCombobox* dPmrSqlList = nullptr;
    tableEditor* dPmrComIdEditor = nullptr;
    tableEditor* dPmrCcEditor = nullptr;
    tableCombobox* dPmrSCRMList = nullptr;
    tableEditor* dPmrKeyEditor = nullptr;
    tableCombobox* nxdnSqlList = nullptr;
    tableEditor* nxdnRanEditor = nullptr;
    tableCombobox* nxdnEncList = nullptr;
    tableEditor* nxdnKeyEditor = nullptr;
    tableCombobox* dcrSqlList = nullptr;
    tableEditor* dcrUcEditor = nullptr;
    tableCombobox* dcrEncList = nullptr;
    tableEditor* dcrKeyEditor = nullptr;

    tableCombobox* vfoListB = nullptr;
    tableEditor* freqEditorB = nullptr;
    tableCombobox* filterListB = nullptr;
    tableCombobox* dataListB = nullptr;
    tableCombobox* toneModesListB = nullptr;
    tableCombobox* dsqlListB = nullptr;
    tableCombobox* tonesListB = nullptr;
    tableCombobox* tsqlListB = nullptr;
    tableCombobox* dtcsListB = nullptr;
    tableCombobox* dtcspListB = nullptr;
    tableCombobox* modesListB = nullptr;
    tableCombobox* duplexListB = nullptr;
    tableEditor* offsetEditorB = nullptr;
    tableCombobox* dvsqlListB = nullptr;
    tableEditor* urEditorB = nullptr;
    tableEditor* r1EditorB = nullptr;
    tableEditor* r2EditorB = nullptr;
    tableCombobox* tuningStepsListB = nullptr;

    rigCapabilities* rigCaps = nullptr;

    QStatusBar* statusBar;
    QProgressBar* progress;

    Ui::memories *ui;
    bool extended = false;

    enum columns {
        columnRecall=0,
        columnNum,
        columnName,
        columnSplit,
        columnSkip,
        columnScan,
        columnVFO,
        columnFrequency,
        columnClarOffset,
        columnRXClar,
        columnTXClar,
        columnMode,
        columnFilter,
        columnData,
        columnDuplex,
        columnToneMode,
        columnTuningStep,
        columnCustomTuningStep,
        columnAttenuator,
        columnPreamplifier,
        columnAntenna,
        columnIPPlus,
        columnDSQL,
        columnTone,
        columnTSQL,
        columnDTCS,
        columnDTCSPolarity,
        columnDVSquelch,
        columnOffset,
        columnUR,
        columnR1,
        columnR2,
        columnP25Sql,
        columnP25Nac,
        columnDPmrSql,
        columnDPmrComid,
        columnDPmrCc,
        columnDPmrSCRM,
        columnDPmrKey,
        columnNxdnSql,
        columnNxdnRan,
        columnNxdnEnc,
        columnNxdnKey,
        columnDcrSql,
        columnDcrUc,
        columnDcrEnc,
        columnDcrKey,
        columnVFOB,
        columnFrequencyB,
        columnModeB,
        columnFilterB,
        columnDataB,
        columnDuplexB,
        columnToneModeB,
        columnDSQLB,
        columnToneB,
        columnTSQLB,
        columnDTCSB,
        columnDTCSPolarityB,
        columnDVSquelchB,
        columnOffsetB,
        columnURB,
        columnR1B,
        columnR2B,
        totalColumns        
    };

    int updateRow(int row, memoryType mem, bool store=true);

    int updateEntry(QStringList& combo, int row, columns column, quint8 data);
    int updateEntry(QStringList& combo, int row, columns column, QString data);
    int updateEntry(int row, columns column, QString data);

    void recallMem(quint32 num);

    struct stepType {
        stepType(){};
        stepType(quint8 num, QString name, quint64 hz) : num(num), name(name), hz(hz) {};
        quint8 num;
        QString name;
        quint64 hz;
    };

    struct commandList {
        commandList(){};
        commandList(queuePriority prio, funcs func, uchar receiver) : prio(prio),func(func), receiver(receiver) {};
        queuePriority prio;
        funcs func;
        uchar receiver;
    };

    QList<commandList> activeCommands;
    QList<funcs> disabledCommands{
        funcFreq,funcMode, funcPBTInner,funcPBTOuter,
        funcAttenuator, funcPreamp, funcAntenna, funcIPPlus, funcFilterWidth,
        funcSelectedFreq, funcUnselectedFreq, funcSelectedMode, funcUnselectedMode
    };

    void enableCell(int col, int row, bool en);
    void configColumns(int row, modeInfo mode, quint8 split=0);
    void configColumnsB(int row, modeInfo mode);
    bool disableCommands = false;

    QAction *addCurrent = new QAction("Add Current");
    QAction *useCurrent = new QAction("Use Current");
};

#endif // MEMORIES_H
