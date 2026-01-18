#include "MemoriesModel.h"
#include "logcategories.h"
#include <QRegularExpression>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QStandardItemModel>

#define MEMORY_TIMEOUT 1000
#define MEMORY_SLOWLOAD 500
#define MEMORY_SATGROUP 200
#define MEMORY_SHORTGROUP 100

MemoriesModel::MemoriesModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_isAdmin(false)
    , m_slowLoad(false)
{
    ensureInitialized();
    // Minimal initialization - wait for populate() to be called
    // Initialize immediately so column names are available
    connect(&m_timeoutTimer, &QTimer::timeout, this, &MemoriesModel::onTimeout);
}

MemoriesModel::~MemoriesModel()
{
    qInfo() << "Deleting memories model";
}

void MemoriesModel::ensureInitialized()
{

    //qDebug() << "ensureInitialized called, m_initialized:" << m_initialized;

    if (m_initialized)
        return;

    m_queue = cachingQueue::getInstance();
    m_rigCaps = m_queue->getRigCaps();

    if (!m_rigCaps) {
        qWarning() << "No rigCaps available";
        return;
    }

    //qDebug() << "Calling initializeModel()";
    initializeModel();
    m_initialized = true;
}

void MemoriesModel::initializeModel()
{
    // Initialize column names
    m_columnNames = QVector<QString>{
        "", "Num", "Name", "Split", "Skip", "Scan", "VFO", "Freq",
        "Clarifier", "Clar RX", "Clar TX", "Mode", "Filter", "Data",
        "Duplex", "Tn Mode", "Step", "Prog Step", "Atten", "Preamp",
        "Ant", "IP Plus", "DSQL", "Tone", "TSQL", "DTCS", "DTCS Pol",
        "DV Sql", "Offset", "UR", "R1", "R2", "P25 SQL", "P25 NAC",
        "dPMR SQL", "dPMR COM ID", "dPMR CC", "dPMR SCRM", "dPMR Key",
        "NXDN SQL", "NXDN RAN", "NXDN Enc", "NXDN Key", "DCR SQL",
        "DCR UC", "DCR Enc", "DCR Key", "VFO B", "Freq B", "Mode B",
        "Filter B", "Data B", "Duplex B", "Tn Mode B", "DSQL B",
        "Tone B", "TSQL B", "DTCS B", "DTCSP B", "DV Sql B",
        "Offset B", "UR B", "R1 B", "R2 B"
    };

    // Initialize column visibility
    m_columnVisible.resize(TotalColumns);
    m_columnVisible.fill(false);
    m_columnVisible[ColRecall] = true;

    // Initialize column types
    m_columnTypes.resize(TotalColumns);
    m_columnTypes[ColRecall] = TypeButton;
    m_columnTypes[ColNum] = TypeNumeric;
    m_columnTypes[ColName] = TypeText;
    m_columnTypes[ColSplit] = TypeCombo;
    m_columnTypes[ColSkip] = TypeCombo;
    m_columnTypes[ColScan] = TypeCombo;
    m_columnTypes[ColVFO] = TypeCombo;
    m_columnTypes[ColFrequency] = TypeFrequency;
    m_columnTypes[ColClarOffset] = TypeNumeric;
    m_columnTypes[ColRXClar] = TypeCombo;
    m_columnTypes[ColTXClar] = TypeCombo;
    m_columnTypes[ColMode] = TypeCombo;
    m_columnTypes[ColFilter] = TypeCombo;
    m_columnTypes[ColData] = TypeCombo;
    m_columnTypes[ColDuplex] = TypeCombo;
    m_columnTypes[ColToneMode] = TypeCombo;
    m_columnTypes[ColTuningStep] = TypeCombo;
    m_columnTypes[ColCustomTuningStep] = TypeNumeric;
    m_columnTypes[ColAttenuator] = TypeCombo;
    m_columnTypes[ColPreamplifier] = TypeCombo;
    m_columnTypes[ColAntenna] = TypeCombo;
    m_columnTypes[ColIPPlus] = TypeCombo;
    m_columnTypes[ColDSQL] = TypeCombo;
    m_columnTypes[ColTone] = TypeCombo;
    m_columnTypes[ColTSQL] = TypeCombo;
    m_columnTypes[ColDTCS] = TypeCombo;
    m_columnTypes[ColDTCSPolarity] = TypeCombo;
    m_columnTypes[ColDVSquelch] = TypeCombo;
    m_columnTypes[ColOffset] = TypeNumeric;
    m_columnTypes[ColUR] = TypeText;
    m_columnTypes[ColR1] = TypeText;
    m_columnTypes[ColR2] = TypeText;
    m_columnTypes[ColFrequencyB] = TypeFrequency;
    m_columnTypes[ColModeB] = TypeCombo;
    m_columnTypes[ColFilterB] = TypeCombo;
    m_columnTypes[ColDataB] = TypeCombo;
    m_columnTypes[ColDuplexB] = TypeCombo;
    m_columnTypes[ColToneModeB] = TypeCombo;
    m_columnTypes[ColDSQLB] = TypeCombo;
    m_columnTypes[ColToneB] = TypeCombo;
    m_columnTypes[ColTSQLB] = TypeCombo;
    m_columnTypes[ColDTCSB] = TypeCombo;
    m_columnTypes[ColDTCSPolarityB] = TypeCombo;
    m_columnTypes[ColDVSquelchB] = TypeCombo;
    m_columnTypes[ColOffsetB] = TypeNumeric;
    m_columnTypes[ColURB] = TypeText;
    m_columnTypes[ColR1B] = TypeText;
    m_columnTypes[ColR2B] = TypeText;

    // Initialize combo lists
    m_clar << "OFF" << "ON";
    m_skip << "OFF" << "SKIP" << "PSKIP";
    m_dataModes << "OFF" << "DATA1";

    if (m_rigCaps->commands.contains(funcDATA2Mod))
        m_dataModes.append("DATA2");
    if (m_rigCaps->commands.contains(funcDATA3Mod))
        m_dataModes.append("DATA3");

    m_duplexModes << "OFF" << "DUP-" << "DUP+" << "RPS";

    if (m_rigCaps->manufacturer == manufKenwood || m_rigCaps->manufacturer == manufYaesu) {
        m_scan << "VFO" << "MEM" << "TUNE" << "QMB" << "NONE" << "PMS";
        if (m_rigCaps->commands.contains(funcMemoryWrite))
            m_writeCommand = funcMemoryWrite;
        if (m_rigCaps->commands.contains(funcMemorySelect))
            m_selectCommand = funcMemorySelect;
        if (m_rigCaps->manufacturer == manufYaesu)
            m_toneModes << "OFF" << "ENC/DEC" << "ENC";
        else
            m_toneModes << "OFF" << "TONE" << "CTCSS";
        m_filters << "FILTER A" << "FILTER B";
        m_split << "SIMP" << "PLUS" << "MINUS";
    } else {
        m_scan << "OFF" << "*1" << "*2" << "*3";
        if (m_rigCaps->hasTransmit)
            m_toneModes << "OFF" << "TONE" << "TSQL";
        else
            m_toneModes << "OFF" << "TSQL";
        m_filters << "FIL1" << "FIL2" << "FIL3";
        m_split << "OFF" << "ON";
    }

    if (m_rigCaps->commands.contains(funcRepeaterDTCS)) {
        m_toneModes.append("DTCS");
        m_toneModes.append("");
        m_toneModes.append("");
        m_toneModes.append("DTCS(T)");
        m_toneModes.append("TONE(T)/DTCS(R)");
        m_toneModes.append("DTCS(T)/TSQL(R)");
        m_toneModes.append("TONE(T)/TSQL(R)");
    }

    for (const auto& t : m_rigCaps->ctcss) {
        m_tones.append(t.name);
    }

    for (const auto& t : m_rigCaps->dtcs) {
        m_dtcs.append(t.name);
    }

    m_dsql << "OFF" << "DSQL" << "CSQL";
    m_p25Sql << "OFF" << "NAC";
    m_dPmrSql << "OFF" << "COM ID" << "CC";
    m_dPmrSCRM << "OFF" << "ON";
    m_nxdnSql << "OFF" << "ON";
    m_nxdnEnc << "OFF" << "ON";
    m_dcrSql << "OFF" << "UC";
    m_dcrEnc << "OFF" << "ON";

    if (m_rigCaps->hasTransmit)
        m_dtcsp << "BOTH N" << "N/R" << "R/N" << "BOTH R";
    else
        m_dtcsp << "NORMAL" << "REVERSE";

    m_ipplus << "OFF" << "ON";

    m_tuningSteps << "None";
    for (const auto& step : m_rigCaps->steps) {
        if (step.num) {
            m_tuningSteps.append(step.name);
        }
    }

    for (const auto& atten : m_rigCaps->attenuators) {
        m_attenuators.append(atten.name);
    }
    if (m_attenuators.isEmpty()) m_attenuators.append("None");

    for (const auto& preamp : m_rigCaps->preamps) {
        m_preamps.append(preamp.name);
    }
    if (m_preamps.isEmpty()) m_preamps.append("None");

    for (const auto& ant : m_rigCaps->antennas) {
        m_antennas.append(ant.name);
    }
    if (m_antennas.isEmpty()) m_antennas.append("None");

    for (int i = 0; i < 100; i++) {
        m_dvsql.append(QString::number(i).rightJustified(2, '0'));
    }

    if (m_rigCaps->commands.contains(funcVFOEqualAB)) {
        m_vfos << "VFOA" << "VFOB";
    } else if (m_rigCaps->commands.contains(funcVFOEqualMS)) {
        m_vfos << "MAIN" << "SUB";
    }

    for (auto& mode : m_rigCaps->modes) {
        m_modes.append(mode.name);
    }
    if (m_modes.isEmpty()) m_modes.append("None");

    // Setup scanning support
    m_canScan = m_rigCaps->commands.contains(funcScanning);

    // Setup memory groups
    m_hasMemoryGroups = m_rigCaps->memGroups > 1;
    m_groupNames.clear();
    m_groupNames.append("Memory Group");

    for (int i = m_rigCaps->memStart; i <= m_rigCaps->memGroups; i++) {
        m_groupNames.append(QString("Group %1").arg(i, 2, 10, QChar('0')));
    }

    if (m_rigCaps->satMemories && m_rigCaps->commands.contains(funcSatelliteMode)) {
        m_groupNames.append("Satellite");
    }

    m_maxProgress = m_rigCaps->memories;

    setupColumns();

    emit hasMemoryGroupsChanged();
    emit groupNamesChanged();
    emit maxProgressChanged();
}

void MemoriesModel::populate()
{
    //qDebug() << "populate() called";

    if (!m_rigCaps) {
        qWarning() << "Cannot populate - no rigCaps";
        return;
    }

    //qDebug() << "Group names count:" << m_groupNames.count();

    if (m_groupNames.count() > 1) {
        //qInfo() << "Memory group items:" << m_groupNames.count();
        setCurrentGroup(1);
    } else {
        setCurrentGroup(0);
    }
}

// QAbstractTableModel interface
int MemoriesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_memories.size();
}

int MemoriesModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return TotalColumns;
}

QVariant MemoriesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_memories.size()) {
        qDebug() << "data() invalid index or out of range. Row:" << index.row() << "memories size:" << m_memories.size();
        return QVariant();
    }

    const MemoryRow& mem = m_memories[index.row()];
    const int col = index.column();

    if (role == Qt::DisplayRole) {
        QVariant result = getCellData(mem, col);
        if (index.row() == 0) {  // Only debug first row to avoid spam
            //qDebug() << "data() row:" << index.row() << "col:" << col << "role: DisplayRole, value:" << result;
        }
        return result;
    }

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return getCellData(mem, col);
    case ColumnVisibleRole:
        return col < m_columnVisible.size() ? m_columnVisible[col] : false;
    case ColumnEditableRole:
        return !m_disableEditing && col != ColNum && col != ColRecall;
    case ColumnTypeRole:
        return col < m_columnTypes.size() ? m_columnTypes[col] : TypeText;
    case ComboOptionsRole:
        return QVariant::fromValue(m_columnComboOptions.value(col));
    case InputMaskRole:
        return m_columnInputMasks.value(col);
    default:
        return QVariant();
    }
}

QVariant MemoriesModel::getCellData(const MemoryRow &mem, int col) const
{
    switch (col) {
    case ColRecall:
        return QVariant(); // Button handled by delegate
    case ColNum:
        return QString::number(mem.memoryNumber).rightJustified(3, '0');
    case ColName:
        return QString(mem.data.name);
    case ColSplit:
        return m_split.value(mem.data.split, "");
    case ColSkip:
        return m_skip.value(mem.data.skip, "");
    case ColScan:
        return m_scan.value(mem.data.scan, "");
    case ColFrequency:
        return QString::number(mem.data.frequency.Hz / 1000000.0, 'f', 5);
    case ColFrequencyB:
        return QString::number(mem.data.frequencyB.Hz / 1000000.0, 'f', 5);
    case ColClarOffset:
        if (mem.data.clarifier > -10000 && mem.data.clarifier < 10000)
            return QString("%1%2").arg(mem.data.clarifier < 0 ? '-' : '+').arg(abs(mem.data.clarifier), 4, 10, QChar('0'));
        return "";
    case ColRXClar:
        return m_clar.value(mem.data.clarRX ? 1 : 0, "");
    case ColTXClar:
        return m_clar.value(mem.data.clarTX ? 1 : 0, "");
    case ColMode:
        for (const auto& mode : m_rigCaps->modes) {
            if (mode.reg == mem.data.mode)
                return mode.name;
        }
        return "";
    case ColModeB:
        for (const auto& mode : m_rigCaps->modes) {
            if (mode.reg == mem.data.modeB)
                return mode.name;
        }
        return "";
    case ColFilter:
        if (m_rigCaps->manufacturer == manufKenwood)
            return m_filters.value(mem.data.filter, "");
        else
            return m_filters.value(mem.data.filter - 1, "");
    case ColFilterB:
        if (m_rigCaps->manufacturer == manufKenwood)
            return m_filters.value(mem.data.filterB, "");
        else
            return m_filters.value(mem.data.filterB - 1, "");
    case ColData:
        return m_dataModes.value(mem.data.datamode, "");
    case ColDataB:
        return m_dataModes.value(mem.data.datamodeB, "");
    case ColDuplex:
        return m_duplexModes.value(mem.data.duplex, "");
    case ColDuplexB:
        return m_duplexModes.value(mem.data.duplexB, "");
    case ColToneMode:
        return m_toneModes.value(mem.data.tonemode, "");
    case ColToneModeB:
        return m_toneModes.value(mem.data.tonemodeB, "");
    case ColTuningStep:
        return m_tuningSteps.value(mem.data.tuningStep, "");
    case ColCustomTuningStep:
        return QString::number(mem.data.progTs);
    case ColAttenuator:
        return QString::number(mem.data.atten);
    case ColPreamplifier:
        return m_preamps.value(mem.data.preamp, "");
    case ColAntenna:
        return m_antennas.value(mem.data.antenna, "");
    case ColIPPlus:
        return m_ipplus.value(mem.data.ipplus, "");
    case ColDSQL:
        return m_dsql.value(mem.data.dsql, "");
    case ColDSQLB:
        return m_dsql.value(mem.data.dsqlB, "");
    case ColTone:
        return mem.data.tone;
    case ColToneB:
        return mem.data.toneB;
    case ColTSQL:
        return mem.data.tsql;
    case ColTSQLB:
        return mem.data.tsqlB;
    case ColDTCS:
        return QString::number(mem.data.dtcs);
    case ColDTCSB:
        return QString::number(mem.data.dtcsB);
    case ColDTCSPolarity:
        return m_dtcsp.value(mem.data.dtcsp, "");
    case ColDTCSPolarityB:
        return m_dtcsp.value(mem.data.dtcspB, "");
    case ColDVSquelch:
        return QString::number(mem.data.dvsql).rightJustified(2, '0');
    case ColDVSquelchB:
        return QString::number(mem.data.dvsqlB).rightJustified(2, '0');
    case ColOffset:
        return QString::number(mem.data.duplexOffset.Hz / 10000.0, 'f', 3);
    case ColOffsetB:
        return QString::number(mem.data.duplexOffsetB.Hz / 10000.0, 'f', 3);
    case ColUR:
        return checkASCII(QString(mem.data.UR)) ? QString(mem.data.UR) : "";
    case ColURB:
        return checkASCII(QString(mem.data.URB)) ? QString(mem.data.URB) : "";
    case ColR1:
        return checkASCII(QString(mem.data.R1)) ? QString(mem.data.R1) : "";
    case ColR1B:
        return checkASCII(QString(mem.data.R1B)) ? QString(mem.data.R1B) : "";
    case ColR2:
        return checkASCII(QString(mem.data.R2)) ? QString(mem.data.R2) : "";
    case ColR2B:
        return checkASCII(QString(mem.data.R2B)) ? QString(mem.data.R2B) : "";
    default:
        return QVariant();
    }
}

bool MemoriesModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= m_memories.size() || role != Qt::EditRole)
        return false;

    if (m_disableEditing && index.column() != ColRecall)
        return false;

    MemoryRow &mem = m_memories[index.row()];
    QString strValue = value.toString();

    // Update the memory data based on column
    switch (index.column()) {
    case ColName:
        strncpy(mem.data.name, strValue.toStdString().c_str(), sizeof(mem.data.name) - 1);
        mem.data.name[sizeof(mem.data.name) - 1] = '\0';
        break;
    case ColFrequency:
        mem.data.frequency.Hz = quint64(strValue.toDouble() * 1000000.0);
        break;
    case ColFrequencyB:
        mem.data.frequencyB.Hz = quint64(strValue.toDouble() * 1000000.0);
        break;
    case ColMode:
        for (const auto& mode : m_rigCaps->modes) {
            if (mode.name == strValue) {
                mem.data.mode = mode.reg;
                break;
            }
        }
        break;
    case ColModeB:
        for (const auto& mode : m_rigCaps->modes) {
            if (mode.name == strValue) {
                mem.data.modeB = mode.reg;
                break;
            }
        }
        break;
    case ColSplit:
        mem.data.split = m_split.indexOf(strValue);
        break;
    case ColSkip:
        mem.data.skip = m_skip.indexOf(strValue);
        break;
    case ColScan:
        mem.data.scan = m_scan.indexOf(strValue);
        break;
    case ColData:
        mem.data.datamode = m_dataModes.indexOf(strValue);
        break;
    case ColDataB:
        mem.data.datamodeB = m_dataModes.indexOf(strValue);
        break;
    case ColFilter:
        if (m_rigCaps->manufacturer == manufKenwood)
            mem.data.filter = m_filters.indexOf(strValue);
        else
            mem.data.filter = m_filters.indexOf(strValue) + 1;
        break;
    case ColFilterB:
        if (m_rigCaps->manufacturer == manufKenwood)
            mem.data.filterB = m_filters.indexOf(strValue);
        else
            mem.data.filterB = m_filters.indexOf(strValue) + 1;
        break;
    case ColDuplex:
        mem.data.duplex = m_duplexModes.indexOf(strValue);
        break;
    case ColDuplexB:
        mem.data.duplexB = m_duplexModes.indexOf(strValue);
        break;
    case ColToneMode:
        mem.data.tonemode = m_toneModes.indexOf(strValue);
        break;
    case ColToneModeB:
        mem.data.tonemodeB = m_toneModes.indexOf(strValue);
        break;
    case ColTone:
        mem.data.tone = strValue;
        break;
    case ColToneB:
        mem.data.toneB = strValue;
        break;
    case ColTSQL:
        mem.data.tsql = strValue;
        break;
    case ColTSQLB:
        mem.data.tsqlB = strValue;
        break;
    case ColDTCS:
        mem.data.dtcs = strValue.toInt();
        break;
    case ColDTCSB:
        mem.data.dtcsB = strValue.toInt();
        break;
    case ColDTCSPolarity:
        mem.data.dtcsp = m_dtcsp.indexOf(strValue);
        break;
    case ColDTCSPolarityB:
        mem.data.dtcspB = m_dtcsp.indexOf(strValue);
        break;
    case ColTuningStep:
        mem.data.tuningStep = m_tuningSteps.indexOf(strValue);
        break;
    case ColCustomTuningStep:
        mem.data.progTs = strValue.toInt();
        break;
    case ColAttenuator:
        mem.data.atten = strValue.toInt();
        break;
    case ColPreamplifier:
        mem.data.preamp = m_preamps.indexOf(strValue);
        break;
    case ColAntenna:
        mem.data.antenna = m_antennas.indexOf(strValue);
        break;
    case ColIPPlus:
        mem.data.ipplus = m_ipplus.indexOf(strValue);
        break;
    case ColDSQL:
        mem.data.dsql = m_dsql.indexOf(strValue);
        break;
    case ColDSQLB:
        mem.data.dsqlB = m_dsql.indexOf(strValue);
        break;
    case ColDVSquelch:
        mem.data.dvsql = strValue.toInt();
        break;
    case ColDVSquelchB:
        mem.data.dvsqlB = strValue.toInt();
        break;
    case ColOffset:
        mem.data.duplexOffset.Hz = quint64(strValue.toDouble() * 10000.0);
        break;
    case ColOffsetB:
        mem.data.duplexOffsetB.Hz = quint64(strValue.toDouble() * 10000.0);
        break;
    case ColClarOffset:
        mem.data.clarifier = strValue.toShort();
        break;
    case ColRXClar:
        mem.data.clarRX = (strValue == "ON");
        break;
    case ColTXClar:
        mem.data.clarTX = (strValue == "ON");
        break;
    case ColUR:
        strncpy(mem.data.UR, strValue.toStdString().c_str(), sizeof(mem.data.UR) - 1);
        break;
    case ColURB:
        strncpy(mem.data.URB, strValue.toStdString().c_str(), sizeof(mem.data.URB) - 1);
        break;
    case ColR1:
        strncpy(mem.data.R1, strValue.toStdString().c_str(), sizeof(mem.data.R1) - 1);
        break;
    case ColR1B:
        strncpy(mem.data.R1B, strValue.toStdString().c_str(), sizeof(mem.data.R1B) - 1);
        break;
    case ColR2:
        strncpy(mem.data.R2, strValue.toStdString().c_str(), sizeof(mem.data.R2) - 1);
        break;
    case ColR2B:
        strncpy(mem.data.R2B, strValue.toStdString().c_str(), sizeof(mem.data.R2B) - 1);
        break;
    default:
        // Column not editable
        return false;
    }

    emit dataChanged(index, index);

    // Write to radio
    if (!m_disableRadioWrite) {
        writeMemoryToRadio(index.row());
    }

    return true;
}

QVariant MemoriesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        return getColumnName(section);
    }
    return QVariant();
}

Qt::ItemFlags MemoriesModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags flags = QAbstractTableModel::flags(index);

    if (!m_disableEditing && index.column() != ColNum && index.column() != ColRecall) {
        flags |= Qt::ItemIsEditable;
    }

    return flags;
}

QHash<int, QByteArray> MemoriesModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractTableModel::roleNames();
    roles[ColumnVisibleRole] = "columnVisible";
    roles[ColumnEditableRole] = "columnEditable";
    roles[ColumnTypeRole] = "columnType";
    roles[ComboOptionsRole] = "comboOptions";
    roles[InputMaskRole] = "inputMask";
    return roles;
}

// Property setters
bool MemoriesModel::isAdmin() const
{
    return m_isAdmin;
}

void MemoriesModel::setIsAdmin(bool isAdmin)
{
    if (m_isAdmin == isAdmin)
        return;
    m_isAdmin = isAdmin;
    emit isAdminChanged();
}

bool MemoriesModel::slowLoad() const
{
    return m_slowLoad;
}

void MemoriesModel::setSlowLoad(bool slowLoad)
{
    if (m_slowLoad == slowLoad)
        return;
    m_slowLoad = slowLoad;
    emit slowLoadChanged();
}

void MemoriesModel::setCanEdit(bool canEdit)
{
    bool disableEditing = !canEdit;
    if (m_disableEditing == disableEditing)
        return;
    m_disableEditing = disableEditing;
    emit canEditChanged();

    // Refresh all data to update editable state
    if (m_memories.size() > 0) {
        emit dataChanged(index(0, 0), index(m_memories.size() - 1, TotalColumns - 1));
    }
}

void MemoriesModel::setCurrentGroup(int group)
{
    if (m_currentGroup == group)
        return;

    m_currentGroup = group;
    emit currentGroupChanged();

    m_timeoutTimer.start(MEMORY_TIMEOUT);
    m_visibleColumns = 1;

    // Determine group memories
    if (group == MEMORY_SATGROUP)
        m_groupMemories = m_rigCaps->satMemories;
    else if (group == MEMORY_SHORTGROUP)
        m_groupMemories = 3;
    else
        m_groupMemories = m_rigCaps->memories;

    m_maxProgress = m_groupMemories;
    emit maxProgressChanged();

    // Clear existing memories
    beginResetModel();
    m_memories.clear();
    endResetModel();

    setLoading(true);
    m_startup = true;

    // Start loading
    if (group == MEMORY_SATGROUP) {
        m_lastMemoryRequested = m_rigCaps->memStart;
        m_queue->add(priorityImmediate, queueItem(funcSatelliteMode, QVariant::fromValue(true), false, 0));

        if (m_slowLoad) {
            QTimer::singleShot(MEMORY_SLOWLOAD, this, &MemoriesModel::loadNextMemory);
        } else {
            loadNextMemory();
        }
    } else {
        qInfo() << "Requesting memory group" << group;
        m_queue->add(priorityImmediate, queueItem(funcSatelliteMode, QVariant::fromValue(false), false, 0));
        m_queue->add(priorityImmediate, queueItem(funcMemoryGroup, QVariant::fromValue<uchar>(group)));

        m_lastMemoryRequested = quint32(group << 16) | (m_rigCaps->memStart & 0xffff);

        if (m_slowLoad) {
            QTimer::singleShot(MEMORY_SLOWLOAD, this, &MemoriesModel::loadNextMemory);
        } else {
            loadNextMemory();
        }
    }
    qInfo() << "Setting memory group to" << group << "and requesting memory" << (m_lastMemoryRequested & 0xffff);
}

void MemoriesModel::setMemoryModeActive(bool active)
{
    if (m_memoryModeActive == active)
        return;

    m_memoryModeActive = active;
    emit memoryModeActiveChanged();
    emit memoryModeRequest(active);

    if (active) {
        qInfo() << "Selecting Memory Mode";
        for (uchar i = 0; i < m_rigCaps->numReceiver; i++) {
            m_queue->add(priorityImmediate, funcMemoryMode, false, i);
        }
    } else {
        qInfo() << "Selecting VFO Mode";
        if (m_rigCaps->commands.contains(funcSatelliteMode))
            m_queue->add(priorityImmediate, queueItem(funcSatelliteMode, QVariant::fromValue(false), false, 0));

        for (uchar i = 0; i < m_rigCaps->numReceiver; i++) {
            m_queue->add(priorityImmediate, funcVFOModeSelect, false, i);
        }
    }
}

// Q_INVOKABLE methods
void MemoriesModel::recallMemory(int row)
{
    if (row < 0 || row >= m_memories.size())
        return;

    int memNum = m_memories[row].memoryNumber;
    qInfo() << "Recalling memory" << memNum;

    if (m_rigCaps->commands.contains(funcMemoryGroup)) {
        m_queue->add(priorityImmediate, queueItem(funcMemoryGroup, QVariant::fromValue<uchar>(quint16(m_currentGroup))));
    }

    m_queue->add(priorityImmediate, queueItem(m_selectCommand, QVariant::fromValue<uint>(quint32((m_currentGroup << 16) | memNum))));

    vfoCommandType t = m_queue->getVfoCommand(vfoA, 0, false);
    m_queue->add(priorityImmediate, t.freqFunc, false, t.receiver);
    m_queue->add(priorityImmediate, t.modeFunc, false, t.receiver);

    if (!m_memoryModeActive) {
        setMemoryModeActive(true);
    }
}

void MemoriesModel::deleteMemory(int row)
{
    if (row < 0 || row >= m_memories.size())
        return;

    int memNum = m_memories[row].memoryNumber;

    if (memNum >= m_rigCaps->memStart && memNum <= m_rigCaps->memories) {
        qInfo() << "Deleting memory" << memNum;

        memoryType currentMemory;
        if (m_currentGroup == MEMORY_SATGROUP) {
            currentMemory.sat = true;
        }
        currentMemory.group = m_currentGroup;
        currentMemory.channel = memNum;
        currentMemory.del = true;

        m_queue->add(priorityImmediate, queueItem((currentMemory.sat ? funcSatelliteMemory : m_writeCommand),
                                                  QVariant::fromValue<memoryType>(currentMemory)));

        // Remove from model
        beginRemoveRows(QModelIndex(), row, row);
        m_memories.removeAt(row);
        endRemoveRows();
    }
}

void MemoriesModel::addCurrentToMemory(int row)
{
    qInfo() << "Add Current to memory";

    // Get current VFO settings and create new memory
    memoryType mem;
    memset(&mem, 0, sizeof(memoryType)); // Initialize to zero

    vfoCommandType ta = m_queue->getVfoCommand(vfoA, 0, false);
    vfoCommandType tb;

    if (m_rigCaps->numReceiver > 1)
        tb = m_queue->getVfoCommand(vfoA, 1, false);
    else
        tb = m_queue->getVfoCommand(vfoB, 0, false);

    // Get VFO A settings
    mem.split = m_queue->getCache(funcSplitStatus).value.value<uchar>();
    mem.frequency = m_queue->getCache(ta.freqFunc, ta.receiver).value.value<freqt>();
    mem.mode = m_queue->getCache(ta.modeFunc, ta.receiver).value.value<modeInfo>().reg;
    mem.datamode = m_queue->getCache(ta.modeFunc, ta.receiver).value.value<modeInfo>().data;
    mem.filter = m_queue->getCache(ta.modeFunc, ta.receiver).value.value<modeInfo>().filter;
    mem.tonemode = m_queue->getCache(funcRepeaterTone, ta.receiver).value.value<uchar>();
    mem.tone = m_queue->getCache(funcToneFreq, ta.receiver).value.value<toneInfo>().name;
    mem.tsql = m_queue->getCache(funcTSQLFreq, ta.receiver).value.value<toneInfo>().name;
    mem.dtcs = m_queue->getCache(funcDTCSCode, ta.receiver).value.value<ushort>();
    mem.dvsql = m_queue->getCache(funcCSQLCode, ta.receiver).value.value<ushort>();

    // Get VFO B settings
    mem.frequencyB = m_queue->getCache(tb.freqFunc, tb.receiver).value.value<freqt>();
    mem.modeB = m_queue->getCache(tb.modeFunc, tb.receiver).value.value<modeInfo>().reg;
    mem.datamodeB = m_queue->getCache(tb.modeFunc, tb.receiver).value.value<modeInfo>().data;
    mem.filterB = m_queue->getCache(tb.modeFunc, tb.receiver).value.value<modeInfo>().filter;
    mem.tonemodeB = m_queue->getCache(funcRepeaterTone, tb.receiver).value.value<uchar>();
    mem.toneB = m_queue->getCache(funcToneFreq, tb.receiver).value.value<toneInfo>().name;
    mem.tsqlB = m_queue->getCache(funcTSQLFreq, tb.receiver).value.value<toneInfo>().name;
    mem.dtcsB = m_queue->getCache(funcDTCSCode, tb.receiver).value.value<ushort>();
    mem.dvsqlB = m_queue->getCache(funcCSQLCode, tb.receiver).value.value<ushort>();

    // Initialize string fields
    memset(mem.UR, 0x0, sizeof(mem.UR));
    memset(mem.URB, 0x0, sizeof(mem.URB));
    memset(mem.R1, 0x0, sizeof(mem.R1));
    memset(mem.R1B, 0x0, sizeof(mem.R1B));
    memset(mem.R2, 0x0, sizeof(mem.R2));
    memset(mem.R2B, 0x0, sizeof(mem.R2B));
    memcpy(mem.name, "<NEW>", 6);

    // Find next available memory number
    quint32 num = m_rigCaps->memStart;
    std::vector<quint16> rows;
    for (int i = 0; i < m_memories.size(); i++) {
        rows.push_back(m_memories[i].memoryNumber);
    }

    if (rows.size() != 0) {
        std::sort(rows.begin(), rows.end());
        auto i = std::adjacent_find(rows.begin(), rows.end(), [](int l, int r) { return l + 1 < r; });
        if (i == rows.end()) {
            if ((m_currentGroup != MEMORY_SATGROUP && rows.back() < m_groupMemories - 1) ||
                (m_currentGroup == MEMORY_SATGROUP && rows.back() < m_rigCaps->satMemories - 1)) {
                num = rows.back() + 1;
            } else {
                qWarning() << "No free memory slots available";
                return;
            }
        } else {
            num = 1 + *i;
        }
    }

    qInfo() << "Adding new memory at slot" << num << "with freq" << mem.frequency.Hz;

    // Set group and channel
    mem.group = m_currentGroup;
    mem.channel = num;
    if (m_currentGroup == MEMORY_SATGROUP) {
        mem.sat = true;
    }

    // Add new row to model
    int newRow = m_memories.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    MemoryRow newMem;
    newMem.memoryNumber = num;
    newMem.data = mem;
    newMem.valid = true;
    m_memories.append(newMem);
    endInsertRows();

    // Write to radio
    writeMemoryToRadio(newRow);
}

void MemoriesModel::updateFromCurrent(int row)
{
    qInfo() << "Update memory from current" << row;

    if (row < 0 || row >= m_memories.size())
        return;

    // Get current VFO settings
    memoryType mem;
    vfoCommandType ta = m_queue->getVfoCommand(vfoA, 0, false);

    mem.split = m_queue->getCache(funcSplitStatus).value.value<uchar>();
    mem.frequency = m_queue->getCache(ta.freqFunc, ta.receiver).value.value<freqt>();
    mem.mode = m_queue->getCache(ta.modeFunc, ta.receiver).value.value<modeInfo>().reg;
    mem.datamode = m_queue->getCache(ta.modeFunc, ta.receiver).value.value<modeInfo>().data;

    memcpy(mem.name, "<UPDATED>", 10);

    // Update the row
    m_memories[row].data = mem;
    emit dataChanged(index(row, 0), index(row, TotalColumns - 1));

    writeMemoryToRadio(row);
}

bool MemoriesModel::isColumnVisible(int column) const
{
    bool visible = false;
    if (column >= 0 && column < m_columnVisible.size()) {
        visible = m_columnVisible[column];
    }
    //qDebug() << "isColumnVisible(" << column << "):" << visible << "size:" << m_columnVisible.size();
    return visible;
}


QString MemoriesModel::getColumnName(int column) const
{

    if (column >= 0 && column < m_columnNames.size()) {
        //qDebug() << "getColumnName(" << column << "):" << m_columnNames[column];
        return m_columnNames[column];
    }
    //qDebug() << "getColumnName(" << column << "): OUT OF RANGE, size is" << m_columnNames.size();
    return QString("Col " + QString::number(column));  // Return something so we can see it
}

QVariantList MemoriesModel::getComboOptions(int column) const
{
    QVariantList result;
    QStringList options = m_columnComboOptions.value(column);
    for (const QString& opt : options) {
        result.append(opt);
    }
    return result;
}

MemoriesModel::ColumnType MemoriesModel::getColumnType(int column) const
{
    if (column >= 0 && column < m_columnTypes.size())
        return m_columnTypes[column];
    return TypeText;
}

QString MemoriesModel::getInputMask(int column) const
{
    return m_columnInputMasks.value(column);
}

void MemoriesModel::startScan()
{
    if (m_scanning)
        return;

    m_scanning = true;
    emit isScanningChanged();

    m_queue->add(priorityImmediate, queueItem(funcScanning, QVariant::fromValue<uchar>(1)));
}

void MemoriesModel::stopScan()
{
    if (!m_scanning)
        return;

    m_scanning = false;
    emit isScanningChanged();

    m_queue->add(priorityImmediate, queueItem(funcScanning, QVariant::fromValue<uchar>(0)));
}

void MemoriesModel::importCSV(const QString &filePath, bool allFields)
{
    QFile data(filePath);
    if (!data.open(QIODevice::ReadOnly)) {
        qWarning() << "Couldn't open file for CSV import" << data.errorString();
        return;
    }
    QTextStream input(&data);
    QStringList row;
    int rows = 0;

    m_progress = m_rigCaps->memStart;
    emit progressChanged();

    // Temporarily disable radio writes during import
    m_disableRadioWrite = true;

    QSet<int> modifiedRows; // Track which rows were modified

    while (readCSVRow(input, &row)) {
        m_progress = rows;
        emit progressChanged();

        if (!rows++) {
            continue; // Skip header row
        }

        if (row.isEmpty())
            continue;

        // Get memory number from first CSV column
        int memoryNumber = row[0].toInt();

        // Find or create row
        int rownum = -1;
        for (int i = 0; i < m_memories.size(); i++) {
            if (m_memories[i].memoryNumber == memoryNumber) {
                rownum = i;
                break;
            }
        }

        if (rownum == -1) {
            // Add new row
            rownum = m_memories.size();
            beginInsertRows(QModelIndex(), rownum, rownum);
            MemoryRow newRow;
            newRow.memoryNumber = memoryNumber;
            m_memories.append(newRow);
            endInsertRows();
        }

        modifiedRows.insert(rownum);

        // Import data into columns
        int colnum = 1; // Start at column 1 (skip button column 0)

        for (int i = 0; i < row.size(); i++) {
            // Skip hidden columns if not importing all fields
            if (!allFields) {
                while (colnum < TotalColumns && !m_columnVisible[colnum]) {
                    colnum++;
                }
            }

            if (colnum >= TotalColumns || i >= row.size())
                break;

            QString data = row[i];

            // Trim trailing spaces
            for (int n = data.size() - 1; n >= 0; --n) {
                if (!data.at(n).isSpace()) {
                    data.truncate(n + 1);
                    break;
                }
            }

            // Apply special formatting based on column type
            switch (colnum) {
            case ColFrequency:
            case ColFrequencyB:
                data = QString::number(data.toDouble(), 'f', 6);
                break;
            case ColDTCS:
            case ColDTCSB:
                data = QString::number(data.toInt()).rightJustified(3, '0');
                break;
            case ColDVSquelch:
            case ColDVSquelchB:
                data = QString::number(data.toInt()).rightJustified(2, '0');
                break;
            case ColTone:
            case ColToneB:
            case ColTSQL:
            case ColTSQLB:
                if (data.endsWith("Hz"))
                    data = data.mid(0, data.length() - 2);
                break;
            case ColFilter:
            case ColFilterB:
                if (!data.startsWith("FIL"))
                    data = "FIL" + data;
                break;
            case ColAntenna:
                if (!data.startsWith("ANT"))
                    data = "ANT " + data;
                break;
            default:
                break;
            }

            // Use setData to update the cell
            setData(index(rownum, colnum), data, Qt::EditRole);

            colnum++;
        }
    }

    // Re-enable editing
    m_disableRadioWrite = false;

    // Now write all modified rows to radio
    for (int rownum : modifiedRows) {
        writeMemoryToRadio(rownum);
    }

    m_progress = m_rigCaps->memories;
    emit progressChanged();
    data.close();
}

void MemoriesModel::exportCSV(const QString &filePath, bool allFields)
{
    QFile data(filePath);
    if (!data.open(QFile::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Couldn't open file for CSV export" << filePath;
        return;
    }
    QTextStream output(&data);

    // Write headers
    bool firstCol = true;
    for (int i = 1; i < TotalColumns; i++) {
        if (!m_columnVisible[i] && !allFields)
            continue;

        if (!firstCol)
            output << ",";

        output << "\"" << m_columnNames[i] << "\"";
        firstCol = false;
    }
    output << "\n";

    // Write data
    for (int i = 0; i < m_memories.size(); i++) {
        firstCol = true;
        for (int j = 1; j < TotalColumns; j++) {
            if (!m_columnVisible[j] && !allFields)
                continue;

            if (!firstCol)
                output << ",";

            QString data = getCellData(m_memories[i], j).toString();
            output << "\"" << data << "\"";
            firstCol = false;
        }
        output << "\n";
    }

    data.close();
}

// Private helper methods
// Private helper methods
void MemoriesModel::setupColumns()
{
    //qDebug() << "setupColumns() called";
    //qDebug() << "Current group:" << m_currentGroup;
    //qDebug() << "rigCaps manufacturer:" << m_rigCaps->manufacturer;

    m_columnVisible.fill(false);
    m_columnVisible[ColRecall] = true;
    m_visibleColumns = 1;

    QVector<memParserFormat> parser;
    if (m_currentGroup == MEMORY_SATGROUP) {
        parser = m_rigCaps->satParser;
        //qDebug() << "Using satParser, size:" << parser.size();
    } else {
        parser = m_rigCaps->memParser;
        //qDebug() << "Using memParser, size:" << parser.size();
    }

    for (const auto& parse : parser) {
        //qDebug() << "Parser spec:" << parse.spec << "len:" << parse.len;
        switch (parse.spec) {
        case 'a':
            // Group selection enabled
            break;
        case 'b':
            //qDebug() << "Enabling ColNum";
            m_columnVisible[ColNum] = true;
            m_visibleColumns++;
            break;
        case 'C':
            m_columnVisible[ColSkip] = true;
            m_columnComboOptions[ColSkip] = m_skip;
            m_visibleColumns++;
            // Fallthrough
            m_scan.clear();
            m_scan << "OFF" << "*1" << "*2" << "*3" << "*4" << "*5" << "*6" << "*7" << "*8" << "*9";
        case 'c':
            m_columnVisible[ColScan] = true;
            m_columnComboOptions[ColScan] = m_scan;
            m_visibleColumns++;
            break;
        case 'd':
            m_columnVisible[ColSplit] = true;
            m_columnComboOptions[ColSplit] = m_split;
            m_columnVisible[ColScan] = true;
            m_columnComboOptions[ColScan] = m_scan;
            m_visibleColumns += 2;
            break;
        case 'D':
            m_columnVisible[ColDuplex] = true;
            m_columnComboOptions[ColDuplex] = m_duplexModes;
            m_visibleColumns++;
            break;
        case 'e':
            m_columnVisible[ColVFO] = true;
            m_columnComboOptions[ColVFO] = m_vfos;
            m_visibleColumns++;
            break;
        case 'E':
            m_columnVisible[ColVFOB] = true;
            m_columnComboOptions[ColVFOB] = m_vfos;
            m_visibleColumns++;
            break;
        case 'f':
            //qDebug() << "Enabling ColFrequency";
            m_columnVisible[ColFrequency] = true;
            m_columnInputMasks[ColFrequency] = "00000.00000";
            m_visibleColumns++;
            break;
        case 'F':
            m_columnVisible[ColFrequencyB] = true;
            m_columnInputMasks[ColFrequencyB] = "00000.00000";
            m_visibleColumns++;
            break;
        case 'g':
            //qDebug() << "Enabling ColMode";
            m_columnVisible[ColMode] = true;
            m_columnComboOptions[ColMode] = m_modes;
            m_visibleColumns++;
            break;
        case 'G':
            m_columnVisible[ColModeB] = true;
            m_columnComboOptions[ColModeB] = m_modes;
            m_visibleColumns++;
            break;
        case 'h':
            m_columnVisible[ColFilter] = true;
            m_columnComboOptions[ColFilter] = m_filters;
            m_visibleColumns++;
            break;
        case 'H':
            m_columnVisible[ColFilterB] = true;
            m_columnComboOptions[ColFilterB] = m_filters;
            m_visibleColumns++;
            break;
        case 'i':
            m_columnVisible[ColData] = true;
            m_columnComboOptions[ColData] = m_dataModes;
            m_visibleColumns++;
            break;
        case 'I':
            m_columnVisible[ColDataB] = true;
            m_columnComboOptions[ColDataB] = m_dataModes;
            m_visibleColumns++;
            break;
        case 'j':
            m_columnVisible[ColDuplex] = true;
            m_columnComboOptions[ColDuplex] = m_duplexModes;
            m_columnVisible[ColToneMode] = true;
            m_columnComboOptions[ColToneMode] = m_toneModes;
            m_visibleColumns += 2;
            break;
        case 'J':
            m_columnVisible[ColDuplexB] = true;
            m_columnComboOptions[ColDuplexB] = m_duplexModes;
            m_columnVisible[ColToneModeB] = true;
            m_columnComboOptions[ColToneModeB] = m_toneModes;
            m_visibleColumns += 2;
            break;
        case 'k':
            m_columnVisible[ColData] = true;
            m_columnComboOptions[ColData] = m_dataModes;
            m_columnVisible[ColToneMode] = true;
            m_columnComboOptions[ColToneMode] = m_toneModes;
            m_visibleColumns += 2;
            break;
        case 'K':
            m_columnVisible[ColDataB] = true;
            m_columnComboOptions[ColDataB] = m_dataModes;
            m_columnVisible[ColToneModeB] = true;
            m_columnComboOptions[ColToneModeB] = m_toneModes;
            m_visibleColumns += 2;
            break;
        case 'l':
            m_columnVisible[ColToneMode] = true;
            m_columnComboOptions[ColToneMode] = m_toneModes;
            m_visibleColumns++;
            break;
        case 'L':
            m_columnVisible[ColToneModeB] = true;
            m_columnComboOptions[ColToneModeB] = m_toneModes;
            m_visibleColumns++;
            break;
        case 'm':
            m_columnVisible[ColDSQL] = true;
            m_columnComboOptions[ColDSQL] = m_dsql;
            m_visibleColumns++;
            break;
        case 'M':
            m_columnVisible[ColDSQLB] = true;
            m_columnComboOptions[ColDSQLB] = m_dsql;
            m_visibleColumns++;
            break;
        case 'n':
            m_columnVisible[ColTone] = true;
            m_columnComboOptions[ColTone] = m_tones;
            m_visibleColumns++;
            break;
        case 'N':
            m_columnVisible[ColToneB] = true;
            m_columnComboOptions[ColToneB] = m_tones;
            m_visibleColumns++;
            break;
        case 'o':
            m_columnVisible[ColTSQL] = true;
            m_columnComboOptions[ColTSQL] = m_tones;
            m_visibleColumns++;
            break;
        case 'O':
            m_columnVisible[ColTSQLB] = true;
            m_columnComboOptions[ColTSQLB] = m_tones;
            m_visibleColumns++;
            break;
        case 'p':
            m_columnVisible[ColDTCSPolarity] = true;
            m_columnComboOptions[ColDTCSPolarity] = m_dtcsp;
            m_visibleColumns++;
            break;
        case 'P':
            m_columnVisible[ColDTCSPolarityB] = true;
            m_columnComboOptions[ColDTCSPolarityB] = m_dtcsp;
            m_visibleColumns++;
            break;
        case 'q':
            m_columnVisible[ColDTCS] = true;
            m_columnComboOptions[ColDTCS] = m_dtcs;
            m_visibleColumns++;
            break;
        case 'Q':
            m_columnVisible[ColDTCSB] = true;
            m_columnComboOptions[ColDTCSB] = m_dtcs;
            m_visibleColumns++;
            break;
        case 'r':
            m_columnVisible[ColDVSquelch] = true;
            m_columnComboOptions[ColDVSquelch] = m_dvsql;
            m_visibleColumns++;
            break;
        case 'R':
            m_columnVisible[ColDVSquelchB] = true;
            m_columnComboOptions[ColDVSquelchB] = m_dvsql;
            m_visibleColumns++;
            break;
        case 's':
            m_columnVisible[ColOffset] = true;
            m_columnInputMasks[ColOffset] = "00.000000";
            m_visibleColumns++;
            break;
        case 'S':
            m_columnVisible[ColOffsetB] = true;
            m_columnInputMasks[ColOffsetB] = "00.000000";
            m_visibleColumns++;
            break;
        case 't':
            m_columnVisible[ColUR] = true;
            m_columnInputMasks[ColUR] = ">xxxxxxxx;_";
            m_visibleColumns++;
            break;
        case 'T':
            m_columnVisible[ColURB] = true;
            m_columnInputMasks[ColURB] = ">xxxxxxxx;_";
            m_visibleColumns++;
            break;
        case 'u':
            m_columnVisible[ColR1] = true;
            m_columnInputMasks[ColR1] = ">xxxxxxxx;_";
            m_visibleColumns++;
            break;
        case 'U':
            m_columnVisible[ColR1B] = true;
            m_columnInputMasks[ColR1B] = ">xxxxxxxx;_";
            m_visibleColumns++;
            break;
        case 'v':
            m_columnVisible[ColR2] = true;
            m_columnInputMasks[ColR2] = ">xxxxxxxx;_";
            m_visibleColumns++;
            break;
        case 'V':
            m_columnVisible[ColR2B] = true;
            m_columnInputMasks[ColR2B] = ">xxxxxxxx;_";
            m_visibleColumns++;
            break;
        case 'W':
            m_columnVisible[ColClarOffset] = true;
            m_columnInputMasks[ColClarOffset] = "x00000";
            m_visibleColumns++;
            break;
        case 'X':
            m_columnVisible[ColRXClar] = true;
            m_columnComboOptions[ColRXClar] = m_clar;
            m_visibleColumns++;
            break;
        case 'Y':
            m_columnVisible[ColTXClar] = true;
            m_columnComboOptions[ColTXClar] = m_clar;
            m_visibleColumns++;
            break;
        case 'w':
            m_columnVisible[ColTuningStep] = true;
            m_columnComboOptions[ColTuningStep] = m_tuningSteps;
            m_columnVisible[ColCustomTuningStep] = true;
            m_columnInputMasks[ColCustomTuningStep] = "0000";
            m_visibleColumns += 2;
            break;
        case 'x':
            m_columnVisible[ColAttenuator] = true;
            m_columnComboOptions[ColAttenuator] = m_attenuators;
            m_columnVisible[ColPreamplifier] = true;
            m_columnComboOptions[ColPreamplifier] = m_preamps;
            m_visibleColumns += 2;
            break;
        case 'y':
            m_columnVisible[ColAntenna] = true;
            m_columnComboOptions[ColAntenna] = m_antennas;
            m_visibleColumns++;
            break;
        case '+':
            m_columnVisible[ColIPPlus] = true;
            m_columnComboOptions[ColIPPlus] = m_ipplus;
            m_visibleColumns++;
            break;
        case 'z':
            m_columnVisible[ColName] = true;
            m_columnInputMasks[ColName] = QString("%1;_").arg("", parse.len, 'x');
            m_visibleColumns++;
            break;
        case 'Z':
            m_extendedView = true;
            m_columnVisible[ColToneMode] = true;
            m_columnComboOptions[ColToneMode] = m_toneModes;
            m_columnVisible[ColTSQL] = true;
            m_columnComboOptions[ColTSQL] = m_tones;
            m_columnVisible[ColDTCS] = true;
            m_columnComboOptions[ColDTCS] = m_dtcs;
            m_columnVisible[ColDTCSPolarity] = true;
            m_columnComboOptions[ColDTCSPolarity] = m_dtcsp;
            m_columnVisible[ColDSQL] = true;
            m_columnComboOptions[ColDSQL] = m_dsql;
            m_columnVisible[ColDVSquelch] = true;
            m_columnComboOptions[ColDVSquelch] = m_dvsql;

            // P25
            m_columnVisible[ColP25Sql] = true;
            m_columnComboOptions[ColP25Sql] = m_p25Sql;
            m_columnVisible[ColP25Nac] = true;
            m_columnInputMasks[ColP25Nac] = "HHH";

            // dPMR
            m_columnVisible[ColDPmrSql] = true;
            m_columnComboOptions[ColDPmrSql] = m_dPmrSql;
            m_columnVisible[ColDPmrComid] = true;
            m_columnInputMasks[ColDPmrComid] = "999";
            m_columnVisible[ColDPmrCc] = true;
            m_columnInputMasks[ColDPmrCc] = "99";
            m_columnVisible[ColDPmrSCRM] = true;
            m_columnComboOptions[ColDPmrSCRM] = m_dPmrSCRM;
            m_columnVisible[ColDPmrKey] = true;
            m_columnInputMasks[ColDPmrKey] = "99999";

            // NXDN
            m_columnVisible[ColNxdnSql] = true;
            m_columnComboOptions[ColNxdnSql] = m_nxdnSql;
            m_columnVisible[ColNxdnRan] = true;
            m_columnInputMasks[ColNxdnRan] = "99";
            m_columnVisible[ColNxdnEnc] = true;
            m_columnComboOptions[ColNxdnEnc] = m_nxdnEnc;
            m_columnVisible[ColNxdnKey] = true;
            m_columnInputMasks[ColNxdnKey] = "99999";

            // DCR
            m_columnVisible[ColDcrSql] = true;
            m_columnComboOptions[ColDcrSql] = m_dcrSql;
            m_columnVisible[ColDcrUc] = true;
            m_columnInputMasks[ColDcrUc] = "999";
            m_columnVisible[ColDcrEnc] = true;
            m_columnComboOptions[ColDcrEnc] = m_dcrEnc;
            m_columnVisible[ColDcrKey] = true;
            m_columnInputMasks[ColDcrKey] = "99999";
            break;
        default:
            break;
        }
    }

    // Yaesu description is separate
    if ((m_rigCaps->commands.contains(funcMemoryTag) || m_rigCaps->commands.contains(funcMemoryTagB))
        && !m_columnVisible[ColName]) {
        m_columnVisible[ColName] = true;
        m_columnInputMasks[ColName] = QString("%1;_").arg("", 12, 'x');
        m_visibleColumns++;
    }

    // Yaesu split is separate
    if (m_rigCaps->commands.contains(funcSplitMemory) && !m_columnVisible[ColFrequencyB]) {
        m_columnVisible[ColFrequencyB] = true;
        m_columnInputMasks[ColFrequencyB] = "00000.00000";
        m_visibleColumns++;
    }

    //qDebug() << "After parsing, visibleColumns:" << m_visibleColumns;
    //qDebug() << "ColNum visible:" << m_columnVisible[ColNum];
    //qDebug() << "ColFrequency visible:" << m_columnVisible[ColFrequency];

    emit visibleColumnsChanged();
    emit layoutChanged();
}

bool MemoriesModel::isRowSaving(int row) const
{
    bool saving = m_savingRows.contains(row);
    if (saving) {
        qDebug() << "Row" << row << "is saving";
    }
    return saving;
}

void MemoriesModel::writeMemoryToRadio(int row)
{
    if (row < 0 || row >= m_memories.size())
        return;

    qDebug() << "writeMemoryToRadio - marking row" << row << "as saving";

    const MemoryRow& mem = m_memories[row];

    memoryType currentMemory = mem.data;
    currentMemory.channel = mem.memoryNumber;
    currentMemory.group = m_currentGroup;

    if (m_currentGroup == MEMORY_SATGROUP) {
        currentMemory.sat = true;
    }

    // Mark as saving
    m_savingRows.insert(row);
    qDebug() << "Saving rows now contains:" << m_savingRows;
    emit rowSavingChanged(row, true);
    emit dataChanged(index(row, 0), index(row, TotalColumns - 1));
    m_queue->add(priorityImmediate, queueItem(
                                        (currentMemory.sat ? funcSatelliteMemory : m_writeCommand),
                                        QVariant::fromValue<memoryType>(currentMemory)));

    // Add tag if supported
    if (m_rigCaps->commands.contains(funcMemoryTag) || m_rigCaps->commands.contains(funcMemoryTagB)) {
        memoryTagType tag;
        tag.num = currentMemory.channel;
        tag.name = QString(currentMemory.name);
        tag.en = 1;
        m_queue->add(priorityImmediate, queueItem(
                                          m_rigCaps->commands.contains(funcMemoryTagB) ? funcMemoryTagB : funcMemoryTag,
                                          QVariant::fromValue<memoryTagType>(tag)));
    }

    // Read back (will clear saving state when received)
    m_queue->add(priorityHighest, queueItem(funcMemoryContents,
                                         QVariant::fromValue<uint>((currentMemory.group << 16) | (currentMemory.channel & 0xffff))));
}

bool MemoriesModel::checkASCII(const QString &str) const
{
    static QRegularExpression exp = QRegularExpression(QStringLiteral("[^\\x{0020}-\\x{007E}]"));
    bool containsNonASCII = str.contains(exp);
    return !containsNonASCII;
}

void MemoriesModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void MemoriesModel::setProgress(int progress)
{
    if (m_progress == progress)
        return;
    m_progress = progress;
    emit progressChanged();
}

void MemoriesModel::setStatusText(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

int MemoriesModel::findRowByMemoryNumber(int memNum) const
{
    for (int i = 0; i < m_memories.size(); i++) {
        if (m_memories[i].memoryNumber == memNum)
            return i;
    }
    return -1;
}

void MemoriesModel::loadNextMemory()
{
    qInfo() << "Requesting memory: " << (m_lastMemoryRequested & 0xffff);
    if (m_currentGroup == MEMORY_SATGROUP) {
        m_queue->add(priorityImmediate, queueItem(funcSatelliteMemory,
                                                  QVariant::fromValue<ushort>(m_lastMemoryRequested & 0xffff)));
    } else {
        m_queue->add(priorityImmediate, queueItem(funcMemoryContents,
                                                  QVariant::fromValue<uint>(m_lastMemoryRequested & 0xffff)));

        if (m_rigCaps->commands.contains(funcMemoryTag)) {
            m_queue->add(priorityImmediate, queueItem(funcMemoryTag,
                                                      QVariant::fromValue<uint>(m_lastMemoryRequested & 0xffff)));
        }
        if (m_rigCaps->commands.contains(funcMemoryTagB)) {
            m_queue->add(priorityImmediate, queueItem(funcMemoryTagB,
                                                      QVariant::fromValue<uint>(m_lastMemoryRequested & 0xffff)));
        }
        if (m_rigCaps->commands.contains(funcSplitMemory)) {
            m_queue->add(priorityImmediate, queueItem(funcSplitMemory,
                                                      QVariant::fromValue<uint>(m_lastMemoryRequested & 0xffff)));
        }
    }
}

// Slots
void MemoriesModel::receiveMemory(memoryType mem)
{
    /*qDebug() << "receiveMemory() called - channel:" << mem.channel
             << "freq:" << mem.frequency.Hz
             << "mode:" << mem.mode;*/
    setProgress(mem.channel & 0xffff);

    // Request next memory if needed
    if ((m_lastMemoryRequested & 0xffff) < m_groupMemories && m_startup) {
        m_lastMemoryRequested++;
        loadNextMemory();
        m_timeoutTimer.start(MEMORY_TIMEOUT);
    } else if (mem.channel == m_groupMemories) {
        m_timeoutTimer.stop();
        setLoading(false);
        m_startup = false;
    }

    m_timeoutCount = 0;

    // Find or create row
    int row = findRowByMemoryNumber(mem.channel);

    if (mem.scan < 4) {
        if (row == -1) {
            beginInsertRows(QModelIndex(), m_memories.size(), m_memories.size());
            MemoryRow newRow;
            newRow.memoryNumber = mem.channel & 0xffff;
            newRow.data = mem;
            newRow.valid = true;
            m_memories.append(newRow);
            endInsertRows();
            row = m_memories.size() - 1;
        } else {
            m_memories[row].data = mem;
            m_memories[row].valid = true;
            emit dataChanged(index(row, 0), index(row, TotalColumns - 1));
        }

        // Clear saving state for this row
        if (m_savingRows.remove(row)) {
            qDebug() << "Removed row" << row << "from saving state";
            // Row was being saved, emit dataChanged to update UI
            emit rowSavingChanged(row, false);  // ADD THIS
            emit dataChanged(index(row, 0), index(row, TotalColumns - 1));
        }
    }
}

void MemoriesModel::receiveMemoryName(memoryTagType tag)
{
    //qDebug() << "Received memory tag id:" << tag.num << "name:" << tag.name;

    int row = findRowByMemoryNumber(tag.num);
    if (row != -1 && checkASCII(tag.name)) {
        strncpy(m_memories[row].data.name, tag.name.toStdString().c_str(), 16);
        emit dataChanged(index(row, ColName), index(row, ColName));
    }
}

void MemoriesModel::receiveMemorySplit(memorySplitType s)
{
    //qDebug() << "Received memory split id:" << s.num << "freq:" << s.freq;

    int row = findRowByMemoryNumber(s.num);
    if (row != -1 && s.freq > 0) {
        m_memories[row].data.frequencyB.Hz = s.freq;
        emit dataChanged(index(row, ColFrequencyB), index(row, ColFrequencyB));
    }
}

void MemoriesModel::onTimeout()
{
    if (m_rigCaps->manufacturer == manufYaesu) {
        m_timeoutTimer.stop();
        setLoading(false);
        m_startup = false;
    } else {
        if (m_timeoutCount < 3) {
            qInfo() << "Timeout receiving memory:" << (m_lastMemoryRequested & 0xffff);
            loadNextMemory();
            m_timeoutTimer.start(MEMORY_TIMEOUT);
            m_timeoutCount++;
        } else {
            m_timeoutCount = 0;
            m_timeoutTimer.stop();
            setLoading(false);
            setStatusText("Timeout receiving memories");
        }
    }
}

void MemoriesModel::configureColumnsForMode(int row, modeInfo mode, quint8 split)
{
    // Enable/disable columns based on mode
    // This would set item flags per cell if needed
    // For now, just emit data changed
    emit dataChanged(index(row, 0), index(row, TotalColumns - 1));
}

void MemoriesModel::configureColumnsBForMode(int row, modeInfo mode)
{
    emit dataChanged(index(row, 0), index(row, TotalColumns - 1));
}

bool MemoriesModel::readCSVRow(QTextStream &in, QStringList *row)
{
    static const int delta[][5] = {
                                   {   1,   2,  -1,   0,  -1  },
                                   {   1,   2,  -1,   0,  -1  },
                                   {   3,   4,   3,   3,  -2  },
                                   {   3,   4,   3,   3,  -2  },
                                   {   1,   3,  -1,   0,  -1  },
                                   };

    row->clear();

    if (in.atEnd())
        return false;

    int state = 0, t;
    QChar ch;
    QString cell;

    while (state >= 0) {
        if (in.atEnd())
            t = 4;
        else {
            in >> ch;
            if (ch == ',') t = 0;
            else if (ch == '\"') t = 1;
            else if (ch == '\n') t = 2;
            else t = 3;
        }

        state = delta[state][t];

        switch (state) {
        case 0:
        case 3:
            cell += ch;
            break;
        case -1:
        case 1:
            row->append(cell);
            cell = "";
            break;
        }
    }

    if (state == -2) {
        qInfo() << "End-of-file found while inside quotes.";
        return false;
    }
    return true;
}
