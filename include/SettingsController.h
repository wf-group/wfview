#ifndef SETTINGSCONTROLLER_H
#define SETTINGSCONTROLLER_H

#include <QObject>
#include <QSettings>
#include <QFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>

#include "audioconverter.h"
#include "rigidentities.h"
#include "rigserver.h"
#include "wfviewtypes.h"
#include "cluster.h"
#include "prefs.h" // So we can get all of the enums
#include "colorprefs.h"
#include "audiodevices.h"


class SettingsController : public QObject {
    Q_OBJECT

    // This is where ALL of the controls will be exposed

public:
    explicit SettingsController(QString file, QObject *p=nullptr);

    preferences* getPrefs() { return &prefs;}
    udpPreferences* getUdpPrefs() { return &udpPrefs;}
    SERVERCONFIG* getServerConfig() { return &serverConfig;}
    colorPrefsType getCurrentColorPreset() {return colorPreset[prefs.currentColorPresetNumber];}
    audioDevices* getAudioDevices() {return audioDev.get();}

    Q_INVOKABLE void load();
    Q_INVOKABLE void save() const;
    // Flags
    Q_DECLARE_FLAGS(prefIfItems, prefIfItem)
    Q_DECLARE_FLAGS(prefColItems, prefColItem)
    Q_DECLARE_FLAGS(prefRsItems, prefRsItem)
    Q_DECLARE_FLAGS(prefRaItems ,prefRaItem)
    Q_DECLARE_FLAGS(prefCtItems, prefCtItem)
    Q_DECLARE_FLAGS(prefLanItems, prefLanItem)
    Q_DECLARE_FLAGS(prefClusterItems, prefClusterItem)
    Q_DECLARE_FLAGS(prefUDPItems, prefUDPItem)
    Q_DECLARE_FLAGS(prefServerItems, prefServerItem)

    Q_FLAGS(prefIfItems)
    Q_FLAGS(prefColItems)
    Q_FLAGS(prefRsItems)
    Q_FLAGS(prefRaItems)
    Q_FLAGS(prefCtItems)
    Q_FLAGS(prefLanItems)
    Q_FLAGS(prefClusterItems)
    Q_FLAGS(prefUDPItems)
    Q_FLAGS(prefServerItems)

signals:
    void changedIfPrefs(SettingsController::prefIfItems i);
    void changedColPrefs(SettingsController::prefColItems i);
    void changedRaPrefs(SettingsController::prefRsItems i);
    void changedRsPrefs(SettingsController::prefRsItems i);
    void changedCtPrefs(SettingsController::prefCtItems i);
    void changedLanPrefs(SettingsController::prefLanItems i);
    void changedClusterPrefs(SettingsController::prefClusterItems i);
    void changedUDPPrefs(SettingsController::prefUDPItems i);
    void changedServerPrefs(SettingsController::prefServerItems i);

private:
    void setDefPrefs();

    QString fileName;

    colorPrefsType colorPreset[numColorPresetsTotal];
    preferences prefs;
    udpPreferences udpPrefs;
    preferences defPrefs;
    udpPreferences udpDefPrefs;

    SERVERCONFIG serverConfig;

    std::unique_ptr<QSettings> settings;
    std::unique_ptr<audioDevices> audioDev;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefIfItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefColItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefRsItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefRaItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefCtItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefLanItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefClusterItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefUDPItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefServerItems)

#endif // SETTINGSCONTROLLER_H

