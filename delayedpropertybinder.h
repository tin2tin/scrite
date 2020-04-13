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

#ifndef DELAYEDPROPERTYBINDER_H
#define DELAYEDPROPERTYBINDER_H

#include <QQuickItem>
#include <QBasicTimer>

class DelayedPropertyBinder : public QQuickItem
{
    Q_OBJECT

public:
    DelayedPropertyBinder(QQuickItem *parent=nullptr);
    ~DelayedPropertyBinder();

    Q_PROPERTY(QVariant set READ set WRITE setSet NOTIFY setChanged)
    void setSet(const QVariant &val);
    QVariant set() const { return m_set; }
    Q_SIGNAL void setChanged();

    Q_PROPERTY(QVariant get READ get NOTIFY getChanged)
    QVariant get() const { return m_get; }
    Q_SIGNAL void getChanged();

    Q_PROPERTY(QVariant initial READ initial WRITE setInitial NOTIFY initialChanged)
    void setInitial(const QVariant &val);
    QVariant initial() const { return m_initial; }
    Q_SIGNAL void initialChanged();

    Q_PROPERTY(int delay READ delay WRITE setDelay NOTIFY delayChanged)
    void setDelay(int val);
    int delay() const { return m_delay; }
    Q_SIGNAL void delayChanged();

private:
    void setGet(const QVariant &val);
    void schedule();
    void timerEvent(QTimerEvent *te);

private:
    int m_delay = 0;
    QVariant m_set;
    QVariant m_get;
    QVariant m_initial;
    QBasicTimer m_timer;
};

#endif // DELAYEDPROPERTYBINDER_H
