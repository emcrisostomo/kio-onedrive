/*
    SPDX-FileCopyrightText: 2020 Nicolas Fella <nicolas.fella@gmx.de>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef ONEDRIVEJOB_H
#define ONEDRIVEJOB_H

#include <QString>
#include <QUrl>
#include <purpose/pluginbase.h>

class OneDriveJob : public Purpose::Job
{
    Q_OBJECT
public:
    OneDriveJob(QObject *parent)
        : Purpose::Job(parent)
    {
    }
    void start() override;
};
#endif /* ONEDRIVEJOB_H */
