/*
 * SPDX-FileCopyrightText: 2017 Lim Yuen Hoe <yuenhoe86@gmail.com>
 * SPDX-FileCopyrightText: 2025 Enrico M. Crisostomo <enrico.crisostomo@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#include "gdrivejob.h"
#include <purpose/pluginbase.h>

#include <KPluginFactory>
#include <QUrl>

class OneDrivePlugin : public Purpose::PluginBase
{
    Q_OBJECT
public:
    OneDrivePlugin(QObject *parent, const QVariantList &args)
        : Purpose::PluginBase(parent)
    {
        Q_UNUSED(args);
    }

    Purpose::Job *createJob() const override
    {
        return new GDriveJob(nullptr);
    }
};

K_PLUGIN_CLASS_WITH_JSON(OneDrivePlugin, "purpose_onedrive.json")

#include "purpose_onedrive.moc"
