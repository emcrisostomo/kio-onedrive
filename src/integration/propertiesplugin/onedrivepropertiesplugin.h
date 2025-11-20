/*
 * SPDX-FileCopyrightText: 2020 David Barchiesi <david@barchie.si>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef ONEDRIVEPROPERTIESPLUGIN_H
#define ONEDRIVEPROPERTIESPLUGIN_H

#include <KPropertiesDialog>
#include <KPropertiesDialogPlugin>

#include "ui_onedrivepropertiesplugin.h"

class OneDrivePropertiesPlugin : public KPropertiesDialogPlugin
{
    Q_OBJECT
public:
    explicit OneDrivePropertiesPlugin(QObject *parent, const QList<QVariant> &args);
    ~OneDrivePropertiesPlugin() override = default;

private:
    QWidget m_widget;
    Ui::OneDrivePropertiesWidget m_ui;
    KFileItem m_item;

    void showEntryDetails(const KIO::UDSEntry &entry);

private Q_SLOTS:
    void statJobFinished(KJob *job);
};

#endif // ONEDRIVEPROPERTIESPLUGIN_H
