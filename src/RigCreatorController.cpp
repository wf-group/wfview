#include <QDebug>
#include "logcategories.h"

#include "RigCreatorController.h"
#include "themebridge.h"

// Access main Qt theme from QML.

RigCreatorController::RigCreatorController(QObject *parent) :
    QObject(parent)
{

    qInfo() << "Creating instance of RigCreatorController()";

    m_store    = new IniStore(this);
    m_settings = new SettingsMap(this);
    m_settings->setStore(m_store);

    connect(m_store, &IniStore::dirtyChanged, this, &RigCreatorController::dirtyChanged);


    for (int i=0;i<funcLastFunc;i++)
        m_commandTypeChoices << QVariantMap{{"text",funcString[i]}, {"value",funcString[i]}};
    emit commandTypeChoicesChanged();

}



RigCreatorController::~RigCreatorController()
{
    qInfo() << "Deleting instance of RigCreatorController()";
}

bool RigCreatorController::loadFile(QString file)
{

    const QUrl url(file);
    if (url.isValid() && url.isLocalFile()) {
        file = url.toLocalFile();          // handles file:///, %20, Windows paths, etc.
    }

    qInfo() << "Loading file: " << file;

    if (file.isEmpty() || !QFileInfo::exists(file))
        return false;

    setLoading(true);

    const bool ok = m_store->load(file);
    if (ok) {
        currentFile = file;
        emit iniPathChanged();
    }

    setLoading(false);

    return ok;
}


void RigCreatorController::changed()
{
    settingsChanged = true;
}

bool RigCreatorController::saveFile(QString file)
{
    qInfo() << "Saving file: " << file;

    const QUrl url(file);
    if (url.isValid() && url.isLocalFile())
        file = url.toLocalFile();

    if (file.isEmpty())
        return false;

    // Optional: enforce extension
    if (!file.endsWith(".rig", Qt::CaseInsensitive))
        file += ".rig";

    // Stamp the file with the wfview version that saved it ([General] Version;
    // QSettings keeps the General group's keys at the top level).
    m_store->set(QStringLiteral("Version"), QStringLiteral(WFVIEW_VERSION_NUMERIC));

    const bool ok = m_store->saveAs(file);
    if (ok) {
        currentFile = file;
        emit iniPathChanged();
    } else {
        // Say so: a silent failure leaves the file unsaved, and closing then
        // asks to save it again and again.
        const QString userFolder = QDir::toNativeSeparators(userRigsFolder().toLocalFile());
        emit showMessage(tr("Rig file not saved"),
                         tr("Could not save %1: %2.\n\n"
                            "Save it to your own rigs folder instead:\n%3\n\n"
                            "wfview uses a rig file there in place of the installed one for the same radio, "
                            "as long as its Version is the same or newer.")
                             .arg(QDir::toNativeSeparators(file), m_store->lastError(), userFolder));
    }
    return ok;
}
