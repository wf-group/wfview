#ifdef BUILD_WFSERVER

#include "serverwizard.h"

#include <iostream>
#include <memory>
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

QString promptConstrainedDefault(const QString& msg, const QString& def, int maxLen, bool allowSpaces)
{
    while (true) {
        QString s = prompt(QString("%1 [%2]: ").arg(msg, def));
        if (s.isEmpty()) {
            s = def;
        }
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

bool promptYesNo(const QString& msg, bool def)
{
    const QString suffix = def ? QStringLiteral(" [Y/n]: ") : QStringLiteral(" [y/N]: ");
    while (true) {
        const QString s = prompt(msg + suffix);
        if (s.isEmpty()) {
            return def;
        }
        if (s.compare("y", Qt::CaseInsensitive) == 0 || s.compare("yes", Qt::CaseInsensitive) == 0) {
            return true;
        }
        if (s.compare("n", Qt::CaseInsensitive) == 0 || s.compare("no", Qt::CaseInsensitive) == 0) {
            return false;
        }
        std::cout << "Please enter y or n.\n";
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
    std::unique_ptr<QSettings> existing(openSettings(settingsFile));

    std::cout << "\n=== wfserver interactive setup wizard ===\n";
    std::cout << "Settings file: " << resolvedPath.toStdString() << "\n\n";

    const bool hasExistingSettings = QFileInfo::exists(resolvedPath) ||
                                     !existing->allKeys().isEmpty() ||
                                     !existing->childGroups().isEmpty();
    bool replaceAll = true;
    if (hasExistingSettings) {
        std::cout << "Existing configuration found.\n";
        while (true) {
            const QString c = prompt("Choose [M]odify existing, [R]eplace all, or [Q]uit [M]: ");
            if (c.isEmpty() || c.compare("m", Qt::CaseInsensitive) == 0) {
                replaceAll = false;
                break;
            }
            if (c.compare("r", Qt::CaseInsensitive) == 0) {
                replaceAll = true;
                break;
            }
            if (c.compare("q", Qt::CaseInsensitive) == 0) {
                std::cout << "Aborted. No changes made.\n";
                return 1;
            }
            std::cout << "Invalid selection.\n";
        }
    }
    if (replaceAll) {
        std::cout << "WARNING: replace mode will ERASE any existing configuration at the\n";
        std::cout << "above path and replace it with the values you enter here.\n";
        if (!promptYesNo("Continue in replace mode?", false)) {
            std::cout << "Aborted. No changes made.\n";
            return 1;
        }
    }

    const int existingAudioSystem = existing->value("AudioSystem", 0).toInt();
    int existingNumRadios = existing->beginReadArray("Radios");
    existing->setArrayIndex(0);
    const QString existingSerialPort = existing->value("SerialPortRadio", "auto").toString();
    const quint32 existingBaud = existing->value("SerialPortBaud", 115200).toUInt();
    const int existingPttType = existing->value("PTTType", int(pttCIV)).toInt();
    const bool existingForceRts = existing->value("ForceRTSasPTT", existingPttType == int(pttRTS)).toBool();
    const QString existingRigName = existing->value("RigName", "IC-7300").toString();
    const QVariant existingCivVariant = existing->value("RigCIVuInt");
    const int existingWaterfallFormat = existing->value("WaterfallFormat", 0).toInt();
    const QString existingRxName = existing->value("AudioInput", "default").toString();
    const QString existingTxName = existing->value("AudioOutput", "default").toString();
    const QString existingGuid = existing->value("GUID", QUuid::createUuid().toString()).toString();
    existing->endArray();
    Q_UNUSED(existingNumRadios)
    existing->beginGroup("Server");
    const int existingControlPort = existing->value("ServerControlPort", 50001).toInt();
    const int existingCivPort = existing->value("ServerCivPort", 50002).toInt();
    const int existingAudioPort = existing->value("ServerAudioPort", 50003).toInt();
    const bool existingWfShareEnabled = existing->value("WfShareEnabled", false).toBool();
    const QString existingWfShareHost = existing->value("WfShareHost", "pbx.wfshare.org").toString();
    const int existingWfSharePort = existing->value("WfSharePort", 4569).toInt();
    const QString existingWfShareUsername = existing->value("WfShareUsername", "").toString();
    const QString existingWfSharePassword = existing->value("WfSharePassword", "").toString();
    const bool existingWfShareDirectEnabled = existing->value("WfShareDirectEnabled", false).toBool();
    const int existingWfShareDirectPort = existing->value("WfShareDirectPort", 4569).toInt();
    existing->beginReadArray("Users");
    existing->setArrayIndex(0);
    const QString existingUsername = existing->value("Username", "user").toString();
    const QString existingEncodedPassword = existing->value("Password", "").toString();
    const int existingUserType = existing->value("UserType", 0).toInt();
    existing->endArray();
    existing->endGroup();

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
    if (!replaceAll) {
        std::cout << "  [K] keep current (" << existingSerialPort.toStdString() << ")\n";
    }
    int portIdx = -1;
    while (portIdx == -1) {
        QString s = prompt("Enter selection: ");
        if (!replaceAll && (s.isEmpty() || s.compare("k", Qt::CaseInsensitive) == 0)) {
            portIdx = -4;
            break;
        }
        if (s.compare("a", Qt::CaseInsensitive) == 0) { portIdx = autoIdx; break; }
        if (s.compare("m", Qt::CaseInsensitive) == 0) { portIdx = manualIdx; break; }
        bool ok = false;
        int v = s.toInt(&ok);
        if (ok && v >= 0 && v < portLabels.size()) { portIdx = v; break; }
        std::cout << "Invalid selection.\n";
    }
    QString serialPort;
    if (portIdx == -4) {
        serialPort = existingSerialPort;
    } else if (portIdx == autoIdx) {
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
    const quint32 defaultBaud = replaceAll ? quint32(115200) : existingBaud;
    while (baud == 0) {
        QString s = prompt(QString("Enter baud rate [%1]: ").arg(defaultBaud));
        if (s.isEmpty()) { baud = defaultBaud; break; }
        bool ok = false;
        quint32 v = s.toUInt(&ok);
        if (ok && v > 0) baud = v;
        else std::cout << "Enter a positive integer.\n";
    }

    // --- PTT type ---
    bool forceRTSasPTT = false;
    pttType_t pttType = pttCIV;
    if (!replaceAll) {
        forceRTSasPTT = existingForceRts;
        pttType = static_cast<pttType_t>(existingPttType);
    }
    std::cout << "\nPTT method -- use RTS for older radios (ex. IC-718),\n";
    std::cout << "and use CI-V for more modern radios (ex. IC-7300).\n";
    while (true) {
        const QString currentPtt = forceRTSasPTT ? QStringLiteral("RTS") : QStringLiteral("CI-V");
        QString s = prompt(QString("Enter 'rts' or 'civ' [%1]: ").arg(currentPtt));
        if (s.isEmpty()) {
            break;
        }
        if (s.compare("civ", Qt::CaseInsensitive) == 0
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
    audioType audioSystem = replaceAll ? qtAudio : static_cast<audioType>(existingAudioSystem);
    QString rxName, txName;

    while (true) {
        int menu;
        while (true) {
            QString s = prompt(QString("\nAudio backend -- [0] Qt  [1] Port Audio  [2] RT Audio [%1]: ")
                                   .arg(static_cast<int>(audioSystem)));
            if (s.isEmpty()) { menu = static_cast<int>(audioSystem); break; }
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

        if (!replaceAll && !existingRxName.isEmpty() && !existingTxName.isEmpty() &&
            promptYesNo(QString("Keep current audio devices (%1 / %2)?")
                            .arg(existingRxName, existingTxName), true)) {
            rxName = existingRxName;
            txName = existingTxName;
        } else {
            int ri = selectAudioDevice("\nAvailable audio INPUT devices:", ins,
                "Note: The audio input should be the radio's receive audio.");
            if (ri == -2) continue;
            int ro = selectAudioDevice("\nAvailable audio OUTPUT devices:", outs,
                "Note: The audio output should go to the radio's transmit audio input.");
            if (ro == -2) continue;

            rxName = devs.getInputName(ri);
            txName = devs.getOutputName(ro);
        }
        break;
    }

    // --- Rig name ---
    std::cout << "\nRig name (recommended: the radio model number, e.g. IC-7300).\n";
    std::cout << "No spaces, printable ASCII, max 16 characters.\n";
    QString rigName = replaceAll
                          ? promptConstrained("Rig name: ", 16, false)
                          : promptConstrainedDefault("Rig name", existingRigName, 16, false);

    // --- CI-V address ---
    std::cout << "\nCI-V address.\n";
    std::cout << "It is strongly recommended to enable CI-V Transceive on the radio\n";
    std::cout << "and then set the CI-V address in wfserver to auto. To manually\n";
    std::cout << "define the CI-V address, type it here. You may type it as an integer\n";
    std::cout << "(ie, \"152\"), or a hexadecimal value (ie, \"0x98\" or \"h98\").\n";
    int civAddr = -1; // -1 => auto (do not write)
    while (true) {
        const QString civDefaultText = (!replaceAll && existingCivVariant.isValid())
                                           ? existingCivVariant.toString()
                                           : QStringLiteral("auto");
        QString s = prompt(QString("CI-V address [%1]: ").arg(civDefaultText));
        if (s.isEmpty() || s.compare("auto", Qt::CaseInsensitive) == 0) {
            if (!replaceAll && existingCivVariant.isValid() && s.isEmpty()) {
                civAddr = existingCivVariant.toInt();
            } else {
                civAddr = -1;
            }
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
    int waterfallFormat = replaceAll ? 0 : existingWaterfallFormat;
    while (true) {
        QString s = prompt(QString("Enter selection [%1]: ").arg(waterfallFormat));
        if (s.isEmpty()) { break; }
        bool ok = false;
        int v = s.toInt(&ok);
        if (ok && (v == 0 || v == 1)) { waterfallFormat = v; break; }
        std::cout << "Enter 0 or 1.\n";
    }

    // --- Network ports ---
    std::cout << "\nNetwork ports (UDP, press Enter to accept defaults):\n";
    int controlPort = promptPort("Control port", replaceAll ? 50001 : existingControlPort);
    int civPort     = promptPort("CI-V port",    replaceAll ? 50002 : existingCivPort);
    int audioPort   = promptPort("Audio port",   replaceAll ? 50003 : existingAudioPort);

    // --- wfshare ---
    std::cout << "\nwfshare remote access:\n";
    std::cout << "  [0] disabled\n";
    std::cout << "  [1] PBX relay registration (pbx.wfshare.org / Asterisk)\n";
    std::cout << "  [2] direct point-to-point listener\n";
    int wfShareMode = 0;
    if (!replaceAll) {
        wfShareMode = existingWfShareDirectEnabled ? 2 : (existingWfShareEnabled ? 1 : 0);
    }
    while (true) {
        QString s = prompt(QString("Enter selection [%1]: ").arg(wfShareMode));
        if (s.isEmpty()) {
            break;
        }
        bool ok = false;
        int v = s.toInt(&ok);
        if (ok && v >= 0 && v <= 2) {
            wfShareMode = v;
            break;
        }
        std::cout << "Enter 0, 1, or 2.\n";
    }

    bool wfShareEnabled = wfShareMode == 1;
    bool wfShareDirectEnabled = wfShareMode == 2;
    int wfShareDirectPort = replaceAll ? 4569 : existingWfShareDirectPort;
    if (wfShareDirectEnabled) {
        wfShareDirectPort = promptPort("wfshare direct listen port", wfShareDirectPort);
        std::cout << "Direct mode uses the server user accounts configured below for authentication.\n";
    }
    QString wfShareHost = replaceAll ? QStringLiteral("pbx.wfshare.org") : existingWfShareHost;
    int wfSharePort = replaceAll ? 4569 : existingWfSharePort;
    QString wfShareUsername = replaceAll ? QString() : existingWfShareUsername;
    QString wfSharePassword = replaceAll ? QString() : existingWfSharePassword;
    if (wfShareEnabled) {
        QString host = prompt(QString("wfshare host [%1]: ").arg(wfShareHost));
        if (!host.isEmpty()) {
            wfShareHost = host;
        }
        wfSharePort = promptPort("wfshare port", wfSharePort);
        wfShareUsername = promptConstrainedDefault("wfshare username", wfShareUsername, 32, false);
        QString passwordPrompt = wfSharePassword.isEmpty()
                                     ? QStringLiteral("wfshare password: ")
                                     : QStringLiteral("wfshare password [keep current]: ");
        QString newPassword = prompt(passwordPrompt);
        if (!newPassword.isEmpty()) {
            wfSharePassword = newPassword;
        } else if (wfSharePassword.isEmpty()) {
            std::cout << "Value required.\n";
            wfSharePassword = promptConstrained("wfshare password: ", 64, false);
        }
    }

    // --- Admin user ---
    std::cout << "\nCreate the initial admin user account.\n";
    std::cout << "(Additional users can be added later by editing the settings file.)\n";
    std::cout << "Printable ASCII only, 16 character max for each field.\n";
    QString username = replaceAll
                           ? promptConstrained("Username: ", 16, false)
                           : promptConstrainedDefault("Username", existingUsername, 16, false);
    QString password;
    QString encodedPassword = existingEncodedPassword;
    if (!replaceAll && !encodedPassword.isEmpty()) {
        QString p = prompt("Password [keep current]: ");
        if (p.isEmpty()) {
            password = QStringLiteral("<unchanged>");
        } else {
            password = p;
            QByteArray encoded;
            passcode(password, encoded);
            encodedPassword = QString(encoded);
        }
    } else {
        password = promptConstrained("Password: ", 16, false);
        QByteArray encoded;
        passcode(password, encoded);
        encodedPassword = QString(encoded);
    }

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
    std::cout << "  WfShareEnabled   = " << (wfShareEnabled ? "true" : "false") << "\n";
    std::cout << "  WfShareDirect    = " << (wfShareDirectEnabled ? "true" : "false") << "\n";
    if (wfShareEnabled) {
        std::cout << "  WfShareHost      = " << wfShareHost.toStdString() << "\n";
        std::cout << "  WfSharePort      = " << wfSharePort << "\n";
        std::cout << "  WfShareUsername  = " << wfShareUsername.toStdString() << "\n";
    }
    if (wfShareDirectEnabled) {
        std::cout << "  WfShareDirectPort= " << wfShareDirectPort << "\n";
    }
    std::cout << "  Users\\1\\Username = " << username.toStdString() << "  (UserType=0)\n";
    std::cout << "  Users\\1\\Password = " << encodedPassword.toStdString();
    if (password == QStringLiteral("<unchanged>")) {
        std::cout << "  (unchanged)\n";
    } else {
        std::cout << "  (decodes as " << password.toStdString() << ")\n";
    }

    QString confirm = prompt("\nSave these settings? (y/N): ");
    if (confirm.compare("y", Qt::CaseInsensitive) != 0) {
        std::cout << "Aborted. No changes made.\n";
        return 1;
    }

    // --- Write settings ---
    QSettings* s = openSettings(settingsFile);
    if (replaceAll) {
        s->clear();
    }

    s->setValue("AudioSystem", static_cast<int>(audioSystem));
    s->setValue("Manufacturer", static_cast<int>(manufIcom));

    auto writeRadioValues = [&](const QString& prefix) {
        if (civAddr >= 0) {
            s->setValue(prefix + "RigCIVuInt", civAddr);
        } else {
            s->remove(prefix + "RigCIVuInt");
        }
        s->setValue(prefix + "PTTType", static_cast<int>(pttType));
        s->setValue(prefix + "ForceRTSasPTT", forceRTSasPTT);
        s->setValue(prefix + "SerialPortRadio", serialPort);
        s->setValue(prefix + "RigName", rigName);
        s->setValue(prefix + "SerialPortBaud", baud);
        s->setValue(prefix + "AudioInput", rxName);
        s->setValue(prefix + "AudioOutput", txName);
        s->setValue(prefix + "WaterfallFormat", waterfallFormat);
        s->setValue(prefix + "GUID", replaceAll ? QUuid::createUuid().toString() : existingGuid);
    };

    if (replaceAll) {
        s->beginWriteArray("Radios");
        s->setArrayIndex(0);
        writeRadioValues(QString());
        s->endArray();
    } else {
        writeRadioValues(QStringLiteral("Radios/1/"));
    }

    s->beginGroup("Server");
    s->setValue("ServerEnabled", true);
    s->setValue("ServerControlPort", controlPort);
    s->setValue("ServerCivPort", civPort);
    s->setValue("ServerAudioPort", audioPort);
    s->setValue("WfShareEnabled", wfShareEnabled);
    s->setValue("WfShareHost", wfShareHost);
    s->setValue("WfSharePort", wfSharePort);
    s->setValue("WfShareUsername", wfShareUsername);
    s->setValue("WfSharePassword", wfSharePassword);
    s->setValue("WfShareDirectEnabled", wfShareDirectEnabled);
    s->setValue("WfShareDirectPort", wfShareDirectPort);
    s->remove("WfShareStationId");

    if (replaceAll) {
        s->beginWriteArray("Users");
        s->setArrayIndex(0);
        s->setValue("Username", username);
        s->setValue("Password", encodedPassword);
        s->setValue("UserType", existingUserType);
        s->endArray();
    } else {
        s->setValue("Users/1/Username", username);
        s->setValue("Users/1/Password", encodedPassword);
        s->setValue("Users/1/UserType", existingUserType);
    }

    s->endGroup();
    s->sync();
    QString writtenPath = s->fileName();
    delete s;

    std::cout << "\nSettings saved to: " << writtenPath.toStdString() << "\n";
    return 0;
}

} // namespace serverwizard

#endif // BUILD_WFSERVER
