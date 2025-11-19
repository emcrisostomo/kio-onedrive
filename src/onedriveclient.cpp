/*
 * SPDX-FileCopyrightText: 2024 KDE Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "onedriveclient.h"

#include <QByteArray>
#include <QEventLoop>
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
    const QByteArray encodedPath = QUrl::toPercentEncoding(cleanedPath, QByteArrayLiteral("/"));
    url.setPath(QStringLiteral("/v1.0/me/drive/root:/%1:/children").arg(QString::fromLatin1(encodedPath)));

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
        const QByteArray encodedPath = QUrl::toPercentEncoding(cleanedPath, QByteArrayLiteral("/"));
        url.setPath(QStringLiteral("/v1.0/me/drive/root:/%1:").arg(QString::fromLatin1(encodedPath)));
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

    const QJsonObject fileObj = object.value(QStringLiteral("file")).toObject();
    if (!fileObj.isEmpty()) {
        item.mimeType = fileObj.value(QStringLiteral("mimeType")).toString();
    } else if (item.isFolder) {
        item.mimeType = QStringLiteral("inode/directory");
    }

    return item;
}
