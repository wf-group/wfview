#ifdef BUILD_WFSERVER
#include <QtCore/QCoreApplication>
#include "keyboard.h"
#include "servermain.h"
#include "serverwizard.h"
#else
#include "MemoriesModel.h"
#include "qqmlapplicationengine.h"
#include <QtQml/qqml.h>   // declares qmlRegisterSingletonType/Instance
#include <QQmlEngine>
#include <QQmlApplicationEngine>
#include <QJSEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QGuiApplication>
#include <QStyleHints>
#include <QTranslator>
#include <QDir>
#include <QFile>
#include <QLocale>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <csignal>
#include <conio.h>
#include <ntsecapi.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

#include <cstring>
#include <iostream>

// Include lots of our headers
#include "logcategories.h"
#include "wfviewtypes.h"
#include "prefs.h"
#include "commhandler.h"
#include "rigcommander.h"
#include "icomcommander.h"
#include "kenwoodcommander.h"
#include "yaesucommander.h"
#include "freqmemory.h"
#include "rigidentities.h"
#include "repeaterattributes.h"

#include "packettypes.h"
#include "rigserver.h"
#include "icomserver.h"
#include "kenwoodserver.h"
#include "yaesuserver.h"
#include "rigctld.h"
#include "audiodevices.h"
#include "cachingqueue.h"

#ifndef BUILD_WFSERVER
#include "cluster.h"
#include "colorprefs.h"
#include "tciserver.h"

#include "usbcontroller.h"
#include "ControllerController.h"

#include "MainController.h"
#include "ReceiverController.h"
#include "RigCreatorController.h"
#include "SelectRadioController.h"
#include "CWSenderController.h"

#include "waterfallitem.h"
#include "spectrumitem.h"



#include "MeterItem.h"
#include "DebugController.h"

#include "logcategories.h"
#include "LoggingController.h"
#endif

bool debugMode=false;

#ifndef BUILD_WFSERVER
struct QtMsgHandlerGuard {
    LoggingController* log = nullptr;
    ~QtMsgHandlerGuard() {
        if (log) log->uninstallQtMessageHandler();
    }
};
#endif

#ifdef BUILD_WFSERVER
// Smart pointer to log file
QScopedPointer<QFile>   m_logFile;
QMutex logMutex;
servermain* w=nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

static int startWfserverRuntime(QCoreApplication* app, const QString& settingsFile, const QString& logFilename,
                                const QString& version, bool serviceMode);

#ifdef Q_OS_WIN
static constexpr const wchar_t* WFSERVER_SERVICE_NAME = L"wfserver";
static constexpr const wchar_t* WFSERVER_SERVICE_DISPLAY_NAME = L"wfserver";

static SERVICE_STATUS_HANDLE serviceStatusHandle = nullptr;
static SERVICE_STATUS serviceStatus = {};
static QCoreApplication* serviceApp = nullptr;
static QVector<QByteArray> serviceArgStorage;
static QVector<char*> serviceArgv;

static bool isWindowsServiceRunArg(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--service=run") == 0)
            return true;
        if (std::strcmp(argv[i], "--service") == 0 && i + 1 < argc && std::strcmp(argv[i + 1], "run") == 0)
            return true;
    }
    return false;
}

static QString wfserverVersionText()
{
    return QString("wfserver version: %1 (Git:%2 on %3 at %4 by %5@%6)\nOperating System: %7 (%8)\nBuild Qt Version %9. Current Qt Version: %10\n")
        .arg(QString(WFVIEW_VERSION))
        .arg(GITSHORT).arg(__DATE__).arg(__TIME__).arg(UNAME).arg(HOST)
        .arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildCpuArchitecture())
        .arg(QT_VERSION_STR).arg(qVersion());
}

static QString serviceDefaultLogFilename()
{
    const QDateTime date = QDateTime::currentDateTime();
    const QString temp = QStandardPaths::standardLocations(QStandardPaths::TempLocation).value(0, QDir::tempPath());
    return QString("%1/wfserver-%2.log").arg(temp, date.toString("yyyyMMddhhmmss"));
}

static void parseCommonServerArgs(int argc, char* argv[], QString* settingsFile, QString* logFilename)
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if ((arg == QStringLiteral("-d")) || (arg == QStringLiteral("--debug"))) {
            debugMode = true;
        } else if (((arg == QStringLiteral("-l")) || (arg == QStringLiteral("--logfile"))) && i + 1 < argc) {
            *logFilename = QString::fromLocal8Bit(argv[++i]);
        } else if (((arg == QStringLiteral("-s")) || (arg == QStringLiteral("--settings"))) && i + 1 < argc) {
            *settingsFile = QString::fromLocal8Bit(argv[++i]);
        }
    }
}

static void setServiceStatus(DWORD state, DWORD win32ExitCode = NO_ERROR, DWORD waitHint = 0)
{
    if (serviceStatusHandle == nullptr)
        return;

    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    serviceStatus.dwCurrentState = state;
    serviceStatus.dwWin32ExitCode = win32ExitCode;
    serviceStatus.dwWaitHint = waitHint;
    serviceStatus.dwControlsAccepted = (state == SERVICE_RUNNING) ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
    serviceStatus.dwCheckPoint = (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING)
                                     ? serviceStatus.dwCheckPoint + 1
                                     : 0;
    SetServiceStatus(serviceStatusHandle, &serviceStatus);
}

static DWORD WINAPI serviceControlHandler(DWORD control, DWORD, LPVOID, LPVOID)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        setServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        if (serviceApp != nullptr) {
            QMetaObject::invokeMethod(serviceApp, []() {
                QCoreApplication::quit();
            }, Qt::QueuedConnection);
        }
        return NO_ERROR;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI serviceMain(DWORD, LPWSTR*)
{
    serviceStatusHandle = RegisterServiceCtrlHandlerExW(WFSERVER_SERVICE_NAME, serviceControlHandler, nullptr);
    if (serviceStatusHandle == nullptr)
        return;

    serviceStatus = {};
    setServiceStatus(SERVICE_START_PENDING, NO_ERROR, 5000);
    int argc = serviceArgv.size();
    QCoreApplication app(argc, serviceArgv.data());
    app.setApplicationName("wfserver");
    app.setOrganizationName("wfview");
    app.setOrganizationDomain("wfview.org");
    serviceApp = &app;

    QString settingsFile;
    QString logFilename = serviceDefaultLogFilename();
    parseCommonServerArgs(argc, serviceArgv.data(), &settingsFile, &logFilename);

    const int rc = startWfserverRuntime(&app, settingsFile, logFilename, wfserverVersionText(), true);
    serviceApp = nullptr;
    setServiceStatus(SERVICE_STOPPED, DWORD(rc));
}

static QString windowsErrorString(DWORD err = GetLastError())
{
    LPWSTR buffer = nullptr;
    const DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, err, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString message = len > 0 ? QString::fromWCharArray(buffer).trimmed() : QStringLiteral("Windows error %1").arg(err);
    if (buffer != nullptr)
        LocalFree(buffer);
    return message;
}

static QString quoteServiceArgument(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

static QString currentWindowsAccount()
{
    const QString domain = qEnvironmentVariable("USERDOMAIN");
    const QString user = qEnvironmentVariable("USERNAME");
    if (!domain.isEmpty() && !user.isEmpty())
        return QStringLiteral("%1\\%2").arg(domain, user);
    return user;
}

static LSA_UNICODE_STRING lsaString(const wchar_t* text)
{
    LSA_UNICODE_STRING result = {};
    result.Buffer = const_cast<PWSTR>(text);
    result.Length = USHORT(wcslen(text) * sizeof(wchar_t));
    result.MaximumLength = result.Length + sizeof(wchar_t);
    return result;
}

static int grantServiceLogonRight(const QString& account)
{
    DWORD sidSize = 0;
    DWORD domainSize = 0;
    SID_NAME_USE sidType = SidTypeUnknown;
    const std::wstring accountName = account.toStdWString();

    LookupAccountNameW(nullptr, accountName.c_str(), nullptr, &sidSize, nullptr, &domainSize, &sidType);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        std::cerr << "Unable to resolve service account " << account.toStdString()
                  << ": " << windowsErrorString().toStdString() << "\n";
        return -1;
    }

    QByteArray sidBuffer(int(sidSize), Qt::Uninitialized);
    std::wstring domain(domainSize, L'\0');
    if (!LookupAccountNameW(nullptr,
                            accountName.c_str(),
                            reinterpret_cast<PSID>(sidBuffer.data()),
                            &sidSize,
                            domain.data(),
                            &domainSize,
                            &sidType)) {
        std::cerr << "Unable to resolve service account " << account.toStdString()
                  << ": " << windowsErrorString().toStdString() << "\n";
        return -1;
    }

    LSA_OBJECT_ATTRIBUTES attributes = {};
    LSA_HANDLE policy = nullptr;
    NTSTATUS status = LsaOpenPolicy(nullptr, &attributes, POLICY_CREATE_ACCOUNT | POLICY_LOOKUP_NAMES, &policy);
    if (status != 0) {
        std::cerr << "Unable to open local security policy: "
                  << windowsErrorString(LsaNtStatusToWinError(status)).toStdString() << "\n";
        return -1;
    }

    LSA_UNICODE_STRING right = lsaString(L"SeServiceLogonRight");
    status = LsaAddAccountRights(policy, reinterpret_cast<PSID>(sidBuffer.data()), &right, 1);
    LsaClose(policy);

    if (status != 0) {
        std::cerr << "Unable to grant 'Log on as a service' to " << account.toStdString()
                  << ": " << windowsErrorString(LsaNtStatusToWinError(status)).toStdString() << "\n";
        return -1;
    }

    return 0;
}

static QString readPasswordFromConsole(const QString& prompt)
{
    std::cout << prompt.toStdString();
    std::cout.flush();

    QString password;
    for (;;) {
        const int ch = _getch();
        if (ch == '\r' || ch == '\n') {
            std::cout << "\n";
            break;
        }
        if (ch == '\b') {
            if (!password.isEmpty())
                password.chop(1);
            continue;
        }
        if (ch == 0 || ch == 0xe0) {
            _getch();
            continue;
        }
        password.append(QChar(char(ch)));
    }

    return password;
}

static QString serviceBinaryPath(const QString& settingsFile)
{
    QStringList args;
    args << quoteServiceArgument(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()))
         << QStringLiteral("--service")
         << QStringLiteral("run");

    if (!settingsFile.isEmpty()) {
        args << QStringLiteral("--settings") << quoteServiceArgument(settingsFile);
    }

    return args.join(QLatin1Char(' '));
}

static int installWindowsService(const QString& settingsFile, bool localSystem, QString account, QString password)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (scm == nullptr) {
        std::cerr << "Unable to open Service Control Manager: " << windowsErrorString().toStdString() << "\n";
        return -1;
    }

    if (!localSystem) {
        if (account.isEmpty())
            account = currentWindowsAccount();
        if (password.isEmpty())
            password = readPasswordFromConsole(QStringLiteral("Password for %1: ").arg(account));
        if (grantServiceLogonRight(account) != 0) {
            CloseServiceHandle(scm);
            return -1;
        }
    }

    const QString binPath = serviceBinaryPath(settingsFile);
    SC_HANDLE service = CreateServiceW(scm,
                                       WFSERVER_SERVICE_NAME,
                                       WFSERVER_SERVICE_DISPLAY_NAME,
                                       SERVICE_ALL_ACCESS,
                                       SERVICE_WIN32_OWN_PROCESS,
                                       SERVICE_AUTO_START,
                                       SERVICE_ERROR_NORMAL,
                                       reinterpret_cast<LPCWSTR>(binPath.utf16()),
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       localSystem ? nullptr : reinterpret_cast<LPCWSTR>(account.utf16()),
                                       localSystem ? nullptr : reinterpret_cast<LPCWSTR>(password.utf16()));

    if (service == nullptr) {
        const DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            service = OpenServiceW(scm, WFSERVER_SERVICE_NAME, SERVICE_CHANGE_CONFIG);
            if (service != nullptr) {
                const BOOL changed = ChangeServiceConfigW(service,
                                                          SERVICE_WIN32_OWN_PROCESS,
                                                          SERVICE_AUTO_START,
                                                          SERVICE_ERROR_NORMAL,
                                                          reinterpret_cast<LPCWSTR>(binPath.utf16()),
                                                          nullptr,
                                                          nullptr,
                                                          nullptr,
                                                          localSystem ? nullptr : reinterpret_cast<LPCWSTR>(account.utf16()),
                                                          localSystem ? nullptr : reinterpret_cast<LPCWSTR>(password.utf16()),
                                                          WFSERVER_SERVICE_DISPLAY_NAME);
                if (!changed) {
                    const DWORD changeErr = GetLastError();
                    CloseServiceHandle(service);
                    CloseServiceHandle(scm);
                    std::cerr << "Unable to update wfserver service: " << windowsErrorString(changeErr).toStdString() << "\n";
                    return -1;
                }
                CloseServiceHandle(service);
                CloseServiceHandle(scm);
                if (localSystem)
                    std::cout << "Updated wfserver service as LocalSystem.\n";
                else
                    std::cout << "Updated wfserver service as " << account.toStdString() << ".\n";
                return 0;
            }
        }

        CloseServiceHandle(scm);
        std::cerr << "Unable to install wfserver service: " << windowsErrorString(err).toStdString() << "\n";
        return -1;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    if (localSystem)
        std::cout << "Installed wfserver service as LocalSystem.\n";
    else
        std::cout << "Installed wfserver service as " << account.toStdString() << ".\n";
    return 0;
}

static int removeWindowsService()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        std::cerr << "Unable to open Service Control Manager: " << windowsErrorString().toStdString() << "\n";
        return -1;
    }

    SC_HANDLE service = OpenServiceW(scm, WFSERVER_SERVICE_NAME, DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        const DWORD err = GetLastError();
        CloseServiceHandle(scm);
        std::cerr << "Unable to open wfserver service: " << windowsErrorString(err).toStdString() << "\n";
        return -1;
    }

    SERVICE_STATUS_PROCESS status = {};
    DWORD bytesNeeded = 0;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)
        && status.dwCurrentState != SERVICE_STOPPED) {
        SERVICE_STATUS ignored = {};
        ControlService(service, SERVICE_CONTROL_STOP, &ignored);
    }

    const BOOL ok = DeleteService(service);
    const DWORD err = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!ok) {
        std::cerr << "Unable to remove wfserver service: " << windowsErrorString(err).toStdString() << "\n";
        return -1;
    }

    std::cout << "Removed wfserver service.\n";
    return 0;
}

static int startWindowsService()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        std::cerr << "Unable to open Service Control Manager: " << windowsErrorString().toStdString() << "\n";
        return -1;
    }

    SC_HANDLE service = OpenServiceW(scm, WFSERVER_SERVICE_NAME, SERVICE_START);
    if (service == nullptr) {
        const DWORD err = GetLastError();
        CloseServiceHandle(scm);
        std::cerr << "Unable to open wfserver service: " << windowsErrorString(err).toStdString() << "\n";
        return -1;
    }

    const BOOL ok = StartServiceW(service, 0, nullptr);
    const DWORD err = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!ok && err != ERROR_SERVICE_ALREADY_RUNNING) {
        std::cerr << "Unable to start wfserver service: " << windowsErrorString(err).toStdString() << "\n";
        return -1;
    }

    std::cout << "Started wfserver service.\n";
    return 0;
}

static int stopWindowsService()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        std::cerr << "Unable to open Service Control Manager: " << windowsErrorString().toStdString() << "\n";
        return -1;
    }

    SC_HANDLE service = OpenServiceW(scm, WFSERVER_SERVICE_NAME, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        const DWORD err = GetLastError();
        CloseServiceHandle(scm);
        std::cerr << "Unable to open wfserver service: " << windowsErrorString(err).toStdString() << "\n";
        return -1;
    }

    SERVICE_STATUS status = {};
    const BOOL ok = ControlService(service, SERVICE_CONTROL_STOP, &status);
    const DWORD err = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!ok && err != ERROR_SERVICE_NOT_ACTIVE) {
        std::cerr << "Unable to stop wfserver service: " << windowsErrorString(err).toStdString() << "\n";
        return -1;
    }

    std::cout << "Stopped wfserver service.\n";
    return 0;
}

static int statusWindowsService()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        std::cerr << "Unable to open Service Control Manager: " << windowsErrorString().toStdString() << "\n";
        return -1;
    }

    SC_HANDLE service = OpenServiceW(scm, WFSERVER_SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        const DWORD err = GetLastError();
        CloseServiceHandle(scm);
        std::cerr << "Unable to open wfserver service: " << windowsErrorString(err).toStdString() << "\n";
        return -1;
    }

    SERVICE_STATUS_PROCESS status = {};
    DWORD bytesNeeded = 0;
    const BOOL ok = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded);
    const DWORD err = GetLastError();
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!ok) {
        std::cerr << "Unable to query wfserver service: " << windowsErrorString(err).toStdString() << "\n";
        return -1;
    }

    QString state;
    switch (status.dwCurrentState) {
    case SERVICE_STOPPED: state = QStringLiteral("stopped"); break;
    case SERVICE_START_PENDING: state = QStringLiteral("start pending"); break;
    case SERVICE_STOP_PENDING: state = QStringLiteral("stop pending"); break;
    case SERVICE_RUNNING: state = QStringLiteral("running"); break;
    case SERVICE_CONTINUE_PENDING: state = QStringLiteral("continue pending"); break;
    case SERVICE_PAUSE_PENDING: state = QStringLiteral("pause pending"); break;
    case SERVICE_PAUSED: state = QStringLiteral("paused"); break;
    default: state = QStringLiteral("unknown"); break;
    }

    std::cout << "wfserver service is " << state.toStdString() << ".\n";
    return 0;
}

static int handleWindowsServiceCommand(const QString& command, const QString& settingsFile,
                                       bool serviceLocalSystem, const QString& serviceAccount,
                                       const QString& servicePassword)
{
    if (command == QStringLiteral("install"))
        return installWindowsService(settingsFile, serviceLocalSystem, serviceAccount, servicePassword);
    if (command == QStringLiteral("install-system"))
        return installWindowsService(settingsFile, true, QString(), QString());
    if (command == QStringLiteral("remove") || command == QStringLiteral("uninstall"))
        return removeWindowsService();
    if (command == QStringLiteral("start"))
        return startWindowsService();
    if (command == QStringLiteral("stop"))
        return stopWindowsService();
    if (command == QStringLiteral("status"))
        return statusWindowsService();

    std::cerr << "Unknown --service command. Use install, remove, start, stop, status, or run.\n";
    return -1;
}

static int runWindowsService(int argc, char* argv[])
{
    serviceArgStorage.clear();
    serviceArgv.clear();
    serviceArgStorage.reserve(argc);
    serviceArgv.reserve(argc);
    for (int i = 0; i < argc; ++i)
        serviceArgStorage.append(QByteArray(argv[i]));
    for (QByteArray& arg : serviceArgStorage)
        serviceArgv.append(arg.data());

    SERVICE_TABLE_ENTRYW serviceTable[] = {
        { const_cast<LPWSTR>(WFSERVER_SERVICE_NAME), serviceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(serviceTable)) {
        std::cerr << "Unable to start wfserver as a Windows service: " << windowsErrorString().toStdString() << "\n";
        return -1;
    }

    return 0;
}

bool __stdcall cleanup(DWORD sig)
 #else
static void cleanup(int sig)
 #endif
{
    switch(sig) {
#ifndef Q_OS_WIN
    case SIGHUP:
        qInfo() << "hangup signal";
        break;
#endif
    case SIGTERM:
        qInfo() << "terminate signal caught";
        QCoreApplication::quit();
        break;
    default:
        break;
    }

 #ifdef Q_OS_WIN
    return true;
 #else
    return;
 #endif
}

static int startWfserverRuntime(QCoreApplication* app, const QString& settingsFile, const QString& logFilename,
                                const QString& version, bool serviceMode)
{
    // Set the logging file before doing anything else.
    m_logFile.reset(new QFile(logFilename));
    // Open the file logging
    if (!m_logFile.data()->open(QFile::WriteOnly | QFile::Truncate | QFile::Text)) {
        std::cerr << "Unable to open log file: " << logFilename.toStdString() << std::endl;
    }
    // Set handler
    qInstallMessageHandler(messageHandler);

    qInfo(logSystem()) << version;

#ifdef Q_OS_WIN
    if (serviceMode) {
        setServiceStatus(SERVICE_RUNNING);
    } else {
        SetConsoleCtrlHandler((PHANDLER_ROUTINE)cleanup, TRUE);
    }
#else
    Q_UNUSED(serviceMode)
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGKILL, cleanup);
#endif

    keyboard* kb = Q_NULLPTR;
    if (!serviceMode) {
        kb = new keyboard();
        kb->start();
    }

    w = new servermain(settingsFile);
    QObject::connect(app, &QCoreApplication::aboutToQuit, app, []() {
        delete w;
        w = nullptr;
    }, Qt::DirectConnection);

    const int result = app->exec();

    if (kb != Q_NULLPTR) {
        kb->quit();
        kb->wait();
        delete kb;
    }

    return result;
}


 #ifndef Q_OS_WIN
void initDaemon()
{
    int i;
    if(getppid()==1)
        return; /* already a daemon */
    i=fork();
    if (i<0)
        exit(1); /* fork error */
    if (i>0)
        exit(0); /* parent exits */

    setsid(); /* obtain a new process group */

    for (i=getdtablesize();i>=0;--i)
        close(i); /* close all descriptors */
    i=open("/dev/null",O_RDWR); dup(i); dup(i);

    signal(SIGCHLD,SIG_IGN);
    signal(SIGTSTP,SIG_IGN);
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
}

 #else

void initDaemon() {
    std::cout << "Background mode does not currently work in Windows\n";
    exit(1);
}

 #endif

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
#endif

int main(int argc, char *argv[])
{

#if defined(BUILD_WFSERVER) && defined(Q_OS_WIN)
    if (isWindowsServiceRunArg(argc, argv))
        return runWindowsService(argc, argv);
#endif

#ifdef BUILD_WFSERVER
    QCoreApplication a(argc, argv);
    a.setApplicationName("wfserver");
#else
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    // Wayland + threaded render loop flashes stale frames during resize;
    // the basic (synchronous) loop eliminates this.
    if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP")) {
        QByteArray platform = qgetenv("QT_QPA_PLATFORM");
        QByteArray session  = qgetenv("XDG_SESSION_TYPE");
        if (platform.startsWith("wayland") || session == "wayland")
            qputenv("QSG_RENDER_LOOP", "basic");
    }
    QGuiApplication a(argc, argv);

    a.setApplicationName("wfview");

#endif

    qRegisterMetaType<rigInfo>("rigInfo");
    qRegisterMetaType<rigTypedef>("rigTypedef");

    qRegisterMetaType<udpPreferences>("udpPreferences"); // Needs to be registered early.
    qRegisterMetaType<manufacturersType_t>("manufacturersType_t");
    qRegisterMetaType<connectionType_t>("connectionType_t");
    qRegisterMetaType<rigCapabilities>("rigCapabilities");
    qRegisterMetaType<duplexMode_t>("duplexMode_t");
    qRegisterMetaType<rptAccessTxRx_t>("rptAccessTxRx_t");
    qRegisterMetaType<rptrAccessData>();
    qRegisterMetaType<toneInfo>();
    qRegisterMetaType<rigInput>();
    qRegisterMetaType<inputTypes>();
    qRegisterMetaType<meter_t>();
    qRegisterMetaType<meterkind>();
    qRegisterMetaType<freqt>();
    qRegisterMetaType<vfo_t>();
    qRegisterMetaType<modeInfo>();
    qRegisterMetaType<rigMode_t>();
    qRegisterMetaType<pttType_t>();
    qRegisterMetaType<audioPacket>();
    qRegisterMetaType<audioSetup>();
    qRegisterMetaType<SERVERCONFIG>();
    qRegisterMetaType<timekind>();
    qRegisterMetaType<datekind>();
    qRegisterMetaType<QList<radio_cap_packet>>();
#ifndef BUILD_WFSERVER
    qRegisterMetaType<QVector<BUTTON>*>();
    qRegisterMetaType<QVector<KNOB>*>();
    qRegisterMetaType<QVector<COMMAND>*>();
    qRegisterMetaType<const COMMAND*>();
    qRegisterMetaType<const USBDEVICE*>();
#endif
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<networkStatus>();
    qRegisterMetaType<IaxStats>();
    qRegisterMetaType<networkAudioLevels>();
    qRegisterMetaType<codecType>();
    qRegisterMetaType<errorType>();
    qRegisterMetaType<usbFeatureType>();
    qRegisterMetaType<funcs>();
    qRegisterMetaType<memoryType>();
    qRegisterMetaType<memoryTagType>();
    qRegisterMetaType<memorySplitType>();
    qRegisterMetaType<antennaInfo>();
    qRegisterMetaType<queueItem>();
    qRegisterMetaType<cacheItem>();
    qRegisterMetaType<spectrumBounds>();
    qRegisterMetaType<centerSpanData>();
    qRegisterMetaType<bandStackType>();
    qRegisterMetaType<widthsType>();
    qRegisterMetaType<yaesu_scope_data>();
    qRegisterMetaType<lpfhpf>();
#ifndef BUILD_WFSERVER
    qRegisterMetaType<QVector<spotData>>();
    qRegisterMetaType<QList<spotData>>();
    qRegisterMetaType<clusterSettings>();
    qRegisterMetaType<colorPrefsType>();
#endif
    qRegisterMetaType<scopeData>();
    qRegisterMetaType<QVector<double>>("QVector<double>");

    a.setOrganizationName("wfview");
    a.setOrganizationDomain("wfview.org");
#ifndef BUILD_WFSERVER
    a.setDesktopFileName("wfview");
#endif


#ifdef QT_DEBUG
    //debugMode = true;
#endif

    QDateTime date = QDateTime::currentDateTime();
    QString formattedTime = date.toString("dd.MM.yyyy hh:mm:ss");
    QString logFilename = (QString("%1/%2-%3.log").arg(QStandardPaths::standardLocations(QStandardPaths::TempLocation)[0]).arg(a.applicationName()).arg(date.toString("yyyyMMddhhmmss")));

    QString settingsFile = NULL;
    QString currentArg;
#ifdef BUILD_WFSERVER
    bool runSetup = false;
    QString serviceCommand;
    QString serviceAccount;
    QString servicePassword;
    bool serviceLocalSystem = false;
#endif


    const QString helpText = QString("\nUsage: -l --logfile filename.log, -s --settings filename.ini, -c --clearconfig CONFIRM, -b --background (not Windows), -d --debug, -v --version"
#ifdef BUILD_WFSERVER
                                     ", --setup (interactive config wizard; modify or replace settings)"
#ifdef Q_OS_WIN
                                     ", --service install|install-system|remove|start|stop|status|run"
#endif
#endif
                                     "\n"); // TODO...
#ifdef BUILD_WFSERVER
    const QString version = QString("wfserver version: %1 (Git:%2 on %3 at %4 by %5@%6)\nOperating System: %7 (%8)\nBuild Qt Version %9. Current Qt Version: %10\n")
        .arg(QString(WFVIEW_VERSION))
        .arg(GITSHORT).arg(__DATE__).arg(__TIME__).arg(UNAME).arg(HOST)
        .arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildCpuArchitecture())
        .arg(QT_VERSION_STR).arg(qVersion());
#else
    const QString version = QString("wfview version: %1 (Git:%2 on %3 at %4 by %5@%6)\nOperating System: %7 (%8)\nBuild Qt Version %9. Current Qt Version: %10\n")
        .arg(QString(WFVIEW_VERSION))
        .arg(GITSHORT).arg(__DATE__).arg(__TIME__).arg(UNAME).arg(HOST)
        .arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildCpuArchitecture())
        .arg(QT_VERSION_STR).arg(qVersion());
#endif

    bool showVersion = false;
#ifndef BUILD_WFSERVER
    QString languageOverride = qEnvironmentVariable("WFVIEW_LANGUAGE");
#endif

    for(int c=1; c<argc; c++)
    {
        //qInfo() << "Argc: " << c << " argument: " << argv[c];
        currentArg = QString::fromLocal8Bit(argv[c]);

        if ((currentArg == "-d") || (currentArg == "--debug"))
        {
            debugMode = true;
        }
        else if ((currentArg == "-l") || (currentArg == "--logfile"))
        {
            if (argc > c)
            {
                logFilename = argv[c + 1];
                c += 1;
            }
        }
        else if ((currentArg == "-s") || (currentArg == "--settings"))
        {
            if (argc > c)
            {
                settingsFile = argv[c + 1];
                c += 1;
            }
        }
#ifndef BUILD_WFSERVER
        else if ((currentArg == "--language") || (currentArg == "--locale"))
        {
            if (c + 1 < argc)
            {
                languageOverride = QString::fromLocal8Bit(argv[c + 1]);
                c += 1;
            }
        }
        else if (currentArg.startsWith(QLatin1String("--language=")))
        {
            languageOverride = currentArg.mid(QStringLiteral("--language=").size());
        }
        else if (currentArg.startsWith(QLatin1String("--locale=")))
        {
            languageOverride = currentArg.mid(QStringLiteral("--locale=").size());
        }
#endif
        else if ((currentArg == "-c") || (currentArg == "--clearconfig"))
        {
            if (argc > c)
            {
                QString confirm = argv[c + 1];
                c += 1;
                if (confirm == "CONFIRM") {
                    QSettings* settings;
                    // Clear config
                    if (settingsFile.isEmpty()) {
                        settings = new QSettings();
                    }
                    else
                    {
                        QString file = settingsFile;
                        QFile info(settingsFile);
                        QString path="";
                        if (!QFileInfo(info).isAbsolute())
                        {
                            path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                            if (path.isEmpty())
                            {
                                path = QDir::homePath();
                            }
                            path = path + "/";
                            file = info.fileName();
                        }
                        settings = new QSettings(path + file, QSettings::Format::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                        settings->setIniCodec("UTF-8");
#endif
                    }
                    settings->clear();
                    delete settings;
                    std::cout << QString("All wfview settings cleared.\n").toStdString();
                    exit(0);
                }
            }
            std::cout << QString("Error: Clear config not confirmed (please add the word CONFIRM), aborting\n").toStdString();
            std::cout << helpText.toStdString();
            exit(-1);
        }
#ifdef BUILD_WFSERVER
        else if ((currentArg == "-b") || (currentArg == "--background"))
        {
            initDaemon();
        }
        else if (currentArg == "--setup")
        {
            runSetup = true;
        }
#ifdef Q_OS_WIN
        else if (currentArg == "--service")
        {
            if (c + 1 < argc)
            {
                serviceCommand = QString::fromLocal8Bit(argv[c + 1]).toLower();
                c += 1;
            }
            else
            {
                serviceCommand = QStringLiteral("status");
            }
        }
        else if (currentArg.startsWith(QLatin1String("--service=")))
        {
            serviceCommand = currentArg.mid(QStringLiteral("--service=").size()).toLower();
        }
        else if ((currentArg == "--service-account") && c + 1 < argc)
        {
            serviceAccount = QString::fromLocal8Bit(argv[c + 1]);
            c += 1;
        }
        else if ((currentArg == "--service-password") && c + 1 < argc)
        {
            servicePassword = QString::fromLocal8Bit(argv[c + 1]);
            c += 1;
        }
        else if (currentArg == "--service-system")
        {
            serviceLocalSystem = true;
        }
#endif
#endif
        else if ((currentArg == "-?") || (currentArg == "--help"))
        {
            std::cout << helpText.toStdString();
            return 0;
        }
        else if ((currentArg == "-v") || (currentArg == "--version"))
        {
            showVersion = true;
	}
        else {
            std::cout << "Unrecognized option: " << currentArg.toStdString();
            std::cout << helpText.toStdString();
            return -1;
        }

    }

#ifndef BUILD_WFSERVER
    // Translator doesn't really make sense for wfserver right now.
    QTranslator myappTranslator;
    qDebug() << "Current translation language: " << myappTranslator.language();

    const QLocale requestedLocale = languageOverride.isEmpty() ? QLocale() : QLocale(languageOverride);
    qDebug() << "Requested translation locale:" << requestedLocale.name();

    bool trResult = myappTranslator.load(requestedLocale, QLatin1String("wfview"), QLatin1String("_"), QLatin1String(":/translations"));
    if(trResult) {
        qDebug() << "Recognized requested language and loaded the translations (or at least found the /translations resource folder). Installing translator.";
        a.installTranslator(&myappTranslator);
    } else {
        qDebug() << "Could not load translation.";
    }

    qDebug() << "Changed to translation language: " << myappTranslator.language();
#endif

    if (showVersion) {
        std::cout << version.toStdString();
        return 0;
    }

#ifdef BUILD_WFSERVER
    if (runSetup) {
        return serverwizard::run(settingsFile);
    }

#ifdef Q_OS_WIN
    if (!serviceCommand.isEmpty()) {
        if (serviceCommand == QStringLiteral("run")) {
            std::cerr << "--service run must be started by the Windows Service Control Manager.\n";
            return -1;
        }
        return handleWindowsServiceCommand(serviceCommand, settingsFile, serviceLocalSystem, serviceAccount, servicePassword);
    }
#endif

    return startWfserverRuntime(&a, settingsFile, logFilename, version, false);
#else

    // Load logging window and install message handler as early as possible
    // Install capture BEFORE engine.load so QML warnings also get captured
    std::cout << "DEBUG: Creating LoggingController..." << std::endl;
    auto g_log = std::make_unique<LoggingController>(&a);
    std::cout << "DEBUG: Setting log file path..." << std::endl;
    g_log->setLogFilePath(logFilename);
    std::cout << "DEBUG: Installing Qt message handler..." << std::endl;
    g_log->installQtMessageHandler();
    std::cout << "DEBUG: Logging initialized successfully" << std::endl;

    // Log initial program information
    qInfo(logSystem()).noquote() << QString("wfview version: %1 (Git:%2 on %3 at %4 by %5@%6)")
                                        .arg(QString(WFVIEW_VERSION),GITSHORT,__DATE__,__TIME__,UNAME,HOST);

    qInfo(logSystem()).noquote() << QString("Operating System: %0 (%1)").arg(QSysInfo::prettyProductName(),QSysInfo::buildCpuArchitecture());
    qInfo(logSystem()).noquote() << "Looking for External Dependencies:";
    qInfo(logSystem()).noquote() << QString("QT Runtime Version: %0").arg(qVersion());
    if (strncmp(QT_VERSION_STR, qVersion(),sizeof(QT_VERSION_STR)))
    {
        qWarning(logSystem()).noquote() << QString("QT Build Version Mismatch: %0").arg(QT_VERSION_STR);
    }

    qInfo(logSystem()).noquote()  << QString("OPUS Version: %0").arg(opus_get_version_string());

#ifdef HID_API_VERSION_MAJOR
    qInfo(logSystem()).noquote() << QString("HIDAPI Version: %0.%1.%2")
                                        .arg(HID_API_VERSION_MAJOR)
                                        .arg(HID_API_VERSION_MINOR)
                                        .arg(HID_API_VERSION_PATCH);

    if (HID_API_VERSION != HID_API_MAKE_VERSION(hid_version()->major, hid_version()->minor, hid_version()->patch)) {
        qWarning(logSystem()).noquote() << QString("HIDAPI Version mismatch: %0.%1.%2")
        .arg(hid_version()->major)
            .arg(hid_version()->minor)
            .arg(hid_version()->patch);
    }
#endif

#ifdef EIGEN_WORLD_VERSION
    qInfo(logSystem()).noquote() << QString("EIGEN Version: %0.%1.%2").arg(EIGEN_WORLD_VERSION).arg(EIGEN_MAJOR_VERSION).arg(EIGEN_MINOR_VERSION);
#endif

#ifdef RTAUDIO_VERSION
    qInfo(logSystem()).noquote() << QString("RTAUDIO Version: %0").arg(RTAUDIO_VERSION);
    if (RTAUDIO_VERSION != RtAudio::getVersion())
    {
        qWarning(logSystem()).noquote() << QString("RTAUDIO Version Mismatch: %0").arg(RtAudio::getVersion().c_str());
    }
#endif

    qInfo(logSystem()).noquote() << QString("PORTAUDIO Version: %0").arg(Pa_GetVersionText());


    a.styleHints()->setWheelScrollLines(1); // one line per wheel click

    // Create MainWindow here
    std::cout << "DEBUG: Opening MainWindow()" << std::endl;
    qDebug(logSystem()) << "Opening MainWindow()";

    //engine.rootContext()->setContextProperty("rig", backend); // shared backend name in QML

    std::cout << "DEBUG: Setting QuickStyle to Fusion" << std::endl;
    QQuickStyle::setStyle("Fusion"); // MUST be before loading any QML that imports Controls
    std::cout << "DEBUG: Creating QQmlApplicationEngine" << std::endl;
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::quit, &a, &QCoreApplication::quit);

    std::cout << "DEBUG: Creating MainController" << std::endl;
    auto g_mwc = std::make_unique<MainController>(settingsFile, logFilename, debugMode, &a);
    std::cout << "DEBUG: MainController created successfully" << std::endl;
    QObject::connect(&a, &QCoreApplication::aboutToQuit, g_mwc.get(), &MainController::shutdown);

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)   
    qmlRegisterSingletonInstance("WFVIEW", 1, 0, "MainController", g_mwc.get());
    qmlRegisterSingletonInstance("WFVIEW", 1, 0, "Logging", g_log.get());
#else
    qmlRegisterSingletonType<MainController>("WFVIEW", 1, 0, "MainController",
                                             [](QQmlEngine*, QJSEngine*) -> QObject* { return g_mwc.get(); });

    qmlRegisterSingletonType<LoggingController>("WFVIEW", 1, 0, "Logging",
                                                [](QQmlEngine*, QJSEngine*) -> QObject* { return g_log.get(); });
#endif

    // Guard ensures handler removed before engine/app destructors run
    QtMsgHandlerGuard handlerGuard{ g_log.get() };

    QObject::connect(&a, &QCoreApplication::aboutToQuit,
                     g_log.get(), &LoggingController::uninstallQtMessageHandler,
                     Qt::DirectConnection);

    qmlRegisterType<ReceiverController>("WFVIEW", 1, 0, "ReceiverController");
    qmlRegisterType<RigCreatorController>("WFVIEW", 1, 0, "RigCreatorController");
    qmlRegisterType<DebugController>("WFVIEW", 1, 0, "DebugController");
    qmlRegisterType<SelectRadioController>("WFVIEW", 1, 0, "SelectRadioController");
    qmlRegisterType<CWSenderController>("WFVIEW", 1, 0, "CWSenderController");
    qmlRegisterType<MemoriesModel>("WFVIEW", 1, 0, "MemoriesModel");
    qmlRegisterType<ControllerController>("WFVIEW", 1, 0, "ControllerController");

    // Members of ReceiverController
    qmlRegisterType<SpectrumItem>("WFVIEW", 1, 0, "SpectrumItem");
    qmlRegisterType<WaterfallItem>("WFVIEW", 1, 0, "WaterfallItem");
    qmlRegisterType<FreqCtrlQuick>("WFVIEW", 1, 0, "FreqCtrlQuick");
    qmlRegisterType<MeterItem>("WFVIEW", 1, 0, "Meter");


    // Helpers
    qmlRegisterType<IniTableModel>("WFVIEW", 1, 0, "IniTableModel");
    qmlRegisterType<IniSortProxy>("WFVIEW", 1, 0, "IniSortProxy");
    qmlRegisterSingletonType<ClipboardProxy>("WFVIEW", 1, 0, "Clipboard",
                                             [](QQmlEngine*, QJSEngine*) -> QObject* { return new ClipboardProxy; });


    std::cout << "DEBUG: Checking if resources are available" << std::endl;
    // Debug: Check if resources are available
    QDir resourcesDir(":/qml");
    std::cout << "DEBUG: QML directory exists: " << (resourcesDir.exists() ? "true" : "false") << std::endl;
    qInfo() << "QML directory exists:" << resourcesDir.exists();
    qInfo() << "Available resources in :/qml:";
    QStringList resources = resourcesDir.entryList();
    for (const QString& res : resources) {
        qInfo() << "  " << res;
        std::cout << "  " << res.toStdString() << std::endl;
    }

    // Debug: Check if MainWindow.qml exists
    QFile mainWindowFile(":/qml/MainWindow.qml");
    std::cout << "DEBUG: MainWindow.qml exists: " << (mainWindowFile.exists() ? "true" : "false") << std::endl;
    qInfo() << "MainWindow.qml exists in qml:" << mainWindowFile.exists();

    // Connect to QML warnings/errors
    std::cout << "DEBUG: Connecting to QML warnings" << std::endl;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                     [](const QList<QQmlError> &warnings) {
                         for (const QQmlError &warning : warnings) {
                             std::cout << "QML Warning/Error: " << warning.toString().toStdString() << std::endl;
                             qWarning() << "QML Warning/Error:" << warning.toString();
                         }
                     });

    std::cout << "DEBUG: Connecting to objectCreated" << std::endl;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &a,
                     [&a](QObject *obj, const QUrl &url) {
                         if (obj)
                             return;

                         const QString msg = QStringLiteral("Failed to load QML:\n%1").arg(url.toString());
                         qCritical().noquote() << msg;

                         // Ensure we really quit even if something is half-initialized
                         QMetaObject::invokeMethod(&a, "quit", Qt::QueuedConnection);
                     },
                     Qt::QueuedConnection);


    std::cout << "DEBUG: About to load MainWindow.qml from resources" << std::endl;
    engine.load(QUrl(QStringLiteral("qrc:/qml/MainWindow.qml")));
    std::cout << "DEBUG: engine.load() returned" << std::endl;
    if (engine.rootObjects().isEmpty())
    {
        qCritical() << "rootObjects is empty";
        return -1;
    }

    qInfo() << "connStatus index:"
            << g_mwc->metaObject()->indexOfProperty("connStatus");

    // This needs to be removed eventually
    //wfmain w(settingsFile, logFilename, debugMode);
    //w.show();

#endif
    return a.exec();

}

#ifdef BUILD_WFSERVER

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Open stream file writes
    if (type == QtDebugMsg && !debugMode)
    {
        return;
    }
    QMutexLocker locker(&logMutex);
    QTextStream out(m_logFile.data());
    QString text;

    // Write the date of recording
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");
    // By type determine to what level belongs message
    
    switch (type)
    {
        case QtDebugMsg:
            out << "DBG ";
            break;
        case QtInfoMsg:
            out << "INF ";
            break;
        case QtWarningMsg:
            out << "WRN ";
            break;
        case QtCriticalMsg:
            out << "CRT ";
            break;
        case QtFatalMsg:
            out << "FTL ";
            break;
    } 
    // Write to the output category of the message and the message itself
    out << context.category << ": " << msg << "\n";
    std::cout << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ").toLocal8Bit().toStdString() << msg.toLocal8Bit().toStdString() << "\n";
    out.flush();    // Clear the buffered data
}
#endif
