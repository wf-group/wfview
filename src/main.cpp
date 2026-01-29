#include "MemoriesModel.h"
#ifdef BUILD_WFSERVER
#include <QtCore/QCoreApplication>
#include "keyboard.h"
#else
#include "qqmlapplicationengine.h"
#include <QtQml/qqml.h>   // declares qmlRegisterSingletonType/Instance
#include <QQmlEngine>
#include <QQmlApplicationEngine>
#include <QJSEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QGuiApplication>
#include <QTranslator>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <csignal>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

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
#include "memories.h"

#include "packettypes.h"
#include "calibrationwindow.h"
#include "repeatersetup.h"
#include "satellitesetup.h"
#include "bandbuttons.h"
#include "frequencyinputwidget.h"
#include "rigserver.h"
#include "icomserver.h"
#include "kenwoodserver.h"
#include "yaesuserver.h"
#include "qledlabel.h"
#include "rigctld.h"
#include "aboutbox.h"
#include "colorprefs.h"
#include "loggingwindow.h"
#include "cluster.h"
#include "audiodevices.h"
#include "sidebandchooser.h"
#include "tciserver.h"

#include "usbcontroller.h"
#include "controllersetup.h"

#include "MainController.h"
#include "ReceiverController.h"
#include "RigCreatorController.h"
#include "SelectRadioController.h"
#include "CWSenderController.h"
#include "cachingqueue.h"

#include "waterfallitem.h"
#include "spectrumitem.h"



#include "MeterItem.h"
#include "DebugController.h"

#include "logcategories.h"
#include "LoggingController.h"

bool debugMode=false;

struct QtMsgHandlerGuard {
    LoggingController* log = nullptr;
    ~QtMsgHandlerGuard() {
        if (log) log->uninstallQtMessageHandler();
    }
};

#ifdef BUILD_WFSERVER
// Smart pointer to log file
QScopedPointer<QFile>   m_logFile;
QMutex logMutex;
servermain* w=nullptr;

#ifdef Q_OS_WIN
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
        if (w!=nullptr) w->deleteLater();
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

#ifdef BUILD_WFSERVER
    QCoreApplication a(argc, argv);
    a.setApplicationName("wfserver");
    keyboard* kb = new keyboard();
    kb->start();
#else
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QApplication a(argc, argv);

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
    qRegisterMetaType<QVector<BUTTON>*>();
    qRegisterMetaType<QVector<KNOB>*>();
    qRegisterMetaType<QVector<COMMAND>*>();
    qRegisterMetaType<const COMMAND*>();
    qRegisterMetaType<const USBDEVICE*>();
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<QVector<spotData>>();
    qRegisterMetaType<networkStatus>();
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
    qRegisterMetaType<clusterSettings>();
    qRegisterMetaType<colorPrefsType>();
    qRegisterMetaType<scopeData>();

    a.setOrganizationName("wfview");
    a.setOrganizationDomain("wfview.org");


#ifdef QT_DEBUG
    //debugMode = true;
#endif

    QDateTime date = QDateTime::currentDateTime();
    QString formattedTime = date.toString("dd.MM.yyyy hh:mm:ss");
    QString logFilename = (QString("%1/%2-%3.log").arg(QStandardPaths::standardLocations(QStandardPaths::TempLocation)[0]).arg(a.applicationName()).arg(date.toString("yyyyMMddhhmmss")));

    QString settingsFile = NULL;
    QString currentArg;


    const QString helpText = QString("\nUsage: -l --logfile filename.log, -s --settings filename.ini, -c --clearconfig CONFIRM, -b --background (not Windows), -d --debug, -v --version\n"); // TODO...
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

    // Translator doesn't really make sense for wfserver right now.
    QTranslator myappTranslator;
    qDebug() << "Current translation language: " << myappTranslator.language();

    bool trResult = myappTranslator.load(QLocale(), QLatin1String("wfview"), QLatin1String("_"), QLatin1String(":/translations"));
    if(trResult) {
        qDebug() << "Recognized requested language and loaded the translations (or at least found the /translations resource folder). Installing translator.";
        a.installTranslator(&myappTranslator);
    } else {
        qDebug() << "Could not load translation.";
    }

    qDebug() << "Changed to translation language: " << myappTranslator.language();
#endif

    for(int c=1; c<argc; c++)
    {
        //qInfo() << "Argc: " << c << " argument: " << argv[c];
        currentArg = QString(argv[c]);

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
#endif
        else if ((currentArg == "-?") || (currentArg == "--help"))
        {
            std::cout << helpText.toStdString();
            return 0;
        }
        else if ((currentArg == "-v") || (currentArg == "--version"))
        {
            std::cout << version.toStdString();
            return 0;
	}
        else {
            std::cout << "Unrecognized option: " << currentArg.toStdString();
            std::cout << helpText.toStdString();
            return -1;
        }

    }

#ifdef BUILD_WFSERVER
    // Set the logging file before doing anything else.
    m_logFile.reset(new QFile(logFilename));
    // Open the file logging
    m_logFile.data()->open(QFile::WriteOnly | QFile::Truncate | QFile::Text);
    // Set handler
    qInstallMessageHandler(messageHandler);

    qInfo(logSystem()) << version;

 #ifdef Q_OS_WIN
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)cleanup, TRUE);
 #else
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGKILL, cleanup);
 #endif
    w = new servermain(settingsFile);
#else

    // Load logging window and install message handler as early as possible
    // Install capture BEFORE engine.load so QML warnings also get captured
    auto g_log = std::make_unique<LoggingController>(&a);
    g_log->setLogFilePath(logFilename);
    g_log->installQtMessageHandler();

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


    a.setWheelScrollLines(1); // one line per wheel click

    // Create MainWindow here
    qDebug(logSystem()) << "Opening MainWindow()";

    //engine.rootContext()->setContextProperty("rig", backend); // shared backend name in QML

    QQuickStyle::setStyle("Fusion"); // MUST be before loading any QML that imports Controls
    QQmlApplicationEngine engine;

    auto g_mwc = std::make_unique<MainController>(settingsFile, logFilename, debugMode, &a);
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

    qmlRegisterSingletonInstance("WFVIEW", 1, 0, "MainController", g_mwc.get());

    qmlRegisterType<ReceiverController>("WFVIEW", 1, 0, "ReceiverController");
    qmlRegisterType<RigCreatorController>("WFVIEW", 1, 0, "RigCreatorController");
    qmlRegisterType<DebugController>("WFVIEW", 1, 0, "DebugController");
    qmlRegisterType<SelectRadioController>("WFVIEW", 1, 0, "SelectRadioController");
    qmlRegisterType<CWSenderController>("WFVIEW", 1, 0, "CWSenderController");
    qmlRegisterType<MemoriesModel>("WFVIEW", 1, 0, "MemoriesModel");

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


    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &a,
                     [&a](QObject *obj, const QUrl &url) {
                         if (obj)
                             return;

                         const QString msg = QStringLiteral("Failed to load QML:\n%1").arg(url.toString());
                         QMessageBox::critical(nullptr, QStringLiteral("Startup error"), msg);

                         // Ensure we really quit even if something is half-initialized
                         QMetaObject::invokeMethod(&a, "quit", Qt::QueuedConnection);
                     },
                     Qt::QueuedConnection);


    engine.load(QUrl(QStringLiteral("qrc:/qml/MainWindow.qml")));
    if (engine.rootObjects().isEmpty())
    {
        QMessageBox::critical(nullptr, QStringLiteral("Startup error"), "rootObjects is empty");
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
