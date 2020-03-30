/****************************************************************************
**
** Copyright (C) Prashanth Udupa, Bengaluru
** Email: prashanth.udupa@gmail.com
**
** This code is distributed under GPL v3. Complete text of the license
** can be found here: https://www.gnu.org/licenses/gpl-3.0.txt
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
****************************************************************************/

#include "scene.h"

#include <QUuid>

SceneHeading::SceneHeading(QObject *parent)
    : QObject(parent),
      m_scene(qobject_cast<Scene*>(parent)),
      m_enabled(true),
      m_moment(NoMoment),
      m_locationType(NoLocationType)
{

}

SceneHeading::~SceneHeading()
{

}

void SceneHeading::setEnabled(bool val)
{
    if(m_enabled == val)
        return;

    m_enabled = val;
    emit enabledChanged();
}

void SceneHeading::setLocationType(SceneHeading::LocationType val)
{
    if(m_locationType == val)
        return;

    m_locationType = val;
    emit locationTypeChanged();
}

QString SceneHeading::locationTypeAsString() const
{
    switch(m_locationType)
    {
    case Interior: return "INT.";
    case Exterior: return "EXT.";
    case Both: return "I/E.";
    default: break;
    }

    return "NONE";
}

void SceneHeading::setLocation(const QString &val)
{
    if(m_location == val)
        return;

    m_location = val;
    emit locationChanged();
}

void SceneHeading::setMoment(SceneHeading::Moment val)
{
    if(m_moment == val)
        return;

    m_moment = val;
    emit momentChanged();
}

QString SceneHeading::momentAsString() const
{
    switch(m_moment)
    {
    case Day: return "DAY";
    case Night: return "NIGHT";
    case Morning: return "MORNING";
    case Afternoon: return "AFTERNOON";
    case Evening: return "EVENING";
    case Later: return "LATER";
    case MomentsLater: return "MOMENTS LATER";
    case Continuous: return "CONTINUOUS";
    case TheNextDay: return "THE NEXT DAY";
    default: break;
    }

    return "NONE";
}

QString SceneHeading::toString() const
{
    return this->locationTypeAsString() + " " + this->location() + " - " + this->momentAsString();
}

///////////////////////////////////////////////////////////////////////////////

SceneElement::SceneElement(QObject *parent)
    : QObject(parent),
      m_type(Action),
      m_scene(qobject_cast<Scene*>(parent))
{
    connect(this, &SceneElement::typeChanged, this, &SceneElement::elementChanged);
    connect(this, &SceneElement::textChanged, this, &SceneElement::elementChanged);
}

SceneElement::~SceneElement()
{
    emit aboutToDelete(this);
}

void SceneElement::setType(SceneElement::Type val)
{
    if(m_type == val)
        return;

    m_type = val;
    emit typeChanged();

    if(m_scene != nullptr)
        emit m_scene->sceneElementChanged(this, Scene::ElementTypeChange);
}

QString SceneElement::typeAsString() const
{
    switch(m_type)
    {
    case Action: return "Action";
    case Character: return "Character";
    case Dialogue: return "Dialogue";
    case Parenthetical: return "Parenthetical";
    case Shot: return "Shot";
    case Transition: return "Transition";
    case Heading: return "Scene Heading";
    }

    return "Unknown";
}

void SceneElement::setText(const QString &val)
{
    if(m_text == val)
        return;

    m_text = val.trimmed();
    emit textChanged();

    if(m_scene != nullptr)
        emit m_scene->sceneElementChanged(this, Scene::ElementTextChange);
}

QString SceneElement::text() const
{
    if(m_type == SceneElement::Parenthetical)
    {
        QString text = m_text;
        if(!text.startsWith("("))
            text.prepend("(");
        if(!text.endsWith(")"))
            text.append(")");
        return text;
    }

    return m_text;
}

bool SceneElement::event(QEvent *event)
{
    if(event->type() == QEvent::ParentChange)
        m_scene = qobject_cast<Scene*>(this->parent());

    return QObject::event(event);
}

///////////////////////////////////////////////////////////////////////////////

Scene::Scene(QObject *parent)
    : QAbstractListModel(parent),
      m_color(Qt::white),
      m_enabled(true),
      m_heading(new SceneHeading(this))
{
    connect(m_heading, &SceneHeading::enabledChanged, this, &Scene::headingChanged);
    connect(m_heading, &SceneHeading::locationTypeChanged, this, &Scene::headingChanged);
    connect(m_heading, &SceneHeading::locationChanged, this, &Scene::headingChanged);
    connect(m_heading, &SceneHeading::momentChanged, this, &Scene::headingChanged);
    connect(this, &Scene::titleChanged, this, &Scene::sceneChanged);
    connect(this, &Scene::colorChanged, this, &Scene::sceneChanged);
    connect(this, &Scene::notesChanged, this, &Scene::sceneChanged);
    connect(this, &Scene::headingChanged, this, &Scene::sceneChanged);
    connect(this, &Scene::elementCountChanged, this, &Scene::sceneChanged);
}

Scene::~Scene()
{
    emit aboutToDelete(this);
}

void Scene::setId(const QString &val)
{
    if(m_id == val || !m_id.isEmpty())
        return;

    m_id = val;
    emit idChanged();
}

QString Scene::id() const
{
    if(m_id.isEmpty())
        m_id = QUuid::createUuid().toString();

    return m_id;
}

void Scene::setTitle(const QString &val)
{
    if(m_title == val)
        return;

    m_title = val;
    emit titleChanged();
}

void Scene::setColor(const QColor &val)
{
    if(m_color == val)
        return;

    m_color = val;
    emit colorChanged();
}

void Scene::setNotes(const QString &val)
{
    if(m_notes == val)
        return;

    m_notes = val;
    emit notesChanged();
}

void Scene::setEnabled(bool val)
{
    if(m_enabled == val)
        return;

    m_enabled = val;
    emit enabledChanged();
}

QQmlListProperty<SceneElement> Scene::elements()
{
    return QQmlListProperty<SceneElement>(
                reinterpret_cast<QObject*>(this),
                static_cast<void*>(this),
                &Scene::staticAppendElement,
                &Scene::staticElementCount,
                &Scene::staticElementAt,
                &Scene::staticClearElements);
}

void Scene::addElement(SceneElement *ptr)
{
    if(ptr == nullptr || m_elements.indexOf(ptr) >= 0)
        return;

    this->beginInsertRows(QModelIndex(), m_elements.size(), m_elements.size());
    m_elements.append(ptr);

    connect(ptr, &SceneElement::elementChanged, this, &Scene::sceneChanged);
    connect(ptr, &SceneElement::aboutToDelete, this, &Scene::removeElement);

    this->endInsertRows();

    emit elementCountChanged();
}

void Scene::insertAfter(SceneElement *ptr, SceneElement *after)
{
    if(ptr == nullptr || m_elements.indexOf(ptr) >= 0)
        return;

    int index = m_elements.indexOf(after);
    if(index < 0)
        return;

    if(index == m_elements.size()-1)
    {
        this->addElement(ptr);
        return;
    }

    this->insertAt(ptr, index+1);
}

void Scene::insertBefore(SceneElement *ptr, SceneElement *before)
{
    if(ptr == nullptr || m_elements.indexOf(ptr) >= 0)
        return;

    int index = m_elements.indexOf(before);
    if(index < 0)
        return;

    this->insertAt(ptr, index);
}

void Scene::insertAt(SceneElement *ptr, int index)
{
    if(index < 0 || index >= m_elements.size())
        return;

    this->beginInsertRows(QModelIndex(), index, index);

    m_elements.insert(index, ptr);
    connect(ptr, &SceneElement::elementChanged, this, &Scene::sceneChanged);
    connect(ptr, &SceneElement::aboutToDelete, this, &Scene::removeElement);

    this->endInsertRows();

    emit elementCountChanged();
}

void Scene::removeElement(SceneElement *ptr)
{
    if(ptr == nullptr)
        return;

    const int row = m_elements.indexOf(ptr);
    if(row < 0)
        return;

    this->beginRemoveRows(QModelIndex(), row, row);

    emit aboutToRemoveSceneElement(ptr);
    m_elements.removeAt(row);

    disconnect(ptr, &SceneElement::elementChanged, this, &Scene::sceneChanged);
    disconnect(ptr, &SceneElement::aboutToDelete, this, &Scene::removeElement);

    this->endRemoveRows();

    emit elementCountChanged();

    if(ptr->parent() == this)
        ptr->deleteLater();
}

SceneElement *Scene::elementAt(int index) const
{
    return index < 0 || index >= m_elements.size() ? nullptr : m_elements.at(index);
}

int Scene::elementCount() const
{
    return m_elements.size();
}

void Scene::clearElements()
{
    while(m_elements.size())
        this->removeElement(m_elements.first());
}

int Scene::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_elements.size();
}

QVariant Scene::data(const QModelIndex &index, int role) const
{
    if(role == SceneElementRole && index.isValid())
        return QVariant::fromValue<QObject*>(this->elementAt(index.row()));

    return QVariant();
}

QHash<int, QByteArray> Scene::roleNames() const
{
    QHash<int,QByteArray> roles;
    roles[SceneElementRole] = "sceneElement";
    return roles;
}

void Scene::setElementsList(const QList<SceneElement *> &list)
{
    Q_FOREACH(SceneElement *item, list)
    {
        if(item->scene() != this)
            return;
    }

    const bool sizeChanged = m_elements.size() != list.size();
    QList<SceneElement*> oldElements = m_elements;

    this->beginResetModel();
    m_elements.clear();
    m_elements.reserve(list.size());
    Q_FOREACH(SceneElement *item, list)
    {
        m_elements.append(item);
        oldElements.removeOne(item);
    }
    this->endResetModel();

    if(sizeChanged)
        emit elementCountChanged();

    emit sceneChanged();

    qDeleteAll(oldElements);
}

void Scene::staticAppendElement(QQmlListProperty<SceneElement> *list, SceneElement *ptr)
{
    reinterpret_cast< Scene* >(list->data)->addElement(ptr);
}

void Scene::staticClearElements(QQmlListProperty<SceneElement> *list)
{
    reinterpret_cast< Scene* >(list->data)->clearElements();
}

SceneElement *Scene::staticElementAt(QQmlListProperty<SceneElement> *list, int index)
{
    return reinterpret_cast< Scene* >(list->data)->elementAt(index);
}

int Scene::staticElementCount(QQmlListProperty<SceneElement> *list)
{
    return reinterpret_cast< Scene* >(list->data)->elementCount();
}

