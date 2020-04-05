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

#ifndef STRUCTUREEXPORTER_H
#define STRUCTUREEXPORTER_H

#include "abstractexporter.h"

class StructureExporter : public AbstractExporter
{
    Q_OBJECT
    Q_CLASSINFO("Format", "Structure Snapshot")
    Q_CLASSINFO("NameFilters", "Image File (*.png)")

public:
    Q_INVOKABLE StructureExporter(QObject *parent=nullptr);
    ~StructureExporter();

protected:
    bool doExport(QIODevice *device); // AbstractExporter interface
    QString polishFileName(const QString &fileName) const; // AbstractDeviceIO interface
};

#endif // STRUCTUREEXPORTER_H
