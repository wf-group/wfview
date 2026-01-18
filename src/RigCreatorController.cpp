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

    return m_store->saveAs(file);
}



void RigCreatorController::closeEvent(QCloseEvent *event)
{

    Q_UNUSED(event)
    if (settingsChanged)
    {
        // Settings have changed since last save
        qInfo() << "Settings have changed since last save";
        //int reply = QMessageBox::question(this,tr("rig creator"),tr("Changes will be lost!"),QMessageBox::Cancel |QMessageBox::Ok);
        //if (reply == QMessageBox::Cancel)
        //{
        //    event->ignore();
       // }
    }
}

