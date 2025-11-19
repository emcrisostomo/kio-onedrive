/*
 * SPDX-FileCopyrightText: 2024 KDE Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>

class QNetworkReply;

namespace OneDrive
{
struct DriveItem {
    QString id;
    QString name;
    QString parentId;
    QString driveId;
    bool isFolder = false;
    qint64 size = 0;
    QDateTime lastModified;
};

struct ListChildrenResult {
    bool success = false;
    int httpStatus = 0;
    QString errorMessage;
    QString nextLink;
    QList<DriveItem> items;
};

class Client : public QObject
{
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);

    [[nodiscard]] ListChildrenResult listChildren(const QString &accessToken, const QString &driveId = QString(), const QString &itemId = QString());
    [[nodiscard]] ListChildrenResult listChildrenByPath(const QString &accessToken, const QString &relativePath);

private:
    QNetworkAccessManager m_network;

    [[nodiscard]] QNetworkRequest buildRequest(const QString &accessToken, const QUrl &url) const;
    [[nodiscard]] QByteArray readReply(QNetworkReply *reply, ListChildrenResult &result) const;
};
}
