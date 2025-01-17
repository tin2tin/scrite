/****************************************************************************
**
** Copyright (C) TERIFLIX Entertainment Spaces Pvt. Ltd. Bengaluru
** Author: Prashanth N Udupa (prashanth.udupa@teriflix.com)
**
** This code is distributed under GPL v3. Complete text of the license
** can be found here: https://www.gnu.org/licenses/gpl-3.0.txt
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
****************************************************************************/

#include "scritedocument.h"

#include "form.h"
#include "undoredo.h"
#include "hourglass.h"
#include "aggregation.h"
#include "application.h"
#include "pdfexporter.h"
#include "odtexporter.h"
#include "htmlexporter.h"
#include "textexporter.h"
#include "notification.h"
#include "htmlimporter.h"
#include "timeprofiler.h"
#include "qobjectfactory.h"
#include "locationreport.h"
#include "characterreport.h"
#include "fountainimporter.h"
#include "fountainexporter.h"
#include "structureexporter.h"
#include "qobjectserializer.h"
#include "finaldraftimporter.h"
#include "finaldraftexporter.h"
#include "screenplaysubsetreport.h"
#include "locationscreenplayreport.h"
#include "characterscreenplayreport.h"
#include "scenecharactermatrixreport.h"

#include <QDir>
#include <QUuid>
#include <QFuture>
#include <QPainter>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrentRun>
#include <QRandomGenerator>
#include <QFileSystemWatcher>
#include <QScopedValueRollback>

ScriteDocumentBackups::ScriteDocumentBackups(QObject *parent)
    : QAbstractListModel(parent)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(50);
    connect(&m_reloadTimer, &QTimer::timeout, this, &ScriteDocumentBackups::reloadBackupFileInformation);
}

ScriteDocumentBackups::~ScriteDocumentBackups()
{

}

QJsonObject ScriteDocumentBackups::at(int index) const
{
    QJsonObject ret;

    if(index < 0 || index >= m_backupFiles.size())
        return ret;

    const QModelIndex idx = this->index(index, 0, QModelIndex());

    const QHash<int,QByteArray> roles = this->roleNames();
    auto it = roles.begin();
    auto end = roles.end();
    while(it != end)
    {
        ret.insert( QString::fromLatin1(it.value()), QJsonValue::fromVariant(this->data(idx, it.key())) );
        ++it;
    }

    return ret;
}

int ScriteDocumentBackups::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_backupFiles.size();
}

QVariant ScriteDocumentBackups::data(const QModelIndex &index, int role) const
{
    if(!index.isValid() || index.row() < 0 || index.row() >= m_backupFiles.size())
        return QVariant();

    const QFileInfo fi = m_backupFiles.at(index.row());
    switch(role)
    {
    case TimestampRole:
        return fi.birthTime().toMSecsSinceEpoch();
    case TimestampAsStringRole:
        return fi.birthTime().toString();
    case Qt::DisplayRole:
    case FileNameRole:
        return fi.baseName();
    case FilePathRole:
        return fi.absoluteFilePath();
    case RelativeTimeRole:
        return relativeTime(fi.birthTime());
    case FileSizeRole:
        return fi.size();
    case MetaDataRole:
        if(!m_metaDataList.at(index.row()).loaded)
            (const_cast<ScriteDocumentBackups*>(this))->loadMetaData(index.row());
        return m_metaDataList.at(index.row()).toJson();
    }

    return QVariant();
}

QHash<int, QByteArray> ScriteDocumentBackups::roleNames() const
{
    static QHash<int, QByteArray> roles =
        {
            { TimestampRole, QByteArrayLiteral("timestamp") },
            { TimestampAsStringRole, QByteArrayLiteral("timestampAsString") },
            { RelativeTimeRole, QByteArrayLiteral("relativeTime")},
            { FileNameRole, QByteArrayLiteral("fileName") },
            { FilePathRole, QByteArrayLiteral("filePath") },
            { FileSizeRole, QByteArrayLiteral("fileSize") },
            { MetaDataRole,QByteArrayLiteral("metaData") }
        };
    return roles;
}

QString ScriteDocumentBackups::relativeTime(const QDateTime &dt)
{
    if(!dt.isValid())
        return QStringLiteral("Unknown Time");

    const QDateTime now = QDateTime::currentDateTime();
    if(now.date() == dt.date())
    {
        const int secsInMin = 60;
        const int secsInHour = secsInMin * 60;

        // Just say how many minutes or hours ago.
        const int nrSecs = dt.time().secsTo(now.time());
        const int nrHours = nrSecs > secsInHour ? qFloor( qreal(nrSecs)/qreal(secsInHour) ) : 0;
        const int nrSecsRemaining = nrSecs-nrHours*secsInHour;
        const int nrMins = nrSecs > secsInMin ? qCeil( qreal(nrSecsRemaining)/qreal(secsInMin) ) : 0;

        if(nrMins == 0)
            return QStringLiteral("Less than a minute ago");
        if(nrHours == 0)
            return QString::number( qCeil(qreal(nrSecs)/qreal(secsInMin)) ) + QStringLiteral("m ago");

        return QString::number(nrHours) + QStringLiteral("h ") + QString::number(nrMins) + QStringLiteral("m ago");
    }

    const int nrDays = dt.date().daysTo(now.date());
    const QString time = dt.time().toString(QStringLiteral("h:mm A"));
    switch(nrDays)
    {
    case 1:
        return QStringLiteral("Yesterday @ ") + time;
    case 2:
        return QStringLiteral("Day before yesterday @ ") + time;
    case 3:
    case 4:
    case 5:
    case 6:
        return QString::number(nrDays) + QStringLiteral(" days ago @ ") + time;
    default:
        break;
    }

    if(nrDays >= 7 && nrDays < 14)
        return QStringLiteral("Last week ") + QLocale::system().standaloneDayName(dt.date().dayOfWeek()) + " @ " + time;

    if(nrDays >= 14 && nrDays < 21)
        return QStringLiteral("Two weeks ago");

    if(nrDays >= 21 && nrDays < 28)
        return QStringLiteral("Three weeks ago");

    if(nrDays >= 28 && nrDays < 60)
        return QStringLiteral("Little more than a month ago");

    return QStringLiteral("More than two months ago");
}

void ScriteDocumentBackups::setDocumentFilePath(const QString &val)
{
    if(m_documentFilePath == val)
        return;

    m_documentFilePath = val;
    emit documentFilePathChanged();

    this->loadBackupFileInformation();
}

void ScriteDocumentBackups::loadBackupFileInformation()
{
    if(m_documentFilePath.isEmpty())
    {
        this->clear();
        return;
    }

    const QFileInfo fi(m_documentFilePath);
    if(!fi.exists() || fi.suffix() != QStringLiteral("scrite"))
    {
        this->clear();
        return;
    }

    const QString backupDirPath(fi.absolutePath() + QStringLiteral("/") + fi.baseName() + QStringLiteral(" Backups"));
    QDir backupDir(backupDirPath);
    if(!backupDir.exists())
    {
        this->clear();
        return;
    }

    if(m_fsWatcher == nullptr)
    {
        m_fsWatcher = new QFileSystemWatcher(this);
        connect(m_fsWatcher, SIGNAL(directoryChanged(QString)), &m_reloadTimer, SLOT(start()));
        m_fsWatcher->addPath(backupDirPath);
    }

    m_backupFilesDir = backupDir;
    this->reloadBackupFileInformation();
}

void ScriteDocumentBackups::reloadBackupFileInformation()
{
    const QString futureWatcherName = QStringLiteral("ReloadFutureWatcher");
    if(this->findChild<QFutureWatcherBase*>(futureWatcherName,Qt::FindDirectChildrenOnly))
    {
        m_reloadTimer.start();
        return;
    }

    /**
     * Why all this circus?
     *
     * Depending on the kind of OS and hard-disk used, querying directory information may take
     * a few milliseconds or maybe even up to a few seconds. We shouldn't however freeze the
     * UI or even the business logic layer during that time. Updating the model is not a critical
     * activity, so it can take its own sweet time.
     *
     * We push directory query to a separate thread and update the model whenever its job is
     * done.
     */
    QFuture<QFileInfoList> future = QtConcurrent::run([=]() -> QFileInfoList {
        return m_backupFilesDir.entryInfoList({QStringLiteral("*.scrite")}, QDir::Files, QDir::Time);
    });
    QFutureWatcher<QFileInfoList> *futureWatcher = new QFutureWatcher<QFileInfoList>(this);
    futureWatcher->setObjectName(futureWatcherName);
    connect(futureWatcher, &QFutureWatcher<QFileInfoList>::finished, [=]() {
        futureWatcher->deleteLater();

        this->beginResetModel();
        m_backupFiles = future.result();
        m_metaDataList.resize(m_backupFiles.size());
        this->endResetModel();

        emit countChanged();
    });
    futureWatcher->setFuture(future);
}

void ScriteDocumentBackups::loadMetaData(int row)
{
    if(row < 0 || row >= m_backupFiles.size())
        return;

    const QString futureWatcherName = QStringLiteral("loadMetaDataFuture");

    const QFileInfo fi = m_backupFiles.at(row);
    const QString fileName = fi.absoluteFilePath();

    QFuture<MetaData> future = QtConcurrent::run([](const QString &fileName) -> MetaData {
        MetaData ret;

        DocumentFileSystem dfs;
        if( !dfs.load(fileName) ) {
            ret.loaded = true;
            return ret;
        }

        const QJsonDocument jsonDoc = QJsonDocument::fromJson(dfs.header());
        const QJsonObject docObj = jsonDoc.object();

        const QJsonObject structure = docObj.value(QStringLiteral("structure")).toObject();
        ret.structureElementCount = structure.value(QStringLiteral("elements")).toArray().size();

        const QJsonObject screenplay = docObj.value(QStringLiteral("screenplay")).toObject();
        ret.screenplayElementCount = screenplay.value(QStringLiteral("elements")).toArray().size();

        ret.loaded = true;

        return ret;
    }, fileName);

    QFutureWatcher<MetaData> *futureWatcher = new QFutureWatcher<MetaData>(this);
    futureWatcher->setObjectName(futureWatcherName);
    connect(futureWatcher, &QFutureWatcher<MetaData>::finished, [=]() {
        if(row < 0 || row >= m_metaDataList.size())
            return;

        m_metaDataList.replace(row, future.result());

        const QModelIndex index = this->index(row, 0);
        emit dataChanged(index, index);
    });
    futureWatcher->setFuture(future);

    connect(this, &ScriteDocumentBackups::modelAboutToBeReset, futureWatcher, &QObject::deleteLater);
}

void ScriteDocumentBackups::clear()
{
    delete m_fsWatcher;
    m_fsWatcher = nullptr;

    m_backupFilesDir = QDir();

    if(m_backupFiles.isEmpty())
        return;

    this->beginResetModel();
    m_backupFiles.clear();
    m_metaDataList.clear();
    m_metaDataList.squeeze();
    this->endResetModel();

    emit countChanged();
}

///////////////////////////////////////////////////////////////////////////////

class DeviceIOFactories
{
public:
    DeviceIOFactories();
    ~DeviceIOFactories();

    QObjectFactory ImporterFactory;
    QObjectFactory ExporterFactory;
    QObjectFactory ReportsFactory;
};

DeviceIOFactories::DeviceIOFactories()
    : ImporterFactory(QByteArrayLiteral("Format")),
      ExporterFactory(QByteArrayLiteral("Format")),
      ReportsFactory(QByteArrayLiteral("Title"))
{
    ImporterFactory.addClass<HtmlImporter>();
    ImporterFactory.addClass<FountainImporter>();
    ImporterFactory.addClass<FinalDraftImporter>();

    ExporterFactory.addClass<OdtExporter>();
    ExporterFactory.addClass<PdfExporter>();
    ExporterFactory.addClass<HtmlExporter>();
    ExporterFactory.addClass<TextExporter>();
    ExporterFactory.addClass<FountainExporter>();
    ExporterFactory.addClass<StructureExporter>();
    ExporterFactory.addClass<FinalDraftExporter>();

    ReportsFactory.addClass<ScreenplaySubsetReport>();
    ReportsFactory.addClass<LocationReport>();
    ReportsFactory.addClass<LocationScreenplayReport>();
    ReportsFactory.addClass<CharacterReport>();
    ReportsFactory.addClass<CharacterScreenplayReport>();
    ReportsFactory.addClass<SceneCharacterMatrixReport>();
}

DeviceIOFactories::~DeviceIOFactories()
{
}

Q_GLOBAL_STATIC(DeviceIOFactories, deviceIOFactories)

ScriteDocument *ScriteDocument::instance()
{
    static ScriteDocument *theInstance = new ScriteDocument(qApp);
    return theInstance;
}

ScriteDocument::ScriteDocument(QObject *parent)
                :QObject(parent),
                  m_autoSaveTimer("ScriteDocument.m_autoSaveTimer"),
                  m_clearModifyTimer("ScriteDocument.m_clearModifyTimer"),
                  m_structure(this, "structure"),
                  m_connectors(this),
                  m_screenplay(this, "screenplay"),
                  m_formatting(this, "formatting"),
                  m_printFormat(this, "printFormat"),
                  m_forms(this, "forms"),
                  m_evaluateStructureElementSequenceTimer("ScriteDocument.m_evaluateStructureElementSequenceTimer")
{
    this->reset();
    this->updateDocumentWindowTitle();

    connect(this, &ScriteDocument::spellCheckIgnoreListChanged, this, &ScriteDocument::markAsModified);
    connect(this, &ScriteDocument::userDataChanged, this, &ScriteDocument::markAsModified);
    connect(this, &ScriteDocument::modifiedChanged, this, &ScriteDocument::updateDocumentWindowTitle);
    connect(this, &ScriteDocument::fileNameChanged, this, &ScriteDocument::updateDocumentWindowTitle);
    connect(this, &ScriteDocument::fileNameChanged, [=]() {
        m_documentBackupsModel.setDocumentFilePath( m_fileName );
    });

    const QVariant ase = Application::instance()->settings()->value("AutoSave/autoSaveEnabled");
    this->setAutoSave( ase.isValid() ? ase.toBool() : m_autoSave );

    const QVariant asd = Application::instance()->settings()->value("AutoSave/autoSaveInterval");
    this->setAutoSaveDurationInSeconds( asd.isValid() ? asd.toInt() : m_autoSaveDurationInSeconds );

    m_autoSaveTimer.setRepeat(true);
    this->prepareAutoSave();
}

ScriteDocument::~ScriteDocument()
{

}

void ScriteDocument::setLocked(bool val)
{
    if(m_locked == val)
        return;

    m_locked = val;
    emit lockedChanged();

    this->markAsModified();
}

bool ScriteDocument::isEmpty() const
{
    const int objectCount = m_structure->elementCount() +
                            m_structure->annotationCount() +
                            m_screenplay->elementCount() +
                            m_structure->notes()->noteCount() +
                            m_structure->characterCount() +
                            m_structure->attachments()->attachmentCount();
    const bool ret = objectCount == 0 && m_screenplay->isEmpty();
    return ret;
}

void ScriteDocument::setAutoSaveDurationInSeconds(int val)
{
    val = qBound(1, val, 3600);
    if(m_autoSaveDurationInSeconds == val)
        return;

    m_autoSaveDurationInSeconds = val;
    Application::instance()->settings()->setValue("AutoSave/autoSaveInterval", val);
    this->prepareAutoSave();
    emit autoSaveDurationInSecondsChanged();
}

void ScriteDocument::setAutoSave(bool val)
{
    if(m_autoSave == val)
        return;

    m_autoSave = val;
    Application::instance()->settings()->setValue("AutoSave/autoSaveEnabled", val);
    this->prepareAutoSave();
    emit autoSaveChanged();
}

void ScriteDocument::setBusy(bool val)
{
    if(m_busy == val)
        return;

    m_busy = val;
    emit busyChanged();

    if(val)
        qApp->setOverrideCursor(Qt::WaitCursor);
    else
        qApp->restoreOverrideCursor();
}

void ScriteDocument::setBusyMessage(const QString &val)
{
    if(m_busyMessage == val)
        return;

    m_busyMessage = val;
    emit busyMessageChanged();

    this->setBusy(!m_busyMessage.isEmpty());
}

void ScriteDocument::setSpellCheckIgnoreList(const QStringList &val)
{
    QStringList val2 = val.toSet().toList(); // so that we eliminate all duplicates
    std::sort(val2.begin(), val2.end());
    if(m_spellCheckIgnoreList == val)
        return;

    m_spellCheckIgnoreList = val;
    emit spellCheckIgnoreListChanged();
}

void ScriteDocument::addToSpellCheckIgnoreList(const QString &word)
{
    if(word.isEmpty() || m_spellCheckIgnoreList.contains(word))
        return;

    m_spellCheckIgnoreList.append(word);
    std::sort(m_spellCheckIgnoreList.begin(), m_spellCheckIgnoreList.end());
    emit spellCheckIgnoreListChanged();
}

Forms *ScriteDocument::globalForms() const
{
    return Forms::global();
}

Form *ScriteDocument::requestForm(const QString &id)
{
    Form *ret = m_forms->findForm(id);
    if(ret)
    {
        ret->ref();
        return ret;
    }

    ret = Forms::global()->findForm(id);
    if(ret)
    {
        const QJsonObject fjs = QObjectSerializer::toJson(ret);
        ret = m_forms->addForm(fjs);
        ret->ref();
        m_forms->append(ret);
    }

    return ret;
}

void ScriteDocument::releaseForm(Form *form)
{
    const int index = form == nullptr ? -1 : m_forms->indexOf(form);
    if(index < 0)
        return;

    if(form->deref() <= 0)
    {
        m_forms->removeAt(index);
        form->deleteLater();
    }
}

/**
When createNewScene() is called as a because of Ctrl+Shift+N shortcut key then
a new scene must be created after all act-breaks. (fuzzyScreenplayInsert = true)

Otherwise it must add a new scene after the current scene in screenplay.
*/
Scene *ScriteDocument::createNewScene(bool fuzzyScreenplayInsert)
{
    QScopedValueRollback<bool> createNewSceneRollback(m_inCreateNewScene, true);

    StructureElement *structureElement = nullptr;
    int structureElementIndex = m_structure->elementCount()-1;
    if(m_structure->currentElementIndex() >= 0)
        structureElementIndex = m_structure->currentElementIndex();

    structureElement = m_structure->elementAt(structureElementIndex);

    Scene *activeScene = structureElement ? structureElement->scene() : nullptr;

    const QVector<QColor> standardColors = Application::standardColors(QVersionNumber());
    const QColor defaultColor = standardColors.at( QRandomGenerator::global()->bounded(standardColors.size()-1) );

    Scene *scene = new Scene(m_structure);
    scene->setColor(activeScene ? activeScene->color() : defaultColor);
    if(m_structure->canvasUIMode() != Structure::IndexCardUI)
        scene->setTitle( QStringLiteral("New Scene") );
    scene->heading()->setEnabled(true);
    scene->heading()->setLocationType(activeScene ? activeScene->heading()->locationType() : QStringLiteral("EXT"));
    scene->heading()->setLocation(activeScene ? activeScene->heading()->location() : QStringLiteral("SOMEWHERE"));
    scene->heading()->setMoment(activeScene ? QStringLiteral("LATER") : QStringLiteral("DAY"));

    SceneElement *firstPara = new SceneElement(scene);
    firstPara->setType(SceneElement::Action);
    scene->addElement(firstPara);

    StructureElement *newStructureElement = new StructureElement(m_structure);
    newStructureElement->setScene(scene);
    m_structure->addElement(newStructureElement);

    const bool asLastScene = m_screenplay->currentElementIndex() < 0 ||
                            (fuzzyScreenplayInsert && m_screenplay->currentElementIndex() == m_screenplay->lastSceneIndex());

    ScreenplayElement *newScreenplayElement = new ScreenplayElement(m_screenplay);
    newScreenplayElement->setScene(scene);
    int newScreenplayElementIndex = -1;
    if(asLastScene)
    {
        newScreenplayElementIndex = m_screenplay->elementCount();
        m_screenplay->addElement(newScreenplayElement);
    }
    else
    {
        newScreenplayElementIndex = m_screenplay->currentElementIndex()+1;
        m_screenplay->insertElementAt(newScreenplayElement, m_screenplay->currentElementIndex()+1);
    }

    if(m_screenplay->elementAt(newScreenplayElementIndex) != newScreenplayElement)
        newScreenplayElementIndex = m_screenplay->indexOfElement(newScreenplayElement);

    m_structure->placeElement(newStructureElement, m_screenplay);
    m_structure->setCurrentElementIndex(m_structure->elementCount()-1);
    m_screenplay->setCurrentElementIndex(newScreenplayElementIndex);

    if(structureElement && !structureElement->stackId().isEmpty())
    {
        ScreenplayElement *spe_before = m_screenplay->elementAt(newScreenplayElementIndex-1);
        ScreenplayElement *spe_after = m_screenplay->elementAt(newScreenplayElementIndex+1);
        if(spe_before && spe_after)
        {
            StructureElement *ste_before = m_structure->elementAt(m_structure->indexOfScene(spe_before->scene()));
            StructureElement *ste_after = m_structure->elementAt(m_structure->indexOfScene(spe_after->scene()));
            if(ste_before && ste_after)
            {
                if(ste_before->stackId() == ste_after->stackId())
                    newStructureElement->setStackId(ste_before->stackId());
            }
        }
    }

    if(newScreenplayElementIndex > 0 && newScreenplayElementIndex == m_screenplay->elementCount()-1)
    {
        ScreenplayElement *prevElement = m_screenplay->elementAt(newScreenplayElementIndex-1);
        if(prevElement->elementType() == ScreenplayElement::BreakElementType)
            scene->setColor(defaultColor);
    }

    emit newSceneCreated(scene, newScreenplayElementIndex);

    scene->setUndoRedoEnabled(true);
    return scene;
}

void ScriteDocument::setUserData(const QJsonObject &val)
{
    if(m_userData == val)
        return;

    m_userData = val;
    emit userDataChanged();
}

void ScriteDocument::setBookmarkedNotes(const QJsonArray &val)
{
    if(m_bookmarkedNotes == val)
        return;

    m_bookmarkedNotes = val;
    emit bookmarkedNotesChanged();
}

void ScriteDocument::reset()
{
    HourGlass hourGlass;

    m_connectors.clear();

    if(m_structure != nullptr)
    {
        disconnect(m_structure, &Structure::currentElementIndexChanged, this, &ScriteDocument::structureElementIndexChanged);
        disconnect(m_structure, &Structure::structureChanged, this, &ScriteDocument::markAsModified);
        disconnect(m_structure, &Structure::elementCountChanged, this, &ScriteDocument::emptyChanged);
        disconnect(m_structure, &Structure::annotationCountChanged, this, &ScriteDocument::emptyChanged);
        disconnect(m_structure->notes(), &Notes::notesModified, this, &ScriteDocument::emptyChanged);
        disconnect(m_structure, &Structure::preferredGroupCategoryChanged, m_screenplay, &Screenplay::updateBreakTitlesLater);
        disconnect(m_structure, &Structure::groupsModelChanged, m_screenplay, &Screenplay::updateBreakTitlesLater);
    }

    if(m_screenplay != nullptr)
    {
        disconnect(m_screenplay, &Screenplay::currentElementIndexChanged, this, &ScriteDocument::screenplayElementIndexChanged);
        disconnect(m_screenplay, &Screenplay::screenplayChanged, this, &ScriteDocument::markAsModified);
        disconnect(m_screenplay, &Screenplay::screenplayChanged, this, &ScriteDocument::evaluateStructureElementSequenceLater);
        disconnect(m_screenplay, &Screenplay::elementRemoved, this, &ScriteDocument::screenplayElementRemoved);
        disconnect(m_screenplay, &Screenplay::emptyChanged, this, &ScriteDocument::emptyChanged);
        disconnect(m_screenplay, &Screenplay::elementCountChanged, this, &ScriteDocument::emptyChanged);
    }

    if(m_formatting != nullptr)
        disconnect(m_formatting, &ScreenplayFormat::formatChanged, this, &ScriteDocument::markAsModified);

    if(m_printFormat != nullptr)
        disconnect(m_printFormat, &ScreenplayFormat::formatChanged, this, &ScriteDocument::markAsModified);

    UndoStack::clearAllStacks();
    m_docFileSystem.reset();

    this->setSessionId( QUuid::createUuid().toString() );
    this->setReadOnly(false);
    this->setLocked(false);

    if(m_formatting == nullptr)
        this->setFormatting(new ScreenplayFormat(this));
    else
        m_formatting->resetToDefaults();

    if(m_printFormat == nullptr)
        this->setPrintFormat(new ScreenplayFormat(this));
    else
        m_printFormat->resetToDefaults();

    this->setForms(new Forms(this));
    this->setScreenplay(new Screenplay(this));
    this->setStructure(new Structure(this));
    this->setBookmarkedNotes(QJsonArray());
    this->setSpellCheckIgnoreList(QStringList());
    this->setFileName(QString());
    this->setUserData(QJsonObject());
    this->evaluateStructureElementSequence();
    this->setModified(false);
    emit emptyChanged();

    connect(m_structure, &Structure::currentElementIndexChanged, this, &ScriteDocument::structureElementIndexChanged);
    connect(m_structure, &Structure::structureChanged, this, &ScriteDocument::markAsModified);
    connect(m_structure, &Structure::elementCountChanged, this, &ScriteDocument::emptyChanged);
    connect(m_structure, &Structure::annotationCountChanged, this, &ScriteDocument::emptyChanged);
    connect(m_structure->notes(), &Notes::notesModified, this, &ScriteDocument::emptyChanged);
    connect(m_structure, &Structure::preferredGroupCategoryChanged, m_screenplay, &Screenplay::updateBreakTitlesLater);
    connect(m_structure, &Structure::groupsModelChanged, m_screenplay, &Screenplay::updateBreakTitlesLater);

    connect(m_screenplay, &Screenplay::currentElementIndexChanged, this, &ScriteDocument::screenplayElementIndexChanged);
    connect(m_screenplay, &Screenplay::screenplayChanged, this, &ScriteDocument::markAsModified);
    connect(m_screenplay, &Screenplay::screenplayChanged, this, &ScriteDocument::evaluateStructureElementSequenceLater);
    connect(m_screenplay, &Screenplay::elementRemoved, this, &ScriteDocument::screenplayElementRemoved);
    connect(m_screenplay, &Screenplay::emptyChanged, this, &ScriteDocument::emptyChanged);
    connect(m_screenplay, &Screenplay::elementCountChanged, this, &ScriteDocument::emptyChanged);

    connect(m_formatting, &ScreenplayFormat::formatChanged, this, &ScriteDocument::markAsModified);
    connect(m_printFormat, &ScreenplayFormat::formatChanged, this, &ScriteDocument::markAsModified);

    emit justReset();
}

bool ScriteDocument::openOrImport(const QString &fileName)
{
    if(fileName.isEmpty())
        return false;

    const QFileInfo fi(fileName);
    const QString absFileName = fi.absoluteFilePath();

    if( fi.suffix() == QStringLiteral("scrite") )
        return this->open( absFileName );

    const QList<QByteArray> keys = ::deviceIOFactories->ImporterFactory.keys();
    for(const QByteArray &key : keys)
    {
        QScopedPointer<AbstractImporter> importer(::deviceIOFactories->ImporterFactory.create<AbstractImporter>(key, this));
        if(importer->canImport(absFileName))
            return this->importFile(importer.data(), fileName);
    }

    return false;
}

bool ScriteDocument::open(const QString &fileName)
{
    if(fileName == m_fileName)
        return false;

    HourGlass hourGlass;

    this->setBusyMessage("Loading " + QFileInfo(fileName).baseName() + " ...");
    this->reset();
    const bool ret = this->load(fileName);
    if(ret)
        this->setFileName(fileName);
    this->setModified(false);
    this->clearBusyMessage();

    return ret;
}

bool ScriteDocument::openAnonymously(const QString &fileName)
{
    HourGlass hourGlass;

    this->setBusyMessage("Loading ...");
    this->reset();
    const bool ret = this->load(fileName);
    this->setModified(false);
    this->clearBusyMessage();

    m_fileName.clear();
    emit fileNameChanged();

    return ret;
}

void ScriteDocument::saveAs(const QString &givenFileName)
{
    HourGlass hourGlass;
    QString fileName = this->polishFileName(givenFileName.trimmed());
    fileName = Application::instance()->sanitiseFileName(fileName);

    m_errorReport->clear();

    if(!this->runSaveSanityChecks(fileName))
        return;

    if(!m_autoSaveMode)
        this->setBusyMessage("Saving to " + QFileInfo(fileName).baseName() + " ...");

    m_progressReport->start();

    emit aboutToSave();

    const QJsonObject json = QObjectSerializer::toJson(this);
    const QByteArray bytes = QJsonDocument(json).toJson();
    m_docFileSystem.setHeader(bytes);

    const bool success = m_docFileSystem.save(fileName);

    if(!success)
    {
        m_errorReport->setErrorMessage( QStringLiteral("Couldn't save document \"") + fileName + QStringLiteral("\"") );
        emit justSaved();
        m_progressReport->finish();
        if(!m_autoSaveMode)
            this->clearBusyMessage();
        return;
    }

#ifndef QT_NO_DEBUG
    {
        const QFileInfo fi(fileName);
        const QString fileName2 = fi.absolutePath() + "/" + fi.baseName() + ".json";
        QFile file2(fileName2);
        file2.open(QFile::WriteOnly);
        file2.write(bytes);
    }
#endif

    this->setFileName(fileName);
    this->setCreatedOnThisComputer(true);

    emit justSaved();

    m_modified = false;
    emit modifiedChanged();

    m_progressReport->finish();

    this->setReadOnly(false);

    if(!m_autoSaveMode)
        this->clearBusyMessage();
}

void ScriteDocument::save()
{
    HourGlass hourGlass;

    if(m_readOnly)
        return;

    if(!this->runSaveSanityChecks(m_fileName))
        return;

    QFileInfo fi(m_fileName);
    if(fi.exists())
    {
        const QString backupDirPath(fi.absolutePath() + "/" + fi.baseName() + " Backups");
        QDir().mkpath(backupDirPath);

        const qint64 now = QDateTime::currentSecsSinceEpoch();

        auto timeGapInSeconds = [now](const QFileInfo &fi) {
            const QString baseName = fi.baseName();
            const QString thenStr = baseName.section('[', 1).section(']', 0, 0);
            const qint64 then = thenStr.toLongLong();
            return now - then;
        };

        const QDir backupDir(backupDirPath);
        QFileInfoList backupEntries = backupDir.entryInfoList(QStringList() << QStringLiteral("*.scrite"), QDir::Files, QDir::Name);
        const bool firstBackup = backupEntries.isEmpty();
        if(!backupEntries.isEmpty())
        {
            static const int maxBackups = 20;
            while(backupEntries.size() > maxBackups-1)
            {
                const QFileInfo oldestEntry = backupEntries.takeFirst();
                QFile::remove(oldestEntry.absoluteFilePath());
            }

            const QFileInfo latestEntry = backupEntries.takeLast();
            if(latestEntry.suffix() == QStringLiteral("scrite"))
            {
                if(timeGapInSeconds(latestEntry) < 60)
                    QFile::remove(latestEntry.absoluteFilePath());
            }
        }

        const QString backupFileName = backupDirPath + "/" + fi.baseName() + " [" + QString::number(now) + "].scrite";
        const bool backupSuccessful = QFile::copy(m_fileName, backupFileName);

        if(firstBackup && backupSuccessful)
            m_documentBackupsModel.loadBackupFileInformation();
    }

    this->saveAs(m_fileName);
}

QStringList ScriteDocument::supportedImportFormats() const
{
    static QList<QByteArray> keys = deviceIOFactories->ImporterFactory.keys();
    static QStringList formats;
    if(formats.isEmpty())
        Q_FOREACH(QByteArray key, keys) formats << key;
    return formats;
}

QString ScriteDocument::importFormatFileSuffix(const QString &format) const
{
    const QMetaObject *mo = deviceIOFactories->ImporterFactory.find(format.toLatin1());
    if(mo == nullptr)
        return QString();

    const int ciIndex = mo->indexOfClassInfo("NameFilters");
    if(ciIndex < 0)
        return QString();

    const QMetaClassInfo classInfo = mo->classInfo(ciIndex);
    return QString::fromLatin1(classInfo.value());
}

QStringList ScriteDocument::supportedExportFormats() const
{
    static QList<QByteArray> keys = deviceIOFactories->ExporterFactory.keys();
    static QStringList formats;
    if(formats.isEmpty())
    {
        Q_FOREACH(QByteArray key, keys) formats << key;
        std::sort(formats.begin(), formats.end());

        if(formats.size() >= 2)
        {
            QList<int> seps;
            for(int i=formats.size()-2; i>=0; i--)
            {
                QString thisFormat = formats.at(i);
                QString previousFormat = formats.at(i+1);
                if(thisFormat.split("/").first() != previousFormat.split("/").first())
                    seps << i+1;
            }

            Q_FOREACH(int sep, seps)
                formats.insert(sep, QString());
        }
    }
    return formats;
}

QString ScriteDocument::exportFormatFileSuffix(const QString &format) const
{
    const QMetaObject *mo = deviceIOFactories->ExporterFactory.find(format.toLatin1());
    if(mo == nullptr)
        return QString();

    const int ciIndex = mo->indexOfClassInfo("NameFilters");
    if(ciIndex < 0)
        return QString();

    const QMetaClassInfo classInfo = mo->classInfo(ciIndex);
    return QString::fromLatin1(classInfo.value());
}

QJsonArray ScriteDocument::supportedReports() const
{
    static QList<QByteArray> keys = deviceIOFactories->ReportsFactory.keys();
    static QJsonArray reports;
    if(reports.isEmpty())
    {
        Q_FOREACH(QByteArray key, keys)
        {
            QJsonObject item;
            item.insert("name", QString::fromLatin1(key));

            const QMetaObject *mo = deviceIOFactories->ReportsFactory.find(key);
            const int ciIndex = mo->indexOfClassInfo("Description");
            if(ciIndex >= 0)
                item.insert("description", QString::fromLatin1(mo->classInfo(ciIndex).value()));
            else
                item.insert("description", QString::fromLatin1(key));

            reports.append(item);
        }
    }

    return reports;
}

QString ScriteDocument::reportFileSuffix() const
{
    return QString("Adobe PDF (*.pdf)");
}

bool ScriteDocument::importFile(const QString &fileName, const QString &format)
{
    HourGlass hourGlass;

    m_errorReport->clear();

    const QByteArray formatKey = format.toLatin1();
    QScopedPointer<AbstractImporter> importer( deviceIOFactories->ImporterFactory.create<AbstractImporter>(formatKey, this) );

    if(importer.isNull())
    {
        m_errorReport->setErrorMessage("Cannot import from this format.");
        return false;
    }

    return this->importFile(importer.data(), fileName);
}

bool ScriteDocument::importFile(AbstractImporter *importer, const QString &fileName)
{
    this->setLoading(true);

    Aggregation aggregation;
    m_errorReport->setProxyFor(aggregation.findErrorReport(importer));
    m_progressReport->setProxyFor(aggregation.findProgressReport(importer));

    importer->setFileName(fileName);
    importer->setDocument(this);
    this->setBusyMessage("Importing from " + QFileInfo(fileName).fileName() + " ...");
    const bool success = importer->read();
    this->clearBusyMessage();

    this->setLoading(false);

    return success;

}

bool ScriteDocument::exportFile(const QString &fileName, const QString &format)
{
    HourGlass hourGlass;

    m_errorReport->clear();

    const QByteArray formatKey = format.toLatin1();
    QScopedPointer<AbstractExporter> exporter( deviceIOFactories->ExporterFactory.create<AbstractExporter>(formatKey, this) );

    if(exporter.isNull())
    {
        m_errorReport->setErrorMessage("Cannot export to this format.");
        return false;
    }

    Aggregation aggregation;
    m_errorReport->setProxyFor(aggregation.findErrorReport(exporter.data()));
    m_progressReport->setProxyFor(aggregation.findProgressReport(exporter.data()));

    exporter->setFileName(fileName);
    exporter->setDocument(this);
    this->setBusyMessage("Exporting to " + QFileInfo(fileName).fileName() + " ...");
    const bool ret = exporter->write();
    this->clearBusyMessage();

    return ret;
}

bool ScriteDocument::exportToImage(int fromSceneIdx, int fromParaIdx, int toSceneIdx, int toParaIdx, const QString &imageFileName)
{
    const int nrScenes = m_screenplay->elementCount();
    if(fromSceneIdx < 0 || fromSceneIdx >= nrScenes)
        return false;

    if(toSceneIdx < 0 || toSceneIdx >= nrScenes)
        return false;

    QTextDocument document;
    // m_printFormat->pageLayout()->configure(&document);
    document.setTextWidth(m_printFormat->pageLayout()->contentWidth());

    QTextCursor cursor(&document);

    auto prepareCursor = [=](QTextCursor &cursor, SceneElement::Type paraType) {
        const qreal pageWidth = m_printFormat->pageLayout()->contentWidth();
        const SceneElementFormat *format = m_printFormat->elementFormat(paraType);
        QTextBlockFormat blockFormat = format->createBlockFormat(&pageWidth);
        QTextCharFormat charFormat = format->createCharFormat(&pageWidth);
        cursor.setCharFormat(charFormat);
        cursor.setBlockFormat(blockFormat);
    };

    for(int i=fromSceneIdx; i<=toSceneIdx; i++)
    {
        const ScreenplayElement *element = m_screenplay->elementAt(i);
        if(element->scene() == nullptr)
            continue;

        const Scene *scene = element->scene();
        int startParaIdx = -1, endParaIdx = -1;

        if(cursor.position() > 0)
        {
            cursor.insertBlock();
            startParaIdx = qMax(fromParaIdx, 0);
        }
        else
            startParaIdx = 0;

        endParaIdx = (i == toSceneIdx) ? qMin(toParaIdx, scene->elementCount()-1) : toParaIdx;
        if(endParaIdx < 0)
            endParaIdx = scene->elementCount()-1;

        if(startParaIdx == 0 && scene->heading()->isEnabled())
        {
            prepareCursor(cursor, SceneElement::Heading);
            cursor.insertText(QStringLiteral("[") + element->resolvedSceneNumber() + QStringLiteral("] "));
            cursor.insertText(scene->heading()->text());
            cursor.insertBlock();
        }

        for(int p=startParaIdx; p<=endParaIdx; p++)
        {
            const SceneElement *para = scene->elementAt(p);
            prepareCursor(cursor, para->type());
            cursor.insertText(para->text());
            if(p < endParaIdx)
                cursor.insertBlock();
        }
    }

    const QSizeF docSize = document.documentLayout()->documentSize() * 2.0;

    QImage image(docSize.toSize(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter paint(&image);
    paint.scale(2.0, 2.0);
    document.drawContents(&paint, QRectF( QPointF(0,0), docSize) );
    paint.end();

    const QString format = QFileInfo(imageFileName).suffix().toUpper();

    return image.save(imageFileName, qPrintable(format));
}

inline QString createTimestampString(const QDateTime &dt = QDateTime::currentDateTime())
{
    static const QString format = QStringLiteral("MMM dd, yyyy HHmmss");
    return dt.toString(format);
}

AbstractExporter *ScriteDocument::createExporter(const QString &format)
{
    const QByteArray formatKey = format.toLatin1();
    AbstractExporter *exporter = deviceIOFactories->ExporterFactory.create<AbstractExporter>(formatKey, this);
    if(exporter == nullptr)
        return nullptr;

    exporter->setDocument(this);

    if(exporter->fileName().isEmpty())
    {
        QString suggestedName = m_screenplay->title();
        if(suggestedName.isEmpty())
            suggestedName = QFileInfo(m_fileName).baseName();
        if(suggestedName.isEmpty())
            suggestedName = QStringLiteral("Scrite - Screenplay");
        else
            suggestedName += QStringLiteral(" - Screenplay");
        suggestedName += QStringLiteral(" - ") + createTimestampString();

        QFileInfo fi(m_fileName);
        if(fi.exists())
            exporter->setFileName( fi.absoluteDir().absoluteFilePath(suggestedName) );
        else
        {
            const QUrl folderUrl( Application::instance()->settings()->value(QStringLiteral("Workspace/lastOpenExportFolderUrl")).toString() );
            const QString path = folderUrl.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                                     : folderUrl.toLocalFile();
            exporter->setFileName( path + QStringLiteral("/") + suggestedName );
        }
    }

    ProgressReport *progressReport = exporter->findChild<ProgressReport*>();
    if(progressReport)
    {
        connect(progressReport, &ProgressReport::statusChanged, [progressReport,this,exporter]() {
            if(progressReport->status() == ProgressReport::Started)
                this->setBusyMessage("Exporting into \"" + exporter->fileName() + "\" ...");
            else if(progressReport->status() == ProgressReport::Finished)
                this->clearBusyMessage();
        });
    }

    return exporter;
}

AbstractReportGenerator *ScriteDocument::createReportGenerator(const QString &report)
{
    const QByteArray reportKey = report.toLatin1();
    AbstractReportGenerator *reportGenerator = deviceIOFactories->ReportsFactory.create<AbstractReportGenerator>(reportKey, this);
    if(reportGenerator == nullptr)
        return nullptr;

    reportGenerator->setDocument(this);

    if(reportGenerator->fileName().isEmpty())
    {
        QString suggestedName = m_screenplay->title();
        if(suggestedName.isEmpty())
            suggestedName = QFileInfo(m_fileName).baseName();
        if(suggestedName.isEmpty())
            suggestedName = QStringLiteral("Scrite");

        const QString reportName = reportGenerator->name();
        const QString suffix = reportGenerator->format() == AbstractReportGenerator::AdobePDF ? ".pdf" : ".odt";
        suggestedName = suggestedName + QStringLiteral(" - ") + reportName + QStringLiteral(" - ") + createTimestampString() + suffix;

        QFileInfo fi(m_fileName);
        if(fi.exists())
            reportGenerator->setFileName( fi.absoluteDir().absoluteFilePath(suggestedName) );
        else
        {
            const QUrl folderUrl( Application::instance()->settings()->value(QStringLiteral("Workspace/lastOpenReportsFolderUrl")).toString() );
            const QString path = folderUrl.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                                     : folderUrl.toLocalFile();
            reportGenerator->setFileName( path + QStringLiteral("/") + suggestedName );
        }
    }

    ProgressReport *progressReport = reportGenerator->findChild<ProgressReport*>();
    if(progressReport)
    {
        connect(progressReport, &ProgressReport::statusChanged, [progressReport,this,reportGenerator]() {
            if(progressReport->status() == ProgressReport::Started)
                this->setBusyMessage("Generating \"" + reportGenerator->fileName() + "\" ...");
            else if(progressReport->status() == ProgressReport::Finished)
                this->clearBusyMessage();
        });
    }

    return reportGenerator;
}

QAbstractListModel *ScriteDocument::structureElementConnectors() const
{
    ScriteDocument *that = const_cast<ScriteDocument*>(this);
    return &(that->m_connectors);
}

void ScriteDocument::clearModified()
{
    if(m_screenplay->elementCount() == 0 && m_structure->elementCount() == 0)
        this->setModified(false);
}

void ScriteDocument::timerEvent(QTimerEvent *event)
{
    if(event->timerId() == m_evaluateStructureElementSequenceTimer.timerId())
    {
        m_evaluateStructureElementSequenceTimer.stop();
        this->evaluateStructureElementSequence();
        return;
    }

    if(event->timerId() == m_autoSaveTimer.timerId())
    {
        if(m_modified && !m_fileName.isEmpty() && QFileInfo(m_fileName).isWritable())
        {
            QScopedValueRollback<bool> autoSave(m_autoSaveMode, true);
            this->save();
        }
        return;
    }

    if(event->timerId() == m_clearModifyTimer.timerId())
    {
        m_clearModifyTimer.stop();
        this->setModified(false);
        return;
    }

    QObject::timerEvent(event);
}

bool ScriteDocument::runSaveSanityChecks(const QString &givenFileName)
{
    const QString fileName = givenFileName.trimmed();

    // Multiple things could go wrong while saving a file.
    // 1. File name is empty.
    if(fileName.isEmpty())
    {
        m_errorReport->setErrorMessage( QStringLiteral("File name cannot be empty") );
        return false;
    }

    QFileInfo fi(fileName);

    // 2. Filename must not contain special characters
    // It is true that file names will have already been sanitized using Application::sanitiseFileName()
    // But we double check it here anyway.
    static const QList<QChar> allowedChars = {'-', '_', '[', ']', '(', ')', '{', '}', '&', ' '};
    const QString baseFileName = fi.baseName();
    for(const QChar ch : baseFileName)
    {
        if(ch.isLetterOrNumber() || ch.isSpace())
            continue;

        if(allowedChars.contains(ch))
            continue;

        m_errorReport->setErrorMessage( QStringLiteral("File name cannot contain special character '%1'").arg(ch) );
        return false;
    }

    // 3. File already exists, but has become readonly now.
    if( fi.exists() && !fi.isWritable() )
    {
        m_errorReport->setErrorMessage( QStringLiteral("Cannot open '%1' for writing.").arg(fileName) );
        return false;
    }

    // 4. Folder in which the file exists seems have become readonly
    QDir dir = fi.absoluteDir();
    {
        // Try to write something in this folder.
        const QString tmpFile = dir.absoluteFilePath( QStringLiteral("scrite_tmp_") + QString::number( QDateTime::currentMSecsSinceEpoch() ) + QStringLiteral(".dat") );
        QFile file(tmpFile);
        if(!file.open(QFile::WriteOnly))
        {
            m_errorReport->setErrorMessage( QStringLiteral("Cannot write into folder '%1'").arg(dir.absolutePath()) );
            return false;
        }

        file.close();
        QFile::remove(tmpFile);
    }

    return true;
}

void ScriteDocument::setReadOnly(bool val)
{
    if(m_readOnly == val)
        return;

    m_readOnly = val;
    emit readOnlyChanged();
}

void ScriteDocument::setLoading(bool val)
{
    if(m_loading == val)
        return;

    m_loading = val;
    emit loadingChanged();
}

void ScriteDocument::prepareAutoSave()
{
    if(m_autoSave)
        m_autoSaveTimer.start(m_autoSaveDurationInSeconds*1000, this);
    else
        m_autoSaveTimer.stop();
}

void ScriteDocument::updateDocumentWindowTitle()
{
    QString title;
    if(m_modified)
        title += QStringLiteral("* ");
    if(m_fileName.isEmpty())
        title += QStringLiteral("[noname]");
    else
        title += QFileInfo(m_fileName).baseName();
    title += QStringLiteral(" - ") + qApp->property("baseWindowTitle").toString();
    this->setDocumentWindowTitle(title);
}

void ScriteDocument::setDocumentWindowTitle(const QString &val)
{
    if(m_documentWindowTitle == val)
        return;

    m_documentWindowTitle = val;
    emit documentWindowTitleChanged(m_documentWindowTitle);
}

void ScriteDocument::setStructure(Structure *val)
{
    if(m_structure == val)
        return;

    if(m_structure != nullptr)
        GarbageCollector::instance()->add(m_structure);

    m_structure = val;
    m_structure->setParent(this);
    m_structure->setObjectName("Document Structure");

    emit structureChanged();
}

void ScriteDocument::setScreenplay(Screenplay *val)
{
    if(m_screenplay == val)
        return;

    if(m_screenplay != nullptr)
        GarbageCollector::instance()->add(m_screenplay);

    m_screenplay = val;
    m_screenplay->setParent(this);
    m_screenplay->setObjectName("Document Screenplay");

    emit screenplayChanged();
}

void ScriteDocument::setFormatting(ScreenplayFormat *val)
{
    if(m_formatting == val)
        return;

    if(m_formatting != nullptr)
        GarbageCollector::instance()->add(m_formatting);

    m_formatting = val;

    if(m_formatting != nullptr)
        m_formatting->setParent(this);

    emit formattingChanged();
}

void ScriteDocument::setPrintFormat(ScreenplayFormat *val)
{
    if(m_printFormat == val)
        return;

    if(m_printFormat != nullptr)
        GarbageCollector::instance()->add(m_printFormat);

    m_printFormat = val;

    if(m_formatting != nullptr)
        m_printFormat->setParent(this);

    emit printFormatChanged();
}

void ScriteDocument::setForms(Forms *val)
{
    if(m_forms == val)
        return;

    if(m_forms != nullptr)
        GarbageCollector::instance()->add(m_forms);

    m_forms = val;

    if(m_forms != nullptr)
        m_forms->setParent(this);

    emit formsChanged();
}

void ScriteDocument::evaluateStructureElementSequence()
{
    m_connectors.reload();
}

void ScriteDocument::evaluateStructureElementSequenceLater()
{
    m_evaluateStructureElementSequenceTimer.start(0, this);
}

void ScriteDocument::markAsModified()
{
    this->setModified(!this->isEmpty());
}

void ScriteDocument::setModified(bool val)
{
    if(m_readOnly)
        val = false;

    if(m_modified == val)
        return;

    if(m_structure == nullptr || m_screenplay == nullptr)
        return;

    m_modified = val;

    emit modifiedChanged();
}

void ScriteDocument::setFileName(const QString &val)
{
    if(m_fileName == val)
        return;        

    m_fileName = this->polishFileName(val);
    emit fileNameChanged();
}

bool ScriteDocument::load(const QString &fileName)
{
    m_errorReport->clear();

    if( QFile(fileName).isReadable() )
    {
        m_errorReport->setErrorMessage( QStringLiteral("Cannot open %1 for reading.").arg(fileName));
        return false;
    }

    int format = DocumentFileSystem::ScriteFormat;
    bool loaded = this->classicLoad(fileName);
    if(!loaded)
        loaded = this->modernLoad(fileName, &format);

    if(!loaded)
    {
        m_errorReport->setErrorMessage( QStringLiteral("%1 is not a Scrite document.").arg(fileName) );
        return false;
    }

    struct LoadCleanup
    {
        LoadCleanup(ScriteDocument *doc)
            : m_document(doc) {
            m_document->m_errorReport->clear();
        }

        ~LoadCleanup() {
            if(m_loadBegun) {
                m_document->m_progressReport->finish();
                m_document->setLoading(false);
            } else
                m_document->m_docFileSystem.reset();
        }

        void begin() {
            m_loadBegun = true;
            m_document->m_progressReport->start();
            m_document->setLoading(true);
        }

    private:
        bool m_loadBegun = false;
        ScriteDocument *m_document;
    } loadCleanup(this);

    const QJsonDocument jsonDoc = format == DocumentFileSystem::ZipFormat ?
                                  QJsonDocument::fromJson(m_docFileSystem.header()) :
                                  QJsonDocument::fromBinaryData(m_docFileSystem.header());

#ifndef QT_NO_DEBUG
    {
        const QFileInfo fi(fileName);
        const QString fileName2 = fi.absolutePath() + "/" + fi.baseName() + ".json";
        QFile file2(fileName2);
        file2.open(QFile::WriteOnly);
        file2.write(jsonDoc.toJson());
    }
#endif

    const QJsonObject json = jsonDoc.object();
    if(json.isEmpty())
    {
        m_errorReport->setErrorMessage( QStringLiteral("%1 is not a Scrite document.").arg(fileName) );
        return false;
    }

    const QJsonObject metaInfo = json.value("meta").toObject();
    if(metaInfo.value("appName").toString().toLower() != qApp->applicationName().toLower())
    {
        m_errorReport->setErrorMessage(QStringLiteral("Scrite document '%1' was created using an unrecognised app.").arg(fileName));
        return false;
    }

    const QVersionNumber docVersion = QVersionNumber::fromString( metaInfo.value(QStringLiteral("appVersion")).toString() );
    const QVersionNumber appVersion = Application::instance()->versionNumber();
    if(appVersion < docVersion)
    {
        m_errorReport->setErrorMessage(QStringLiteral("Scrite document '%1' was created using an updated version.").arg(fileName));
        return false;
    }

    m_fileName = fileName;
    emit fileNameChanged();

    const bool ro = QFileInfo(fileName).permission(QFile::WriteUser) == false;
    this->setReadOnly(ro);
    this->setModified(false);

    loadCleanup.begin();

    UndoStack::ignoreUndoCommands = true;
    const bool ret = QObjectSerializer::fromJson(json, this);
    if(m_screenplay->currentElementIndex() == 0)
        m_screenplay->setCurrentElementIndex(-1);
    UndoStack::ignoreUndoCommands = false;
    UndoStack::clearAllStacks();

    // When we finish loading, QML begins lazy initialization of the UI
    // for displaying the document. In the process even a small 1/2 pixel
    // change in element location on the structure canvas for example,
    // causes this document to marked as modified. Which is a bummer for the user
    // who will notice that a document is marked as modified immediately after
    // loading it. So, we set this timer here to ensure that modified flag is
    // set to false after the QML UI has finished its lazy loading.
    m_clearModifyTimer.start(100, this);

    if(ro)
    {
        Notification *notification = new Notification(this);
        connect(notification, &Notification::dismissed, &Notification::deleteLater);

        notification->setTitle(QStringLiteral("File only has read permission."));
        notification->setText(QStringLiteral("This document is being opened in read only mode.\nTo edit this document, please apply write-permissions for the file in your computer."));
        notification->setAutoClose(false);
        notification->setActive(true);
    }

    emit justLoaded();

    return ret;
}

bool ScriteDocument::classicLoad(const QString &fileName)
{
    if(fileName.isEmpty())
        return false;

    QFile file(fileName);
    if(!file.open(QFile::ReadOnly))
        return false;

    static const QByteArray classicMarker("qbjs");
    const QByteArray marker = file.read(classicMarker.length());
    if(marker != classicMarker)
        return false;

    file.seek(0);

    const QByteArray bytes = file.readAll();
    m_docFileSystem.setHeader(bytes);
    return true;
}

bool ScriteDocument::modernLoad(const QString &fileName, int *format)
{
    DocumentFileSystem::Format dfsFormat;
    const bool ret = m_docFileSystem.load(fileName, &dfsFormat);
    if(format)
        *format = dfsFormat;
    return ret;
}

void ScriteDocument::structureElementIndexChanged()
{
    if(m_screenplay == nullptr || m_structure == nullptr || m_syncingStructureScreenplayCurrentIndex || m_inCreateNewScene)
        return;

    QScopedValueRollback<bool> rollback(m_syncingStructureScreenplayCurrentIndex, true);

    StructureElement *element = m_structure->elementAt(m_structure->currentElementIndex());
    if(element == nullptr)
    {
        m_screenplay->setActiveScene(nullptr);
        m_screenplay->setCurrentElementIndex(-1);
    }
    else
        m_screenplay->setActiveScene(element->scene());
}

void ScriteDocument::screenplayElementIndexChanged()
{
    if(m_screenplay == nullptr || m_structure == nullptr || m_syncingStructureScreenplayCurrentIndex || m_inCreateNewScene)
        return;

    QScopedValueRollback<bool> rollback(m_syncingStructureScreenplayCurrentIndex, true);

    ScreenplayElement *element = m_screenplay->elementAt(m_screenplay->currentElementIndex());
    if(element != nullptr)
    {
        int index = m_structure->indexOfScene(element->scene());
        m_structure->setCurrentElementIndex(index);
    }
}

void ScriteDocument::setCreatedOnThisComputer(bool val)
{
    if(m_createdOnThisComputer == val)
        return;

    m_createdOnThisComputer = val;
    emit createdOnThisComputerChanged();
}

void ScriteDocument::screenplayElementRemoved(ScreenplayElement *ptr, int)
{
    Scene *scene = ptr->scene();
    int index = m_screenplay->firstIndexOfScene(scene);
    if(index < 0)
    {
        index = m_structure->indexOfScene(scene);
        if(index >= 0)
        {
            StructureElement *element = m_structure->elementAt(index);
            element->setStackId(QString());
        }
    }
}

void ScriteDocument::prepareForSerialization()
{
    // Nothing to do
}

void ScriteDocument::prepareForDeserialization()
{
    // Nothing to do
}

bool ScriteDocument::canSerialize(const QMetaObject *, const QMetaProperty &) const
{
    return true;
}

void ScriteDocument::serializeToJson(QJsonObject &json) const
{
    QJsonObject metaInfo;
    metaInfo.insert( QStringLiteral("appName"), qApp->applicationName());
    metaInfo.insert( QStringLiteral("orgName"), qApp->organizationName());
    metaInfo.insert( QStringLiteral("orgDomain"), qApp->organizationDomain());

    /**
     * Nightly builds are x.y.z, where z is odd for nightly builds and even
     * for public builds. Users using nightly builds must be able to create a file
     * using it and open it using the public builds. Just in case the nightly
     * build crashes, they shouldnt be constrained from opening the file using a
     * previous public build.
     */
    QVersionNumber appVersion = QVersionNumber::fromString(Application::instance()->versionNumber().toString());
    if(appVersion.microVersion() % 2)
        appVersion = QVersionNumber(appVersion.majorVersion(), appVersion.minorVersion(), appVersion.microVersion()-1);

    metaInfo.insert( QStringLiteral("appVersion"), appVersion.toString());

    QJsonObject systemInfo;
    systemInfo.insert( QStringLiteral("machineHostName"), QSysInfo::machineHostName());
    systemInfo.insert( QStringLiteral("machineUniqueId"), QString::fromLatin1(QSysInfo::machineUniqueId()));
    systemInfo.insert( QStringLiteral("prettyProductName"), QSysInfo::prettyProductName());
    systemInfo.insert( QStringLiteral("productType"), QSysInfo::productType());
    systemInfo.insert( QStringLiteral("productVersion"), QSysInfo::productVersion());
    metaInfo.insert( QStringLiteral("system"), systemInfo);

    QJsonObject installationInfo;
    installationInfo.insert( QStringLiteral("id"), Application::instance()->installationId());
    installationInfo.insert( QStringLiteral("since"), Application::instance()->installationTimestamp().toMSecsSinceEpoch());
    installationInfo.insert( QStringLiteral("launchCount"), Application::instance()->launchCounter());
    metaInfo.insert( QStringLiteral("installation"), installationInfo);

    json.insert( QStringLiteral("meta"), metaInfo);
}

void ScriteDocument::deserializeFromJson(const QJsonObject &json)
{
    const QJsonObject metaInfo = json.value( QStringLiteral("meta") ).toObject();
    const QJsonObject systemInfo = metaInfo.value( QStringLiteral("system") ).toObject();

    const QString thisMachineId = QString::fromLatin1(QSysInfo::machineUniqueId());
    const QString jsonMachineId = systemInfo.value( QStringLiteral("machineUniqueId") ).toString() ;
    this->setCreatedOnThisComputer(jsonMachineId == thisMachineId);

    const QString appVersion = metaInfo.value( QStringLiteral("appVersion") ).toString();
    const QVersionNumber version = QVersionNumber::fromString(appVersion);
    if( version <= QVersionNumber(0,1,9) )
    {
        const qreal dx = -130;
        const qreal dy = -22;

        const int nrElements = m_structure->elementCount();
        for(int i=0; i<nrElements; i++)
        {
            StructureElement *element = m_structure->elementAt(i);
            element->setX( element->x()+dx );
            element->setY( element->y()+dy );
        }
    }

    if( version <= QVersionNumber(0,2,6) )
    {
        const int nrElements = m_structure->elementCount();
        for(int i=0; i<nrElements; i++)
        {
            StructureElement *element = m_structure->elementAt(i);
            Scene *scene = element->scene();
            if(scene == nullptr)
                continue;

            SceneHeading *heading = scene->heading();
            QString val = heading->locationType();
            if(val ==  QStringLiteral("INTERIOR") )
                val =  QStringLiteral("INT") ;
            if(val ==  QStringLiteral("EXTERIOR") )
                val =  QStringLiteral("EXT") ;
            if(val ==  QStringLiteral("BOTH") )
                val = "I/E";
            heading->setLocationType(val);
        }
    }

    if(m_screenplay->currentElementIndex() < 0)
    {
        if(m_screenplay->elementCount() > 0)
            m_screenplay->setCurrentElementIndex(0);
        else if(m_structure->elementCount() > 0)
            m_structure->setCurrentElementIndex(0);
    }

    const QVector<QColor> versionColors = Application::standardColors(version);
    const QVector<QColor> newColors     = Application::standardColors(QVersionNumber());
    if(versionColors != newColors)
    {
        auto evalNewColor = [versionColors,newColors](const QColor &color) {
            const int oldColorIndex = versionColors.indexOf(color);
            const QColor newColor = oldColorIndex < 0 ? newColors.last() : newColors.at( oldColorIndex%newColors.size() );
            return newColor;
        };

        const int nrElements = m_structure->elementCount();
        for(int i=0; i<nrElements; i++)
        {
            StructureElement *element = m_structure->elementAt(i);
            Scene *scene = element->scene();
            if(scene == nullptr)
                continue;

            scene->setColor( evalNewColor(scene->color()) );

            const int nrNotes = scene->notes()->noteCount();
            for(int n=0; n<nrNotes; n++)
            {
                Note *note = scene->notes()->noteAt(n);
                note->setColor( evalNewColor(note->color()) );
            }
        }

        const int nrNotes = m_structure->notes()->noteCount();
        for(int n=0; n<nrNotes; n++)
        {
            Note *note = m_structure->notes()->noteAt(n);
            note->setColor( evalNewColor(note->color()) );
        }

        const int nrChars = m_structure->characterCount();
        for(int c=0; c<nrChars; c++)
        {
            Character *character = m_structure->characterAt(c);

            const int nrNotes = character->notes()->noteCount();
            for(int n=0; n<nrNotes; n++)
            {
                Note *note = character->notes()->noteAt(n);
                note->setColor( evalNewColor(note->color()) );
            }
        }
    }

    // With Version 0.3.9, we have completely changed the way in which we
    // store formatting options. So, the old formatting options data doesnt
    // work anymore. We better reset to defaults in the new version and then
    // let the user alter it anyway he sees fit.
    if( version <= QVersionNumber(0,3,9) )
    {
        m_formatting->resetToDefaults();
        m_printFormat->resetToDefaults();
    }

    // Starting with 0.4.5, it is possible for users to lock a document
    // such that it is editable only the system in which it was created.
    if(m_locked && !m_readOnly)
    {
        const QJsonObject installationInfo = metaInfo.value( QStringLiteral("installation") ).toObject();
        const QString docClientId = installationInfo.value( QStringLiteral("id") ).toString();
        const QString myClientId = Application::instance()->installationId();
        if(!myClientId.isEmpty() && !docClientId.isEmpty() )
        {
            const bool ro = myClientId != docClientId;
            this->setReadOnly( ro );
            if(ro)
            {
                Notification *notification = new Notification(this);
                connect(notification, &Notification::dismissed, &Notification::deleteLater);

                notification->setTitle(QStringLiteral("Document is locked for edit."));
                notification->setText(QStringLiteral("This document is being opened in read only mode.\nYou cannot edit this document on your computer, because it has been locked for edit on another computer.\nYou can however save a copy using the 'Save As' option and edit the copy on your computer."));
                notification->setAutoClose(false);
                notification->setActive(true);
            }
        }
    }

    if(version <= QVersionNumber(0,4,7))
    {
        for(int i=SceneElement::Min; i<=SceneElement::Max; i++)
        {
            SceneElementFormat *format = m_formatting->elementFormat( SceneElement::Type(i) );
            if(qFuzzyCompare(format->lineHeight(), 1.0))
                format->setLineHeight(0.85);
            const qreal lineHeight = format->lineHeight();

            format = m_printFormat->elementFormat( SceneElement::Type(i) );
            format->setLineHeight(lineHeight);
        }
    }

    // From version 0.4.14 onwards we allow users to set their own custom fonts
    // for each language. This is a deviation from using "Courier Prime" as the
    // default Latin font.
    m_formatting->useUserSpecifiedFonts();
    m_printFormat->useUserSpecifiedFonts();

    // Although its not specified anywhere that transitions must be right aligned,
    // many writers who are early adopters of Scrite are insisting on it.
    // So, going forward transition paragraphs will be right aligned by default.
    if(version <= QVersionNumber(0,5,1))
    {
        SceneElementFormat *format = m_formatting->elementFormat(SceneElement::Transition);
        format->setTextAlignment(Qt::AlignRight);

        format = m_printFormat->elementFormat(SceneElement::Transition);
        format->setTextAlignment(Qt::AlignRight);
    }

    // Documents created using Scrite version 0.5.2 or before use SynopsisEditorUI
    // by default.
    if(version <= QVersionNumber(0,5,2))
        m_structure->setCanvasUIMode(Structure::SynopsisEditorUI);
}

QString ScriteDocument::polishFileName(const QString &givenFileName) const
{
    QString fileName = givenFileName.trimmed();

    if(!fileName.isEmpty())
    {
        QFileInfo fi(fileName);
        if(fi.isDir())
            fileName = fi.absolutePath() + QStringLiteral("/Screenplay-") + QString::number(QDateTime::currentSecsSinceEpoch()) + QStringLiteral(".scrite");
        else if(fi.suffix() != QStringLiteral("scrite"))
            fileName += QStringLiteral(".scrite");
    }

    return fileName;
}

void ScriteDocument::setSessionId(QString val)
{
    if(m_sessionId == val)
        return;

    m_sessionId = val;
    emit sessionIdChanged();
}

///////////////////////////////////////////////////////////////////////////////

StructureElementConnectors::StructureElementConnectors(ScriteDocument *parent)
    : QAbstractListModel(parent),
      m_document(parent)
{

}

StructureElementConnectors::~StructureElementConnectors()
{

}

StructureElement *StructureElementConnectors::fromElement(int row) const
{
    if(row < 0 || row >= m_items.size())
        return nullptr;

    const Item item = m_items.at(row);
    return item.from;
}

StructureElement *StructureElementConnectors::toElement(int row) const
{
    if(row < 0 || row >= m_items.size())
        return nullptr;

    const Item item = m_items.at(row);
    return item.to;
}

QString StructureElementConnectors::label(int row) const
{
    if(row < 0 || row >= m_items.size())
        return nullptr;

    const Item item = m_items.at(row);
    return item.label;
}

int StructureElementConnectors::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant StructureElementConnectors::data(const QModelIndex &index, int role) const
{
    if(index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const Item item = m_items.at(index.row());
    switch(role)
    {
    case FromElementRole: return QVariant::fromValue<QObject*>(item.from);
    case ToElementRole: return QVariant::fromValue<QObject*>(item.to);
    case LabelRole: return item.label;
    default: break;
    }

    return QVariant();
}

QHash<int, QByteArray> StructureElementConnectors::roleNames() const {
    QHash<int,QByteArray> roles;
    roles[FromElementRole] = QByteArrayLiteral("connectorFromElement");
    roles[ToElementRole] = QByteArrayLiteral("connectorToElement");
    roles[LabelRole] = QByteArrayLiteral("connectorLabel");
    return roles;
}

void StructureElementConnectors::clear()
{
    if(m_items.isEmpty())
        return;

    this->beginResetModel();
    m_items.clear();
    this->endResetModel();
    emit countChanged();
}

void StructureElementConnectors::reload()
{
    const Structure *structure = m_document->structure();
    const Screenplay *screenplay = m_document->screenplay();
    if(structure == nullptr || screenplay == nullptr)
    {
        this->clear();
        return;
    }

    const int nrElements = screenplay->elementCount();
    if(nrElements <= 1)
    {
        this->clear();
        return;
    }

    ScreenplayElement *fromElement = nullptr;
    ScreenplayElement *toElement = nullptr;
    int fromIndex = -1;
    int toIndex = -1;
    int itemIndex = 0;

    const bool itemsWasEmpty = m_items.isEmpty();
    if(itemsWasEmpty)
        this->beginResetModel();

    for(int i=0; i<nrElements-1; i++)
    {
        fromElement = fromElement ? fromElement : screenplay->elementAt(i);
        toElement = toElement ? toElement : screenplay->elementAt(i+1);
        fromIndex = fromIndex >= 0 ? fromIndex : structure->indexOfScene(fromElement->scene());
        toIndex = toIndex >= 0 ? toIndex : structure->indexOfScene(toElement->scene());

        if(fromIndex >= 0 && toIndex >= 0)
        {
            Item item;
            item.from = structure->elementAt(fromIndex);
            item.to = structure->elementAt(toIndex);
            item.label = QString::number(itemIndex+1);

            if(itemsWasEmpty)
                m_items.append(item);
            else
            {
                if(itemIndex < m_items.size())
                {
                    if( !(m_items.at(itemIndex) == item) )
                    {
                        this->beginRemoveRows(QModelIndex(), itemIndex, itemIndex);
                        m_items.removeAt(itemIndex);
                        this->endRemoveRows();

                        this->beginInsertRows(QModelIndex(), itemIndex, itemIndex);
                        m_items.insert(itemIndex, item);
                        this->endInsertRows();
                    }
                }
                else
                {
                    this->beginInsertRows(QModelIndex(), itemIndex, itemIndex);
                    m_items.append(item);
                    this->endInsertRows();
                    emit countChanged();
                }
            }

            ++itemIndex;

            fromElement = toElement;
            fromIndex = toIndex;
        }
        else
        {
            fromElement = nullptr;
            fromIndex = -1;
        }

        toElement = nullptr;
        toIndex = -1;
    }

    if(itemsWasEmpty)
    {
        this->endResetModel();
        emit countChanged();
    }
    else
    {
        const int expectedCount = itemIndex;
        if(m_items.size() > expectedCount)
        {
            this->beginRemoveRows(QModelIndex(), expectedCount, m_items.size()-1);
            while(m_items.size() != expectedCount)
                m_items.removeLast();
            this->endRemoveRows();
        }
    }
}

QJsonObject ScriteDocumentBackups::MetaData::toJson() const
{
    QJsonObject ret;
    ret.insert(QStringLiteral("loaded"), this->loaded);
    ret.insert(QStringLiteral("structureElementCount"), this->structureElementCount);
    ret.insert(QStringLiteral("screenplayElementCount"), this->screenplayElementCount);
    ret.insert(QStringLiteral("sceneCount"), qMax(this->structureElementCount,this->screenplayElementCount));
    return ret;
}
