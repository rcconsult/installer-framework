/**************************************************************************
**
** This file is part of Qt SDK**
**
** Copyright (c) 2011 Nokia Corporation and/or its subsidiary(-ies).*
**
** Contact:  Nokia Corporation qt-info@nokia.com**
**
** No Commercial Usage
**
** This file contains pre-release code and may not be distributed.
** You may use this file in accordance with the terms and conditions
** contained in the Technology Preview License Agreement accompanying
** this package.
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception version
** 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** If you are unsure which license is appropriate for your use, please contact
** (qt-info@nokia.com).
**
**************************************************************************/
#include "progresscoordinator.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>

using namespace QInstaller;

QT_BEGIN_NAMESPACE
uint qHash(QPointer<QObject> key)
{
    return qHash(key.data());
}
QT_END_NAMESPACE

ProgressCoordninator::ProgressCoordninator(QObject *parent)
    : QObject(parent),
    m_currentCompletePercentage(0),
    m_currentBasePercentage(0),
    m_manualAddedPercentage(0),
    m_reservedPercentage(0),
    m_undoMode(false),
    m_reachedPercentageBeforeUndo(0)
{
    // it has to be in the main thread to be able refresh the ui with processEvents
    Q_ASSERT(thread() == qApp->thread());
}

ProgressCoordninator::~ProgressCoordninator()
{
}

ProgressCoordninator* ProgressCoordninator::instance()
{
    static ProgressCoordninator* instance = 0;
    if (instance == 0)
        instance = new ProgressCoordninator(qApp);
    return instance;
}

void ProgressCoordninator::reset()
{
    disconnectAllSenders();
    m_senderPendingCalculatedPercentageHash.clear();
    m_senderPartProgressSizeHash.clear();
    m_installationLabelText.clear();
    m_currentCompletePercentage = 0;
    m_currentBasePercentage = 0;
    m_manualAddedPercentage = 0;
    m_reservedPercentage = 0;
    m_undoMode = false;
    m_reachedPercentageBeforeUndo = 0;
    emit detailTextResetNeeded();
}

void ProgressCoordninator::registerPartProgress(QObject *sender, const char *signal, double partProgressSize)
{
    Q_ASSERT(sender);
    Q_ASSERT(QString::fromLatin1(signal).contains(QLatin1String("(double)")));
    Q_ASSERT(partProgressSize <= 1);

    m_senderPartProgressSizeHash.insert(sender, partProgressSize);
    bool isConnected = connect(sender, signal, this, SLOT(partProgressChanged(double)));
    Q_UNUSED(isConnected);
    Q_ASSERT(isConnected);
}

void ProgressCoordninator::partProgressChanged(double fraction)
{
    if (fraction < 0 || fraction > 1) {
        qWarning() << QString(QLatin1String("The fraction is outside from possible value:"))
            << QString::number(fraction);
        return;
    }

    double partProgressSize = m_senderPartProgressSizeHash.value(sender(), 0);
    if (partProgressSize == 0) {
        qWarning() << QString(QLatin1String("It seems that this sender was not registered in the right way:"))
            << sender();
        return;
    }

    if (m_undoMode) {
        //qDebug() << "fraction: " << fraction;
        double maxSize = m_reachedPercentageBeforeUndo * partProgressSize;
        double pendingCalculatedPartPercentage = maxSize * fraction;

         // allPendingCalculatedPartPercentages has negative values
        double newCurrentCompletePercentage = m_currentBasePercentage - pendingCalculatedPartPercentage
            + allPendingCalculatedPartPercentages(sender());

        //we can't check this here, because some round issues can make it little bit under 0 or over 100
        //Q_ASSERT(newCurrentCompletePercentage >= 0);
        //Q_ASSERT(newCurrentCompletePercentage <= 100);
        if (newCurrentCompletePercentage < 0) {
            qDebug() << newCurrentCompletePercentage << " is smaller then 0 - this should happen max once";
            newCurrentCompletePercentage = 0;
        }
        if (newCurrentCompletePercentage > 100) {
            qDebug() << newCurrentCompletePercentage << " is bigger then 100 - this should happen max once";
            newCurrentCompletePercentage = 100;
        }

        if (qRound(m_currentCompletePercentage) < qRound(newCurrentCompletePercentage)) {
            qFatal("This should not happen!");
        }

        m_currentCompletePercentage = newCurrentCompletePercentage;
        if (fraction == 1) {
            m_currentBasePercentage = m_currentBasePercentage - pendingCalculatedPartPercentage;
            m_senderPendingCalculatedPercentageHash.insert(sender(), 0);
        } else {
            m_senderPendingCalculatedPercentageHash.insert(sender(), pendingCalculatedPartPercentage);
        }

    } else { //if (m_undoMode)
        int availablePercentagePoints = 100 - m_manualAddedPercentage - m_reservedPercentage;
        double pendingCalculatedPartPercentage = availablePercentagePoints * partProgressSize * fraction;
        //double checkValue = allPendingCalculatedPartPercentages(sender());

        double newCurrentCompletePercentage = m_manualAddedPercentage + m_currentBasePercentage
            + pendingCalculatedPartPercentage + allPendingCalculatedPartPercentages(sender());

        //we can't check this here, because some round issues can make it little bit under 0 or over 100
        //Q_ASSERT(newCurrentCompletePercentage >= 0);
        //Q_ASSERT(newCurrentCompletePercentage <= 100);
        if (newCurrentCompletePercentage < 0) {
            qDebug() << newCurrentCompletePercentage << " is smaller then 0 - this should happen max once";
            newCurrentCompletePercentage = 0;
        }

        if (newCurrentCompletePercentage > 100) {
            qDebug() << newCurrentCompletePercentage << " is bigger then 100 - this should happen max once";
            newCurrentCompletePercentage = 100;
        }

        if (qRound(m_currentCompletePercentage) > qRound(newCurrentCompletePercentage)) {
            qFatal("This should not happen!");
        }
        m_currentCompletePercentage = newCurrentCompletePercentage;

        if (fraction == 1 || fraction == 0) {
            m_currentBasePercentage = m_currentBasePercentage + pendingCalculatedPartPercentage;
            m_senderPendingCalculatedPercentageHash.insert(sender(), 0);
        } else {
            m_senderPendingCalculatedPercentageHash.insert(sender(), pendingCalculatedPartPercentage);
        }
    } //if (m_undoMode)
}


/*!
    Contains the installation progress percentage.
*/
int ProgressCoordninator::progressInPercentage() const
{
    int currentValue = qRound(m_currentCompletePercentage);
    Q_ASSERT( currentValue <= 100);
    Q_ASSERT( currentValue >= 0);
    return currentValue;
}

void ProgressCoordninator::disconnectAllSenders()
{
    foreach (QPointer<QObject> sender, m_senderPartProgressSizeHash.keys()) {
        if (!sender.isNull()) {
            bool isDisconnected = sender->disconnect(this);
            Q_UNUSED(isDisconnected);
            Q_ASSERT(isDisconnected);
        }
    }
}

void ProgressCoordninator::setUndoMode()
{
    Q_ASSERT(!m_undoMode);
    m_undoMode = true;

    disconnectAllSenders();
    m_senderPendingCalculatedPercentageHash.clear();

    m_reachedPercentageBeforeUndo = progressInPercentage();
    m_currentBasePercentage = m_reachedPercentageBeforeUndo;
}

void ProgressCoordninator::addManualPercentagePoints(int value)
{
    m_manualAddedPercentage = m_manualAddedPercentage + value;
    if (m_undoMode) {
        //we don't do other things in the undomode, maybe later if the last percentage point comes to early
        return;
    }

    m_currentCompletePercentage = m_currentCompletePercentage + value;
    if (m_currentCompletePercentage > 100.0)
        m_currentCompletePercentage = 100.0;

    qApp->processEvents(); //makes the result available in the ui
}

void ProgressCoordninator::addReservePercentagePoints(int value)
{
    m_reservedPercentage = m_reservedPercentage + value;
}

void ProgressCoordninator::setLabelText(const QString &text)
{
    if (m_installationLabelText == text)
        return;
    m_installationLabelText = text;
}

/*!
    Contains the installation progress label text.
*/
QString ProgressCoordninator::labelText() const
{
    return m_installationLabelText;
}

void ProgressCoordninator::emitDetailTextChanged(const QString &text)
{
    emit detailTextChanged(text);
}

void ProgressCoordninator::emitLabelAndDetailTextChanged(const QString &text)
{
    emit detailTextChanged(text);
    m_installationLabelText = QString(text).remove(QLatin1String("\n"));
    qApp->processEvents(); //makes the result available in the ui
}

double ProgressCoordninator::allPendingCalculatedPartPercentages(QObject *excludeKeyObject)
{
    double result = 0;
    QHash<QPointer<QObject>, double>::iterator it = m_senderPendingCalculatedPercentageHash.begin();
    while (it != m_senderPendingCalculatedPercentageHash.end()) {
        if (it.key() != excludeKeyObject)
            result += it.value();
        it++;
    }
    return result;
}
