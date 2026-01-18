#ifndef MEMORIESMODEL_H
#define MEMORIESMODEL_H

#include <QAbstractTableModel>
#include <QTimer>
#include <QRegularExpression>
#include "wfviewtypes.h"
#include "rigidentities.h"
#include "cachingqueue.h"

class MemoriesModel : public QAbstractTableModel
{
    Q_OBJECT
    Q_PROPERTY(bool isAdmin READ isAdmin WRITE setIsAdmin NOTIFY isAdminChanged)
    Q_PROPERTY(bool slowLoad READ slowLoad WRITE setSlowLoad NOTIFY slowLoadChanged)
    Q_PROPERTY(int visibleColumnCount READ visibleColumnCount NOTIFY visibleColumnsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int maxProgress READ maxProgress NOTIFY maxProgressChanged)
    Q_PROPERTY(bool canEdit READ canEdit WRITE setCanEdit NOTIFY canEditChanged)
    Q_PROPERTY(bool isScanning READ isScanning NOTIFY isScanningChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QStringList groupNames READ groupNames NOTIFY groupNamesChanged)
    Q_PROPERTY(int currentGroup READ currentGroup WRITE setCurrentGroup NOTIFY currentGroupChanged)
    Q_PROPERTY(bool hasMemoryGroups READ hasMemoryGroups NOTIFY hasMemoryGroupsChanged)
    Q_PROPERTY(bool canScan READ canScan NOTIFY canScanChanged)
    Q_PROPERTY(bool memoryModeActive READ memoryModeActive WRITE setMemoryModeActive NOTIFY memoryModeActiveChanged)

public:
    enum MemoryRoles {
        ColumnVisibleRole = Qt::UserRole + 1,
        ColumnEditableRole,
        ColumnTypeRole,
        ComboOptionsRole,
        InputMaskRole
    };

    enum ColumnType {
        TypeText,
        TypeCombo,
        TypeFrequency,
        TypeNumeric,
        TypeButton
    };

    explicit MemoriesModel(QObject *parent = nullptr);
    ~MemoriesModel() override;

    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Property getters
    bool isAdmin() const;
    bool slowLoad() const;
    int visibleColumnCount() const { return m_visibleColumns; }
    bool loading() const { return m_loading; }
    int progress() const { return m_progress; }
    int maxProgress() const { return m_maxProgress; }
    bool canEdit() const { return !m_disableEditing; }
    bool isScanning() const { return m_scanning; }
    QString statusText() const { return m_statusText; }
    QStringList groupNames() const { return m_groupNames; }
    int currentGroup() const { return m_currentGroup; }
    bool hasMemoryGroups() const { return m_hasMemoryGroups; }
    bool canScan() const { return m_canScan; }
    bool memoryModeActive() const { return m_memoryModeActive; }

    // Property setters
    void setIsAdmin(bool isAdmin);
    void setSlowLoad(bool slowLoad);
    void setCanEdit(bool canEdit);
    void setCurrentGroup(int group);
    void setMemoryModeActive(bool active);

    // Invokable methods for QML
    Q_INVOKABLE void recallMemory(int row);
    Q_INVOKABLE void deleteMemory(int row);
    Q_INVOKABLE void addCurrentToMemory(int row);
    Q_INVOKABLE void updateFromCurrent(int row);
    Q_INVOKABLE bool isColumnVisible(int column) const;
    Q_INVOKABLE QString getColumnName(int column) const;
    Q_INVOKABLE QVariantList getComboOptions(int column) const;
    Q_INVOKABLE ColumnType getColumnType(int column) const;
    Q_INVOKABLE QString getInputMask(int column) const;
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void importCSV(const QString &filePath, bool allFields);
    Q_INVOKABLE void exportCSV(const QString &filePath, bool allFields);
    Q_INVOKABLE void populate();
    Q_INVOKABLE bool isRowSaving(int row) const;

signals:
    void isAdminChanged();
    void slowLoadChanged();
    void visibleColumnsChanged();
    void loadingChanged();
    void progressChanged();
    void maxProgressChanged();
    void canEditChanged();
    void canScanChanged();
    void isScanningChanged();
    void statusTextChanged();
    void groupNamesChanged();
    void currentGroupChanged();
    void hasMemoryGroupsChanged();
    void memoryModeActiveChanged();
    void memoryModeRequest(bool enable);
    void rowSavingChanged(int row, bool saving);

public slots:
    void receiveMemory(memoryType mem);
    void receiveMemoryName(memoryTagType tag);
    void receiveMemorySplit(memorySplitType s);

private slots:
    void onTimeout();
    void loadNextMemory();

private:
    bool m_initialized = false;
    void ensureInitialized();

    struct MemoryRow {
        memoryType data;
        bool valid = false;
        int memoryNumber = 0;
    };

    enum Column {
        ColRecall = 0,
        ColNum,
        ColName,
        ColSplit,
        ColSkip,
        ColScan,
        ColVFO,
        ColFrequency,
        ColClarOffset,
        ColRXClar,
        ColTXClar,
        ColMode,
        ColFilter,
        ColData,
        ColDuplex,
        ColToneMode,
        ColTuningStep,
        ColCustomTuningStep,
        ColAttenuator,
        ColPreamplifier,
        ColAntenna,
        ColIPPlus,
        ColDSQL,
        ColTone,
        ColTSQL,
        ColDTCS,
        ColDTCSPolarity,
        ColDVSquelch,
        ColOffset,
        ColUR,
        ColR1,
        ColR2,
        ColP25Sql,
        ColP25Nac,
        ColDPmrSql,
        ColDPmrComid,
        ColDPmrCc,
        ColDPmrSCRM,
        ColDPmrKey,
        ColNxdnSql,
        ColNxdnRan,
        ColNxdnEnc,
        ColNxdnKey,
        ColDcrSql,
        ColDcrUc,
        ColDcrEnc,
        ColDcrKey,
        ColVFOB,
        ColFrequencyB,
        ColModeB,
        ColFilterB,
        ColDataB,
        ColDuplexB,
        ColToneModeB,
        ColDSQLB,
        ColToneB,
        ColTSQLB,
        ColDTCSB,
        ColDTCSPolarityB,
        ColDVSquelchB,
        ColOffsetB,
        ColURB,
        ColR1B,
        ColR2B,
        TotalColumns
    };

    void initializeModel();
    void setupColumns();
    void configureColumnsForMode(int row, modeInfo mode, quint8 split = 0);
    void configureColumnsBForMode(int row, modeInfo mode);
    QVariant getCellData(const MemoryRow &mem, int col) const;
    void writeMemoryToRadio(int row);
    bool checkASCII(const QString &str) const;
    void setLoading(bool loading);
    void setProgress(int progress);
    void setStatusText(const QString &text);
    int findRowByMemoryNumber(int memNum) const;
    bool readCSVRow(QTextStream &in, QStringList *row);

    cachingQueue* m_queue = nullptr;
    rigCapabilities* m_rigCaps = nullptr;

    QVector<MemoryRow> m_memories;
    QVector<bool> m_columnVisible;
    QVector<QString> m_columnNames;
    QVector<ColumnType> m_columnTypes;
    QMap<int, QStringList> m_columnComboOptions;
    QMap<int, QString> m_columnInputMasks;

    QTimer m_timeoutTimer;
    QTimer m_slowLoadTimer;

    bool m_isAdmin = false;
    bool m_slowLoad = false;
    bool m_startup = true;
    bool m_loading = false;
    bool m_disableEditing = true;
    bool m_disableRadioWrite = false;
    bool m_scanning = false;
    bool m_memoryModeActive = false;
    bool m_hasMemoryGroups = false;
    bool m_canScan = false;
    bool m_extendedView = false;

    int m_visibleColumns = 1;
    int m_progress = 0;
    int m_maxProgress = 100;
    int m_currentGroup = 0;
    int m_groupMemories = 0;
    quint32 m_lastMemoryRequested = 0; // Must be unsigned int
    int m_timeoutCount = 0;
    int m_retries = 0;

    QString m_statusText;
    QString m_region;
    QStringList m_groupNames;

    funcs m_writeCommand = funcMemoryContents;
    funcs m_selectCommand = funcMemoryMode;

    // Combo option lists
    QStringList m_clar, m_split, m_scan, m_skip, m_vfos;
    QStringList m_duplexModes, m_modes, m_dataModes, m_filters;
    QStringList m_tones, m_toneModes, m_dtcs, m_dtcsp;
    QStringList m_dsql, m_dvsql, m_tuningSteps;
    QStringList m_preamps, m_attenuators, m_antennas, m_ipplus;
    QStringList m_p25Sql, m_dPmrSql, m_dPmrSCRM;
    QStringList m_nxdnSql, m_nxdnEnc, m_dcrSql, m_dcrEnc;

    QSet<int> m_savingRows;  // Track which rows are currently saving

};

#endif // MEMORIESMODEL_H
