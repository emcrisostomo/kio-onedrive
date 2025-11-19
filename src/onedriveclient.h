/*
 * SPDX-FileCopyrightText: 2024 KDE Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>

class QIODevice;

class QNetworkReply;

namespace OneDrive
{
struct DriveItem {
    QString id;
    QString name;
    QString parentId;
    QString driveId;
    QString remoteDriveId;
    QString remoteItemId;
    QString mimeType;
    QString downloadUrl;
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

struct DriveItemResult {
    bool success = false;
    int httpStatus = 0;
    QString errorMessage;
    DriveItem item;
};

struct DownloadResult {
    bool success = false;
    int httpStatus = 0;
    QString errorMessage;
    QByteArray data;
};

struct DeleteResult {
    bool success = false;
    int httpStatus = 0;
    QString errorMessage;
};

struct UploadResult {
    bool success = false;
    int httpStatus = 0;
    QString errorMessage;
    DriveItem item;
};

struct DriveInfo {
    QString id;
    QString name;
};

struct QuotaResult {
    bool success = false;
    int httpStatus = 0;
    QString errorMessage;
    qint64 total = 0;
    qint64 remaining = 0;
};

struct DrivesResult {
    bool success = false;
    int httpStatus = 0;
    QString errorMessage;
    QList<DriveInfo> drives;
};

class Client : public QObject
{
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);

    [[nodiscard]] ListChildrenResult listChildren(const QString &accessToken, const QString &driveId = QString(), const QString &itemId = QString());
    [[nodiscard]] ListChildrenResult listChildrenByPath(const QString &accessToken, const QString &relativePath);
    [[nodiscard]] DriveItemResult getItemByPath(const QString &accessToken, const QString &relativePath);
    [[nodiscard]] DriveItemResult getItemById(const QString &accessToken, const QString &driveId, const QString &itemId);
    [[nodiscard]] DownloadResult downloadItem(const QString &accessToken, const QString &itemId, const QString &downloadUrl = QString());
    [[nodiscard]] ListChildrenResult listSharedWithMe(const QString &accessToken);
    [[nodiscard]] DrivesResult listSharedDrives(const QString &accessToken);
    [[nodiscard]] DriveItemResult getDriveItemByPath(const QString &accessToken, const QString &driveId, const QString &itemId, const QString &relativePath);
    [[nodiscard]] QuotaResult fetchDriveQuota(const QString &accessToken);
    [[nodiscard]] ListChildrenResult listDriveChildren(const QString &accessToken, const QString &driveId, const QString &itemId = QString());
    [[nodiscard]] DeleteResult deleteItem(const QString &accessToken, const QString &itemId, const QString &driveId = QString());
    [[nodiscard]] UploadResult
    uploadItemByPath(const QString &accessToken, const QString &relativePath, QIODevice *source, const QString &mimeType = QString());
    [[nodiscard]] UploadResult
    uploadItemById(const QString &accessToken, const QString &driveId, const QString &itemId, QIODevice *source, const QString &mimeType = QString());
    [[nodiscard]] DriveItemResult
    updateItem(const QString &accessToken, const QString &driveId, const QString &itemId, const QString &newName, const QString &parentPath = QString());

private:
    QNetworkAccessManager m_network;

    [[nodiscard]] QNetworkRequest buildRequest(const QString &accessToken, const QUrl &url) const;
    [[nodiscard]] QByteArray readReply(QNetworkReply *reply, ListChildrenResult &result) const;
    [[nodiscard]] DriveItem parseItem(const QJsonObject &object) const;
};
}
