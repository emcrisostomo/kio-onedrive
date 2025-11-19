/*
 * SPDX-FileCopyrightText: 2024 KDE Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "onedriveclient.h"

#include <QByteArray>
#include <QEventLoop>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

using namespace OneDrive;

Client::Client(QObject *parent)
    : QObject(parent)
{
}

ListChildrenResult Client::listChildren(const QString &accessToken, const QString &driveId, const QString &itemId)
{
    ListChildrenResult result;
    if (accessToken.isEmpty()) {
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token");
        result.httpStatus = 401;
        return result;
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    if (driveId.isEmpty()) {
        url.setPath(QStringLiteral("/v1.0/me/drive/root/children"));
    } else if (itemId.isEmpty()) {
        url.setPath(QStringLiteral("/v1.0/drives/%1/root/children").arg(driveId));
    } else {
        url.setPath(QStringLiteral("/v1.0/drives/%1/items/%2/children").arg(driveId, itemId));
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$top"), QStringLiteral("200"));
    query.addQueryItem(QStringLiteral("$select"), QStringLiteral("id,name,size,parentReference,folder,file,lastModifiedDateTime,@microsoft.graph.downloadUrl"));
    url.setQuery(query);

    const QNetworkRequest request = buildRequest(accessToken, url);
    QNetworkReply *reply = m_network.get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray payload = readReply(reply, result);
    if (!result.success) {
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    const QJsonObject root = doc.object();
    const QJsonArray values = root.value(QStringLiteral("value")).toArray();
    result.nextLink = root.value(QStringLiteral("@odata.nextLink")).toString();

    for (const QJsonValue &value : values) {
        const QJsonObject obj = value.toObject();
        result.items.append(parseItem(obj));
    }

    result.success = true;
    return result;
}

ListChildrenResult Client::listChildrenByPath(const QString &accessToken, const QString &relativePath)
{
    const QString cleanedPath = relativePath.trimmed();
    if (cleanedPath.isEmpty()) {
        return listChildren(accessToken);
    }

    ListChildrenResult result;
    if (accessToken.isEmpty()) {
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token");
        result.httpStatus = 401;
        return result;
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    url.setPath(QStringLiteral("/v1.0/me/drive/root:/%1:/children").arg(cleanedPath), QUrl::DecodedMode);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$top"), QStringLiteral("200"));
    query.addQueryItem(QStringLiteral("$select"), QStringLiteral("id,name,size,parentReference,folder,file,lastModifiedDateTime,@microsoft.graph.downloadUrl"));
    url.setQuery(query);

    const QNetworkRequest request = buildRequest(accessToken, url);
    QNetworkReply *reply = m_network.get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray payload = readReply(reply, result);
    if (!result.success) {
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    const QJsonObject root = doc.object();
    const QJsonArray values = root.value(QStringLiteral("value")).toArray();
    result.nextLink = root.value(QStringLiteral("@odata.nextLink")).toString();

    for (const QJsonValue &value : values) {
        const QJsonObject obj = value.toObject();
        result.items.append(parseItem(obj));
    }

    result.success = true;
    return result;
}

DriveItemResult Client::getItemByPath(const QString &accessToken, const QString &relativePath)
{
    DriveItemResult result;
    if (accessToken.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token");
        return result;
    }

    const QString cleanedPath = relativePath.trimmed();
    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    if (cleanedPath.isEmpty()) {
        url.setPath(QStringLiteral("/v1.0/me/drive/root"));
    } else {
        url.setPath(QStringLiteral("/v1.0/me/drive/root:/%1:").arg(cleanedPath), QUrl::DecodedMode);
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$select"), QStringLiteral("id,name,size,parentReference,folder,file,lastModifiedDateTime,@microsoft.graph.downloadUrl"));
    url.setQuery(query);

    const QNetworkRequest request = buildRequest(accessToken, url);
    QNetworkReply *reply = m_network.get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    result.item = parseItem(doc.object());
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    result.success = true;
    return result;
}

DriveItemResult Client::getItemById(const QString &accessToken, const QString &driveId, const QString &itemId)
{
    DriveItemResult result;
    if (accessToken.isEmpty() || driveId.isEmpty() || itemId.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token or drive item information");
        return result;
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    url.setPath(QStringLiteral("/v1.0/drives/%1/items/%2").arg(driveId, itemId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$select"), QStringLiteral("id,name,size,parentReference,folder,file,lastModifiedDateTime,@microsoft.graph.downloadUrl"));
    url.setQuery(query);

    const QNetworkRequest request = buildRequest(accessToken, url);
    QNetworkReply *reply = m_network.get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    result.item = parseItem(doc.object());
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    result.success = true;
    return result;
}

DownloadResult Client::downloadItem(const QString &accessToken, const QString &itemId, const QString &downloadUrl)
{
    DownloadResult result;
    QNetworkRequest request;

    if (!downloadUrl.isEmpty()) {
        request = QNetworkRequest(QUrl(downloadUrl));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    } else {
        if (accessToken.isEmpty() || itemId.isEmpty()) {
            result.httpStatus = 401;
            result.errorMessage = QStringLiteral("Missing access token or item ID");
            return result;
        }
        QUrl url(QStringLiteral("https://graph.microsoft.com"));
        url.setPath(QStringLiteral("/v1.0/me/drive/items/%1/content").arg(itemId));
        request = buildRequest(accessToken, url);
    }

    QNetworkReply *reply = m_network.get(request);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    result.data = reply->readAll();
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    result.success = true;
    return result;
}

QNetworkRequest Client::buildRequest(const QString &accessToken, const QUrl &url) const
{
    QNetworkRequest request(url);
    // Microsoft Graph occasionally breaks newer HTTP/2 sessions, so stick to HTTP/1.1.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

QByteArray Client::readReply(QNetworkReply *reply, ListChildrenResult &result) const
{
    reply->deleteLater();
    QByteArray data;
    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.success = false;
        return data;
    }

    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    data = reply->readAll();
    result.success = true;
    return data;
}

DriveItem Client::parseItem(const QJsonObject &object) const
{
    DriveItem item;
    item.id = object.value(QStringLiteral("id")).toString();
    item.name = object.value(QStringLiteral("name")).toString();
    item.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
    item.lastModified = QDateTime::fromString(object.value(QStringLiteral("lastModifiedDateTime")).toString(), Qt::ISODate);
    item.isFolder = object.contains(QStringLiteral("folder"));
    item.downloadUrl = object.value(QStringLiteral("@microsoft.graph.downloadUrl")).toString();

    const QJsonObject parent = object.value(QStringLiteral("parentReference")).toObject();
    item.parentId = parent.value(QStringLiteral("id")).toString();
    item.driveId = parent.value(QStringLiteral("driveId")).toString();

    const QJsonObject remoteItem = object.value(QStringLiteral("remoteItem")).toObject();
    if (!remoteItem.isEmpty()) {
        const QJsonObject remoteParent = remoteItem.value(QStringLiteral("parentReference")).toObject();
        item.remoteDriveId = remoteParent.value(QStringLiteral("driveId")).toString();
        item.remoteItemId = remoteItem.value(QStringLiteral("id")).toString();
    }

    const QJsonObject fileObj = object.value(QStringLiteral("file")).toObject();
    if (!fileObj.isEmpty()) {
        item.mimeType = fileObj.value(QStringLiteral("mimeType")).toString();
    } else if (item.isFolder) {
        item.mimeType = QStringLiteral("inode/directory");
    }

    return item;
}
ListChildrenResult Client::listSharedWithMe(const QString &accessToken)
{
    ListChildrenResult result;
    if (accessToken.isEmpty()) {
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token");
        result.httpStatus = 401;
        return result;
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    url.setPath(QStringLiteral("/v1.0/me/drive/sharedWithMe"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$top"), QStringLiteral("200"));
    query.addQueryItem(
        QStringLiteral("$select"),
        QStringLiteral("id,name,size,parentReference,folder,file,lastModifiedDateTime,@microsoft.graph.downloadUrl,remoteItem,remoteItem.parentReference"));
    url.setQuery(query);

    const QNetworkRequest request = buildRequest(accessToken, url);
    QNetworkReply *reply = m_network.get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonObject root = doc.object();
    const QJsonArray values = root.value(QStringLiteral("value")).toArray();
    result.nextLink = root.value(QStringLiteral("@odata.nextLink")).toString();

    for (const QJsonValue &value : values) {
        const QJsonObject obj = value.toObject();
        DriveItem item = parseItem(obj.value(QStringLiteral("remoteItem")).toObject());
        item.remoteDriveId = item.driveId;
        item.remoteItemId = item.id;
        item.id = obj.value(QStringLiteral("id")).toString();
        result.items.append(item);
    }

    reply->deleteLater();
    result.success = true;
    return result;
}

DrivesResult Client::listSharedDrives(const QString &accessToken)
{
    DrivesResult result;
    if (accessToken.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token");
        return result;
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com/v1.0/me/drives"));
    QNetworkReply *reply = m_network.get(buildRequest(accessToken, url));

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonArray values = doc.object().value(QStringLiteral("value")).toArray();
    for (const auto &value : values) {
        const QJsonObject drive = value.toObject();
        DriveInfo info;
        info.id = drive.value(QStringLiteral("id")).toString();
        info.name = drive.value(QStringLiteral("name")).toString();
        if (!info.id.isEmpty()) {
            result.drives.append(info);
        }
    }

    reply->deleteLater();
    result.success = true;
    return result;
}

QuotaResult Client::fetchDriveQuota(const QString &accessToken)
{
    QuotaResult result;
    if (accessToken.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token");
        return result;
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$select"), QStringLiteral("quota"));
    url.setQuery(query);

    QNetworkReply *reply = m_network.get(buildRequest(accessToken, url));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonObject quota = doc.object().value(QStringLiteral("quota")).toObject();
    result.total = static_cast<qint64>(quota.value(QStringLiteral("total")).toDouble());
    const qint64 remaining = static_cast<qint64>(quota.value(QStringLiteral("remaining")).toDouble());
    result.remaining = remaining;
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    result.success = true;
    return result;
}

ListChildrenResult Client::listDriveChildren(const QString &accessToken, const QString &driveId, const QString &itemId)
{
    ListChildrenResult result;
    if (accessToken.isEmpty() || driveId.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token or drive ID");
        return result;
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    if (itemId.isEmpty()) {
        url.setPath(QStringLiteral("/v1.0/drives/%1/root/children").arg(driveId));
    } else {
        url.setPath(QStringLiteral("/v1.0/drives/%1/items/%2/children").arg(driveId, itemId));
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$top"), QStringLiteral("200"));
    query.addQueryItem(QStringLiteral("$select"), QStringLiteral("id,name,size,parentReference,folder,file,lastModifiedDateTime,@microsoft.graph.downloadUrl"));
    url.setQuery(query);

    QNetworkReply *reply = m_network.get(buildRequest(accessToken, url));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray payload = readReply(reply, result);
    if (!result.success) {
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    const QJsonObject root = doc.object();
    const QJsonArray values = root.value(QStringLiteral("value")).toArray();
    result.nextLink = root.value(QStringLiteral("@odata.nextLink")).toString();

    for (const auto &value : values) {
        result.items.append(parseItem(value.toObject()));
    }

    result.success = true;
    return result;
}

DeleteResult Client::deleteItem(const QString &accessToken, const QString &itemId, const QString &driveId)
{
    DeleteResult result;
    if (accessToken.isEmpty() || itemId.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token or item ID");
        return result;
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    if (driveId.isEmpty()) {
        url.setPath(QStringLiteral("/v1.0/me/drive/items/%1").arg(itemId));
    } else {
        url.setPath(QStringLiteral("/v1.0/drives/%1/items/%2").arg(driveId, itemId));
    }

    QNetworkReply *reply = m_network.deleteResource(buildRequest(accessToken, url));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    result.success = true;
    return result;
}

static QString effectiveMimeType(const QString &mimeType)
{
    return mimeType.isEmpty() ? QStringLiteral("application/octet-stream") : mimeType;
}

UploadResult Client::uploadItemByPath(const QString &accessToken, const QString &relativePath, QIODevice *source, const QString &mimeType)
{
    UploadResult result;
    if (accessToken.isEmpty() || relativePath.trimmed().isEmpty() || !source) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing upload information");
        return result;
    }

    if (!source->isOpen() && !source->open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("Failed to open upload source");
        return result;
    }
    source->seek(0);

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    url.setPath(QStringLiteral("/v1.0/me/drive/root:/%1:/content").arg(relativePath), QUrl::DecodedMode);

    QNetworkRequest request = buildRequest(accessToken, url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, effectiveMimeType(mimeType));

    QNetworkReply *reply = m_network.put(request, source);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    result.item = parseItem(doc.object());
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    result.success = true;
    return result;
}

UploadResult Client::uploadItemById(const QString &accessToken, const QString &driveId, const QString &itemId, QIODevice *source, const QString &mimeType)
{
    UploadResult result;
    if (accessToken.isEmpty() || itemId.isEmpty() || !source) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing upload information");
        return result;
    }

    if (!source->isOpen() && !source->open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("Failed to open upload source");
        return result;
    }
    source->seek(0);

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    if (driveId.isEmpty()) {
        url.setPath(QStringLiteral("/v1.0/me/drive/items/%1/content").arg(itemId));
    } else {
        url.setPath(QStringLiteral("/v1.0/drives/%1/items/%2/content").arg(driveId, itemId));
    }

    QNetworkRequest request = buildRequest(accessToken, url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, effectiveMimeType(mimeType));

    QNetworkReply *reply = m_network.put(request, source);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    result.item = parseItem(doc.object());
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    result.success = true;
    return result;
}

DriveItemResult Client::updateItem(const QString &accessToken, const QString &driveId, const QString &itemId, const QString &newName, const QString &parentPath)
{
    DriveItemResult result;
    if (accessToken.isEmpty() || itemId.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token or item ID");
        return result;
    }

    if (newName.isEmpty() && parentPath.isEmpty()) {
        result.success = true;
        return result;
    }

    QJsonObject payload;
    if (!newName.isEmpty()) {
        payload.insert(QStringLiteral("name"), newName);
    }
    if (!parentPath.isEmpty()) {
        QJsonObject parentRef;
        parentRef.insert(QStringLiteral("path"), parentPath);
        payload.insert(QStringLiteral("parentReference"), parentRef);
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    if (driveId.isEmpty()) {
        url.setPath(QStringLiteral("/v1.0/me/drive/items/%1").arg(itemId));
    } else {
        url.setPath(QStringLiteral("/v1.0/drives/%1/items/%2").arg(driveId, itemId));
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("$select"), QStringLiteral("id,name,size,parentReference,folder,file,lastModifiedDateTime,@microsoft.graph.downloadUrl"));
    url.setQuery(query);

    QNetworkRequest request = buildRequest(accessToken, url);
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_network.sendCustomRequest(request, "PATCH", body);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    result.item = parseItem(doc.object());
    result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    result.success = true;
    return result;
}
