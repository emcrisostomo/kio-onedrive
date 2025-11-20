/*
 * SPDX-FileCopyrightText: 2013-2014 Daniel Vrátil <dvratil@redhat.com>
 * SPDX-FileCopyrightText: 2025 Enrico M. Crisostomo <enrico.crisostomo@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef KIO_ONEDRIVE_H
#define KIO_ONEDRIVE_H

#include "onedriveaccount.h"
#include "onedriveclient.h"
#include "pathcache.h"

#include <KIO/WorkerBase>

#include <memory>

class AbstractAccountManager;

class QTemporaryFile;

class KIOGDrive : public KIO::WorkerBase
{
public:
    enum Action {
        Success,
        Fail,
        Restart,
    };

    explicit KIOGDrive(const QByteArray &protocol, const QByteArray &pool_socket, const QByteArray &app_socket);
    ~KIOGDrive() override;

    virtual KIO::WorkerResult openConnection() Q_DECL_OVERRIDE;
    virtual KIO::WorkerResult listDir(const QUrl &url) Q_DECL_OVERRIDE;
    virtual KIO::WorkerResult mkdir(const QUrl &url, int permissions) Q_DECL_OVERRIDE;

    virtual KIO::WorkerResult stat(const QUrl &url) Q_DECL_OVERRIDE;
    virtual KIO::WorkerResult get(const QUrl &url) Q_DECL_OVERRIDE;
    virtual KIO::WorkerResult put(const QUrl &url, int permissions, KIO::JobFlags flags) Q_DECL_OVERRIDE;

    virtual KIO::WorkerResult copy(const QUrl &src, const QUrl &dest, int permissions, KIO::JobFlags flags) Q_DECL_OVERRIDE;
    virtual KIO::WorkerResult rename(const QUrl &src, const QUrl &dest, KIO::JobFlags flags) Q_DECL_OVERRIDE;
    virtual KIO::WorkerResult del(const QUrl &url, bool isfile) Q_DECL_OVERRIDE;

    virtual KIO::WorkerResult mimetype(const QUrl &url) Q_DECL_OVERRIDE;
    KIO::WorkerResult fileSystemFreeSpace(const QUrl &url) Q_DECL_OVERRIDE;

private:
    Q_DISABLE_COPY(KIOGDrive)

    enum PathFlags {
        None = 0,
        PathIsFolder = 1,
        PathIsFile = 2,
    };

    enum class FetchEntryFlags {
        None = 0,
        CurrentDir = 1,
    };

    static KIO::UDSEntry newAccountUDSEntry();
    static KIO::UDSEntry sharedWithMeUDSEntry();
    static KIO::UDSEntry accountToUDSEntry(const QString &accountName);

    [[nodiscard]] KIO::WorkerResult listAccounts();
    [[nodiscard]] KIO::WorkerResult createAccount();

    [[nodiscard]] KIO::WorkerResult listSharedDrivesRoot(const QUrl &url);
    [[nodiscard]] KIO::WorkerResult createSharedDrive(const QUrl &url);
    [[nodiscard]] KIO::WorkerResult deleteSharedDrive(const QUrl &url);
    [[nodiscard]] KIO::WorkerResult statSharedDrive(const QUrl &url);
    [[nodiscard]] KIO::UDSEntry fetchSharedDrivesRootEntry(const QString &accountId, FetchEntryFlags flags = FetchEntryFlags::None);

    [[nodiscard]] std::pair<KIO::WorkerResult, QString> resolveFileIdFromPath(const QString &path, PathFlags flags = None);
    [[nodiscard]] std::pair<KIO::WorkerResult, QString> resolveSharedWithMeKey(const QUrl &url, const QString &accountId, const OneDriveAccountPtr &account);
    QString resolveSharedDriveId(const QString &idOrName, const QString &accountId);

    OneDriveAccountPtr getAccount(const QString &accountName);

    [[nodiscard]] std::pair<KIO::WorkerResult, QString> rootFolderId(const QString &accountId);
    [[nodiscard]] KIO::WorkerResult listAccountRoot(const QUrl &url, const QString &accountId, const OneDriveAccountPtr &account);
    [[nodiscard]] KIO::WorkerResult listFolderByPath(const QUrl &url, const QString &accountId, const OneDriveAccountPtr &account, const QString &relativePath);
    [[nodiscard]] KIO::UDSEntry driveItemToEntry(const OneDrive::DriveItem &item) const;
    void cacheSharedWithMeEntries(const QString &accountId, const QList<OneDrive::DriveItem> &items);

    [[nodiscard]] KIO::WorkerResult putUpdate(const QUrl &url);
    [[nodiscard]] KIO::WorkerResult putCreate(const QUrl &url);
    [[nodiscard]] KIO::WorkerResult readPutData(QTemporaryFile &tmpFile, const QString &fileName, QString *detectedMimeType = nullptr);

    std::unique_ptr<AbstractAccountManager> m_accountManager;
    PathCache m_cache;
    OneDrive::Client m_graphClient;

    QMap<QString /* account */, QString /* rootId */> m_rootIds;
    QMap<QString /* account */, QString /* driveType */> m_driveTypes;
};

#endif // KIO_ONEDRIVE_H
