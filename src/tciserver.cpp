#include "tciserver.h"

#include "logcategories.h"

#include <QtGlobal>



// Based on TCI v1.9 commands
static const tciCommandStruct tci_commands[] =
{
    // u=uchar s=short f=float b=bool x=hz m=mode s=string
    { "vfo_limits",         funcNone,       typeFreq,   typeFreq,   typeNone},
    { "if_limits",          funcNone,       typeFreq,   typeFreq,   typeNone},
    { "trx_count",          funcNone,       typeUChar,  typeNone,   typeNone},
    { "channel_count",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "device",             funcNone,       typeShort,  typeNone,   typeNone},
    { "receive_only",       funcNone,       typeBinary, typeNone,   typeNone},
    { "modulations_list",   funcNone,       typeShort,  typeNone,   typeNone},
    { "protocol",           funcNone,       typeShort,  typeNone,   typeNone},
    { "ready",              funcNone,       typeNone,   typeNone,   typeNone},
    { "start",              funcNone,       typeNone,   typeNone,   typeNone},
    { "stop",               funcNone,       typeNone,   typeNone,   typeNone},
    { "dds",                funcNone,       typeUChar,  typeFreq,   typeNone},
    { "if",                 funcNone,       typeUChar,  typeUChar,  typeFreq},
    { "vfo",                funcFreq,   typeUChar,  typeUChar,  typeFreq},
    { "modulation",         funcMode,   typeUChar,  typeMode,   typeNone},
    { "trx",                funcTransceiverStatus,       typeUChar, typeBinary, typeNone},
    { "tune",               funcTunerStatus,typeUChar,  typeBinary, typeNone},
    { "drive",              funcRFPower,    typeUShort,  typeNone,   typeNone},
    { "tune_drive",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "rit_enable",         funcRitStatus,  typeUChar,  typeBinary, typeNone},
    { "xit_enable",         funcXitStatus,  typeUChar,  typeBinary, typeNone},
    { "split_enable",       funcSplitStatus,typeUChar,  typeBinary, typeNone},
    { "rit_offset",         funcRitFreq,    typeUChar,  typeShort,  typeNone},
    { "xit_offset",         funcXitFreq,    typeUChar,  typeShort,  typeNone},
    { "rx_channel_enable",  funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_filter_band",     funcFilterWidth,typeUChar,  typeShort,  typeShort},
    { "cw_macros_speed",    funcNone,       typeUChar,  typeNone,   typeNone},
    { "cw_macros_delay",    funcNone,       typeUChar,  typeNone,   typeNone},
    { "cw_keyer_speed",     funcKeySpeed,   typeUChar,  typeNone,   typeNone},
    { "volume",             funcAfGain,     typeUChar,  typeNone,   typeNone},
    { "mute",               funcAFMute,     typeBinary, typeNone,   typeNone},
    { "rx_mute",            funcAFMute,     typeUChar,  typeBinary, typeNone},
    { "rx_volume",          funcAfGain,     typeUChar,  typeUChar,  typeShort},
    { "rx_balance",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "mon_volume",         funcMonitorGain,typeShort,  typeNone,   typeNone},
    { "mon_enable",         funcMonitor,    typeBinary, typeNone,   typeNone},
    { "agc_mode",           funcAGC,        typeUChar,  typeString, typeNone},
    { "agc_gain",           funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_nb_enable",       funcNoiseBlanker,typeUChar, typeBinary, typeNone},
    { "rx_nb_param",        funcNBLevel,    typeUChar,  typeUChar,  typeUShort},
    { "rx_bin_enable",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_nr_enable",       funcNoiseReduction,typeUChar,typeBinary,typeNone},
    { "rx_anc_enable",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_anf_enable",      funcAutoNotch,  typeUChar,  typeBinary, typeNone},
    { "rx_apf_enable",      funcAudioPeakFilter,typeUChar,typeBinary,typeNone},
    { "rx_dse_enable",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_nf_enable",       funcManualNotch,typeUChar,  typeBinary, typeNone},
    { "lock",               funcDialLock,   typeUChar,  typeBinary, typeNone},
    { "sql_enable",         funcSMeterSqlStatus,typeUChar,typeBinary,typeNone},
    { "sql_level",          funcSquelch,    typeUChar,  typeShort,  typeNone},
    { "tx_enable",          funcNone,       typeUChar,  typeUChar,  typeNone},
    { "cw_macros_speed_up", funcNone,       typeUChar,  typeUChar,  typeNone},
    { "cw_macros_speed_down", funcNone,     typeUChar,  typeUChar,  typeNone},
    { "spot",               funcNone,       typeUChar,  typeUChar,  typeNone},
    { "spot_delete",        funcNone,       typeUChar,  typeUChar,  typeNone},
    { "iq_samplerate",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "audio_samplerate",   funcNone,       typeUChar,  typeNone,   typeNone},
    { "iq_start",           funcNone,       typeUChar,  typeNone,   typeNone},
    { "iq_stop",            funcNone,       typeUChar,  typeNone,   typeNone},
    { "audio_start",        funcNone,       typeUChar,  typeNone,   typeNone},
    { "audio_stop",         funcNone,       typeUChar,  typeNone,   typeNone},
    { "line_out_start",     funcNone,       typeUChar,  typeNone,   typeNone},
    { "line_out_stop",      funcNone,       typeUChar,  typeNone,   typeNone},
    { "line_out_recorder_start", funcNone,  typeUChar,  typeNone,   typeNone},
    { "line_out_recorder_save",  funcNone,  typeUChar,  typeNone,   typeNone},
    { "line_out_recorder_start", funcNone,  typeUChar,  typeNone,   typeNone},
    { "clicked_on_spot",    funcNone,       typeUChar,  typeNone,   typeNone},
    { "rx_clicked_on_spot", funcNone,       typeUChar,  typeNone,   typeNone},
    { "tx_footswitch",      funcNone,       typeUChar,  typeBinary,   typeNone},
    { "tx_frequency",       funcNone,       typeUChar,  typeBinary,   typeNone},
    { "app_focus",          funcNone,       typeUChar,  typeBinary,   typeNone},
    { "set_in_focus",       funcNone,       typeUChar,  typeBinary,   typeNone},
    { "keyer",              funcNone,       typeUChar,  typeBinary,   typeNone},
    { "rx_sensors_enable",  funcNone,       typeUChar,  typeBinary,   typeNone},
    { "tx_sensors_enable",  funcNone,       typeUChar,  typeBinary,   typeNone},
    { "rx_sensors",         funcNone,       typeUChar,  typeBinary,   typeNone},
    { "tx_sensors",         funcNone,       typeUChar,  typeBinary,   typeNone},
    { "audio_stream_sample_type", funcNone, typeUChar,  typeBinary,   typeNone},
    { "audio_stream_channels", funcNone,    typeUChar,  typeBinary,   typeNone},
    { "audio_stream_samples", funcNone,     typeUChar,  typeBinary,   typeNone},
    { "digl_offset",        funcNone,       typeUChar,  typeBinary,   typeNone},
    { "digu_offset",        funcNone,       typeUChar,  typeBinary,   typeNone},
    { "rx_smeter",          funcSMeter,     typeUChar,  typeUChar,    typeDouble},
    { 0x0,                   funcNone,       typeNone,   typeNone,     typeNone },
};

#define MAXNAMESIZE 32

tciServer::tciServer(QObject *parent) :
    QObject(parent)
{

}

void tciServer::init(quint16 port) {

    server = new QWebSocketServer(QStringLiteral("TCI Server"), QWebSocketServer::NonSecureMode, this);

    if (server->listen(QHostAddress::Any,port)) {
        qInfo() << "tciServer() listening on" << port;
        connect (server, &QWebSocketServer::newConnection, this, &tciServer::onNewConnection);
        connect (server, &QWebSocketServer::closed, this, &tciServer::socketDisconnected);
    }

    this->setObjectName("TCI Server");
    queue = cachingQueue::getInstance();
    rigCaps = queue->getRigCaps();

    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));
    connect(queue,SIGNAL(cacheUpdated(cacheItem)),this,SLOT(receiveCache(cacheItem)));

    // Setup txChrono packet.
    txChrono.resize(iqHeaderSize);
    dataStream *pStream = reinterpret_cast<dataStream*>(txChrono.data());
    pStream->receiver = 0;
    pStream->sampleRate = 48000;
    pStream->format = IqFloat32;
    pStream->codec = 0u;
    pStream->type = TxChrono;
    pStream->crc = 0u;
    pStream->length = TCI_AUDIO_LENGTH;
    pStream->channels = 1u;

}

void tciServer::setupTxPacket(int length)
{
    Q_UNUSED(length)
}

tciServer::~tciServer()
{
    server->close();
    auto it = clients.begin();

    while (it != clients.end())
    {
        it.key()->deleteLater();
        it = clients.erase(it);
    }
}

void tciServer::receiveRigCaps(rigCapabilities *caps)
{
    rigCaps = caps;
}

void tciServer::onNewConnection()
{
    
    QWebSocket *pSocket = server->nextPendingConnection();

    if (rigCaps == nullptr)
    {
        qWarning(logTCIServer()) << "No current rig connection, denying connection request.";
        pSocket->abort();
        return;
    }

    qInfo(logTCIServer()) << "Got rigCaps for" << rigCaps->modelName;

    connect(pSocket, &QWebSocket::textMessageReceived, this, &tciServer::processIncomingTextMessage);
    connect(pSocket, &QWebSocket::binaryMessageReceived, this, &tciServer::processIncomingBinaryMessage);
    connect(pSocket, &QWebSocket::disconnected, this, &tciServer::socketDisconnected);

    qInfo() << "TCI client connected:" << pSocket;
    clients.insert(pSocket,connStatus());
    sendInitialState(pSocket);
}

void tciServer::sendInitialState(QWebSocket *socket)
{
    if (!socket || !rigCaps)
        return;

    const auto sendTciText = [socket](const QString &text) {
        qDebug() << "TCI Text Message sent:" << text;
        socket->sendTextMessage(text);
    };

    sendTciText(QStringLiteral("protocol:WFVIEW,1.9;"));
    sendTciText(QStringLiteral("device:%1;").arg(rigCaps->modelName));
    sendTciText(QStringLiteral("receive_only:%1;").arg(rigCaps->hasTransmit ? "false" : "true"));
    sendTciText(QStringLiteral("trx_count:%1;").arg(rigCaps->numReceiver));
    sendTciText(QStringLiteral("channels_count:%1;").arg(rigCaps->numVFO));
    sendTciText(QStringLiteral("channel_count:%1;").arg(rigCaps->numVFO));

    quint64 start=UINT64_MAX;
    quint64 end=0;
    for (auto &band: rigCaps->bands)
    {
        if (start > band.lowFreq)
            start = band.lowFreq;
        if (end < band.highFreq)
            end = band.highFreq;
    }
    if (start == UINT64_MAX)
        start = 0;
    sendTciText(QStringLiteral("vfo_limits:%1,%2;").arg(start).arg(end));
    sendTciText(QStringLiteral("if_limits:-19531,19531;"));

    QString mods = "modulations_list:";
    for (modeInfo &mi: rigCaps->modes)
    {

        mods+=mi.name.toUpper();
        mods+=",";
        if (mi.reg == modeUSB || mi.reg == modeLSB)
        {
            mods+=QString("DIG%0").arg(mi.name.at(0));
            mods+=",";
        }
    }
    mods.chop(1);
    mods+=";";
    sendTciText(mods);
    for (uchar rx = 0; rx < rigCaps->numReceiver; ++rx)
    {
        vfoCommandType rxVfo = queue->getVfoCommand(vfoA,rx,false);
        sendTciText(QStringLiteral("vfo:%1,0,%2;").arg(rx).arg(cacheFreq(rxVfo.freqFunc,rxVfo.receiver).Hz));
        sendTciText(QStringLiteral("modulation:%1,%2;").arg(rx).arg(tciMode(cacheMode(rxVfo.modeFunc,rxVfo.receiver))));
        sendTciText(QStringLiteral("trx:%1,%2;").arg(rx).arg(cacheBool(funcTransceiverStatus,rx) ? "true" : "false"));
        sendTciText(QStringLiteral("split_enable:%1,%2;").arg(rx).arg(cacheBool(funcSplitStatus,rx) ? "true" : "false"));
        sendTciText(QStringLiteral("rx_channel_enable:%1,0,true;").arg(rx));
        sendTciText(QStringLiteral("tx_enable:%1,%2;").arg(rx).arg(rigCaps->hasTransmit ? "true" : "false"));
    }
    sendTciText(QStringLiteral("iq_samplerate:48000;"));
    sendTciText(QStringLiteral("audio_samplerate:48000;"));
    sendTciText(QStringLiteral("audio_stream_sample_type:float32;"));
    sendTciText(QStringLiteral("audio_stream_channels:2;"));
    sendTciText(QStringLiteral("audio_stream_samples:2048;"));
    sendTciText(QStringLiteral("tx_stream_audio_buffering:50;"));
    sendTciText(QStringLiteral("volume:%1;").arg(toTciRange(funcAfGain, -60, 0, 0)));
    sendTciText(QStringLiteral("mute:%1;").arg(cacheBool(funcAFMute, 0) ? "true" : "false"));
    sendTciText(QStringLiteral("mon_volume:%1;").arg(toTciRange(funcMonitorGain, -60, 0, 0)));
    sendTciText(QStringLiteral("mon_enable:%1;").arg(cacheBool(funcMonitor, 0) ? "true" : "false"));
    sendTciText(QStringLiteral("start;"));
    sendTciText(QStringLiteral("ready;"));
}

int tciServer::getValueRange(funcs func, char min, char max,uchar rx)
{
    int val=min;
    if (rigCaps->commands.contains(func))
    {
        auto cmd = rigCaps->commands.find(func);
        float r = min + (float(queue->getCache(func,rx).value.toInt() - cmd->minVal) * (max - min)) / (float)(cmd->maxVal - cmd->minVal);
        val =  static_cast<int>(r + (r < 0 ? -0.5f : 0.5f));   // round to nearest
    }
    return val;
}

int tciServer::fromTciRange(funcs func, int value, int min, int max) const
{
    if (!rigCaps || !rigCaps->commands.contains(func) || max == min)
        return value;

    const auto cmd = rigCaps->commands.value(func);
    const double scaled = cmd.minVal + (double(value - min) * double(cmd.maxVal - cmd.minVal)) / double(max - min);
    return qBound(int(cmd.minVal), int(scaled + (scaled < 0.0 ? -0.5 : 0.5)), int(cmd.maxVal));
}

int tciServer::toTciRange(funcs func, int min, int max, uchar rx) const
{
    if (!rigCaps || !rigCaps->commands.contains(func) || max == min)
        return min;

    const auto cmd = rigCaps->commands.value(func);
    const int raw = cacheInt(func, rx, cmd.minVal);
    if (cmd.maxVal == cmd.minVal)
        return min;

    const double scaled = min + (double(raw - cmd.minVal) * double(max - min)) / double(cmd.maxVal - cmd.minVal);
    return qBound(min, int(scaled + (scaled < 0.0 ? -0.5 : 0.5)), max);
}

QVariant tciServer::cacheValue(funcs func, uchar receiver) const
{
    return queue ? queue->getCache(func, receiver).value : QVariant();
}

bool tciServer::cacheBool(funcs func, uchar receiver, bool fallback) const
{
    const QVariant value = cacheValue(func, receiver);
    return value.isValid() ? value.toBool() : fallback;
}

int tciServer::cacheInt(funcs func, uchar receiver, int fallback) const
{
    const QVariant value = cacheValue(func, receiver);
    return value.isValid() ? value.toInt() : fallback;
}

double tciServer::cacheDouble(funcs func, uchar receiver, double fallback) const
{
    const QVariant value = cacheValue(func, receiver);
    return value.isValid() ? value.toDouble() : fallback;
}

freqt tciServer::cacheFreq(funcs func, uchar receiver) const
{
    const QVariant value = cacheValue(func, receiver);
    return value.isValid() ? value.value<freqt>() : freqt();
}

modeInfo tciServer::cacheMode(funcs func, uchar receiver) const
{
    const QVariant value = cacheValue(func, receiver);
    return value.isValid() ? value.value<modeInfo>() : modeInfo();
}

void tciServer::queueIfSupported(funcs func, const QVariant &value, uchar receiver, bool unique)
{
    if (!queue || !rigCaps || func == funcNone || !value.isValid() || !rigCaps->commands.contains(func))
        return;

    const queueItem item(func, value, false, receiver);
    if (unique)
        queue->addUnique(priorityImmediate, item);
    else
        queue->add(priorityImmediate, item);
}

void tciServer::broadcastText(const QString &message, QWebSocket *except)
{
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (it.value().connected && it.key() != except)
            it.key()->sendTextMessage(message);
    }
}

void tciServer::processIncomingTextMessage(QString message)
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());

    auto it = clients.find(pClient);

    if (it == clients.end())
    {
        qInfo() << "Cannot find status for connection" << pClient;
        return;
    }

    it.value().connected = true;

    qDebug() << "TCI Text Message received:" << message;
    const QStringList commands = message.split(';', Qt::SkipEmptyParts);
    for (const QString &rawCommand: commands)
        processCommand(pClient, rawCommand.trimmed());
}

void tciServer::processCommand(QWebSocket *client, const QString &rawCommand)
{
    if (!client || rawCommand.isEmpty())
        return;

    const int separator = rawCommand.indexOf(':');
    const QString command = (separator >= 0 ? rawCommand.left(separator) : rawCommand).trimmed().toLower();
    const QString payload = separator >= 0 ? rawCommand.mid(separator + 1) : QString();
    QStringList args = payload.isEmpty() ? QStringList() : payload.split(',', Qt::KeepEmptyParts);
    for (QString &arg: args)
        arg = arg.trimmed();

    updateClientState(client, command, args);

    const bool changed = setCommandValue(command, args);
    if (changed)
        return;

    const QString reply = commandReply(command, args);
    if (!reply.isEmpty()) {
        qDebug() << "Reply:" << reply;
        client->sendTextMessage(reply);
    }
}

void tciServer::updateClientState(QWebSocket *client, const QString &command, const QStringList &args)
{
    auto it = clients.find(client);
    if (it == clients.end())
        return;

    if (command == QLatin1String("audio_start"))
        it.value().rxaudio = true;
    else if (command == QLatin1String("audio_stop"))
        it.value().rxaudio = false;
    else if (command == QLatin1String("iq_start"))
        it.value().iqaudio = true;
    else if (command == QLatin1String("iq_stop"))
        it.value().iqaudio = false;
    else if (command == QLatin1String("line_out_start"))
        it.value().lineout = true;
    else if (command == QLatin1String("line_out_stop"))
        it.value().lineout = false;
    else if (command == QLatin1String("rx_sensors_enable") && args.size() >= 2)
        it.value().rxSensors = args[1].compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    else if (command == QLatin1String("tx_sensors_enable") && args.size() >= 2)
        it.value().txSensors = args[1].compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    else if (command == QLatin1String("audio_samplerate") && !args.isEmpty())
        it.value().audioSampleRate = args[0].toInt();
    else if (command == QLatin1String("audio_stream_channels") && !args.isEmpty())
        it.value().audioChannels = qBound(1, args[0].toInt(), 2);
    else if (command == QLatin1String("audio_stream_samples") && !args.isEmpty())
        it.value().audioSamples = qBound(100, args[0].toInt(), 2048);
    else if (command == QLatin1String("tx_stream_audio_buffering") && !args.isEmpty())
        it.value().txAudioBuffering = qBound(50, args[0].toInt(), 500);
    else if (command == QLatin1String("audio_stream_sample_type") && !args.isEmpty()) {
        const QString sampleType = args[0].toLower();
        if (sampleType == QLatin1String("int16"))
            it.value().audioSampleType = IqInt16;
        else if (sampleType == QLatin1String("int24"))
            it.value().audioSampleType = IqInt24;
        else if (sampleType == QLatin1String("int32"))
            it.value().audioSampleType = IqInt32;
        else
            it.value().audioSampleType = IqFloat32;
    }
}

bool tciServer::setCommandValue(const QString &command, const QStringList &args)
{
    if (!queue || !rigCaps || args.isEmpty())
        return false;

    const auto isBool = [](const QString &value) {
        return value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 ||
               value.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0;
    };
    const auto boolValue = [](const QString &value) {
        return value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    };

    if (command == QLatin1String("split_enable") && args.size() == 1 && isBool(args[0]))
        return false; // Newer clients use this as a status probe; do not change split.

    const uchar rx = uchar(args.value(0).toUInt());
    const uchar vfo = uchar(args.value(1).toUInt());
    const vfoCommandType vfoSet = queue->getVfoCommand(vfo_t(vfo), rx, true);

    if (command == QLatin1String("vfo") && args.size() >= 3) {
        queueIfSupported(vfoSet.freqFunc, QVariant::fromValue<freqt>(freqt(args[2].toULongLong(), args[2].toDouble() / 1E6, activeVFO)), vfoSet.receiver, true);
        return true;
    }
    if (command == QLatin1String("modulation") && args.size() >= 2) {
        queueIfSupported(vfoSet.modeFunc, QVariant::fromValue<modeInfo>(rigMode(args[1])), vfoSet.receiver);
        return true;
    }
    if (command == QLatin1String("trx") && args.size() >= 2) {
        queueIfSupported(funcTransceiverStatus, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("tune") && args.size() >= 2) {
        queueIfSupported(funcTunerStatus, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("drive") && args.size() >= 2) {
        queueIfSupported(funcRFPower, QVariant::fromValue<ushort>(ushort(fromTciRange(funcRFPower, args[1].toInt(), 0, 100))), rx, true);
        return true;
    }
    if (command == QLatin1String("rit_enable") && args.size() >= 2) {
        queueIfSupported(funcRitStatus, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("xit_enable") && args.size() >= 2) {
        queueIfSupported(funcXitStatus, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("split_enable") && args.size() >= 2 && isBool(args[1])) {
        queueIfSupported(funcSplitStatus, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("rit_offset") && args.size() >= 2) {
        queueIfSupported(funcRitFreq, QVariant::fromValue<short>(short(args[1].toInt())), rx, true);
        return true;
    }
    if (command == QLatin1String("xit_offset") && args.size() >= 2) {
        queueIfSupported(funcXitFreq, QVariant::fromValue<short>(short(args[1].toInt())), rx, true);
        return true;
    }
    if (command == QLatin1String("rx_filter_band") && args.size() >= 3) {
        queueIfSupported(funcFilterWidth, QVariant::fromValue<ushort>(ushort(qAbs(args[2].toInt() - args[1].toInt()))), rx, true);
        return true;
    }
    if (command == QLatin1String("cw_keyer_speed") && !args.isEmpty()) {
        queueIfSupported(funcKeySpeed, QVariant::fromValue<ushort>(ushort(args[0].toUInt())), 0, true);
        return true;
    }
    if (command == QLatin1String("volume") && !args.isEmpty()) {
        queueIfSupported(funcAfGain, QVariant::fromValue<ushort>(ushort(fromTciRange(funcAfGain, args[0].toInt(), -60, 0))), 0, true);
        return true;
    }
    if (command == QLatin1String("mute") && !args.isEmpty()) {
        queueIfSupported(funcAFMute, QVariant::fromValue(boolValue(args[0])), 0);
        return true;
    }
    if (command == QLatin1String("rx_mute") && args.size() >= 2) {
        queueIfSupported(funcAFMute, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("rx_volume") && args.size() >= 3) {
        queueIfSupported(funcAfGain, QVariant::fromValue<ushort>(ushort(fromTciRange(funcAfGain, args[2].toInt(), -60, 0))), rx, true);
        return true;
    }
    if (command == QLatin1String("mon_volume") && !args.isEmpty()) {
        queueIfSupported(funcMonitorGain, QVariant::fromValue<ushort>(ushort(fromTciRange(funcMonitorGain, args[0].toInt(), -60, 0))), 0, true);
        return true;
    }
    if (command == QLatin1String("mon_enable") && !args.isEmpty()) {
        queueIfSupported(funcMonitor, QVariant::fromValue(boolValue(args[0])), 0);
        return true;
    }
    if (command == QLatin1String("rx_nb_enable") && args.size() >= 2) {
        queueIfSupported(funcNoiseBlanker, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("rx_nr_enable") && args.size() >= 2) {
        queueIfSupported(funcNoiseReduction, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("rx_anf_enable") && args.size() >= 2) {
        queueIfSupported(funcAutoNotch, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("rx_apf_enable") && args.size() >= 2) {
        queueIfSupported(funcAudioPeakFilter, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("rx_nf_enable") && args.size() >= 2) {
        queueIfSupported(funcManualNotch, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if ((command == QLatin1String("lock") || command == QLatin1String("vfo_lock")) && args.size() >= 2) {
        queueIfSupported(funcDialLock, QVariant::fromValue(boolValue(args.last())), rx);
        return true;
    }
    if (command == QLatin1String("sql_enable") && args.size() >= 2) {
        queueIfSupported(funcSMeterSqlStatus, QVariant::fromValue(boolValue(args[1])), rx);
        return true;
    }
    if (command == QLatin1String("sql_level") && args.size() >= 2) {
        queueIfSupported(funcSquelch, QVariant::fromValue<ushort>(ushort(fromTciRange(funcSquelch, args[1].toInt(), -140, 0))), rx, true);
        return true;
    }

    return false;
}

QString tciServer::commandReply(const QString &command, const QStringList &args) const
{
    if (!queue)
        return {};

    uchar rx = uchar(args.value(0).toUInt());
    uchar vfo = uchar(args.value(1).toUInt());

    if (command == QLatin1String("split_enable") && args.size() == 1 &&
        (args[0].compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 ||
         args[0].compare(QLatin1String("false"), Qt::CaseInsensitive) == 0))
        rx = 0;

    const vfoCommandType vfoRead = queue->getVfoCommand(vfo_t(vfo), rx, false);

    if (command == QLatin1String("start") || command == QLatin1String("stop") ||
        command == QLatin1String("keyer") || command == QLatin1String("app_focus") ||
        command == QLatin1String("set_in_focus"))
        return command + QStringLiteral(";");
    if (command == QLatin1String("vfo"))
        return QStringLiteral("vfo:%1,%2,%3;").arg(rx).arg(vfo).arg(cacheFreq(vfoRead.freqFunc, vfoRead.receiver).Hz);
    if (command == QLatin1String("modulation"))
        return QStringLiteral("modulation:%1,%2;").arg(rx).arg(tciMode(cacheMode(vfoRead.modeFunc, vfoRead.receiver)));
    if (command == QLatin1String("trx"))
        return QStringLiteral("trx:%1,%2;").arg(rx).arg(cacheBool(funcTransceiverStatus, rx) ? "true" : "false");
    if (command == QLatin1String("tune"))
        return QStringLiteral("tune:%1,%2;").arg(rx).arg(cacheBool(funcTunerStatus, rx) ? "true" : "false");
    if (command == QLatin1String("drive"))
        return QStringLiteral("drive:%1,%2;").arg(rx).arg(toTciRange(funcRFPower, 0, 100, rx));
    if (command == QLatin1String("rit_enable"))
        return QStringLiteral("rit_enable:%1,%2;").arg(rx).arg(cacheBool(funcRitStatus, rx) ? "true" : "false");
    if (command == QLatin1String("xit_enable"))
        return QStringLiteral("xit_enable:%1,%2;").arg(rx).arg(cacheBool(funcXitStatus, rx) ? "true" : "false");
    if (command == QLatin1String("split_enable"))
        return QStringLiteral("split_enable:%1,%2;").arg(rx).arg(cacheBool(funcSplitStatus, rx) ? "true" : "false");
    if (command == QLatin1String("rit_offset"))
        return QStringLiteral("rit_offset:%1,%2;").arg(rx).arg(cacheInt(funcRitFreq, rx));
    if (command == QLatin1String("xit_offset"))
        return QStringLiteral("xit_offset:%1,%2;").arg(rx).arg(cacheInt(funcXitFreq, rx));
    if (command == QLatin1String("rx_filter_band")) {
        const int width = cacheInt(funcFilterWidth, rx, 0);
        return QStringLiteral("rx_filter_band:%1,%2,%3;").arg(rx).arg(-width / 2).arg(width / 2);
    }
    if (command == QLatin1String("cw_keyer_speed") || command == QLatin1String("cw_macros_speed"))
        return QStringLiteral("%1:%2;").arg(command).arg(cacheInt(funcKeySpeed, 0));
    if (command == QLatin1String("volume"))
        return QStringLiteral("volume:%1;").arg(toTciRange(funcAfGain, -60, 0, 0));
    if (command == QLatin1String("mute"))
        return QStringLiteral("mute:%1;").arg(cacheBool(funcAFMute, 0) ? "true" : "false");
    if (command == QLatin1String("rx_mute"))
        return QStringLiteral("rx_mute:%1,%2;").arg(rx).arg(cacheBool(funcAFMute, rx) ? "true" : "false");
    if (command == QLatin1String("rx_volume"))
        return QStringLiteral("rx_volume:%1,%2,%3;").arg(rx).arg(vfo).arg(toTciRange(funcAfGain, -60, 0, rx));
    if (command == QLatin1String("mon_volume"))
        return QStringLiteral("mon_volume:%1;").arg(toTciRange(funcMonitorGain, -60, 0, 0));
    if (command == QLatin1String("mon_enable"))
        return QStringLiteral("mon_enable:%1;").arg(cacheBool(funcMonitor, 0) ? "true" : "false");
    if (command == QLatin1String("rx_nb_enable"))
        return QStringLiteral("rx_nb_enable:%1,%2;").arg(rx).arg(cacheBool(funcNoiseBlanker, rx) ? "true" : "false");
    if (command == QLatin1String("rx_nr_enable"))
        return QStringLiteral("rx_nr_enable:%1,%2;").arg(rx).arg(cacheBool(funcNoiseReduction, rx) ? "true" : "false");
    if (command == QLatin1String("rx_anf_enable"))
        return QStringLiteral("rx_anf_enable:%1,%2;").arg(rx).arg(cacheBool(funcAutoNotch, rx) ? "true" : "false");
    if (command == QLatin1String("rx_apf_enable"))
        return QStringLiteral("rx_apf_enable:%1,%2;").arg(rx).arg(cacheBool(funcAudioPeakFilter, rx) ? "true" : "false");
    if (command == QLatin1String("rx_nf_enable"))
        return QStringLiteral("rx_nf_enable:%1,%2;").arg(rx).arg(cacheBool(funcManualNotch, rx) ? "true" : "false");
    if (command == QLatin1String("lock"))
        return QStringLiteral("lock:%1,%2;").arg(rx).arg(cacheBool(funcDialLock, rx) ? "true" : "false");
    if (command == QLatin1String("vfo_lock"))
        return QStringLiteral("vfo_lock:%1,%2,%3;").arg(rx).arg(vfo).arg(cacheBool(funcDialLock, rx) ? "true" : "false");
    if (command == QLatin1String("sql_enable"))
        return QStringLiteral("sql_enable:%1,%2;").arg(rx).arg(cacheBool(funcSMeterSqlStatus, rx) ? "true" : "false");
    if (command == QLatin1String("sql_level"))
        return QStringLiteral("sql_level:%1,%2;").arg(rx).arg(toTciRange(funcSquelch, -140, 0, rx));
    if (command == QLatin1String("audio_start") || command == QLatin1String("audio_stop") ||
        command == QLatin1String("iq_start") || command == QLatin1String("iq_stop") ||
        command == QLatin1String("line_out_start") || command == QLatin1String("line_out_stop") ||
        command == QLatin1String("audio_samplerate") || command == QLatin1String("iq_samplerate") ||
        command == QLatin1String("audio_stream_sample_type") || command == QLatin1String("audio_stream_channels") ||
        command == QLatin1String("audio_stream_samples") || command == QLatin1String("tx_stream_audio_buffering") ||
        command == QLatin1String("rx_sensors_enable") || command == QLatin1String("tx_sensors_enable"))
        return command + (args.isEmpty() ? QStringLiteral(";") : QStringLiteral(":") + args.join(',') + QStringLiteral(";"));

    return {};
}

void tciServer::processIncomingBinaryMessage(QByteArray message)
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (pClient) {
        if (message.size() < qsizetype(iqHeaderSize))
            return;
        dataStream *pStream = reinterpret_cast<dataStream*>(message.data());
        const qsizetype headerSize = qsizetype(iqHeaderSize);
        const qsizetype payloadSize = qsizetype(pStream->length) * qsizetype(sizeof(float));
        if (message.size() >= headerSize && pStream->type == TxAudioStream && pStream->length > 0 &&
            message.size() >= headerSize + payloadSize)
        {
            QByteArray tempData(payloadSize,0x0);
            memcpy(tempData.data(),pStream->data,tempData.size());
            //qInfo() << QString("Received audio from client: %0 Sample: %1 Format: %2 Length: %3(%4 bytes) Type: %5").arg(pStream->receiver).arg(pStream->sampleRate).arg(pStream->format).arg(pStream->length).arg(tempData.size()).arg(pStream->type);
            emit sendTCIAudio(tempData);
        }
    }
}

void tciServer::socketDisconnected()
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    qInfo() << "TCI client disconnected:" << pClient
            << "closeCode:" << (pClient ? pClient->closeCode() : QWebSocketProtocol::CloseCodeNormal)
            << "closeReason:" << (pClient ? pClient->closeReason() : QString())
            << "error:" << (pClient ? pClient->errorString() : QString());
    if (pClient) {
        clients.remove(pClient);
        pClient->deleteLater();
    }
}

void tciServer::receiveTCIAudio(audioPacket audio){
    /*
     *      quint32 receiver; //!< receiver’s periodic number
            quint32 sampleRate; //!< sample rate
            quint32 format; //!< always equals 4 (float 32 bit)
            quint32 codec; //!< compression algorithm (not implemented yet), always 0
            quint32 crc; //!< check sum (not implemented yet), always 0
            quint32 length; //!< length of data field
            quint32 type; //!< type of data stream
            quint32 reserv[9]; //!< reserved
            float data[4096]; //!< data field
    */

    //dataStream *pStream = reinterpret_cast<dataStream*>(audio.data());

    rxAudioData.resize(iqHeaderSize + audio.data.size());
    dataStream *pStream = reinterpret_cast<dataStream*>(rxAudioData.data());
    pStream->receiver = 0;
    pStream->sampleRate = 48000;
    pStream->format = IqFloat32;
    pStream->codec = 0u;
    pStream->type = RxAudioStream;
    pStream->crc = 0u;
    pStream->length = quint32(audio.data.size() / sizeof (float));
    pStream->channels = 1u;

    memcpy(pStream->data,audio.data.data(),audio.data.size());

    auto it = clients.begin();
    while (it != clients.end())
    {
        if (it.value().connected && it.value().rxaudio)
        {
            //qInfo() << "Sending audio to client:" << it.key() << "size" << audio.data.size();
            it.key()->sendBinaryMessage(txChrono);
            it.key()->sendBinaryMessage(rxAudioData);

        }
        ++it;
    }
}


void tciServer::receiveCache(cacheItem item)
{
    QStringList messages;
    funcs func = item.command;
    uchar vfo = 0;

    if (func == funcFreqTR || func == funcSelectedFreq || func == funcFreqGet || func == funcFreqSet)
        func = funcFreq;
    else if (func == funcUnselectedFreq) {
        func = funcFreq;
        vfo = 1;
    } else if (func == funcModeTR || func == funcSelectedMode || func == funcModeGet || func == funcModeSet)
        func = funcMode;
    else if (func == funcUnselectedMode) {
        func = funcMode;
        vfo = 1;
    }

    if (func == funcFreq) {
        const freqt freq = item.value.value<freqt>();
        if (rigCaps && rigCaps->numReceiver > 1 && item.receiver > 0)
            vfo = 1;
        messages << QStringLiteral("vfo:%1,%2,%3;").arg(item.receiver).arg(vfo).arg(freq.Hz);
        messages << QStringLiteral("tx_frequency:%1;").arg(freq.Hz);
        dBmConversion = freq.Hz > 30000000 ? 93 : 73;
    } else if (func == funcMode) {
        messages << QStringLiteral("modulation:%1,%2;").arg(item.receiver).arg(tciMode(item.value.value<modeInfo>()));
    } else if (func == funcTransceiverStatus) {
        messages << QStringLiteral("trx:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
        messages << QStringLiteral("tx_footswitch:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcTunerStatus) {
        messages << QStringLiteral("tune:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcSplitStatus) {
        messages << QStringLiteral("split_enable:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcRitStatus) {
        messages << QStringLiteral("rit_enable:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcXitStatus) {
        messages << QStringLiteral("xit_enable:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcRitFreq) {
        messages << QStringLiteral("rit_offset:%1,%2;").arg(item.receiver).arg(item.value.toInt());
    } else if (func == funcXitFreq) {
        messages << QStringLiteral("xit_offset:%1,%2;").arg(item.receiver).arg(item.value.toInt());
    } else if (func == funcRFPower) {
        messages << QStringLiteral("drive:%1,%2;").arg(item.receiver).arg(toTciRange(funcRFPower, 0, 100, item.receiver));
    } else if (func == funcKeySpeed) {
        messages << QStringLiteral("cw_keyer_speed:%1;").arg(item.value.toInt());
        messages << QStringLiteral("cw_macros_speed:%1;").arg(item.value.toInt());
    } else if (func == funcAfGain) {
        messages << QStringLiteral("volume:%1;").arg(toTciRange(funcAfGain, -60, 0, item.receiver));
        messages << QStringLiteral("rx_volume:%1,0,%2;").arg(item.receiver).arg(toTciRange(funcAfGain, -60, 0, item.receiver));
    } else if (func == funcAFMute) {
        messages << QStringLiteral("mute:%1;").arg(item.value.toBool() ? "true" : "false");
        messages << QStringLiteral("rx_mute:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcMonitorGain) {
        messages << QStringLiteral("mon_volume:%1;").arg(toTciRange(funcMonitorGain, -60, 0, item.receiver));
    } else if (func == funcMonitor) {
        messages << QStringLiteral("mon_enable:%1;").arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcSquelch) {
        messages << QStringLiteral("sql_level:%1,%2;").arg(item.receiver).arg(toTciRange(funcSquelch, -140, 0, item.receiver));
    } else if (func == funcSMeterSqlStatus) {
        messages << QStringLiteral("sql_enable:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcNoiseBlanker) {
        messages << QStringLiteral("rx_nb_enable:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcNoiseReduction) {
        messages << QStringLiteral("rx_nr_enable:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcAutoNotch) {
        messages << QStringLiteral("rx_anf_enable:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcAudioPeakFilter) {
        messages << QStringLiteral("rx_apf_enable:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcManualNotch) {
        messages << QStringLiteral("rx_nf_enable:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcDialLock) {
        messages << QStringLiteral("lock:%1,%2;").arg(item.receiver).arg(item.value.toBool() ? "true" : "false");
        messages << QStringLiteral("vfo_lock:%1,%2,%3;").arg(item.receiver).arg(vfo).arg(item.value.toBool() ? "true" : "false");
    } else if (func == funcFilterWidth) {
        const int width = item.value.toInt();
        messages << QStringLiteral("rx_filter_band:%1,%2,%3;").arg(item.receiver).arg(-width / 2).arg(width / 2);
    } else if (func == funcSMeter) {
        const double level = item.value.toDouble() - dBmConversion;
        messages << QStringLiteral("rx_sensors:%1,%2;").arg(item.receiver).arg(level, 0, 'f', 1);
        messages << QStringLiteral("rx_channel_sensors:%1,%2,%3;").arg(item.receiver).arg(vfo).arg(level, 0, 'f', 1);
    } else if (func == funcPowerMeter || func == funcSWRMeter || func == funcALCMeter || func == funcCompMeter) {
        const double mic = cacheDouble(funcALCMeter, item.receiver, 0.0);
        const double power = cacheDouble(funcPowerMeter, item.receiver, 0.0);
        const double swr = cacheDouble(funcSWRMeter, item.receiver, 1.0);
        messages << QStringLiteral("tx_sensors:%1,%2,%3,%4,%5;").arg(item.receiver).arg(mic, 0, 'f', 1).arg(power, 0, 'f', 1).arg(power, 0, 'f', 1).arg(swr, 0, 'f', 1);
    }

    if (messages.isEmpty())
        return;

    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (!it.value().connected)
            continue;
        for (const QString &message: messages) {
            if ((message.startsWith(QLatin1String("rx_sensors:")) || message.startsWith(QLatin1String("rx_channel_sensors:"))) && !it.value().rxSensors)
                continue;
            if (message.startsWith(QLatin1String("tx_sensors:")) && !it.value().txSensors)
                continue;
            it.key()->sendTextMessage(message);
        }
    }
}


QString tciServer::tciMode(modeInfo m) const
{
    QString ret="";
    if (m.mk == modeUSB && m.data && m.data != 0xff)
        ret="digu";
    else if (m.mk == modeLSB && m.data != 0xff)
        ret="digl";
    else if (m.mk == modeFM)
        ret="nfm";
    else
        ret = m.name.toLower();
    return ret;
}

modeInfo tciServer::rigMode(QString mode)
{
    modeInfo m;
    m.mk=modeUnknown;
    qInfo() << "Searching for mode" << mode;
    for (modeInfo &mi: rigCaps->modes)
    {
        if (mode.toLower() =="digl" && mi.mk == modeLSB)
        {
            m = modeInfo(mi);
            m.data = true;
            break;
        }
        else if (mode.toLower() == "digu" && mi.mk == modeUSB)
        {
            m = modeInfo(mi);
            m.data = true;
            break;
        } else if ((mode.toLower() == "nfm" && mi.mk == modeFM) || (mode.toLower() == mi.name.toLower()))
        {
            m = modeInfo(mi);
            m.data = false;
            break;
        }
    }
    m.filter = 1;
    qInfo(logTCIServer()) << "Got Mode:" << m.name << "data:" << m.data;
    return m;
}
