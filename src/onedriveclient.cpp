/*
 * SPDX-FileCopyrightText: 2024 KDE Contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "onedriveclient.h"
#include "onedrivedebug.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
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
        const QString requestId = QString::fromUtf8(reply->rawHeader(QByteArrayLiteral("request-id")));
        qCWarning(ONEDRIVE) << "Graph getItemByPath failed" << url << result.httpStatus << result.errorMessage << "requestId:" << requestId;
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
    if (accessToken.isEmpty() || itemId.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token or drive item information");
        return result;
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

DriveItemResult Client::getDriveItemByPath(const QString &accessToken, const QString &driveId, const QString &itemId, const QString &relativePath)
{
    DriveItemResult result;
    if (accessToken.isEmpty() || driveId.isEmpty() || itemId.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token or drive information");
        return result;
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    const QString cleanedPath = relativePath.trimmed();
    if (cleanedPath.isEmpty()) {
        url.setPath(QStringLiteral("/v1.0/drives/%1/items/%2").arg(driveId, itemId));
    } else {
        url.setPath(QStringLiteral("/v1.0/drives/%1/items/%2:/%3:").arg(driveId, itemId, cleanedPath), QUrl::DecodedMode);
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

DownloadResult Client::downloadItem(const QString &accessToken, const QString &itemId, const QString &downloadUrl, const QString &driveId)
{
    DownloadResult result;
    if (accessToken.isEmpty() || itemId.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing access token or item ID");
        return result;
    }

    auto tryDownload = [&](QNetworkRequest req, bool withAuth, const char *label) -> bool {
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        if (withAuth && !accessToken.isEmpty()) {
            req.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
        }

        QNetworkReply *reply = m_network.get(req);
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Handle redirects ourselves so we can drop auth on the pre-signed URL.
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            const QUrl redirectUrl = reply->header(QNetworkRequest::LocationHeader).toUrl();
            reply->deleteLater();
            if (!redirectUrl.isValid()) {
                qCWarning(ONEDRIVE) << "Download attempt" << label << "redirect with invalid Location header";
                return false;
            }

            QNetworkRequest redirectReq(redirectUrl);
            redirectReq.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
            redirectReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            // Pre-signed URL should work anonymously; try anon first, then with auth if requested.
            QNetworkReply *redirectReply = m_network.get(redirectReq);
            QEventLoop redirectLoop;
            connect(redirectReply, &QNetworkReply::finished, &redirectLoop, &QEventLoop::quit);
            redirectLoop.exec();

            if (redirectReply->error() == QNetworkReply::NoError) {
                result.data = redirectReply->readAll();
                result.httpStatus = redirectReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                redirectReply->deleteLater();
                result.success = true;
                return true;
            }

            if (withAuth) {
                redirectReq.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
                QNetworkReply *redirectReplyAuth = m_network.get(redirectReq);
                QEventLoop redirectLoopAuth;
                connect(redirectReplyAuth, &QNetworkReply::finished, &redirectLoopAuth, &QEventLoop::quit);
                redirectLoopAuth.exec();
                if (redirectReplyAuth->error() == QNetworkReply::NoError) {
                    result.data = redirectReplyAuth->readAll();
                    result.httpStatus = redirectReplyAuth->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    redirectReplyAuth->deleteLater();
                    result.success = true;
                    return true;
                }
                qCWarning(ONEDRIVE) << "Download attempt" << label << "redirect follow failed" << redirectUrl
                                    << redirectReplyAuth->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() << redirectReplyAuth->errorString();
                result.errorMessage = redirectReplyAuth->errorString();
                result.httpStatus = redirectReplyAuth->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                redirectReplyAuth->deleteLater();
                return false;
            }

            qCWarning(ONEDRIVE) << "Download attempt" << label << "redirect follow failed" << redirectUrl
                                << redirectReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() << redirectReply->errorString();
            result.errorMessage = redirectReply->errorString();
            result.httpStatus = redirectReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            redirectReply->deleteLater();
            return false;
        }

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(ONEDRIVE) << "Download attempt" << label << "failed" << req.url() << status << reply->errorString();
            result.errorMessage = reply->errorString();
            result.httpStatus = status;
            reply->deleteLater();
            return false;
        }

        result.data = reply->readAll();
        result.httpStatus = status;
        reply->deleteLater();
        result.success = true;
        return true;
    };

    QString resolvedDownloadUrl = downloadUrl;
    if (resolvedDownloadUrl.isEmpty()) {
        const auto refreshedItem = getItemById(accessToken, driveId, itemId);
        if (refreshedItem.success && !refreshedItem.item.downloadUrl.isEmpty()) {
            resolvedDownloadUrl = refreshedItem.item.downloadUrl;
        } else if (!refreshedItem.success) {
            qCWarning(ONEDRIVE) << "Could not refresh download URL for item" << itemId << refreshedItem.httpStatus << refreshedItem.errorMessage;
        }
    }

    // Prefer the pre-signed URL (should require no auth); fall back to Graph content endpoints if missing or failing.
    if (!resolvedDownloadUrl.isEmpty()) {
        QNetworkRequest fallbackReq{QUrl(resolvedDownloadUrl)};
        if (tryDownload(fallbackReq, false, "signed-url-anon")) {
            return result;
        }
        if (tryDownload(fallbackReq, true, "signed-url-bearer")) {
            return result;
        }
    } else {
        qCWarning(ONEDRIVE) << "Download URL missing for item" << itemId << "- falling back to Graph content endpoints";
    }

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    url.setPath(QStringLiteral("/v1.0/me/drive/items/%1/content").arg(itemId));
    QNetworkRequest bearerReq = buildRequest(accessToken, url);
    if (tryDownload(bearerReq, true, "me-content")) {
        return result;
    }

    if (!driveId.isEmpty()) {
        QUrl driveUrl(QStringLiteral("https://graph.microsoft.com"));
        driveUrl.setPath(QStringLiteral("/v1.0/drives/%1/items/%2/content").arg(driveId, itemId));
        QNetworkRequest driveReq = buildRequest(accessToken, driveUrl);
        if (tryDownload(driveReq, true, "drive-content")) {
            return result;
        }
    }

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
    item.parentPath = parent.value(QStringLiteral("path")).toString();
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

DriveItemResult Client::createFolder(const QString &accessToken, const QString &driveId, const QString &parentId, const QString &name)
{
    DriveItemResult result;
    if (accessToken.isEmpty() || parentId.isEmpty() || name.trimmed().isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token or parent information");
        return result;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("name"), name);
    payload.insert(QStringLiteral("folder"), QJsonObject());
    payload.insert(QStringLiteral("@microsoft.graph.conflictBehavior"), QStringLiteral("fail"));

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    if (driveId.isEmpty()) {
        url.setPath(QStringLiteral("/v1.0/me/drive/items/%1/children").arg(parentId));
    } else {
        url.setPath(QStringLiteral("/v1.0/drives/%1/items/%2/children").arg(driveId, parentId));
    }

    QNetworkRequest request = buildRequest(accessToken, url);
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_network.post(request, body);

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

DriveItemResult Client::copyItem(const QString &accessToken,
                                 const QString &driveId,
                                 const QString &itemId,
                                 const QString &newName,
                                 const QString &parentPath,
                                 const QString &destinationPath)
{
    DriveItemResult result;
    if (accessToken.isEmpty() || itemId.isEmpty() || parentPath.isEmpty()) {
        result.httpStatus = 401;
        result.errorMessage = QStringLiteral("Missing Microsoft Graph access token or copy information");
        return result;
    }

    QJsonObject payload;
    if (!newName.isEmpty()) {
        payload.insert(QStringLiteral("name"), newName);
    }
    QJsonObject parentRef;
    parentRef.insert(QStringLiteral("path"), parentPath);
    payload.insert(QStringLiteral("parentReference"), parentRef);

    QUrl url(QStringLiteral("https://graph.microsoft.com"));
    Q_UNUSED(driveId)
    url.setPath(QStringLiteral("/v1.0/me/drive/items/%1/copy").arg(itemId));

    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    qCDebug(ONEDRIVE) << "Graph copy POST" << url << body;
    QNetworkReply *reply = m_network.post(buildRequest(accessToken, url), body);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        result.errorMessage = reply->errorString();
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return result;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray immediateData = reply->readAll();
    const QString monitorUrl = QString::fromUtf8(reply->rawHeader("Location"));
    reply->deleteLater();

    if (status == 200 || status == 201) {
        if (!immediateData.isEmpty()) {
            result.item = parseItem(QJsonDocument::fromJson(immediateData).object());
        }
        result.httpStatus = status;
        result.success = true;
        return result;
    }

    if (status != 202 || monitorUrl.isEmpty()) {
        result.httpStatus = status;
        result.errorMessage = immediateData.isEmpty() ? QStringLiteral("Failed to start copy operation") : QString::fromUtf8(immediateData);
        const QString requestId = QString::fromUtf8(reply->rawHeader(QByteArrayLiteral("request-id")));
        qCWarning(ONEDRIVE) << "Graph copy POST unexpected response" << status << result.errorMessage << "requestId:" << requestId;
        return result;
    }

    auto finalizeResult = [&](const QJsonObject &monitorObj) -> DriveItemResult {
        DriveItemResult finalResult;
        QString targetId = monitorObj.value(QStringLiteral("resourceId")).toString();
        const QString resourceLocation = monitorObj.value(QStringLiteral("resourceLocation")).toString();
        if (targetId.startsWith(QLatin1Char('/'))) {
            const QStringList parts = targetId.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            if (!parts.isEmpty()) {
                targetId = parts.last();
            }
        }
        if (!targetId.isEmpty()) {
            finalResult = getItemById(accessToken, QString(), targetId);
        } else if (!resourceLocation.isEmpty()) {
            QNetworkReply *resourceReply = m_network.get(buildRequest(accessToken, QUrl(resourceLocation)));
            QEventLoop resourceLoop;
            connect(resourceReply, &QNetworkReply::finished, &resourceLoop, &QEventLoop::quit);
            resourceLoop.exec();
            if (resourceReply->error() == QNetworkReply::NoError) {
                finalResult.item = parseItem(QJsonDocument::fromJson(resourceReply->readAll()).object());
                finalResult.httpStatus = resourceReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                finalResult.success = true;
            } else {
                finalResult.errorMessage = resourceReply->errorString();
                finalResult.httpStatus = resourceReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            }
            resourceReply->deleteLater();
        }
        if (!finalResult.success && finalResult.errorMessage.isEmpty()) {
            finalResult.errorMessage = QStringLiteral("Copy completed but destination item could not be retrieved");
            finalResult.httpStatus = 500;
        }
        return finalResult;
    };

    QElapsedTimer timer;
    timer.start();
    const int timeoutMs = 120000;
    const int delayMs = 500;

    while (timer.elapsed() < timeoutMs) {
        QNetworkRequest monitorRequest = buildRequest(accessToken, QUrl(monitorUrl));
        monitorRequest.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
        QNetworkReply *monitorReply = m_network.get(monitorRequest);
        QEventLoop monitorLoop;
        connect(monitorReply, &QNetworkReply::finished, &monitorLoop, &QEventLoop::quit);
        monitorLoop.exec();

        const int httpStatus = monitorReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray monitorData = monitorReply->readAll();
        const QJsonObject monitorObj = monitorData.isEmpty() ? QJsonObject() : QJsonDocument::fromJson(monitorData).object();

        if (monitorReply->error() != QNetworkReply::NoError) {
            const QString statusValue = monitorObj.value(QStringLiteral("status")).toString();
            if (httpStatus == 401) {
                if (statusValue.compare(QStringLiteral("completed"), Qt::CaseInsensitive) == 0) {
                    monitorReply->deleteLater();
                    auto finalResult = finalizeResult(monitorObj);
                    return finalResult;
                }
                const auto destinationItem = getItemByPath(accessToken, destinationPath);
                if (destinationItem.success) {
                    monitorReply->deleteLater();
                    return destinationItem;
                }
                const QString requestId = QString::fromUtf8(monitorReply->rawHeader(QByteArrayLiteral("request-id")));
                qCDebug(ONEDRIVE) << "Graph copy monitor returned 401, retrying" << requestId;
                monitorReply->deleteLater();
                QThread::msleep(delayMs);
                continue;
            }
            result.errorMessage = monitorReply->errorString();
            result.httpStatus = httpStatus;
            const QString requestId = QString::fromUtf8(monitorReply->rawHeader(QByteArrayLiteral("request-id")));
            qCWarning(ONEDRIVE) << "Graph copy monitor failed" << result.httpStatus << result.errorMessage << "requestId:" << requestId;
            monitorReply->deleteLater();
            return result;
        }

        monitorReply->deleteLater();

        const QString statusValue = monitorObj.value(QStringLiteral("status")).toString();

        if (statusValue.compare(QStringLiteral("completed"), Qt::CaseInsensitive) == 0) {
            auto finalResult = finalizeResult(monitorObj);
            return finalResult;
        }

        if (statusValue.compare(QStringLiteral("failed"), Qt::CaseInsensitive) == 0) {
            const QJsonObject errorObj = monitorObj.value(QStringLiteral("error")).toObject();
            result.errorMessage = errorObj.value(QStringLiteral("message")).toString();
            if (result.errorMessage.isEmpty()) {
                result.errorMessage = QString::fromUtf8(monitorData);
            }
            result.httpStatus = monitorObj.value(QStringLiteral("statusCode")).toInt();
            if (result.httpStatus == 0) {
                result.httpStatus = 500;
            }
            return result;
        }

        QThread::msleep(delayMs);
    }

    result.httpStatus = 504;
    result.errorMessage = QStringLiteral("Timed out waiting for copy operation");
    return result;
}
