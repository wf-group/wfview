#include "rigctld.h"
#include "logcategories.h"
#include "rigctlcompat.h"

#include <QDateTime>
#include <QThread>


static struct
{
    quint64 mode;
    rigMode_t mk;
    const char* str;
    uchar data=0;
} mode_str[] =
{
    { RIG_MODE_AM, modeAM, "AM" },
    { RIG_MODE_CW, modeCW,  "CW" },
    { RIG_MODE_USB, modeUSB, "USB" },
    { RIG_MODE_LSB, modeLSB, "LSB" },
    { RIG_MODE_RTTY, modeRTTY, "RTTY" },
    { RIG_MODE_FM, modeFM, "FM" },
    { RIG_MODE_WFM, modeWFM, "WFM" },
    { RIG_MODE_CWR, modeCW_R, "CWR" },
    { RIG_MODE_RTTYR, modeRTTY_R, "RTTYR" },
    { RIG_MODE_AMS, modeUnknown, "AMS" },
    { RIG_MODE_PKTLSB, modeLSB, "PKTLSB", 1U},
    { RIG_MODE_PKTUSB, modeUSB, "PKTUSB" , 1U},
    { RIG_MODE_PKTLSB, modeLSB, "LSB-D", 1U},
    { RIG_MODE_PKTLSB, modeUSB, "USB-D", 1U},
    { RIG_MODE_PKTFM,  modeFM, "PKTFM", true},
    { RIG_MODE_PKTFMN, modeUnknown, "PKTFMN", 1U},
    { RIG_MODE_ECSSUSB, modeUnknown, "ECSSUSB" },
    { RIG_MODE_ECSSLSB, modeUnknown, "ECSSLSB" },
    { RIG_MODE_FAX, modeUnknown, "FAX" },
    { RIG_MODE_SAM, modeUnknown, "SAM" },
    { RIG_MODE_SAL, modeUnknown, "SAL" },
    { RIG_MODE_SAH, modeUnknown, "SAH" },
    { RIG_MODE_DSB, modeUnknown, "DSB"},
    { RIG_MODE_FMN, modeUnknown, "FMN" },
    { RIG_MODE_PKTAM, modeUnknown, "PKTAM"},
    { RIG_MODE_P25, modeP25, "P25"},
    { RIG_MODE_DSTAR, modeDV, "D-STAR"},
    { RIG_MODE_DPMR, modedPMR, "DPMR"},
    { RIG_MODE_NXDNVN, modeNXDN_VN, "NXDN-VN"},
    { RIG_MODE_NXDN_N, modeNXDN_N, "NXDN-N"},
    { RIG_MODE_DCR, modeUnknown, "DCR"},
    { RIG_MODE_AMN, modeUnknown, "AMN"},
    { RIG_MODE_PSK, modePSK, "PSK"},
    { RIG_MODE_PSKR, modePSK_R, "PSKR"},
    { RIG_MODE_C4FM, modeUnknown, "C4FM"},
    { RIG_MODE_SPEC, modeUnknown, "SPEC"},
    { RIG_MODE_NONE, modeUnknown, "" },
};


static const subCommandStruct levels_str[] =
{
    {"PREAMP",funcPreamp,typeUChar},
    {"ATT",funcAttenuator,typeUChar},
    {"VOXDELAY",funcVOXDelay,typeUChar},
    {"AF",funcAfGain,typeFloat},
    {"RF",funcRfGain,typeFloat},
    {"SQL",funcSquelch,typeFloat},
    {"IF",funcFilterWidth,typeFloat},
    {"APF",funcAPFLevel,typeFloat},
    {"NR",funcNRLevel,typeFloat},
    {"PBT_IN",funcPBTInner,typeFloat},
    {"PBT_OUT",funcPBTOuter,typeFloat},
    {"CWPITCH",funcCwPitch,typeUShort},
    {"RFPOWER",funcRFPower,typeFloat},
    {"MICGAIN",funcMicGain,typeFloat},
    {"KEYSPD",funcKeySpeed,typeUShort},
    {"NOTCHF",funcNotchFilter,typeUChar},
    {"COMP",funcCompressorLevel,typeFloat},
    {"AGC",funcAGCTimeConstant,typeUChar},
    {"BKINDL",funcBreakInDelay,typeFloat},
    {"BAL",funcNone,typeFloat},
    {"METER",funcNone,typeFloat},
    {"VOXGAIN",funcVoxGain,typeFloat},
    {"ANTIVOX",funcAntiVoxGain,typeFloat},
    {"SLOPE_LOW",funcNone,typeFloat},
    {"SLOPE_HIGH",funcNone,typeFloat},
    {"BKIN_DLYMS",funcBreakInDelay,typeFloat},
    {"RAWSTR",funcNone,typeFloat},
    {"_RESERVED27",funcNone,typeNone},
    {"SWR",funcSWRMeter,typeSWR},
    {"ALC",funcALCMeter,typeDouble},
    {"STRENGTH",funcSMeter,typeDouble},
    {"_RESERVED31",funcNone,typeNone},
    {"RFPOWER_METER",funcPowerMeter,typeDouble},
    {"COMP_METER",funcCompMeter,typeDouble},
    {"VD_METER",funcVdMeter,typeDouble},
    {"ID_METER",funcIdMeter,typeDouble},
    {"NOTCHF_RAW",funcNone,typeFloat},
    {"MONITOR_GAIN",funcMonitorGain,typeFloat},
    {"NQ",funcNone,typeFloat},
    {"RFPOWER_METER_WATTS",funcNone,typeFloat},
    {"SPECTRUM_MODE",funcNone,typeFloat},
    {"SPECTRUM_SPAN",funcNone,typeFloat},
    {"SPECTRUM_EDGE_LOW",funcNone,typeFloat},
    {"SPECTRUM_EDGE_HIGH",funcNone,typeFloat},
    {"SPECTRUM_SPEED",funcNone,typeFloat},
    {"SPECTRUM_REF",funcNone,typeFloat},
    {"SPECTRUM_AVG",funcNone,typeFloat},
    {"SPECTRUM_ATT",funcNone,typeFloat},
    {"TEMP_METER",funcNone,typeFloat},
    {"BAND_SELECT",funcNone,typeFloat},
    {"USB_AF",funcNone,typeFloat},
    {"",funcNone,'\x0'}
};

static const subCommandStruct functions_str[] =
{
    {"FAGC",funcNone,typeBinary},
    {"NB",funcNoiseBlanker,typeBinary},
    {"COMP",funcCompressor,typeBinary},
    {"VOX",funcVox,typeBinary},
    {"TONE",funcRepeaterTone,typeBinary},
    {"TQSL",funcRepeaterTSQL,typeBinary},
    {"SBKIN",funcBreakIn,typeUChar},
    {"FBKIN",funcBreakIn,typeUChar},
    {"ANF",funcAutoNotch,typeBinary},
    {"NR",funcNoiseReduction,typeBinary},
    {"AIP",funcNone,typeBinary},
    {"APF",funcAPFType,typeBinary},
    {"MON",funcMonitor,typeBinary},
    {"MN",funcManualNotch,typeBinary},
    {"RF",funcNone,typeBinary},
    {"ARO",funcNone,typeBinary},
    {"LOCK",funcLockFunction,typeBinary},
    {"MUTE",funcAFMute,typeBinary},
    {"VSC",funcNone,typeBinary},
    {"REV",funcNone,typeBinary},
    {"SQL",funcSquelch,typeBinary}, // Actually an integer as ICOM doesn't provide on/off for squelch
    {"ABM",funcNone,typeBinary},
    {"BC",funcNone,typeBinary},
    {"MBC",funcNone,typeBinary},
    {"RIT",funcRitStatus,typeBinary},
    {"AFC",funcNone,typeBinary},
    {"SATMODE",funcSatelliteMode,typeBinary},
    {"SCOPE",funcScopeOnOff,typeBinary},
    {"RESUME",funcNone,typeBinary},
    {"TBURST",funcNone,typeBinary},
    {"TUNER",funcTunerStatus,typeBinary},
    {"XIT",funcNone,typeBinary},
    {"NB2",funcNone,typeBinary},
    {"CSQL",funcNone,typeBinary},
    {"AFLT",funcNone,typeBinary},
    {"ANL",funcNone,typeBinary},
    {"BC2",funcNone,typeBinary},
    {"DUAL_WATCH",funcVFODualWatch,typeBinary},
    {"DIVERSITY",funcNone,typeBinary},
    {"DSQL",funcNone,typeBinary},
    {"SCEN",funcNone,typeBinary},
    {"SLICE",funcNone,typeBinary},
    {"TRANSCEIVE",funcCIVTransceive,typeBinary},
    {"SPECTRUM",funcScopeOnOff,typeBinary},
    {"SPECTRUM_HOLD",funcNone,typeBinary},
    {"SEND_MORSE",funcSendCW,typeBinary},
    {"SEND_VOICE_MEM",funcVoiceTX,typeBinary},
    {"OVF_STATUS",funcOverflowStatus,typeBinary},
    {"SYNC",funcNone,typeBinary},
    {"",funcNone,typeBinary}
};

static const subCommandStruct params_str[] =
{
    {"ANN",funcNone,typeUChar},
    {"APO",funcNone,typeUChar},
    {"BACKLIGHT",funcBackLightLevel,typeUChar},
    {"_RESERVED3",funcNone,typeNone},
    {"BEEP",funcNone,typeUChar},
    {"TIME",funcTime,typeUChar},
    {"BAT",funcNone,typeUChar},
    {"KEYLIGHT",funcNone,typeUChar},
    {"",funcNone,typeNone}
};

#define ARG_IN1  0x01
#define ARG_OUT1 0x02
#define ARG_IN2  0x04
#define ARG_OUT2 0x08
#define ARG_IN3  0x10
#define ARG_OUT3 0x20
#define ARG_IN4  0x40
#define ARG_OUT4 0x80
#define ARG_OUT5 0x100
#define ARG_NONE    0
#define ARG_IN  (ARG_IN1|ARG_IN2|ARG_IN3|ARG_IN4)
#define ARG_OUT  (ARG_OUT1|ARG_OUT2|ARG_OUT3|ARG_OUT4)
#define ARG_IN_LINE 0x4000
#define ARG_NOVFO 0x8000
#define MAXNAMESIZE 32


static const commandStruct commands_list[] =
{
    { 'F',  "set_freq",         funcFreqSet,       typeFreq,    ARG_IN, "Frequency" },
    { 'f',  "get_freq",         funcFreqGet,       typeFreq,    ARG_OUT, "Frequency" },
    { 'M',  "set_mode",         funcModeSet,            typeMode,     ARG_IN, "Mode", "Passband" },
    { 'm',  "get_mode",         funcModeGet,            typeMode,     ARG_OUT, "Mode", "Passband" },
    { 'I',  "set_split_freq",   funcFreqSet,     typeFreq,     ARG_IN, "TX Frequency" },
    { 'i',  "get_split_freq",   funcFreqGet,     typeFreq,     ARG_OUT, "TX Frequency" },
    { 'X',  "set_split_mode",   funcSplitStatus,        typeMode,     ARG_IN, "TX Mode", "TX Passband" },
    { 'x',  "get_split_mode",   funcSplitStatus,        typeMode,     ARG_OUT, "TX Mode", "TX Passband" },
    { 'K',  "set_split_freq_mode",  funcModeSet,           typeFreq,     ARG_IN,    "TX Frequency", "TX Mode", "TX Passband" },
    { 'k',  "get_split_freq_mode",  funcModeGet,           typeFreq,     ARG_OUT,   "TX Frequency", "TX Mode", "TX Passband" },
    { 'S',  "set_split_vfo",    funcSplitStatus,        typeSplitVFO, ARG_IN, "Split", "TX VFO" },
    { 's',  "get_split_vfo",    funcSplitStatus,        typeSplitVFO, ARG_OUT, "Split", "TX VFO" },
    { 'N',  "set_ts",           funcTuningStep,         typeUChar,    ARG_IN, "Tuning Step" },
    { 'n',  "get_ts",           funcTuningStep,         typeUChar,    ARG_OUT, "Tuning Step" },
    { 'L',  "set_level",        funcRigctlLevel,        typeLevel,    ARG_IN, "Level", "Level Value" },
    { 'l',  "get_level",        funcRigctlLevel,        typeLevel,    ARG_IN1 | ARG_OUT2, "Level", "Level Value" },
    { 'U',  "set_func",         funcRigctlFunction,     typeLevel,    ARG_IN, "Func", "Func Status" },
    { 'u',  "get_func",         funcRigctlFunction,     typeLevel,    ARG_IN1 | ARG_OUT2, "Func", "Func Status" },
    { 'P',  "set_parm",         funcRigctlParam,        typeLevel,    ARG_IN1 | ARG_NOVFO, "Parm", "Parm Value" },
    { 'p',  "get_parm",         funcRigctlParam,        typeLevel,    ARG_IN1 | ARG_OUT2, "Parm", "Parm Value" },
    { 'G',  "vfo_op",           funcSelectVFO,          typeUChar,    ARG_IN, "Mem/VFO Op" },
    { 'g',  "scan",             funcScanning,           typeUChar,    ARG_IN, "Scan Fct", "Scan Channel" },
    { 'A',  "set_trn",          funcCIVTransceive,      typeUChar,    ARG_IN  | ARG_NOVFO, "Transceive" },
    { 'a',  "get_trn",          funcCIVTransceive,      typeUChar,    ARG_OUT | ARG_NOVFO, "Transceive" },
    { 'R',  "set_rptr_shift",   funcSendFreqOffset,     typeUChar,    ARG_IN, "Rptr Shift" },
    { 'r',  "get_rptr_shift",   funcReadFreqOffset,     typeUChar,    ARG_OUT, "Rptr Shift" },
    { 'O',  "set_rptr_offs",    funcSendFreqOffset,     typeUChar,    ARG_IN, "Rptr Offset" },
    { 'o',  "get_rptr_offs",    funcReadFreqOffset,     typeUChar,    ARG_OUT, "Rptr Offset" },
    { 'C',  "set_ctcss_tone",   funcToneFreq,           typeUChar,    ARG_IN, "CTCSS Tone" },
    { 'c',  "get_ctcss_tone",   funcToneFreq,           typeUChar,    ARG_OUT, "CTCSS Tone" },
    { 'D',  "set_dcs_code",     funcDTCSCode,           typeUChar,    ARG_IN, "DCS Code" },
    { 'd',  "get_dcs_code",     funcDTCSCode,           typeUChar,    ARG_OUT, "DCS Code" },
    { 0x90, "set_ctcss_sql",    funcTSQLFreq,           typeUChar,    ARG_IN, "CTCSS Sql" },
    { 0x91, "get_ctcss_sql",    funcTSQLFreq,           typeUChar,    ARG_OUT, "CTCSS Sql" },
    { 0x92, "set_dcs_sql",      funcCSQLCode,           typeUChar,    ARG_IN, "DCS Sql" },
    { 0x93, "get_dcs_sql",      funcCSQLCode,           typeUChar,    ARG_OUT, "DCS Sql" },
    { 'V',  "set_vfo",          funcSelectVFO,          typeVFO,    ARG_IN  | ARG_NOVFO, "VFO" },
    { 'v',  "get_vfo",          funcSelectVFO,          typeVFO,    ARG_NOVFO | ARG_OUT, "VFO" },
    { 'T',  "set_ptt",          funcTransceiverStatus,  typeBinary,    ARG_IN, "VFO", "PTT" },
    { 't',  "get_ptt",          funcTransceiverStatus,  typeBinary,    ARG_OUT, "VFO", "PTT" },
    { 'E',  "set_mem",          funcMemoryContents,     typeMode,    ARG_IN, "Memory#" },
    { 'e',  "get_mem",          funcMemoryContents,     typeMode,    ARG_OUT, "Memory#" },
    { 'H',  "set_channel",      funcMemoryMode,         typeUChar,    ARG_IN  | ARG_NOVFO, "Channel"},
    { 'h',  "get_channel",      funcMemoryMode,         typeUChar,    ARG_IN  | ARG_NOVFO, "Channel", "Read Only" },
    { 'B',  "set_bank",         funcMemoryGroup,        typeUChar,    ARG_IN, "Bank" },
    { '_',  "get_info",         funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO, "Info" },
    { 'J',  "set_rit",          funcRitFreq,            typeShort,    ARG_IN, "RIT" },
    { 'j',  "get_rit",          funcRitFreq,            typeShort,    ARG_OUT, "RIT" },
    { 'Z',  "set_xit",          funcXitFreq,            typeShort,    ARG_IN, "XIT" },
    { 'z',  "get_xit",          funcXitFreq,            typeShort,    ARG_OUT, "XIT" },
    { 'Y',  "set_ant",          funcAntenna,            typeUChar,    ARG_IN, "Antenna", "Option" },
    { 'y',  "get_ant",          funcAntenna,            typeUChar,    ARG_IN1 | ARG_OUT2 | ARG_NOVFO, "AntCurr", "Option", "AntTx", "AntRx" },
    { 0x87, "set_powerstat",    funcPowerControl,       typeBinary,    ARG_IN  | ARG_NOVFO, "Power Status" },
    { 0x88, "get_powerstat",    funcPowerControl,       typeBinary,    ARG_OUT | ARG_NOVFO, "Power Status" },
    { 0x89, "send_dtmf",        funcNone,               typeUChar,    ARG_IN, "Digits" },
    { 0x8a, "recv_dtmf",        funcNone,               typeUChar,    ARG_OUT, "Digits" },
    { '*',  "reset",            funcNone,               typeUChar,    ARG_IN, "Reset" },
    { 'w',  "send_cmd",         funcNone,               typeUChar,    ARG_IN1 | ARG_IN_LINE | ARG_OUT2 | ARG_NOVFO, "Cmd", "Reply" },
    { 'W',  "send_cmd_rx",      funcNone,               typeUChar,    ARG_IN | ARG_OUT2 | ARG_NOVFO, "Cmd", "Reply"},
    { 'b',  "send_morse",       funcSendCW,             typeString,   ARG_IN | ARG_NOVFO  | ARG_IN_LINE, "Morse" },
    { 0xbb, "stop_morse",       funcSendCW,             typeString,   },
    { 0xbc, "wait_morse",       funcSendCW,             typeUChar,    },
    { 0x94, "send_voice_mem",   funcVoiceTX,            typeUChar,    ARG_IN, "Voice Mem#" },
    { 0x8b, "get_dcd",          funcSMeterSqlStatus,    typeBinary,   ARG_OUT, "DCD" },
    { 0x8d, "set_twiddle",      funcNone,               typeUChar,    ARG_IN  | ARG_NOVFO, "Timeout (secs)" },
    { 0x8e, "get_twiddle",      funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO, "Timeout (secs)" },
    { 0x97, "uplink",           funcNone,               typeUChar,    ARG_IN | ARG_NOVFO, "1=Sub, 2=Main" },
    { 0x95, "set_cache",        funcNone,               typeUChar,    ARG_IN | ARG_NOVFO, "Timeout (msecs)" },
    { 0x96, "get_cache",        funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO, "Timeout (msecs)" },
    { '2',  "power2mW",         funcNone,               typeUChar,    ARG_IN1 | ARG_IN2 | ARG_IN3 | ARG_OUT1 | ARG_NOVFO, "Power [0.0..1.0]", "Frequency", "Mode", "Power mW" },
    { '4',  "mW2power",         funcNone,               typeUChar,    ARG_IN1 | ARG_IN2 | ARG_IN3 | ARG_OUT1 | ARG_NOVFO, "Pwr mW", "Freq", "Mode", "Power [0.0..1.0]" },
    { '1',  "dump_caps",        funcNone,               typeUChar,    ARG_NOVFO },
    { '3',  "dump_conf",        funcNone,               typeUChar,    ARG_NOVFO },
    { 0x8f, "dump_state",       funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO },
    { 0xf0, "chk_vfo",          funcNone,               typeUChar,    ARG_NOVFO, "ChkVFO" },   /* rigctld only--check for VFO mode */
    { 0xf2, "set_vfo_opt",      funcNone,               typeUChar,    ARG_NOVFO | ARG_IN, "Status" }, /* turn vfo option on/off */
    { 0xf3, "get_vfo_info",     funcSelectVFO,          typeVFOInfo,    ARG_IN1 | ARG_NOVFO | ARG_OUT5, "VFO", "Freq", "Mode", "Width", "Split", "SatMode" }, /* get several vfo parameters at once */
    { 0xf5, "get_rig_info",     funcNone,               typeUChar,    ARG_NOVFO | ARG_OUT, "RigInfo" }, /* get several vfo parameters at once */
    { 0xf4, "get_vfo_list",     funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO, "VFOs" },
    { 0xf6, "get_modes",        funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO, "Modes" },
    { 0xf9, "get_clock",        funcTime,               typeUChar,    ARG_NOVFO },
    { 0xf8, "set_clock",        funcTime,               typeUChar,    ARG_IN | ARG_NOVFO, "YYYYMMDDHHMMSS.sss+ZZ" },
    { 0xf1, "halt",             funcNone,               typeUChar,    ARG_NOVFO },   /* rigctld only--halt the daemon */
    { 0x8c, "pause",            funcNone,               typeUChar,    ARG_IN, "Seconds" },
    { 0x98, "password",         funcNone,               typeUChar,    ARG_IN | ARG_NOVFO, "Password" },
    { 0xf7, "get_mode_bandwidths", funcNone,            typeUChar,    ARG_IN | ARG_NOVFO, "Mode" },
    { 0xa0, "set_separator",     funcNone,              typeUChar,    ARG_IN | ARG_NOVFO, "Separator" },
    { 0xa1, "get_separator",     funcNone,              typeUChar,    ARG_NOVFO, "Separator" },
    { 0xa2, "set_lock_mode",     funcNone,              typeUChar,    ARG_IN | ARG_NOVFO, "Locked" },
    { 0xa3, "get_lock_mode",     funcNone,              typeUChar,    ARG_NOVFO, "Locked" },
    { 0xa4, "send_raw",          funcNone,              typeUChar,    ARG_NOVFO | ARG_IN1 | ARG_IN2 | ARG_OUT3, "Terminator", "Command", "Send raw answer" },
    { 0xa5, "client_version",    funcNone,              typeUChar,    ARG_NOVFO | ARG_IN1, "Version", "Client version" },
    { 0x00, "", funcNone, typeNone},
};




rigCtlD::rigCtlD(QObject* parent) :
    QTcpServer(parent)
{
}

rigCtlD::~rigCtlD()
{
    qDebug(logRigCtlD()) << "closing rigctld";
}


int rigCtlD::startServer(qint16 port)
{
    if (!this->listen(QHostAddress::Any, port)) {
        qCritical(logRigCtlD()) << "could not start on port" << port << errorString();
        return -1;
    }
    else
    {
        qDebug(logRigCtlD()) << "started on port" << port;
    }

    return 0;
}

void rigCtlD::incomingConnection(qintptr socket) {
    rigCtlClient* client = new rigCtlClient(socket, this);
    connect(this, SIGNAL(onStopped()), client, SLOT(closeSocket()));
}


void rigCtlD::stopServer()
{
    qDebug(logRigCtlD()) << "stopping server";
    emit onStopped();
    close();
}

void rigCtlClient::receiveRigCaps(rigCapabilities* caps)
{
    if (caps != nullptr) {
        qDebug(logRigCtlD()) << "Got rigcaps for:" << caps->modelName;
    } else
    {
        qWarning(logRigCtlD()) << "Rig has gone away, closing rigctld client connection";
        closeSocket();
    }
    this->rigCaps = caps;
    updateVfoList();
}

rigCtlClient::rigCtlClient(int socketId, rigCtlD* parent) : QObject(parent)
{

    this->setObjectName("RigCtlD");
    queue = cachingQueue::getInstance();
    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));
    rigCaps = queue->getRigCaps();
    commandBuffer.clear();
    sessionId = socketId;
    socket = new QTcpSocket(this);
    this->parent = parent;
    if (!socket->setSocketDescriptor(sessionId))
    {
        qCritical(logRigCtlD()) << "error binding socket:" << sessionId << socket->errorString();
        return;
    }
    connect(socket, SIGNAL(readyRead()), this, SLOT(socketReadyRead()), Qt::DirectConnection);
    connect(socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()), Qt::DirectConnection);
    connect(parent, SIGNAL(sendData(QString)), this, SLOT(sendData(QString)), Qt::DirectConnection);
    qInfo(logRigCtlD()) << " session connected: " << sessionId;

    // Find what VFOs we have:
    if (rigCaps != nullptr)
        updateVfoList();
}

void rigCtlClient::socketReadyRead()
{

    if (this->rigCaps == nullptr) {
        qWarning(logRigCtlD()) << "Rig has gone away, closing connection";
        closeSocket();
        return;
    }


    commandBuffer.append(socket->readAll());

    int lastNewline = commandBuffer.lastIndexOf('\n');
    if (lastNewline < 0)
        return; // no full line yet

    QByteArray toProcess = commandBuffer.left(lastNewline + 1);
    commandBuffer.remove(0, lastNewline + 1);

    QStringList commandList = QString::fromLatin1(toProcess).split('\n');

    QString sep = rigctlSeparator;

    int ret = -RIG_EINVAL;
    bool found = false;
    bool sendStatus = true;
    bool setCommand = false;
    bool longCommand = false;
    bool extended = false;

    QStringList response;

    for (QString &commands : commandList)
    {
        restart:
        if (commands.endsWith('\r'))
        {
            commands.chop(1); // Remove last character
        }

        if (commands.isEmpty())
        {
            continue;
        }

        setCommand = false;
        longCommand = false;
        extended = false;
        sendStatus = true;
        ret = -RIG_EINVAL;
        sep = rigctlSeparator;

        // We have a full line so process command.

        qDebug(logRigCtlD) << sessionId  << "RX:" << commands;

        if (commands[0] == ';' || commands[0] == '|' || commands[0] == ',')
        {
            sep = commands[0].toLatin1();
            commands.remove(0,1);
            extended=true;
        }
        else if (commands[0] == '+')
        {
            //qDebug(logRigCtlD) << "Extended response requested for this command";
            extended = true;
            sep = "\n";
            commands.remove(0,1);
        }
        else if (commands[0] == '#')
        {
            continue;
        }
        else if (commands[0].toLower() == 'q')
        {
            closeSocket();
            return;
        }


        if (commands[0] == '\\')
        {
            commands.remove(0,1);
            //qDebug(logRigCtlD) << "This is a long form command";
            longCommand = true;
        }

        //qDebug(logRigCtlD) << "Got command to parse:" << commands;

        QStringList command = commands.split(" ");
        command.removeAll({}); // Remove any empty strings (double-whitespace issue)
        if (command.isEmpty())
        {
            continue;
        }
        found=false;
        for (int i=0; commands_list[i].sstr != 0x00; i++)
        {
            if ((longCommand && !strncmp(command[0].toLocal8Bit(), commands_list[i].str,MAXNAMESIZE)) ||
                   (!longCommand && !commands.isNull() && commands[0].toLatin1() == commands_list[i].sstr))
            {
                if ((commands_list[i].flags & ARG_IN_LINE ) == ARG_IN_LINE && !longCommand)
                {
                    command[0].remove(0,1);
                    command.removeAll({});
                }
                else
                {
                    command.removeFirst(); // Remove the actual command so it only contains params
                }
                found = true;
                if (extended && commands_list[i].sstr != 0xf0){
                    // First we need to repeat the original command back:
                    QString repeat=QString("%1:").arg(commands_list[i].str);
                    for( const auto &c: std::as_const(command))
                    {
                        repeat+=QString(" %0").arg(c);
                    }
                    response.append(repeat);
                }

                if (((commands_list[i].flags & ARG_IN) == ARG_IN))
                {
                    // For debugging only comment next line M0VSE
                    //qInfo(logRigCtlD()) << "Received set command" << commands;
                    setCommand=true;
                }

                if (commands_list[i].func == funcRigctlFunction)
                {
                    ret = getSubCommand(response, extended, commands_list[i], functions_str, command);
                    commands.clear();
                    break;
                } else if (commands_list[i].func == funcRigctlLevel)
                {
                    ret = getSubCommand(response, extended, commands_list[i], levels_str, command);
                    commands.clear();
                    break;
                } else if (commands_list[i].func == funcRigctlParam)
                {
                    ret = getSubCommand(response, extended, commands_list[i], params_str, command);
                    commands.clear();
                    break;
                } else if (commands_list[i].sstr == 0x8f)
                {
                    ret = dumpState(response, extended);
                    break;
                } else if (commands_list[i].sstr == '1')
                {
                    ret = dumpCaps(response, extended);
                    setCommand=true;
                    break;
                }
                else if (commands_list[i].sstr == '2')
                {
                    // power2mW
                    ret = power2mW(response, extended, commands_list[i], command);
                    setCommand=true;
                    break;
                }
                else if (commands_list[i].sstr == '3')
                {
                    response.append(QString("Model: WFVIEW(%0)").arg(rigCaps->modelName));
                    ret = RIG_OK;
                    break;

                }
                else if (commands_list[i].sstr == '4')
                {
                    // mW2power
                    ret = mW2power(response, extended, commands_list[i], command);
                    setCommand=true;
                    break;
                }
                else if (commands_list[i].sstr == 0xf0)
                {
                    chkVfoEecuted = true;
                    response.append(QString::number(rigctlVfoOpt));
                    ret = RIG_OK;
                    // chk_vfo doesn't output RPRT
                    sendStatus=false;
                    break;
                }
                else if (commands_list[i].sstr == '_')
                {
                    response.append(QString("Model: %1, rigctl model: %2").arg(rigCaps->modelName).arg(rigCaps->rigctlModel));
                    ret = RIG_OK;
                    break;
                }
                else if (commands_list[i].sstr == 0xf2)
                {
                    if (command.isEmpty()) {
                        ret = -RIG_EINVAL;
                    } else {
                        rigctlVfoOpt = command[0].toInt();
                        ret = RIG_OK;
                    }
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 0x95)
                {
                    if (command.isEmpty()) {
                        ret = -RIG_EINVAL;
                    } else {
                        rigctlCacheTimeoutMs = command[0].toInt();
                        ret = RIG_OK;
                    }
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 0x96)
                {
                    response.append(QString::number(rigctlCacheTimeoutMs));
                    ret = RIG_OK;
                    break;
                }
                else if (commands_list[i].sstr == 0x8d)
                {
                    if (command.isEmpty()) {
                        ret = -RIG_EINVAL;
                    } else {
                        rigctlTwiddleTimeoutSec = command[0].toInt();
                        ret = RIG_OK;
                    }
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 0x8e)
                {
                    response.append(QString::number(rigctlTwiddleTimeoutSec));
                    ret = RIG_OK;
                    break;
                }
                else if (commands_list[i].sstr == 0x8c)
                {
                    const int seconds = command.isEmpty() ? 0 : command[0].toInt();
                    if (seconds > 0)
                        QThread::sleep(static_cast<unsigned long>(seconds));
                    ret = RIG_OK;
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 'G')
                {
                    if (command.isEmpty()) {
                        ret = -RIG_EINVAL;
                    } else {
                        const QString op = command[0].toUpper();
                        bool ok = false;
                        const int opValue = command[0].toInt(&ok, 0);
                        funcs opFunc = funcNone;

                        if (op == "CPY" || (ok && opValue == (1 << 0)))
                            opFunc = funcVFOEqualMS;
                        else if (op == "XCHG" || (ok && opValue == (1 << 1)))
                            opFunc = funcVFOSwapMS;
                        else if (op == "FROM_VFO" || (ok && opValue == (1 << 2)))
                            opFunc = funcMemoryWrite;
                        else if (op == "TO_VFO" || (ok && opValue == (1 << 3)))
                            opFunc = funcMemoryToVFO;
                        else if (op == "MCL" || (ok && opValue == (1 << 4)))
                            opFunc = funcMemoryClear;

                        if (opFunc != funcNone && rigCaps->commands.contains(opFunc)) {
                            queue->add(priorityImmediate, queueItem(opFunc, QVariant(), false, queue->getState().receiver));
                            ret = RIG_OK;
                        } else {
                            ret = -RIG_ENAVAIL;
                        }
                    }
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 0x98)
                {
                    ret = RIG_OK;
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 0xf1)
                {
                    parent->stopServer();
                    ret = RIG_OK;
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 0xa0)
                {
                    if (command.isEmpty()) {
                        ret = -RIG_EINVAL;
                    } else {
                        bool ok = false;
                        int code = command[0].toInt(&ok, 0);
                        rigctlSeparator = QString(QChar(ok ? code : command[0][0].unicode()));
                        ret = RIG_OK;
                    }
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 0xa1)
                {
                    response.append(QString("0x%1").arg(rigctlSeparator.isEmpty() ? 0 : rigctlSeparator[0].unicode(), 0, 16));
                    ret = RIG_OK;
                    break;
                }
                else if (commands_list[i].sstr == 0xf4)
                {
                    response.append(availableVfoNames().join(" "));
                    ret = RIG_OK;
                    break;
                }
                else if (commands_list[i].sstr == 0xf5)
                {
                    response.append(QString("Model: %1, rigctl model: %2").arg(rigCaps->modelName).arg(rigCaps->rigctlModel));
                    ret = RIG_OK;
                    break;
                }
                else if (commands_list[i].sstr == 0xf6)
                {
                    QStringList modes;
                    const quint64 supportedModes = getRadioModes();
                    for (int m = 0; mode_str[m].str[0] != '\0'; ++m) {
                        if ((supportedModes & mode_str[m].mode) && !modes.contains(mode_str[m].str))
                            modes.append(mode_str[m].str);
                    }
                    response.append(modes.join(" "));
                    ret = RIG_OK;
                    break;
                }
                else if (commands_list[i].sstr == 0xf7)
                {
                    if (command.isEmpty()) {
                        ret = -RIG_EINVAL;
                    } else {
                        response.append(rigctlcompat::modeBandwidthResponse(command[0]));
                        ret = RIG_OK;
                    }
                    break;
                }
                else if (commands_list[i].sstr == 0xf9)
                {
                    QDate currentDate = QDate::currentDate();
                    timekind currentTime{static_cast<quint8>(QTime::currentTime().hour()), static_cast<quint8>(QTime::currentTime().minute()), false};
                    timekind utcOffset{0, 0, false};

                    if (rigCaps->commands.contains(funcDate)) {
                        const cacheItem dateItem = queue->getCache(funcDate, 0);
                        if (dateItem.value.canConvert<datekind>()) {
                            const datekind d = dateItem.value.value<datekind>();
                            currentDate = QDate(d.year, d.month, d.day);
                        }
                    }
                    if (rigCaps->commands.contains(funcTime)) {
                        const cacheItem timeItem = queue->getCache(funcTime, 0);
                        if (timeItem.value.canConvert<timekind>())
                            currentTime = timeItem.value.value<timekind>();
                    }
                    if (rigCaps->commands.contains(funcUTCOffset)) {
                        const cacheItem offsetItem = queue->getCache(funcUTCOffset, 0);
                        if (offsetItem.value.canConvert<timekind>())
                            utcOffset = offsetItem.value.value<timekind>();
                    }

                    response.append(QString("%1T%2:%3:00.000%4%5:%6")
                                        .arg(currentDate.toString("yyyy-MM-dd"))
                                        .arg(currentTime.hours, 2, 10, QChar('0'))
                                        .arg(currentTime.minutes, 2, 10, QChar('0'))
                                        .arg(utcOffset.isMinus ? "-" : "+")
                                        .arg(utcOffset.hours, 2, 10, QChar('0'))
                                        .arg(utcOffset.minutes, 2, 10, QChar('0')));
                    ret = RIG_OK;
                    break;
                }
                else if (commands_list[i].sstr == 0xf8)
                {
                    const QString text = command.isEmpty() ? QString() : command.join(' ');
                    QDateTime dt;
                    if (text.compare("local", Qt::CaseInsensitive) == 0) {
                        dt = QDateTime::currentDateTime();
                    } else if (text.compare("utc", Qt::CaseInsensitive) == 0) {
                        dt = QDateTime::currentDateTimeUtc();
                    } else {
                        dt = QDateTime::fromString(text, Qt::ISODate);
                    }

                    if (!dt.isValid()) {
                        ret = -RIG_EINVAL;
                    } else {
                        const QDate date = dt.date();
                        const QTime time = dt.time();
                        const int offsetSeconds = dt.offsetFromUtc();
                        const int absOffsetMinutes = qAbs(offsetSeconds) / 60;
                        if (rigCaps->commands.contains(funcDate)) {
                            queue->add(priorityImmediate, queueItem(funcDate, QVariant::fromValue<datekind>(
                                datekind{static_cast<uint16_t>(date.year()), static_cast<quint8>(date.month()), static_cast<quint8>(date.day())}), false, 0));
                        }
                        if (rigCaps->commands.contains(funcTime)) {
                            queue->add(priorityImmediate, queueItem(funcTime, QVariant::fromValue<timekind>(
                                timekind{static_cast<quint8>(time.hour()), static_cast<quint8>(time.minute()), false}), false, 0));
                        }
                        if (rigCaps->commands.contains(funcUTCOffset)) {
                            queue->add(priorityImmediate, queueItem(funcUTCOffset, QVariant::fromValue<timekind>(
                                timekind{static_cast<quint8>(absOffsetMinutes / 60), static_cast<quint8>(absOffsetMinutes % 60), offsetSeconds < 0}), false, 0));
                        }
                        ret = RIG_OK;
                    }
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 0xa5)
                {
                    qDebug(logRigCtlD()) << "rigctld client version:" << command.join(' ');
                    ret = RIG_OK;
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 0xa2)
                {
                    if (command.isEmpty()) {
                        ret = -RIG_EINVAL;
                    } else {
                        modeLock = static_cast<uchar>(command[0].toUInt());
                        ret = RIG_OK;
                    }
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].sstr == 0xa3)
                {
                    response.append(QString::number(modeLock));
                    ret = RIG_OK;
                    setCommand = true;
                    break;
                }
                else if (commands_list[i].func == funcNone)
                {
                    ret = -RIG_ENIMPL;
                    break;
                }
                // Special commands are funcNone so will not get called here
                if (commands_list[i].func != funcNone) {
                    ret = getCommand(response, extended, commands_list[i],command, commands);
                }
                break;
            }
        }

        if (setCommand)
        {
            break;  // Cannot be a compound command so just output result.
        }

        if (!longCommand && !commands.isEmpty() ){
            commands.remove(0,1);
            goto restart; // Need to restart loop without going to next command incase of compound command
        }

    }

    // We have finished parsing all commands and have a response to send (hopefully)
    //commandBuffer.clear();
    for (int i = 0;i<response.size();i++)
    {
        if (!response[i].isEmpty()) {
            sendData(QString("%0%1").arg(response[i],sep));
        }
    }

    if (sendStatus && (ret < 0 || setCommand || extended || !found))
        sendData(QString("RPRT %1\n").arg(QString::number(ret)));
}

void rigCtlClient::socketDisconnected()
{
    qInfo(logRigCtlD()) << sessionId << "disconnected";
    socket->deleteLater();
    this->deleteLater();
}

void rigCtlClient::closeSocket()
{
    socket->close();
}

void rigCtlClient::sendData(QString data)
{
    qDebug(logRigCtlD()) << sessionId << "TX:" << data;
    if (socket != nullptr && socket->isValid() && socket->isOpen())
    {
        socket->write(data.toLatin1());
    }
    else
    {
        qWarning(logRigCtlD()) << "socket not open; dropping rigctld response";
    }
}

QString rigCtlClient::getFilter(quint8 mode, quint8 filter) {
    
    if (mode == 3 || mode == 7 || mode == 12 || mode == 17) {
        switch (filter) {
        case 1:
            return QString("1200");
        case 2:
            return QString("500");
        case 3:
            return QString("250");
        }
    }
    else if (mode == 4 || mode == 8)
    {
        switch (filter) {
        case 1:
            return QString("2400");
        case 2:
            return QString("500");
        case 3:
            return QString("250");
        }
    }
    else if (mode == 2)
    {
        switch (filter) {
        case 1:
            return QString("9000");
        case 2:
            return QString("6000");
        case 3:
            return QString("3000");
        }
    }
    else if (mode == 5)
    {
        switch (filter) {
        case 1:
            return QString("15000");
        case 2:
            return QString("10000");
        case 3:
            return QString("7000");
        }
    }
    else { // SSB or unknown mode
        switch (filter) {
        case 1:
            return QString("3000");
        case 2:
            return QString("2400");
        case 3:
            return QString("1800");
        }
    }
    return QString("");
}

QString rigCtlClient::getMode(modeInfo mode)
{
    for (int i = 0; mode_str[i].str[0] != '\0'; i++)
    {
        if (mode_str[i].mk == mode.mk && mode_str[i].data == mode.data)
        {
            return(QString(mode_str[i].str));
        }
    }
    return("UNKNOWN");
}

bool rigCtlClient::getMode(QString modeString, modeInfo& mode)
{
    //qInfo() << "Looking for mode (set)" << modeString;
    for (int i = 0; mode_str[i].str[0] != '\0'; i++)
    {
        if (QString(mode_str[i].str) == modeString)
        {
            for (auto &m: rigCaps->modes)
            {
                if (m.mk == mode_str[i].mk)
                {
                    mode = modeInfo(m);
                    mode.data = mode_str[i].data;
                    mode.filter = 1;
                    return true;
                }
            }
        }
    }
    return false;
}


quint32 rigCtlClient::getAntennas()
{
    QList<int> antennaNumbers;
    for (const auto &antenna : rigCaps->antennas)
        antennaNumbers.append(antenna.num);
    return rigctlcompat::antennaMask(antennaNumbers);
}

int rigCtlClient::hamlibAntIndexFromRigNum(int rigNum)
{
    QList<int> antennaNumbers;
    for (const auto &antenna : rigCaps->antennas)
        antennaNumbers.append(antenna.num);
    return rigctlcompat::hamlibAntennaIndex(rigNum, antennaNumbers);
}

quint8 rigCtlClient::rigAntNumFromHamlibIndex(int hamlibIndex)
{
    QList<int> antennaNumbers;
    for (const auto &antenna : rigCaps->antennas)
        antennaNumbers.append(antenna.num);
    return rigctlcompat::rigAntennaNumber(hamlibIndex, antennaNumbers);
}

quint64 rigCtlClient::getRadioModes(QString md) 
{
    quint64 modes = 0;
    for (auto&& mode : rigCaps->modes)
    {
        for (int i = 0; mode_str[i].str[0] != '\0'; i++)
        {
            QString mstr = QString(mode_str[i].str);
            if (rigCaps->inputs.size())
            {
                if (mstr.contains(mode.name))
                {
                    // qDebug(logRigCtlD()) << "Found data mode:" << mode.name << mode_str[i].mode;
                    if (md.isEmpty() || mstr.contains(md)) {
                        modes |= mode_str[i].mode;
                    }
                }
            }
            else if (mode.name==mstr)
            {
                //qDebug(logRigCtlD()) << "Found mode:" << mode.name << mode_str[i].mode;
                if (md.isEmpty() || mstr==md) 
                {
                    modes |= mode_str[i].mode;
                } 
            }
        }
    }
    return modes;
}

QString rigCtlClient::getAntName(quint8 ant)
{
    return rigctlcompat::antennaName(ant);
}

quint8 rigCtlClient::antFromName(QString name) {
    return rigctlcompat::antennaIndexFromName(name);
}

bool rigCtlClient::isVfoName(const QString &vfo) const
{
    return rigctlcompat::isVfoName(vfo);
}

QStringList rigCtlClient::availableVfoNames() const
{
    QStringList vfos;
    if (!rigCaps)
        return vfos;

    if (rigCaps->commands.contains(funcMemoryMode))
        vfos.append("MEM");

    if (rigCaps->numReceiver > 0 && rigCaps->commands.contains(funcVFOMainSelect))
        vfos.append("Main");
    if (rigCaps->numReceiver > 1 && rigCaps->commands.contains(funcVFOSubSelect))
        vfos.append("Sub");

    if (rigCaps->numVFO > 0 && rigCaps->commands.contains(funcVFOASelect))
        vfos.append("VFOA");
    else if (rigCaps->numReceiver > 0 && rigCaps->commands.contains(funcVFOMainSelect))
        vfos.append("VFOA");

    if (rigCaps->numVFO > 1 && rigCaps->commands.contains(funcVFOBSelect))
        vfos.append("VFOB");
    else if (rigCaps->numReceiver > 1 && rigCaps->commands.contains(funcVFOSubSelect))
        vfos.append("VFOB");

    vfos.removeDuplicates();
    return vfos;
}

void rigCtlClient::updateVfoList()
{
    vfoList = 0;
    const QStringList vfos = availableVfoNames();
    if (vfos.contains("VFOA"))
        vfoList |= 1 << 0;
    if (vfos.contains("VFOB"))
        vfoList |= 1 << 1;
    if (vfos.contains("Sub"))
        vfoList |= 1 << 25;
    if (vfos.contains("Main"))
        vfoList |= 1 << 26;
    if (vfos.contains("MEM"))
        vfoList |= 1 << 28;
}

rigStateType rigCtlClient::vfoFromName(QString vfo) {

    rigStateType state = queue->getState();
    if (vfo.toUpper() == "VFOA")
    {
        if (rigCaps->commands.contains(funcVFOASelect))
            state.vfo = vfoA;
        else if (rigCaps->commands.contains(funcVFOMainSelect))
            state.receiver = 0;
    }
    else if (vfo.toUpper() == "VFOB")
    {
        if (rigCaps->commands.contains(funcVFOBSelect))
            state.vfo = vfoB;
        else if (rigCaps->commands.contains(funcVFOSubSelect))
            state.receiver = 1;
    }
    else if (vfo.toUpper() == "MAIN")
    {
        state.vfo = vfoMain;
        if (rigCaps->commands.contains(funcVFOMainSelect))
            state.receiver = 0;
        else if (rigCaps->commands.contains(funcVFOASelect))
            state.vfo = vfoA;
    }
    else if (vfo.toUpper() == "SUB")
    {
        state.vfo = vfoSub;
        if (rigCaps->commands.contains(funcVFOSubSelect))
            state.receiver = 1;
        else if (rigCaps->commands.contains(funcVFOBSelect))
            state.vfo = vfoB;
    }
    else if (vfo.toUpper() == "MEM")
    {
        if (rigCaps->commands.contains(funcMemoryMode))
            state.vfo = vfoMem;
    }

    //qInfo() << "vfoFromName" << vfo << "returns" << v;
    return state;
}

QString rigCtlClient::getVfoName(vfo_t vfo)
{
    QString ret;
    switch (vfo) {
    case vfoMain:
        ret = "Main";
        break;
    case vfoSub:
        ret = "Sub";
        break;
    case vfoA:
        ret = "VFOA";
        break;
    case vfoB:
        ret = "VFOB";
        break;
    case vfoCurrent:
        ret = "currVFO";
        break;
    case vfoMem:
        ret = "MEM";
        break;
    default:
        ret = "None";
        break;
    }

    //qInfo() << "vfoName" << vfo << "returns" << ret;

    return ret;
}

unsigned long rigCtlClient::doCrc(quint8* p, size_t n)
{
    unsigned long crc = 0xfffffffful;
    size_t i;

    if (crcTable[0] == 0) { genCrc(crcTable); }

    for (i = 0; i < n; i++)
    {
        crc = crcTable[*p++ ^ (crc & 0xff)] ^ (crc >> 8);
    }

    return ((~crc) & 0xffffffff);
}

void rigCtlClient::genCrc(unsigned long crcTable[])
{
    unsigned long POLYNOMIAL = 0xEDB88320;
    quint8 b = 0;

    while (0 != ++b)
    {
        unsigned long remainder = b;
        unsigned long bit;
        for (bit = 8; bit > 0; --bit)
        {
            if (remainder & 1)
            {
                remainder = (remainder >> 1) ^ POLYNOMIAL;
            }
            else
            {
                remainder = (remainder >> 1);
            }
        }
        crcTable[(size_t)b] = remainder;
    }
}

int rigCtlClient::getCommand(QStringList& response, bool extended, const commandStruct cmd, QStringList params , QString fullcmd )
{
    // This is a main command
    int ret = -RIG_EINVAL;
    funcs func = cmd.func;
    rigStateType state=queue->getState();


    if (rigCaps == nullptr)
        return ret;

    if (((cmd.flags & ARG_IN) == ARG_IN) && params.size())
    {
        // We are expecting a second argument to the command as it is a set
        QVariant val;
        ret = RIG_OK;
        switch (cmd.type)
        {
        case typeUChar:
            if (cmd.func == funcLockFunction)
                val.setValue(modeLock);
            else if (cmd.func == funcAntenna) {
                int antennaParam = 0;
                if (params.size() > 1 && isVfoName(params[0])) {
                    state = vfoFromName(params[0]);
                    antennaParam = 1;
                }

                if (params[antennaParam] == "?") {
                    QStringList antennas;
                    for (const auto &antenna : rigCaps->antennas)
                        antennas.append(getAntName(static_cast<quint8>(hamlibAntIndexFromRigNum(antenna.num))));
                    if (rigCaps->commands.contains(funcRXAntenna))
                        antennas.append("RXANT");
                    response.append(antennas.join(" "));
                    return RIG_OK;
                }

                bool ok = false;
                int antennaMask = params[antennaParam].toInt(&ok, 0);
                int antennaIndex = 0;
                if (ok && antennaMask > 0) {
                    while (((antennaMask >> antennaIndex) & 0x01) == 0 && antennaIndex < 8)
                        ++antennaIndex;
                } else if (!ok && (params[antennaParam].compare("RXANT", Qt::CaseInsensitive) == 0
                                   || params[antennaParam].compare("RX", Qt::CaseInsensitive) == 0)) {
                    antennaIndex = 31;
                } else if (!ok) {
                    antennaIndex = antFromName(params[antennaParam]);
                }

                antennaInfo antenna;
                antenna.antenna = antennaIndex < 8 ? rigAntNumFromHamlibIndex(antennaIndex) : 0;
                antenna.rx = false;

                const cacheItem current = queue->getCache(funcAntenna, state.receiver);
                if (current.value.canConvert<antennaInfo>()) {
                    const antennaInfo currentAntenna = current.value.value<antennaInfo>();
                    antenna.rx = currentAntenna.rx;
                    if (antennaIndex == 31)
                        antenna.antenna = currentAntenna.antenna;
                }
                if (params.size() > antennaParam + 1)
                    antenna.rx = params[antennaParam + 1].toInt() != 0;
                else if (antennaIndex == 31)
                    antenna.rx = true;

                val.setValue(antenna);
            }
            else
                val.setValue(static_cast<uchar>(params[0].toInt()));
            break;
        case typeShort:
            val.setValue(static_cast<short>(params[0].toInt()));
            break;
        case typeUShort:
            val.setValue(static_cast<ushort>(params[0].toInt()));
            break;
        case typeFloat:
            val.setValue(static_cast<ushort>(float(params[0].toFloat() * 255.0)));  // rigctl sends 0.0-1.0, we expect 0-255
            break;
        case typeFloatDiv:
            val.setValue(static_cast<uchar>(float(params[0].toFloat() / 10.0)));
            break;
        case typeFloatDiv5:
            val.setValue(static_cast<ushort>(float(params[0].toFloat() * 5.1)));
            break;
        case typeBinary:
            if (params.length()>1){
                queue->add(priorityImmediate, queueItem(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoFromName(params[0]).vfo),false,state.receiver));
                val.setValue(static_cast<bool>(params[1].toInt()));
            } else {
                val.setValue(static_cast<bool>(params[0].toInt()));
            }
            break;
        case typeFreq:
        {            
            freqt f;
            if (cmd.sstr == 'I') {
                state.vfo=splitVfo;
                state.receiver = uchar((rigCaps->numVFO == 1 && rigCaps->numReceiver>1));
            }

            if (params.length() > 1) {
                state = vfoFromName(params[0]);

                f.Hz = static_cast<quint64>(params[1].toDouble());
            } else {
                f.Hz = static_cast<quint64>(params[0].toDouble());
            }

            {
                const vfoCommandType vfoCommand = queue->getVfoCommand(state.vfo, state.receiver, true);
                func = vfoCommand.freqFunc;
                state.receiver = vfoCommand.receiver;
            }
            f.VFO = activeVFO;
            f.MHzDouble = static_cast<double>(f.Hz/1000000.0);
            val.setValue(f);
            break;
        }
        case typeSplitVFO:

            val.setValue(static_cast<bool>(params[0].toInt()));
            if (params.size() > 1) {// Just in case we have invalid info
                //qInfo() << "Split VFO" << params[0] << "VFO" << params[1];
                splitVfo = vfoFromName(params[1]).vfo;
            }
            break;

        case typeMode:
        {
            if (cmd.sstr == 'X') {
                state.vfo=splitVfo;
                state.receiver = uchar((rigCaps->numVFO==1 && rigCaps->numReceiver>1));
            }

            modeInfo mi;
            QString mode="";
            QString width="";

            if (params.size() == 3)
            {
                state=vfoFromName(params[0]);
                mode=params[1];
                width=params[2];
            }
            else if (params.size() == 2)
            {
                mode=params[0];
                width=params[1];
            }

            // We have VFO, Mode and PB
            {
                const vfoCommandType vfoCommand = queue->getVfoCommand(state.vfo, state.receiver, true);
                func = vfoCommand.modeFunc;
                state.receiver = vfoCommand.receiver;
            }

            bool ok;
            int pb = width.toInt(&ok);
            if (ok && pb>0) {
                queue->add(priorityImmediate, queueItem(funcFilterWidth, QVariant::fromValue<ushort>(pb),false,state.receiver));
            }

            if (getMode(mode,mi))
            {
                val.setValue(mi);
            } else {
                qWarning(logRigCtlD()) << "Mode not found:" << params[0];
                return -RIG_EINVAL;
            }
            break;
        }
        case typeVFO:
            // Setting VFO:
            if (params[0] == "?") {
                // This is a query for a list of values
                response.append(availableVfoNames().join(" "));
                return RIG_OK;
            }
            else
            {
                state = vfoFromName(params[0]);
                val.setValue(state.vfo);
            }
            break;
        case typeString:
        {
            // Only used for CW?
            Q_UNUSED(fullcmd)
            val.setValue(params.join(' '));
            break;
        }
        default:
            qWarning(logRigCtlD()) << "Unable to parse value of type" << cmd.type << "Command" << cmd.str;
            return -RIG_EINVAL;
        }

        if (rigCaps->commands.contains(func)) {
            queue->add(priorityImmediate, queueItem(func, val,false,state.receiver));
            if (func == funcTransceiverStatus)
                emit parent->setPTT(val.toBool());
        }

    } else {
        // Simple get command
        cacheItem item;

        // Build QStringList of Prefixes. This is a bit messy, but not sure how else do to it?
        QStringList prefixes=buildPrefixes(cmd,extended);

        if (!prefixes.length())
        {
            qWarning(logRigCtlD()) << "No prefixes found for cmd" << cmd.str << "using func" << funcString[func] << "aborting";
        }

        if (cmd.sstr=='i' ||  cmd.sstr=='x') {
            if (splitVfo == vfoSub && rigCaps->numReceiver > 1) {
                state.receiver=1;
            }
            state.vfo = splitVfo;
        }
        if (cmd.func == funcAntenna && params.size() && params[0] != "?" && isVfoName(params[0]))
            state = vfoFromName(params[0]);

        if (func == funcFreqGet) {
            const vfoCommandType vfoCommand = queue->getVfoCommand(state.vfo, state.receiver, false);
            func = vfoCommand.freqFunc;
            state.receiver = vfoCommand.receiver;
        } else if (func == funcModeGet) {
            const vfoCommandType vfoCommand = queue->getVfoCommand(state.vfo, state.receiver, false);
            func = vfoCommand.modeFunc;
            state.receiver = vfoCommand.receiver;
        }

        //qInfo() << "getting Cache Value for func" << funcString[func] << "on rx" << state.receiver;
        if (rigCaps->commands.contains(func))
            item = queue->getCache(func,state.receiver);

        if (cmd.type != typeVFOInfo && cmd.func != funcAntenna && prefixes.length() && params.length())
        {
            response.append(QString("%0%1").arg(prefixes[0], params[0]));
        }

        ret = RIG_OK;
        switch (cmd.type){
        case typeBinary:
        {
            bool b = item.value.toBool();
            response.append(QString("%0%1").arg(prefixes[prefixes.length()-1], QString::number(b)));
            break;
        }
        case typeUChar:
        {
            if (cmd.func == funcAntenna) {
                if (params.size() && params[0] == "?") {
                    QStringList antennas;
                    for (const auto &antennaEntry : rigCaps->antennas)
                        antennas.append(getAntName(static_cast<quint8>(hamlibAntIndexFromRigNum(antennaEntry.num))));
                    if (rigCaps->commands.contains(funcRXAntenna))
                        antennas.append("RXANT");
                    response.append(antennas.join(" "));
                    break;
                }

                antennaInfo antenna{0, false};
                if (item.value.canConvert<antennaInfo>())
                    antenna = item.value.value<antennaInfo>();

                const int currentMask = 1 << hamlibAntIndexFromRigNum(antenna.antenna);
                if (extended) {
                    if (prefixes.length() > 0)
                        response.append(QString("%1%2").arg(prefixes[0], QString::number(currentMask)));
                    if (prefixes.length() > 1)
                        response.append(QString("%1%2").arg(prefixes[1], QString::number(0)));
                    if (prefixes.length() > 2)
                        response.append(QString("%1%2").arg(prefixes[2], QString::number(currentMask)));
                    if (prefixes.length() > 3)
                        response.append(QString("%1%2").arg(prefixes[3], QString::number(antenna.rx ? currentMask : 0)));
                } else {
                    response.append(QString::number(currentMask));
                    if (prefixes.length() > 1) {
                        response.append(QString::number(0));
                        response.append(QString::number(currentMask));
                        response.append(QString::number(antenna.rx ? currentMask : 0));
                    }
                }
                break;
            }
            [[fallthrough]];
        }
        case typeUShort:
        case typeShort:
        {
            int i = item.value.toInt();

            if (cmd.func == funcLockFunction)
                i = modeLock;

            response.append(QString("%0%1").arg(prefixes[prefixes.length()-1], QString::number(i)));
            break;
        }
        case typeFloat:
        {
            float f = item.value.toFloat() / 255.0;
            response.append(QString("%0%1").arg(prefixes[prefixes.length()-1],QString::number(f,typeFloat,6)));
            break;
        }
        case typeFloatDiv:
        {
            float f = item.value.toFloat() * 10;
            response.append(QString("%0%1").arg(prefixes[prefixes.length()-1],QString::number(f,typeFloat,6)));
            break;
        }
        case typeFloatDiv5:
        {
            float f = item.value.toFloat() / 5.1;
            response.append(QString("%0%1").arg(prefixes[prefixes.length()-1],QString::number(f,typeFloat,6)));
            break;
        }
        case typeFreq:
        { // Frequency
            freqt f = item.value.value<freqt>();
            response.append(QString("%0%1.000000").arg(prefixes[prefixes.length()-1],QString::number(f.Hz)));
            break;
        }
        case typeVFO:
        { // VFO
            //uchar v = item.value.value<uchar>();
            response.append(QString("%0%1").arg(prefixes[prefixes.length()-1],getVfoName(state.vfo)));
            break;
        }
        case typeSplitVFO:
        { // VFO
            bool v = item.value.value<uchar>();
            if (prefixes.length()>0)
                response.append(QString("%0%1").arg(prefixes[0],QString::number(v)));
            if (prefixes.length()>1)
                response.append(QString("%0%1").arg(prefixes[1],getVfoName(splitVfo)));
            break;
        }

        case typeVFOInfo:
        {
            if (rigCaps->numReceiver > 1 && params.size() && params[0] == "Sub") {
                state.receiver=1;
            }

            const vfoCommandType freqCommand = queue->getVfoCommand(state.vfo, state.receiver, false);
            const funcs freqFunc = freqCommand.freqFunc;
            const funcs modeFunc = freqCommand.modeFunc;
            state.receiver = freqCommand.receiver;

            if (prefixes.length()>1)
                response.append(QString("%0%1").arg(prefixes[1],QString::number(queue->getCache(freqFunc,state.receiver).value.value<freqt>().Hz)));
            if (prefixes.length()>2)
                response.append(QString("%0%1").arg(prefixes[2],getMode(queue->getCache(modeFunc,state.receiver).value.value<modeInfo>())));
            if (prefixes.length()>3)
                    response.append(QString("%0%1").arg(prefixes[3],QString::number(queue->getCache(rigCaps->commands.contains(funcFilterWidth)?funcFilterWidth:funcNone,state.receiver).value.value<ushort>())));
            if (prefixes.length()>4) /* Split is only ever on the first rx */
                response.append(QString("%0%1").arg(prefixes[4],QString::number(queue->getCache(rigCaps->commands.contains(funcSplitStatus)?funcSplitStatus:funcNone,0).value.value<duplexMode_t>())));
            if (prefixes.length()>5)
                response.append(QString("%0%1").arg(prefixes[5],QString::number(queue->getCache(rigCaps->commands.contains(funcSatelliteMode)?funcSatelliteMode:funcNone,state.receiver).value.value<bool>())));
            break;
        }

        case typeMode:
        { // Mode
            modeInfo m = item.value.value<modeInfo>();
            cacheItem f = queue->getCache(funcFilterWidth,state.receiver);
            if (prefixes.length() > 0)
                response.append(QString("%0%1").arg(prefixes[0],getMode(m)));
            if (prefixes.length() > 1)
                response.append(QString("%0%1").arg(prefixes[1],QString::number(f.value.value<ushort>())));
            break;
        }
        case typeString:
        {
            // Stop sending CW if a blank command is received.
            // If 2nd letter of fullcmd is space , send space.
            if ( fullcmd.mid(1,1) == ' ' )
            {
                queue->add(priorityImmediate, queueItem(func, QString(" "),false,0));
            }
            else
            {
                queue->add(priorityImmediate, queueItem(func, QString(),false,0));
            }
            break;
        }
        default:
            qWarning(logRigCtlD()) << "Unsupported type (FIXME):" << item.value.typeName();
            ret = -RIG_EINVAL;
            return ret;
        }
    }

    return ret;
}


int rigCtlClient::getSubCommand(QStringList& response, bool extended, const commandStruct cmd, const subCommandStruct sub[], QStringList params)
{
    Q_UNUSED(extended)

    int ret = -RIG_EINVAL;

    if (rigCaps == nullptr)
        return ret;

    rigStateType state = queue->getState();

    QString resp;
    // Get the required subCommand and process it
    if (params.isEmpty())
    {
        return ret;
    }

    if (params[0][0].toLatin1() == '?')
    {
        for (int i=0; sub[i].str[0] != '\0' ;i++)
        {
            if (sub[i].func != funcNone && rigCaps->commands.contains(sub[i].func)) {
                resp.append(sub[i].str);
                resp.append(" ");
            }
        }
        if (resp.endsWith(' '))
            resp.chop(1);
        response.append(resp);
        ret = RIG_OK;
    }
    else
    {
        for (int i = 0 ; sub[i].str[0] != '\0'; i++)
        {
            if (!strncmp(params[0].toLocal8Bit(),sub[i].str,MAXNAMESIZE))
            {
                ret = RIG_OK;

                if (((cmd.flags & ARG_IN) == ARG_IN) && params.size() > 1)
                {
                    // We are expecting a second argument to the command
                    QVariant val;
                    if (cmd.func == funcRigctlParam && params[0] == "TIME") {
                        const int seconds = qBound(0, params[1].toInt(), 24 * 60 * 60 - 1);
                        timekind time;
                        time.hours = static_cast<quint8>(seconds / 3600);
                        time.minutes = static_cast<quint8>((seconds % 3600) / 60);
                        time.isMinus = false;
                        val.setValue(time);
                        if (rigCaps->commands.contains(sub[i].func))
                            queue->add(priorityImmediate, queueItem(sub[i].func, val,false,state.receiver));
                        break;
                    }
                    switch (sub[i].type)
                    {
                    case typeUShort:
                        val.setValue(static_cast<ushort>(params[1].toInt()));  // rigctl sends 0.0-1.0, we expect 0-255
                        break;
                   case typeShort:
                        val.setValue(static_cast<short>(params[1].toInt()));  // rigctl sends 0.0-1.0, we expect 0-255
                        break;
                    case typeUChar:
                    {
                        uchar v = static_cast<uchar>(params[1].toInt());
                        if (params[0] == "FBKIN")
                            v = (v << 1) & 0x02; // BREAKIN is not bool!
                        if (params[0] == "AGC") {
                            const int agc = rigctlcompat::rigAgcFromHamlib(params[1].toInt());
                            if (agc < 0)
                                return -RIG_EINVAL;
                            v = static_cast<uchar>(agc);
                        }
                        if (sub[i].func == funcPreamp)
                            v = static_cast<uchar>(qBound(0, params[1].toInt() / 10, 2));
                        val.setValue(v);
                        break;
                    }
                    case typeFloat:
                        val.setValue(static_cast<ushort>(float(params[1].toFloat() * 255.0)));  // rigctl sends 0.0-1.0, we expect 0-255
                        //qInfo(logRigCtlD()) << "Setting float value" << params[0] << "to" << val << "original" << params[1];
                        break;
                    case typeFloatDiv:
                        val.setValue(static_cast<uchar>(float(params[1].toFloat() / 10.0)));
                        break;
                    case typeFloatDiv5:
                        val.setValue(static_cast<ushort>(float(params[1].toFloat() * 5.1)));
                        break;
                    case typeBinary:
                        val.setValue(static_cast<bool>(params[1].toInt()));
                        break;
                    default:
                        qWarning(logRigCtlD()) << "Unable to parse value of type" << sub[i].type;
                        return -RIG_EINVAL;
                    }
                    if (rigCaps->commands.contains(sub[i].func))
                        queue->add(priorityImmediate, queueItem(sub[i].func, val,false,state.receiver));
                } else if (params.size() == 1){
                    // Not expecting a second argument as it is a get so dump the cache
                    cacheItem item;
                    if (rigCaps->commands.contains(sub[i].func))
                        item = queue->getCache(sub[i].func,state.receiver);
                    int val = 0;

                    if (cmd.func == funcRigctlParam && params[0] == "TIME") {
                        if (item.value.canConvert<timekind>()) {
                            const timekind time = item.value.value<timekind>();
                            response.append(QString::number((time.hours * 3600) + (time.minutes * 60)));
                        } else {
                            const QTime now = QTime::currentTime();
                            response.append(QString::number((now.hour() * 3600) + (now.minute() * 60) + now.second()));
                        }
                        break;
                    }

                    switch (sub[i].type)
                    {
                    case typeBinary:
                        resp.append(QString("%1").arg(item.value.toBool()));
                        break;
                    case typeUChar:
                    {
                        val = item.value.toInt();
                        if (params[0] == "FBKIN")
                            val = (val >> 1) & 0x01;
                        if (params[0] == "AGC") {
                            val = rigctlcompat::hamlibAgcFromRig(val);
                            if (val < 0)
                                return -RIG_EINVAL;
                        }
                        if (sub[i].func == funcPreamp)
                            val *= 10;
                        resp.append(QString::number(val));
                        break;
                    }
                    case typeSWR:
                    case typeDouble:
                        resp.append(QString::number(item.value.toDouble(),'f',6));
                        break;
                    case typeUShort:
                    case typeShort:
                        resp.append(QString::number(item.value.toInt()));
                        break;
                    case typeFloat:
                        resp.append(QString::number(item.value.toFloat() / 255.0,'f',6));
                        break;
                    case typeFloatDiv:
                        resp.append(QString::number(item.value.toFloat() * 10.0,'f',6));
                        break;
                    case typeFloatDiv5:
                        resp.append(QString::number(item.value.toFloat()/5.1,'f',6));
                        break;
                    default:
                        qWarning(logRigCtlD()) << funcString[sub[i].func] << "Unhandled (" << item.value.typeName() << ")" << item.value.toUInt() << "OUT" << val;
                        ret = -RIG_EINVAL;
                    }
                    qDebug(logRigCtlD()) << "Sending " << funcString[sub[i].func] <<  "data:" << resp;
                    response.append(resp);
                }
                else
                {
                    qWarning(logRigCtlD()) << "Invalid number of params" <<  params.size();
                    ret = -RIG_EINVAL;
                }
                break;
            }
        }
    }
    return ret;
}


int rigCtlClient::dumpState(QStringList &response, bool extended)
{
    Q_UNUSED(extended)

    quint64 modes = getRadioModes();

    // rigctld protocol version
    response.append("1");
    // Radio model
    response.append(QString::number(rigCaps->rigctlModel));
    // Print something (used to be ITU region)
    response.append("0");
    // Supported RX bands (startf,endf,modes,low_power,high_power,vfo,ant)
    quint32 lowFreq = 0;
    quint32 highFreq = 0;
    for (const bandType &band : std::as_const(rigCaps->bands))
    {
        if (lowFreq == 0 || band.lowFreq < lowFreq)
            lowFreq = band.lowFreq;
        if (band.highFreq > highFreq)
            highFreq = band.highFreq;
    }
    response.append(QString("%1.000000 %2.000000 0x%3 %4 %5 0x%6 0x%7").arg(lowFreq).arg(highFreq)
                        .arg(modes, 0, 16).arg(-1).arg(-1).arg(vfoList, 0, 16).arg(getAntennas(), 0, 16));
    response.append("0 0 0 0 0 0 0");

    if (rigCaps->hasTransmit) {
        // Supported TX bands (startf,endf,modes,low_power,high_power,vfo,ant)
        for (const bandType &band : std::as_const(rigCaps->bands))
        {
            response.append(QString("%1.000000 %2.000000 0x%3 %4 %5 0x%6 0x%7").arg(band.lowFreq).arg(band.highFreq)
                                .arg(modes, 0, 16).arg(2000).arg(100000).arg(0x16000000, 0, 16).arg(getAntennas(), 0, 16));
        }
    }
    response.append("0 0 0 0 0 0 0");
    for (auto& step: rigCaps->steps)
    {
        if (step.num > 0)
            response.append(QString("0x%1 %2").arg(modes, 0, 16).arg(step.hz));
    }
    response.append("0 0");

    modes = getRadioModes("SB");
    if (modes) {
        response.append(QString("0x%1 3000").arg(modes, 0, 16));
        response.append(QString("0x%1 2400").arg(modes, 0, 16));
        response.append(QString("0x%1 1800").arg(modes, 0, 16));
    }
    modes = getRadioModes("AM");
    if (modes) {
        response.append(QString("0x%1 9000").arg(modes, 0, 16));
        response.append(QString("0x%1 6000").arg(modes, 0, 16));
        response.append(QString("0x%1 3000").arg(modes, 0, 16));
    }
    modes = getRadioModes("CW");
    if (modes) {
        response.append(QString("0x%1 1200").arg(modes, 0, 16));
        response.append(QString("0x%1 500").arg(modes, 0, 16));
        response.append(QString("0x%1 200").arg(modes, 0, 16));
    }
    modes = getRadioModes("FM");
    if (modes) {
        response.append(QString("0x%1 15000").arg(modes, 0, 16));
        response.append(QString("0x%1 10000").arg(modes, 0, 16));
        response.append(QString("0x%1 7000").arg(modes, 0, 16));
    }
    modes = getRadioModes("RTTY");
    if (modes) {
        response.append(QString("0x%1 2400").arg(modes, 0, 16));
        response.append(QString("0x%1 500").arg(modes, 0, 16));
        response.append(QString("0x%1 250").arg(modes, 0, 16));
    }
    modes = getRadioModes("PSK");
    if (modes) {
        response.append(QString("0x%1 1200").arg(modes, 0, 16));
        response.append(QString("0x%1 500").arg(modes, 0, 16));
        response.append(QString("0x%1 250").arg(modes, 0, 16));
    }
    response.append("0 0");
    response.append("9900");
    response.append("9900");
    response.append("10000");
    response.append("0");
    QString preamps="";
    if(rigCaps->commands.contains(funcPreamp)) {
        for (auto &pre: rigCaps->preamps)
        {
            if (pre.num == 0)
                continue;
            preamps.append(QString(" %1").arg(pre.num*10));
        }
        if (preamps.endsWith(" "))
            preamps.chop(1);
    }
    else {
        preamps = "0";
    }
    response.append(preamps);

    QString attens = "";
    if(rigCaps->commands.contains(funcAttenuator)) {
        for (const auto &att : rigCaps->attenuators)
        {
            if (att.num == 0)
                continue;
            attens.append(QString(" %1").arg(att.num));
        }
        if (attens.endsWith(" "))
            attens.chop(1);
    }
    else {
        attens = "0";
    }
    response.append(attens);

    qulonglong hasFuncs=0;
    qulonglong hasLevels=0;
    qulonglong hasParams=0;
    for (int i = 0 ; functions_str[i].str[0] != '\0'; i++)
    {
        if (functions_str[i].func != funcNone && rigCaps->commands.contains(functions_str[i].func))
        {
            hasFuncs |= 1ULL << i;
        }
    }
    response.append(QString("0x%0").arg(hasFuncs,16,16,QChar('0')));
    response.append(QString("0x%0").arg(hasFuncs,16,16,QChar('0')));

    for (int i = 0 ; levels_str[i].str[0] != '\0'; i++)
    {
        if (levels_str[i].func != funcNone && rigCaps->commands.contains(levels_str[i].func))
        {
            hasLevels |= 1ULL << i;
        }
    }
    response.append(QString("0x%0").arg(hasLevels,16,16,QChar('0')));
    response.append(QString("0x%0").arg(hasLevels,16,16,QChar('0')));

    for (int i = 0 ; params_str[i].str[0] != '\0'; i++)
    {
        if (params_str[i].func != funcNone && rigCaps->commands.contains(params_str[i].func))
        {
            hasParams |= 1ULL << i;
        }
    }
    response.append(QString("0x%0").arg(hasParams,16,16,QChar('0')));
    response.append(QString("0x%0").arg(hasParams,16,16,QChar('0')));

    if (chkVfoEecuted) {
        const vfoCommandType freqSetCommand = queue->getVfoCommand(vfoA, 0, true);
        const vfoCommandType freqGetCommand = queue->getVfoCommand(vfoA, 0, false);
        const vfoCommandType modeSetCommand = queue->getVfoCommand(vfoA, 0, true);
        uint targetableVfo = 0;
        uint vfoOps = 0;
        const bool hasSetVfo = rigCaps->commands.contains(funcVFOASelect)
                               || rigCaps->commands.contains(funcVFOBSelect)
                               || rigCaps->commands.contains(funcVFOMainSelect)
                               || rigCaps->commands.contains(funcVFOSubSelect)
                               || rigCaps->commands.contains(funcMemoryMode);
        if (freqSetCommand.freqFunc != funcNone && rigCaps->commands.contains(freqSetCommand.freqFunc))
            targetableVfo |= 1 << 0; // Hamlib RIG_TARGETABLE_FREQ
        if (modeSetCommand.modeFunc != funcNone && rigCaps->commands.contains(modeSetCommand.modeFunc))
            targetableVfo |= 1 << 1; // Hamlib RIG_TARGETABLE_MODE
        if (rigCaps->commands.contains(funcVFOEqualMS))
            vfoOps |= 1 << 0; // Hamlib RIG_OP_CPY
        if (rigCaps->commands.contains(funcVFOSwapMS))
            vfoOps |= 1 << 1; // Hamlib RIG_OP_XCHG
        if (rigCaps->commands.contains(funcMemoryWrite))
            vfoOps |= 1 << 2; // Hamlib RIG_OP_FROM_VFO
        if (rigCaps->commands.contains(funcMemoryToVFO))
            vfoOps |= 1 << 3; // Hamlib RIG_OP_TO_VFO
        if (rigCaps->commands.contains(funcMemoryClear))
            vfoOps |= 1 << 4; // Hamlib RIG_OP_MCL

        response.append(QString("vfo_ops=0x%1").arg(vfoOps, 0, 16));
        response.append(QString("ptt_type=0x%1").arg(rigCaps->hasTransmit && rigCaps->commands.contains(funcTransceiverStatus), 0, 16));
        response.append(QString("targetable_vfo=0x%1").arg(targetableVfo, 0, 16));
        response.append(QString("has_set_vfo=0x%1").arg(hasSetVfo, 0, 16));
        response.append(QString("has_get_vfo=0x%1").arg(1, 0, 16));
        response.append(QString("has_set_freq=0x%1").arg(freqSetCommand.freqFunc != funcNone && rigCaps->commands.contains(freqSetCommand.freqFunc), 0, 16));
        response.append(QString("has_get_freq=0x%1").arg(freqGetCommand.freqFunc != funcNone && rigCaps->commands.contains(freqGetCommand.freqFunc), 0, 16));
        response.append(QString("has_set_conf=0x%1").arg(0, 0, 16));
        response.append(QString("has_get_conf=0x%1").arg(0, 0, 16));
        response.append(QString("has_power2mW=0x%1").arg(1, 0, 16));
        response.append(QString("has_mW2power=0x%1").arg(1, 0, 16));
        response.append(QString("timeout=0x%1").arg(1000, 0, 16));
        response.append(QString("has_get_ant=0x%1").arg(rigCaps->commands.contains(funcAntenna), 0, 16));
        response.append(QString("has_set_ant=0x%1").arg(rigCaps->commands.contains(funcAntenna), 0, 16));
        response.append(QString("rig_model=%1").arg(rigCaps->rigctlModel));
        response.append("rigctld_version=wfview 3.00");
        response.append("level_gran=0=0,0,0");
        response.append("parm_gran=0=0,0,0");
        response.append("hamlib_version=4.7");
        response.append("done");
    }

    return RIG_OK;
}

int rigCtlClient::dumpCaps(QStringList &response, bool extended)
{
    Q_UNUSED(extended)
    QString manufacturer;
    switch (rigCaps->manufacturer) {
    case manufIcom:
        manufacturer = "Icom";
        break;
    case manufKenwood:
        manufacturer = "Kenwood";
        break;
    case manufYaesu:
        manufacturer = "Yaesu";
        break;
    default:
        manufacturer = "Unknown";
        break;
    }

    response.append(QString("Caps dump for model: %1").arg(rigCaps->modelID));
    response.append(QString("Model Name:\t%1").arg(rigCaps->modelName));
    response.append(QString("Mfg Name:\t%1").arg(manufacturer));
    response.append(QString("Backend version:\twfview"));
    response.append(QString("Backend copyright:\twfview contributors"));
    if (rigCaps->hasTransmit) {
        response.append(QString("Rig type:\tTransceiver"));
    }
    else
    {
        response.append(QString("Rig type:\tReceiver"));
    }
    if(rigCaps->commands.contains(funcTransceiverStatus)) {
        response.append(QString("PTT type:\tRig capable"));
    }
    response.append(QString("DCD type:\tRig capable"));
    response.append(QString("Port type:\tNetwork link"));
    return RIG_OK;
}

QStringList rigCtlClient::buildPrefixes(commandStruct cmd,bool extended)
{
    QStringList prefixes;
    if (extended) {
        if (cmd.arg1 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg1));
        if (cmd.arg2 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg2));
        if (cmd.arg3 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg3));
        if (cmd.arg4 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg4));
        if (cmd.arg5 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg5));
        if (cmd.arg6 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg6));
    } else {
        if (cmd.arg1 != NULL)
            prefixes.append("");
        if (cmd.arg2 != NULL)
            prefixes.append("");
        if (cmd.arg3 != NULL)
            prefixes.append("");
        if (cmd.arg4 != NULL)
            prefixes.append("");
        if (cmd.arg5 != NULL)
            prefixes.append("");
        if (cmd.arg6 != NULL)
            prefixes.append("");
    }
    return prefixes;
}

int rigCtlClient::power2mW(QStringList& response, bool extended, const commandStruct cmd, QStringList params )
{
    int ret=RIG_OK;
    QStringList prefixes = buildPrefixes(cmd,extended);
    rigStateType state = queue->getState();
    if (params.size() >= 2)
    {
        bool ok=false;
        float power = params[0].toFloat(&ok);
        quint64 freq = params[1].toULongLong(&ok);
        if (ok)
        {
            if (freq == 0)
            {
                // Frequency was zero, so get cached freq
                freq = queue->getCache(queue->getVfoCommand(state.vfo,state.receiver,false).freqFunc,state.receiver).value.value<freqt>().Hz;
            }
            for (auto &b: rigCaps->bands)
            {
                // Highest frequency band is always first!
                if (freq >= b.lowFreq && freq <= b.highFreq)
                {
                    // This frequency is contained within this band!
                    int p = int((b.power * power) * 1000);
                    if (prefixes.length()>3)
                        response.append(QString("%0%1").arg(prefixes[3],QString::number(p)));
                    break;
                }
            }
        }
        else
        {
            ret = RIG_EINVAL;
        }
    } else {
        ret = RIG_EINVAL;
    }
    return ret;
}

int rigCtlClient::mW2power(QStringList& response, bool extended, const commandStruct cmd, QStringList params )
{
    int ret=RIG_OK;
    QStringList prefixes = buildPrefixes(cmd,extended);
    rigStateType state = queue->getState();
    if (params.size() > 2)
    {
        bool ok=false;
        int power = params[0].toInt(&ok);
        quint64 freq = params[1].toULongLong(&ok);
        if (ok)
        {
            if (freq == 0)
            {
                // Frequency was zero, so get cached freq
                freq = queue->getCache(queue->getVfoCommand(state.vfo,state.receiver,false).freqFunc,state.receiver).value.value<freqt>().Hz;
            }
            for (auto &b: rigCaps->bands)
            {
                // Highest frequency band is always first!
                if (freq >= b.lowFreq && freq <= b.highFreq)
                {
                    // This frequency is contained within this band!
                    float p = float(power / (b.power * 1000.0));
                    if (prefixes.length()>3)
                        response.append(QString("%0%1").arg(prefixes[3],QString::number(p,'f',5)));
                    break;
                }
            }
        }
        else
        {
            ret = RIG_EINVAL;
        }

    } else {
        ret = RIG_EINVAL;
    }

    return ret;
}
