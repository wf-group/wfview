#ifdef BUILD_WFSERVER

#include "serverwizard.h"

#include <iostream>
#include <string>

#include <QCoreApplication>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QHostInfo>
#include <QSerialPortInfo>
#include <QUuid>
#include <QFontMetrics>
#include <QFont>

#include "audiodevices.h"
#include "wfviewtypes.h"
#include "icomudpbase.h" // passcode()

namespace {

QString prompt(const QString& msg)
{
    std::cout << msg.toStdString() << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        std::cout << "\nInput closed, aborting.\n";
        std::exit(1);
    }
    return QString::fromStdString(line).trimmed();
}

bool isPrintableAscii(const QString& s)
{
    for (QChar c : s) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7E) return false;
    }
    return true;
}

QString promptConstrained(const QString& msg, int maxLen, bool allowSpaces)
{
    while (true) {
        QString s = prompt(msg);
        if (s.isEmpty()) {
            std::cout << "Value required.\n";
            continue;
        }
        if (s.length() > maxLen) {
            std::cout << "Too long (max " << maxLen << " characters).\n";
            continue;
        }
        if (!isPrintableAscii(s)) {
            std::cout << "Only printable ASCII characters allowed.\n";
            continue;
        }
        if (!allowSpaces && s.contains(' ')) {
            std::cout << "Spaces are not allowed.\n";
            continue;
        }
        return s;
    }
}

int promptPort(const QString& label, int def)
{
    while (true) {
        QString s = prompt(QString("  %1 [%2]: ").arg(label).arg(def));
        if (s.isEmpty()) return def;
        bool ok = false;
        int v = s.toInt(&ok);
        if (ok && v > 0 && v < 65536) return v;
        std::cout << "Enter a valid port number (1-65535).\n";
    }
}

QString resolveSettingsPath(const QString& settingsFile)
{
    if (settingsFile.isNull() || settingsFile.isEmpty()) {
        QSettings probe;
        return probe.fileName();
    }
    QFileInfo info(settingsFile);
    if (info.isAbsolute()) return settingsFile;
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (path.isEmpty()) path = QDir::homePath();
    return path + "/" + info.fileName();
}

QSettings* openSettings(const QString& settingsFile)
{
    if (settingsFile.isNull() || settingsFile.isEmpty()) {
        return new QSettings();
    }
    QFileInfo info(settingsFile);
    if (info.isAbsolute()) {
        return new QSettings(settingsFile, QSettings::IniFormat);
    }
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (path.isEmpty()) path = QDir::homePath();
    return new QSettings(path + "/" + info.fileName(), QSettings::IniFormat);
}

int selectFromList(const QString& header, const QStringList& items)
{
    std::cout << header.toStdString() << "\n";
    for (int i = 0; i < items.size(); ++i) {
        std::cout << "  [" << i << "] " << items[i].toStdString() << "\n";
    }
    while (true) {
        QString s = prompt("Enter selection number: ");
        bool ok = false;
        int v = s.toInt(&ok);
        if (ok && v >= 0 && v < items.size()) return v;
        std::cout << "Invalid selection.\n";
    }
}

// Returns -2 if user entered "Q" to re-pick audio system.
int selectAudioDevice(const QString& header, const QStringList& items, const QString& footer = QString())
{
    std::cout << header.toStdString() << "\n";
    for (int i = 0; i < items.size(); ++i) {
        std::cout << "  [" << i << "] " << items[i].toStdString() << "\n";
    }
    std::cout << "  [Q] Re-select audio backend\n";
    if (!footer.isEmpty()) {
        std::cout << footer.toStdString() << "\n";
    }
    while (true) {
        QString s = prompt("Enter selection number (or Q): ");
        if (s.compare("q", Qt::CaseInsensitive) == 0) return -2;
        bool ok = false;
        int v = s.toInt(&ok);
        if (ok && v >= 0 && v < items.size()) return v;
        std::cout << "Invalid selection.\n";
    }
}

} // namespace

namespace serverwizard {

int run(const QString& settingsFile)
{
    const QString resolvedPath = resolveSettingsPath(settingsFile);

    std::cout << "\n=== wfserver interactive setup wizard ===\n";
    std::cout << "Settings file: " << resolvedPath.toStdString() << "\n\n";
    std::cout << "WARNING: continuing will ERASE any existing configuration at the\n";
    std::cout << "above path and replace it with the values you enter here.\n";
    {
        QString c = prompt("Press 'y' to continue, anything else to abort: ");
        if (c.compare("y", Qt::CaseInsensitive) != 0) {
            std::cout << "Aborted. No changes made.\n";
            return 1;
        }
    }

    // --- Serial port ---
    QStringList portLabels;
    QStringList portPaths;
    const auto available = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& p : available) {
        QString path;
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
        path = "/dev/" + p.portName();
#else
        path = p.portName();
#endif
        QString label = QString("%1  [%2 %3 S/N:%4]")
                            .arg(path, p.manufacturer(), p.description(), p.serialNumber());
        portLabels.append(label);
        portPaths.append(path);
    }
    const int autoIdx = -2;
    const int manualIdx = -3;

    std::cout << "\nAvailable serial ports:\n";
    for (int i = 0; i < portLabels.size(); ++i) {
        std::cout << "  [" << i << "] " << portLabels[i].toStdString() << "\n";
    }
    std::cout << "  [A] auto  (wfserver auto-detects an Icom serial port at startup)\n";
    std::cout << "  [M] Enter a serial port path manually\n";
    int portIdx = -1;
    while (portIdx == -1) {
        QString s = prompt("Enter selection: ");
        if (s.compare("a", Qt::CaseInsensitive) == 0) { portIdx = autoIdx; break; }
        if (s.compare("m", Qt::CaseInsensitive) == 0) { portIdx = manualIdx; break; }
        bool ok = false;
        int v = s.toInt(&ok);
        if (ok && v >= 0 && v < portLabels.size()) { portIdx = v; break; }
        std::cout << "Invalid selection.\n";
    }
    QString serialPort;
    if (portIdx == autoIdx) {
        serialPort = "auto";
    } else if (portIdx == manualIdx) {
        while (serialPort.isEmpty()) {
            serialPort = prompt("Enter serial port path (e.g. /dev/ttyUSB0 or COM3): ");
            if (serialPort.isEmpty()) std::cout << "Value required.\n";
        }
    } else {
        serialPort = portPaths[portIdx];
    }

    // --- Baud rate ---
    std::cout << "\nCommon baud rates: 4800, 9600, 19200, 38400, 57600, 115200\n";
    quint32 baud = 0;
    while (baud == 0) {
        QString s = prompt("Enter baud rate [115200]: ");
        if (s.isEmpty()) { baud = 115200; break; }
        bool ok = false;
        quint32 v = s.toUInt(&ok);
        if (ok && v > 0) baud = v;
        else std::cout << "Enter a positive integer.\n";
    }

    // --- PTT type ---
    bool forceRTSasPTT = false;
    pttType_t pttType = pttCIV;
    std::cout << "\nPTT method -- use RTS for older radios (ex. IC-718),\n";
    std::cout << "and use CI-V for more modern radios (ex. IC-7300).\n";
    while (true) {
        QString s = prompt("Enter 'rts' or 'civ' [CI-V]: ");
        if (s.isEmpty() || s.compare("civ", Qt::CaseInsensitive) == 0
            || s.compare("ci-v", Qt::CaseInsensitive) == 0) {
            forceRTSasPTT = false;
            pttType = pttCIV;
            break;
        }
        if (s.compare("rts", Qt::CaseInsensitive) == 0) {
            forceRTSasPTT = true;
            pttType = pttRTS;
            break;
        }
        std::cout << "Please enter 'rts' or 'civ'.\n";
    }

    // --- Audio backend + devices (loopable) ---
    // Menu values match the audioType enum (qtAudio=0, portAudio=1, rtAudio=2).
    audioType audioSystem = qtAudio;
    QString rxName, txName;

    while (true) {
        int menu;
        while (true) {
            QString s = prompt("\nAudio backend -- [0] Qt  [1] Port Audio  [2] RT Audio [0]: ");
            if (s.isEmpty()) { menu = 0; break; }
            bool ok = false;
            menu = s.toInt(&ok);
            if (ok && menu >= 0 && menu <= 2) break;
            std::cout << "Enter 0, 1, or 2.\n";
        }
        audioSystem = static_cast<audioType>(menu);

        audioDevices devs(audioSystem, QFontMetrics(QFont()));
        devs.enumerate();
        QStringList ins = devs.getInputs();
        QStringList outs = devs.getOutputs();

        if (ins.isEmpty() || outs.isEmpty()) {
            std::cout << "This audio backend reported no "
                      << (ins.isEmpty() ? "input" : "output")
                      << " devices. Please choose another backend.\n";
            continue;
        }

        int ri = selectAudioDevice("\nAvailable audio INPUT devices:", ins,
            "Note: The audio input should be the radio's receive audio.");
        if (ri == -2) continue;
        int ro = selectAudioDevice("\nAvailable audio OUTPUT devices:", outs,
            "Note: The audio output should go to the radio's transmit audio input.");
        if (ro == -2) continue;

        rxName = devs.getInputName(ri);
        txName = devs.getOutputName(ro);
        break;
    }

    // --- Rig name ---
    std::cout << "\nRig name (recommended: the radio model number, e.g. IC-7300).\n";
    std::cout << "No spaces, printable ASCII, max 16 characters.\n";
    QString rigName = promptConstrained("Rig name: ", 16, false);

    // --- CI-V address ---
    std::cout << "\nCI-V address.\n";
    std::cout << "It is strongly recommended to enable CI-V Transceive on the radio\n";
    std::cout << "and then set the CI-V address in wfserver to auto. To manually\n";
    std::cout << "define the CI-V address, type it here. You may type it as an integer\n";
    std::cout << "(ie, \"152\"), or a hexadecimal value (ie, \"0x98\" or \"h98\").\n";
    int civAddr = -1; // -1 => auto (do not write)
    while (true) {
        QString s = prompt("CI-V address [auto]: ");
        if (s.isEmpty() || s.compare("auto", Qt::CaseInsensitive) == 0) {
            civAddr = -1;
            break;
        }
        QString norm = s;
        int base = 10;
        if (norm.startsWith("0x", Qt::CaseInsensitive)) {
            norm = norm.mid(2);
            base = 16;
        } else if (norm.startsWith("h", Qt::CaseInsensitive)) {
            norm = norm.mid(1);
            base = 16;
        }
        bool ok = false;
        int v = norm.toInt(&ok, base);
        if (ok && v >= 0 && v <= 255) {
            civAddr = v;
            break;
        }
        std::cout << "Enter a value 0-255 (decimal, 0xNN, or hNN), or 'auto'.\n";
    }

    // --- Waterfall format ---
    std::cout << "\nWaterfall format:\n";
    std::cout << "  [0] chunk format, serial-style\n";
    std::cout << "  [1] combined format, network-style\n";
    int waterfallFormat = 0;
    while (true) {
        QString s = prompt("Enter selection [0]: ");
        if (s.isEmpty()) { waterfallFormat = 0; break; }
        bool ok = false;
        int v = s.toInt(&ok);
        if (ok && (v == 0 || v == 1)) { waterfallFormat = v; break; }
        std::cout << "Enter 0 or 1.\n";
    }

    // --- Network ports ---
    std::cout << "\nNetwork ports (UDP, press Enter to accept defaults):\n";
    int controlPort = promptPort("Control port", 50001);
    int civPort     = promptPort("CI-V port",    50002);
    int audioPort   = promptPort("Audio port",   50003);

    // --- Admin user ---
    std::cout << "\nCreate the initial admin user account.\n";
    std::cout << "(Additional users can be added later by editing the settings file.)\n";
    std::cout << "Printable ASCII only, 16 character max for each field.\n";
    QString username = promptConstrained("Username: ", 16, false);
    QString password = promptConstrained("Password: ", 16, false);

    // --- Review ---
    auto backendLabel = [](audioType t) -> const char* {
        switch (t) { case qtAudio: return "Qt"; case portAudio: return "Port Audio";
                     case rtAudio: return "RT Audio"; default: return "?"; }
    };
    std::cout << "\n--- Review ---\n";
    std::cout << "Settings file: " << resolvedPath.toStdString() << "\n";
    std::cout << "\n[General]\n";
    std::cout << "  AudioSystem      = " << static_cast<int>(audioSystem)
              << "  (" << backendLabel(audioSystem) << ")\n";
    std::cout << "  Manufacturer     = " << static_cast<int>(manufIcom) << "\n";
    std::cout << "\n[Radios]\n";
    std::cout << "  SerialPortRadio  = " << serialPort.toStdString() << "\n";
    std::cout << "  SerialPortBaud   = " << baud << "\n";
    std::cout << "  PTTType          = " << static_cast<int>(pttType)
              << "  (" << (forceRTSasPTT ? "RTS" : "CI-V") << ")\n";
    std::cout << "  ForceRTSasPTT    = " << (forceRTSasPTT ? "true" : "false") << "\n";
    std::cout << "  AudioInput       = " << rxName.toStdString() << "\n";
    std::cout << "  AudioOutput      = " << txName.toStdString() << "\n";
    std::cout << "  RigName          = " << rigName.toStdString() << "\n";
    std::cout << "  RigCIVuInt       = "
              << (civAddr < 0 ? std::string("<NONE> (auto)") : std::to_string(civAddr)) << "\n";
    std::cout << "  WaterfallFormat  = " << waterfallFormat << "\n";
    std::cout << "\n[Server]\n";
    std::cout << "  ServerEnabled    = true\n";
    std::cout << "  ServerControlPort= " << controlPort << "\n";
    std::cout << "  ServerCivPort    = " << civPort << "\n";
    std::cout << "  ServerAudioPort  = " << audioPort << "\n";
    QByteArray encoded;
    passcode(password, encoded);
    std::cout << "  Users\\1\\Username = " << username.toStdString() << "  (UserType=0)\n";
    std::cout << "  Users\\1\\Password = " << QString(encoded).toStdString()
              << "  (decodes as " << password.toStdString() << ")\n";

    QString confirm = prompt("\nSave these settings? (y/N): ");
    if (confirm.compare("y", Qt::CaseInsensitive) != 0) {
        std::cout << "Aborted. No changes made.\n";
        return 1;
    }

    // --- Write settings ---
    QSettings* s = openSettings(settingsFile);
    s->clear();

    s->setValue("AudioSystem", static_cast<int>(audioSystem));
    s->setValue("Manufacturer", static_cast<int>(manufIcom));

    s->beginWriteArray("Radios");
    s->setArrayIndex(0);
    if (civAddr >= 0) {
        s->setValue("RigCIVuInt", civAddr);
    }
    s->setValue("PTTType", static_cast<int>(pttType));
    s->setValue("ForceRTSasPTT", forceRTSasPTT);
    s->setValue("SerialPortRadio", serialPort);
    s->setValue("RigName", rigName);
    s->setValue("SerialPortBaud", baud);
    s->setValue("AudioInput", rxName);
    s->setValue("AudioOutput", txName);
    s->setValue("WaterfallFormat", waterfallFormat);
    s->setValue("GUID", QUuid::createUuid().toString());
    s->endArray();

    s->beginGroup("Server");
    s->setValue("ServerEnabled", true);
    s->setValue("ServerControlPort", controlPort);
    s->setValue("ServerCivPort", civPort);
    s->setValue("ServerAudioPort", audioPort);

    s->beginWriteArray("Users");
    s->setArrayIndex(0);
    s->setValue("Username", username);
    s->setValue("Password", QString(encoded));
    s->setValue("UserType", 0);
    s->endArray();

    s->endGroup();
    s->sync();
    QString writtenPath = s->fileName();
    delete s;

    std::cout << "\nSettings saved to: " << writtenPath.toStdString() << "\n";
    return 0;
}

} // namespace serverwizard

#endif // BUILD_WFSERVER
