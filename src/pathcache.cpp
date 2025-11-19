/*
 * SPDX-FileCopyrightText: 2014 Daniel Vrátil <dvratil@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "pathcache.h"
#include "onedrivedebug.h"

#include <QDateTime>

PathCache::PathCache()
{
}

PathCache::~PathCache()
{
}

void PathCache::insertPath(const QString &path, const QString &fileId)
{
    if (path.startsWith(QLatin1Char('/'))) {
        m_pathIdMap.insert(path.mid(1), fileId);
    } else {
        m_pathIdMap.insert(path, fileId);
    }
}

QString PathCache::idForPath(const QString &path) const
{
    if (path.startsWith(QLatin1Char('/'))) {
        return m_pathIdMap[path.mid(1)];
    } else {
        return m_pathIdMap[path];
    }
}

QStringList PathCache::descendants(const QString &path) const
{
    const QString fullPath = path.endsWith(QLatin1Char('/')) ? path : path + QLatin1Char('/');

    QStringList descendants;
    for (auto iter = m_pathIdMap.begin(); iter != m_pathIdMap.end(); ++iter) {
        if (!iter.key().startsWith(fullPath)) {
            // Not a descendant at all
            continue;
        }

        if (iter.key().lastIndexOf(QLatin1Char('/')) >= fullPath.size()) {
            // Not a direct descendant
            continue;
        }

        descendants.append(iter.key());
    }

    return descendants;
}

void PathCache::removePath(const QString &path)
{
    if (path.startsWith(QLatin1Char('/'))) {
        m_pathIdMap.remove(path.mid(1));
    } else {
        m_pathIdMap.remove(path);
    }
}

void PathCache::dump()
{
    qCDebug(ONEDRIVE) << "==== DUMP ====";
    for (auto iter = m_pathIdMap.constBegin(); iter != m_pathIdMap.constEnd(); ++iter) {
        qCDebug(ONEDRIVE) << iter.key() << " => " << iter.value();
    }
    qCDebug(ONEDRIVE) << "==== DUMP ====";
}
