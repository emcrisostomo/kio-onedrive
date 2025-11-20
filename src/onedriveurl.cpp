/*
 * SPDX-FileCopyrightText: 2014 Daniel Vrátil <dvratil@redhat.com>
 * SPDX-FileCopyrightText: 2016 Elvis Angelaccio <elvis.angelaccio@kde.org>
 * SPDX-FileCopyrightText: 2019 David Barchiesi <david@barchie.si>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "onedriveurl.h"

const QString OneDriveUrl::Scheme = QLatin1String("onedrive");
const QString OneDriveUrl::SharedWithMeDir = QLatin1String("Shared With Me");
const QString OneDriveUrl::SharedDrivesDir = QLatin1String("Shared Drives");
const QString OneDriveUrl::TrashDir = QLatin1String("trash");
const QString OneDriveUrl::NewAccountPath = QLatin1String("new-account");

OneDriveUrl::OneDriveUrl(const QUrl &url)
    : m_url(url)
{
    const auto path = url.adjusted(QUrl::StripTrailingSlash).path();
    m_components = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

QString OneDriveUrl::account() const
{
    if (isRoot()) {
        return QString();
    }

    return m_components.at(0);
}

QString OneDriveUrl::filename() const
{
    if (m_components.isEmpty()) {
        return QString();
    }

    return m_components.last();
}

bool OneDriveUrl::isRoot() const
{
    return m_components.isEmpty();
}

bool OneDriveUrl::isAccountRoot() const
{
    return m_components.length() == 1 && !isNewAccountPath();
}

bool OneDriveUrl::isNewAccountPath() const
{
    return m_components.length() == 1 && m_components.at(0) == NewAccountPath;
}

bool OneDriveUrl::isTopLevel() const
{
    return m_components.length() == 2;
}

bool OneDriveUrl::isSharedWithMeRoot() const
{
    return m_components.length() == 2 && m_components.at(1) == SharedWithMeDir;
}

bool OneDriveUrl::isSharedWithMeTopLevel() const
{
    return m_components.length() == 3 && m_components.at(1) == SharedWithMeDir;
}

bool OneDriveUrl::isSharedWithMe() const
{
    return m_components.length() > 2 && m_components.at(1) == SharedWithMeDir;
}

bool OneDriveUrl::isSharedDrivesRoot() const
{
    return m_components.length() == 2 && m_components.at(1) == SharedDrivesDir;
}

bool OneDriveUrl::isSharedDrive() const
{
    return m_components.length() == 3 && m_components.at(1) == SharedDrivesDir;
}

bool OneDriveUrl::isTrashDir() const
{
    return m_components.length() == 2 && m_components.at(1) == TrashDir;
}

bool OneDriveUrl::isTrashed() const
{
    return m_components.length() > 2 && m_components.at(1) == TrashDir;
}

QUrl OneDriveUrl::url() const
{
    return m_url;
}

QString OneDriveUrl::parentPath() const
{
    if (isRoot()) {
        return QString();
    }

    auto path = m_components;
    path.removeLast();

    return QLatin1Char('/') + path.join(QLatin1Char('/'));
}

QStringList OneDriveUrl::pathComponents() const
{
    return m_components;
}

QString OneDriveUrl::buildSharedDrivePath(const QString &accountId, const QString &drive)
{
    return QStringLiteral("/%1/%2/%3").arg(accountId, SharedDrivesDir, drive);
}
