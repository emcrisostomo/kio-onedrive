/*
 * SPDX-FileCopyrightText: 2013-2014 Daniel Vrátil <dvratil@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "kio_gdrive.h"
#include "gdrive_udsentry.h"
#include "gdrivebackend.h"
#include "gdriveurl.h"
#include "onedrivedebug.h"
#include "onedriveversion.h"

#include <QApplication>
#include <QIODevice>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QUrlQuery>
#include <QUuid>

#include <KIO/Job>
#include <KLocalizedString>

class KIOPluginForMetaData : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.slave.onedrive" FILE "onedrive.json")
};

extern "C" {
int Q_DECL_EXPORT kdemain(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kio_onedrive"));

    if (argc != 4) {
        fprintf(stderr, "Usage: kio_onedrive protocol domain-socket1 domain-socket2\n");
        exit(-1);
    }

    KIOGDrive slave(argv[1], argv[2], argv[3]);
    slave.dispatchLoop();
    return 0;
}
}

KIOGDrive::KIOGDrive(const QByteArray &protocol, const QByteArray &pool_socket, const QByteArray &app_socket)
    : WorkerBase("onedrive", pool_socket, app_socket)
{
    Q_UNUSED(protocol);

    m_accountManager.reset(new AccountManager);

    qCDebug(ONEDRIVE) << "KIO OneDrive ready: version" << ONEDRIVE_VERSION_STRING;
}

KIOGDrive::~KIOGDrive()
{
    closeConnection();
}

KIO::WorkerResult KIOGDrive::fileSystemFreeSpace(const QUrl &url)
{
    const auto gdriveUrl = GDriveUrl(url);
    if (gdriveUrl.isNewAccountPath()) {
        qCDebug(ONEDRIVE) << "fileSystemFreeSpace is not supported for new-account url";
        return KIO::WorkerResult::pass();
    }
    if (gdriveUrl.isRoot()) {
        qCDebug(ONEDRIVE) << "fileSystemFreeSpace is not supported for onedrive root url";
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_STAT, url.toDisplayString());
    }

    qCDebug(ONEDRIVE) << "Getting fileSystemFreeSpace for" << url;
    const QString accountId = gdriveUrl.account();
    const auto account = getAccount(accountId);
    if (account->accountName().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("%1 isn't a known GDrive account", accountId));
    }

    const auto quotaResult = m_graphClient.fetchDriveQuota(account->accessToken());
    if (!quotaResult.success) {
        if (quotaResult.httpStatus == 401 || quotaResult.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, quotaResult.errorMessage);
    }

    if (quotaResult.total > 0) {
        setMetaData(QStringLiteral("total"), QString::number(quotaResult.total));
    }
    if (quotaResult.remaining >= 0) {
        setMetaData(QStringLiteral("available"), QString::number(quotaResult.remaining));
    }

    return KIO::WorkerResult::pass();
}

OneDriveAccountPtr KIOGDrive::getAccount(const QString &accountName)
{
    return m_accountManager->account(accountName);
}

KIO::WorkerResult KIOGDrive::openConnection()
{
    qCDebug(ONEDRIVE) << "Ready to talk to GDrive";
    return KIO::WorkerResult::pass();
}

KIO::UDSEntry KIOGDrive::newAccountUDSEntry()
{
    KIO::UDSEntry entry;

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, GDriveUrl::NewAccountPath);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18nc("login in a new google account", "New account"));
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("list-add-user"));
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR);

    return entry;
}

KIO::UDSEntry KIOGDrive::sharedWithMeUDSEntry()
{
    KIO::UDSEntry entry;

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, GDriveUrl::SharedWithMeDir);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18nc("folder containing OneDrive files shared with me", "Shared With Me"));
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder-publicshare"));
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR);

    return entry;
}

KIO::UDSEntry KIOGDrive::accountToUDSEntry(const QString &accountNAme)
{
    KIO::UDSEntry entry;

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, accountNAme);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, accountNAme);
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_SIZE, 0);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("onedrive"));

    return entry;
}

KIO::WorkerResult KIOGDrive::createAccount()
{
    const OneDriveAccountPtr account = m_accountManager->createAccount();
    if (!account->accountName().isEmpty()) {
        // Redirect to the account we just created.
        redirection(QUrl(QStringLiteral("onedrive:/%1").arg(account->accountName())));
        return KIO::WorkerResult::pass();
    }

    if (m_accountManager->accounts().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("There are no Google Drive accounts enabled. Please add at least one."));
    }

    // Redirect to the root, we already have some account.
    redirection(QUrl(QStringLiteral("onedrive:/")));
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::listAccounts()
{
    const auto accounts = m_accountManager->accounts();
    if (accounts.isEmpty()) {
        return createAccount();
    }

    for (const QString &account : accounts) {
        const KIO::UDSEntry entry = accountToUDSEntry(account);
        listEntry(entry);
    }

    KIO::UDSEntry newAccountEntry = newAccountUDSEntry();
    listEntry(newAccountEntry);

    // Create also non-writable UDSentry for "."
    KIO::UDSEntry entry;
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_SIZE, 0);
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    listEntry(entry);

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::listSharedDrivesRoot(const QUrl &url)
{
    const auto gdriveUrl = GDriveUrl(url);
    const QString accountId = gdriveUrl.account();
    const auto account = getAccount(accountId);
    const auto drivesResult = m_graphClient.listSharedDrives(account->accessToken());
    if (!drivesResult.success) {
        qCWarning(ONEDRIVE) << "Graph listSharedDrives failed for" << accountId << drivesResult.httpStatus << drivesResult.errorMessage;
        if (drivesResult.httpStatus == 401 || drivesResult.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, drivesResult.errorMessage);
    }

    for (const auto &drive : drivesResult.drives) {
        KIO::UDSEntry entry;
        entry.fastInsert(KIO::UDSEntry::UDS_NAME, drive.name);
        entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, drive.name);
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
        entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder-cloud"));
        entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
        entry.fastInsert(GDriveUDSEntryExtras::Id, drive.id);
        listEntry(entry);
        m_cache.insertPath(QStringLiteral("/%1/%2/%3").arg(accountId, GDriveUrl::SharedDrivesDir, drive.name), drive.id);
    }

    auto entry = fetchSharedDrivesRootEntry(accountId, FetchEntryFlags::CurrentDir);
    listEntry(entry);

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::createSharedDrive(const QUrl &url)
{
    Q_UNUSED(url)
    return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Creating shared libraries is not supported."));
}

KIO::WorkerResult KIOGDrive::deleteSharedDrive(const QUrl &url)
{
    Q_UNUSED(url)
    return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Shared drives are not supported yet."));
}

KIO::WorkerResult KIOGDrive::statSharedDrive(const QUrl &url)
{
    const auto gdriveUrl = GDriveUrl(url);
    const QString accountId = gdriveUrl.account();
    const auto account = getAccount(accountId);

    QString sharedDriveId = m_cache.idForPath(url.path());
    if (sharedDriveId.isEmpty()) {
        const auto drivesResult = m_graphClient.listSharedDrives(account->accessToken());
        if (!drivesResult.success) {
            if (drivesResult.httpStatus == 401 || drivesResult.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, drivesResult.errorMessage);
        }
        for (const auto &drive : drivesResult.drives) {
            const QString pathKey = QStringLiteral("%1/%2/%3").arg(accountId, GDriveUrl::SharedDrivesDir, drive.name);
            m_cache.insertPath(pathKey, drive.id);
            if (drive.name == gdriveUrl.filename()) {
                sharedDriveId = drive.id;
            }
        }
    }
    if (sharedDriveId.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    const auto graphItem = m_graphClient.getItemById(account->accessToken(), sharedDriveId, QString());
    if (!graphItem.success) {
        if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage);
    }

    const auto entry = driveItemToEntry(graphItem.item);
    statEntry(entry);
    return KIO::WorkerResult::pass();
}

KIO::UDSEntry KIOGDrive::fetchSharedDrivesRootEntry(const QString &accountId, FetchEntryFlags flags)
{
    Q_UNUSED(accountId)
    const bool canCreateDrives = false;

    KIO::UDSEntry entry;

    if (flags == FetchEntryFlags::CurrentDir) {
        entry.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
    } else {
        entry.fastInsert(KIO::UDSEntry::UDS_NAME, GDriveUrl::SharedDrivesDir);
        entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, i18n("Shared Drives"));
    }
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_SIZE, 0);
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("onedrive"));

    qlonglong udsAccess = S_IRUSR | S_IXUSR;
    // If user is allowed to create shared Drives, add write bit on directory
    if (canCreateDrives) {
        udsAccess |= S_IWUSR;
    }
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, udsAccess);

    return entry;
}

class RecursionDepthCounter
{
public:
    RecursionDepthCounter()
    {
        ++sDepth;
    }
    ~RecursionDepthCounter()
    {
        --sDepth;
    }

    RecursionDepthCounter(const RecursionDepthCounter &) = delete;
    RecursionDepthCounter &operator=(const RecursionDepthCounter &) = delete;

    int depth() const
    {
        return sDepth;
    }

private:
    static int sDepth;
};

int RecursionDepthCounter::sDepth = 0;

std::pair<KIO::WorkerResult, QString> KIOGDrive::resolveSharedWithMeKey(const QUrl &url, const QString &accountId, const OneDriveAccountPtr &account)
{
    QString remoteKey = m_cache.idForPath(url.path());
    if (!remoteKey.isEmpty()) {
        return {KIO::WorkerResult::pass(), remoteKey};
    }

    const auto gdriveUrl = GDriveUrl(url);
    const QStringList components = gdriveUrl.pathComponents();
    if (components.size() < 3) {
        return {KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path()), QString()};
    }

    const QString shareRootPath = QStringLiteral("/%1/%2/%3").arg(accountId, GDriveUrl::SharedWithMeDir, components.at(2));
    QString shareRootKey = m_cache.idForPath(shareRootPath);
    if (shareRootKey.isEmpty()) {
        const auto refreshItems = m_graphClient.listSharedWithMe(account->accessToken());
        if (!refreshItems.success) {
            if (refreshItems.httpStatus == 401 || refreshItems.httpStatus == 403) {
                return {KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString()), QString()};
            }
            return {KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, refreshItems.errorMessage), QString()};
        }
        cacheSharedWithMeEntries(accountId, refreshItems.items);
        shareRootKey = m_cache.idForPath(shareRootPath);
    }

    if (shareRootKey.isEmpty()) {
        return {KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path()), QString()};
    }

    const QStringList rootIds = shareRootKey.split(QLatin1Char('|'));
    if (rootIds.size() != 2) {
        return {KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path()), QString()};
    }

    const QStringList relativeComponents = components.mid(3);
    if (relativeComponents.isEmpty()) {
        m_cache.insertPath(url.path(), shareRootKey);
        return {KIO::WorkerResult::pass(), shareRootKey};
    }

    const QString relativePath = relativeComponents.join(QStringLiteral("/"));
    const auto graphItem = m_graphClient.getDriveItemByPath(account->accessToken(), rootIds.at(0), rootIds.at(1), relativePath);
    if (!graphItem.success) {
        if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
            return {KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString()), QString()};
        }
        if (graphItem.httpStatus == 404) {
            return {KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path()), QString()};
        }
        return {KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage), QString()};
    }

    const QString resolvedDriveId = graphItem.item.driveId.isEmpty() ? rootIds.at(0) : graphItem.item.driveId;
    const QString resolvedKey = QStringLiteral("%1|%2").arg(resolvedDriveId, graphItem.item.id);
    m_cache.insertPath(url.path(), resolvedKey);
    return {KIO::WorkerResult::pass(), resolvedKey};
}

std::pair<KIO::WorkerResult, QString> KIOGDrive::resolveFileIdFromPath(const QString &path, PathFlags flags)
{
    qCDebug(ONEDRIVE) << "Resolving file ID for" << path;

    if (path.isEmpty()) {
        return {KIO::WorkerResult::pass(), QString()};
    }

    const QString fileId = m_cache.idForPath(path);
    if (!fileId.isEmpty()) {
        qCDebug(ONEDRIVE) << "Resolved" << path << "to" << fileId << "(from cache)";
        return {KIO::WorkerResult::pass(), fileId};
    }

    QUrl url;
    url.setScheme(GDriveUrl::Scheme);
    url.setPath(path);
    const auto gdriveUrl = GDriveUrl(url);
    Q_ASSERT(!gdriveUrl.isRoot());

    if (gdriveUrl.isAccountRoot() || gdriveUrl.isTrashDir() || gdriveUrl.isSharedWithMeRoot()) {
        qCDebug(ONEDRIVE) << "Resolved" << path << "to account root";
        return rootFolderId(gdriveUrl.account());
    }

    if (gdriveUrl.isSharedDrive()) {
        // The gdriveUrl.filename() could be the Shared Drive id or
        // the name depending on whether we are navigating from a parent
        // or accessing the url directly, use the shared drive specific
        // solver to disambiguate
        return {KIO::WorkerResult::pass(), resolveSharedDriveId(gdriveUrl.filename(), gdriveUrl.account())};
    }

    if (gdriveUrl.isSharedDrivesRoot()) {
        qCDebug(ONEDRIVE) << "Resolved" << path << "to Shared Drives root";
        return {KIO::WorkerResult::pass(), QString()};
    }

    auto isPersonalPath = [](const GDriveUrl &url) {
        return !url.isSharedWithMeRoot() && !url.isSharedWithMe() && !url.isSharedDrivesRoot() && !url.isSharedDrive() && !url.isTrashDir() && !url.isTrashed();
    };

    if (isPersonalPath(gdriveUrl)) {
        const QString accountId = gdriveUrl.account();
        const auto account = getAccount(accountId);
        if (account->accountName().isEmpty()) {
            return {KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("%1 isn't a known GDrive account", accountId)), QString()};
        }

        const QStringList components = gdriveUrl.pathComponents();
        if (components.size() < 2) {
            return {KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, path), QString()};
        }
        const QString relativePath = components.mid(1).join(QStringLiteral("/"));
        const auto graphItem = m_graphClient.getItemByPath(account->accessToken(), relativePath);
        if (!graphItem.success) {
            if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
                return {KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString()), QString()};
            }
            if (graphItem.httpStatus == 404) {
                return {KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString()), QString()};
            }
            return {KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage), QString()};
        }

        if ((flags & KIOGDrive::PathIsFolder) && !graphItem.item.isFolder) {
            return {KIO::WorkerResult::fail(KIO::ERR_IS_FILE, url.toDisplayString()), QString()};
        }
        if ((flags & KIOGDrive::PathIsFile) && graphItem.item.isFolder) {
            return {KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.toDisplayString()), QString()};
        }

        m_cache.insertPath(path, graphItem.item.id);
        qCDebug(ONEDRIVE) << "Resolved" << path << "to" << graphItem.item.id << "(via Graph)";
        return {KIO::WorkerResult::pass(), graphItem.item.id};
    }

    return {KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, path), QString()};
}

QString KIOGDrive::resolveSharedDriveId(const QString &idOrName, const QString &accountId)
{
    const auto account = getAccount(accountId);
    const auto drivesResult = m_graphClient.listSharedDrives(account->accessToken());
    if (!drivesResult.success) {
        return QString();
    }

    for (const auto &drive : drivesResult.drives) {
        const QString pathKey = QStringLiteral("%1/%2/%3").arg(accountId, GDriveUrl::SharedDrivesDir, drive.name);
        m_cache.insertPath(pathKey, drive.id);
        if (drive.name == idOrName) {
            return drive.id;
        }
    }

    return QString();
}

std::pair<KIO::WorkerResult, QString> KIOGDrive::rootFolderId(const QString &accountId)
{
    auto it = m_rootIds.constFind(accountId);
    if (it == m_rootIds.cend()) {
        qCDebug(ONEDRIVE) << "Getting root ID for" << accountId << "via Graph";
        const auto account = getAccount(accountId);
        if (account->accountName().isEmpty()) {
            return {KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("%1 isn't a known GDrive account", accountId)), QString()};
        }

        const auto graphItem = m_graphClient.getItemByPath(account->accessToken(), QString());
        if (!graphItem.success) {
            if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
                return {KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, accountId), QString()};
            }
            return {KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage), QString()};
        }

        if (graphItem.item.id.isEmpty()) {
            qCWarning(ONEDRIVE) << "Failed to obtain root ID";
            return {KIO::WorkerResult::pass(), QString()};
        }

        auto v = m_rootIds.insert(accountId, graphItem.item.id);
        return {KIO::WorkerResult::pass(), *v};
    }

    return {KIO::WorkerResult::pass(), *it};
}

KIO::UDSEntry KIOGDrive::driveItemToEntry(const OneDrive::DriveItem &item) const
{
    KIO::UDSEntry entry;
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, item.name);
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, item.name);

    if (item.isFolder) {
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
        entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
    } else {
        QMimeDatabase db;
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG);
        entry.fastInsert(KIO::UDSEntry::UDS_SIZE, item.size);
        if (item.mimeType.isEmpty()) {
            const auto mime = db.mimeTypeForFile(item.name, QMimeDatabase::MatchExtension);
            entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, mime.name());
        } else {
            entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, item.mimeType);
        }
    }

    if (item.lastModified.isValid()) {
        entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, item.lastModified.toSecsSinceEpoch());
    }

    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
    return entry;
}

void KIOGDrive::cacheSharedWithMeEntries(const QString &accountId, const QList<OneDrive::DriveItem> &items)
{
    const QString pathPrefix = QStringLiteral("%1/%2/").arg(accountId, GDriveUrl::SharedWithMeDir);
    for (const auto &item : items) {
        if (item.remoteDriveId.isEmpty() || item.remoteItemId.isEmpty()) {
            continue;
        }
        m_cache.insertPath(pathPrefix + item.name, QStringLiteral("%1|%2").arg(item.remoteDriveId, item.remoteItemId));
    }
}

KIO::WorkerResult KIOGDrive::listAccountRoot(const QUrl &url, const QString &accountId, const OneDriveAccountPtr &account)
{
    auto sharedWithMeEntry = sharedWithMeUDSEntry();
    listEntry(sharedWithMeEntry);

    return listFolderByPath(url, accountId, account, QString());
}

KIO::WorkerResult KIOGDrive::listFolderByPath(const QUrl &url, const QString &accountId, const OneDriveAccountPtr &account, const QString &relativePath)
{
    const auto graphResult =
        relativePath.isEmpty() ? m_graphClient.listChildren(account->accessToken()) : m_graphClient.listChildrenByPath(account->accessToken(), relativePath);
    if (!graphResult.success) {
        qCWarning(ONEDRIVE) << "Graph listChildren failed for" << accountId << relativePath << graphResult.httpStatus << graphResult.errorMessage;
        if (graphResult.httpStatus == 401 || graphResult.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("Failed to list OneDrive files for %1: %2", accountId, graphResult.errorMessage));
    }

    const QString pathPrefix = url.path().endsWith(QLatin1Char('/')) ? url.path() : url.path() + QLatin1Char('/');
    for (const auto &item : graphResult.items) {
        const KIO::UDSEntry entry = driveItemToEntry(item);
        listEntry(entry);
        m_cache.insertPath(pathPrefix + item.name, item.id);
    }

    KIO::UDSEntry dotEntry;
    dotEntry.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
    dotEntry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    dotEntry.fastInsert(KIO::UDSEntry::UDS_SIZE, 0);
    dotEntry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
    listEntry(dotEntry);

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::listDir(const QUrl &url)
{
    qCDebug(ONEDRIVE) << "Going to list" << url;

    const auto gdriveUrl = GDriveUrl(url);

    if (gdriveUrl.isRoot()) {
        return listAccounts();
    }
    if (gdriveUrl.isNewAccountPath()) {
        return createAccount();
    }

    // We are committed to listing an url that belongs to
    // an account (i.e. not root or new account path), lets
    // make sure we know about the account
    const QString accountId = gdriveUrl.account();
    const auto account = getAccount(accountId);
    if (account->accountName().isEmpty()) {
        qCDebug(ONEDRIVE) << "Unknown account" << accountId << "for" << url;
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("%1 isn't a known GDrive account", accountId));
    }

    if (gdriveUrl.isAccountRoot()) {
        return listAccountRoot(url, accountId, account);
    }

    if (gdriveUrl.isSharedDrivesRoot() || gdriveUrl.isSharedDrive()) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Shared drives are not supported yet."));
    }

    if (gdriveUrl.isSharedWithMeRoot()) {
        const auto sharedItems = m_graphClient.listSharedWithMe(account->accessToken());
        if (!sharedItems.success) {
            qCWarning(ONEDRIVE) << "Graph sharedWithMe failed for" << accountId << sharedItems.httpStatus << sharedItems.errorMessage;
            if (sharedItems.httpStatus == 401 || sharedItems.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, sharedItems.errorMessage);
        }

        cacheSharedWithMeEntries(accountId, sharedItems.items);
        const QString pathPrefix = url.path().mid(1) + (url.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/"));
        for (const auto &item : sharedItems.items) {
            const KIO::UDSEntry entry = driveItemToEntry(item);
            listEntry(entry);
        }

        KIO::UDSEntry dotEntry;
        dotEntry.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
        dotEntry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
        dotEntry.fastInsert(KIO::UDSEntry::UDS_SIZE, 0);
        dotEntry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
        listEntry(dotEntry);
        return KIO::WorkerResult::pass();
    }
    if (gdriveUrl.isSharedWithMe()) {
        const auto [keyResult, remoteKey] = resolveSharedWithMeKey(url, accountId, account);
        if (!keyResult.success()) {
            return keyResult;
        }

        const QStringList ids = remoteKey.split(QLatin1Char('|'));
        if (ids.size() != 2) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        }

        const auto graphResult = m_graphClient.listChildren(account->accessToken(), ids.at(0), ids.at(1));
        if (!graphResult.success) {
            if (graphResult.httpStatus == 401 || graphResult.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            if (graphResult.httpStatus == 404) {
                return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphResult.errorMessage);
        }

        const QString pathPrefix = url.path().endsWith(QLatin1Char('/')) ? url.path() : url.path() + QLatin1Char('/');
        for (const auto &item : graphResult.items) {
            const KIO::UDSEntry entry = driveItemToEntry(item);
            listEntry(entry);
            m_cache.insertPath(pathPrefix + item.name, QStringLiteral("%1|%2").arg(item.driveId, item.id));
        }

        KIO::UDSEntry dotEntry;
        dotEntry.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
        dotEntry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
        dotEntry.fastInsert(KIO::UDSEntry::UDS_SIZE, 0);
        dotEntry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
        listEntry(dotEntry);
        return KIO::WorkerResult::pass();
    }

    if (!gdriveUrl.isSharedWithMe() && !gdriveUrl.isSharedWithMeRoot() && !gdriveUrl.isSharedDrivesRoot() && !gdriveUrl.isSharedDrive()
        && !gdriveUrl.isTrashDir() && !gdriveUrl.isTrashed()) {
        const auto components = gdriveUrl.pathComponents();
        const QString relativePath = components.mid(1).join(QStringLiteral("/"));
        return listFolderByPath(url, accountId, account, relativePath);
    }

    return listFolderByPath(url, accountId, account, gdriveUrl.pathComponents().mid(1).join(QStringLiteral("/")));
}

KIO::WorkerResult KIOGDrive::mkdir(const QUrl &url, int permissions)
{
    // NOTE: We deliberately ignore the permissions field here, because GDrive
    // does not recognize any privileges that could be mapped to standard UNIX
    // file permissions.
    Q_UNUSED(permissions);

    qCDebug(ONEDRIVE) << "Creating directory" << url;

    const auto gdriveUrl = GDriveUrl(url);
    const QString accountId = gdriveUrl.account();
    // At least account and new folder name
    if (gdriveUrl.isRoot() || gdriveUrl.isAccountRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    if (gdriveUrl.isSharedDrive()) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Shared drives are not supported yet."));
    }
    auto isPersonalPath = [](const GDriveUrl &path) {
        return !path.isSharedWithMeRoot() && !path.isSharedWithMe() && !path.isSharedDrivesRoot() && !path.isSharedDrive() && !path.isTrashDir()
            && !path.isTrashed();
    };

    if (!isPersonalPath(gdriveUrl)) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Only personal OneDrive content can be modified for now."));
    }

    const auto account = getAccount(accountId);
    if (account->accountName().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("%1 isn't a known GDrive account", accountId));
    }

    const QStringList components = gdriveUrl.pathComponents();
    if (components.size() < 2) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    const QString folderName = gdriveUrl.filename();
    if (folderName.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    auto relativeParentPath = [](const QStringList &parts) {
        if (parts.size() <= 2) {
            return QString();
        }
        return parts.mid(1, parts.size() - 2).join(QStringLiteral("/"));
    };

    const QString parentRelativePath = relativeParentPath(components);
    const auto parentItem = m_graphClient.getItemByPath(account->accessToken(), parentRelativePath);
    if (!parentItem.success) {
        if (parentItem.httpStatus == 401 || parentItem.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        if (parentItem.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, gdriveUrl.parentPath());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, parentItem.errorMessage);
    }

    if (!parentItem.item.isFolder) {
        return KIO::WorkerResult::fail(KIO::ERR_IS_FILE, gdriveUrl.parentPath());
    }

    const auto createResult = m_graphClient.createFolder(account->accessToken(), parentItem.item.driveId, parentItem.item.id, folderName);
    if (!createResult.success) {
        if (createResult.httpStatus == 401 || createResult.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        if (createResult.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, gdriveUrl.parentPath());
        }
        if (createResult.httpStatus == 409) {
            return KIO::WorkerResult::fail(KIO::ERR_FILE_ALREADY_EXIST, url.path());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, createResult.errorMessage);
    }

    const QString normalizedPath = url.adjusted(QUrl::StripTrailingSlash).path();
    if (!normalizedPath.isEmpty() && !createResult.item.id.isEmpty()) {
        m_cache.insertPath(normalizedPath, createResult.item.id);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::stat(const QUrl &url)
{
    // TODO We should be using StatDetails to limit how we respond to a stat request
    // const QString statDetails = metaData(QStringLiteral("statDetails"));
    // KIO::StatDetails details = statDetails.isEmpty() ? KIO::StatDefaultDetails : static_cast<KIO::StatDetails>(statDetails.toInt());
    // qCDebug(ONEDRIVE) << "Going to stat()" << url << "for details" << details;

    const auto gdriveUrl = GDriveUrl(url);
    if (gdriveUrl.isRoot()) {
        // TODO Can we stat() root?
        return KIO::WorkerResult::pass();
    }
    if (gdriveUrl.isNewAccountPath()) {
        qCDebug(ONEDRIVE) << "stat()ing new-account path";
        const KIO::UDSEntry entry = newAccountUDSEntry();
        statEntry(entry);
        return KIO::WorkerResult::pass();
    }

    const QString accountId = gdriveUrl.account();
    const auto account = getAccount(accountId);

    if (gdriveUrl.isSharedWithMeRoot()) {
        qCDebug(ONEDRIVE) << "stat()ing Shared With Me path";
        const KIO::UDSEntry entry = sharedWithMeUDSEntry();
        statEntry(entry);
        return KIO::WorkerResult::pass();
    }
    if (gdriveUrl.isSharedDrivesRoot() || gdriveUrl.isSharedDrive()) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Shared drives are not supported yet."));
    }
    if (gdriveUrl.isSharedWithMe()) {
        const auto [keyResult, remoteKey] = resolveSharedWithMeKey(url, accountId, account);
        if (!keyResult.success()) {
            return keyResult;
        }

        const QStringList ids = remoteKey.split(QLatin1Char('|'));
        if (ids.size() != 2) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        }

        const auto graphItem = m_graphClient.getItemById(account->accessToken(), ids.at(0), ids.at(1));
        if (!graphItem.success) {
            if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            if (graphItem.httpStatus == 404) {
                return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage);
        }

        const KIO::UDSEntry entry = driveItemToEntry(graphItem.item);
        statEntry(entry);
        return KIO::WorkerResult::pass();
    }

    // We are committed to stat()ing an url that belongs to
    // an account (i.e. not root or new account path), lets
    // make sure we know about the account
    if (account->accountName().isEmpty()) {
        qCDebug(ONEDRIVE) << "Unknown account" << accountId << "for" << url;
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("%1 isn't a known GDrive account", accountId));
    }

    if (gdriveUrl.isAccountRoot()) {
        qCDebug(ONEDRIVE) << "stat()ing account root";
        const KIO::UDSEntry entry = accountToUDSEntry(accountId);
        statEntry(entry);
        return KIO::WorkerResult::pass();
    }
    if (gdriveUrl.isSharedDrivesRoot()) {
        qCDebug(ONEDRIVE) << "stat()ing Shared Drives root";
        const auto entry = fetchSharedDrivesRootEntry(accountId);
        statEntry(entry);
        return KIO::WorkerResult::pass();
    }
    if (gdriveUrl.isSharedDrive()) {
        qCDebug(ONEDRIVE) << "stat()ing Shared Drive" << url;
        return statSharedDrive(url);
    }

    if (!gdriveUrl.isSharedWithMe() && !gdriveUrl.isSharedWithMeRoot() && !gdriveUrl.isSharedDrivesRoot() && !gdriveUrl.isSharedDrive()
        && !gdriveUrl.isTrashDir() && !gdriveUrl.isTrashed()) {
        const QString relativePath = gdriveUrl.pathComponents().mid(1).join(QStringLiteral("/"));
        const auto graphItem = m_graphClient.getItemByPath(account->accessToken(), relativePath);
        if (!graphItem.success) {
            qCWarning(ONEDRIVE) << "Graph getItemByPath failed for" << accountId << relativePath << graphItem.httpStatus << graphItem.errorMessage;
            if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            if (graphItem.httpStatus == 404) {
                return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage);
        }

        const KIO::UDSEntry entry = driveItemToEntry(graphItem.item);
        statEntry(entry);
        m_cache.insertPath(url.path(), graphItem.item.id);
        return KIO::WorkerResult::pass();
    }

    if (gdriveUrl.isSharedWithMe()) {
        const auto [keyResult, remoteKey] = resolveSharedWithMeKey(url, accountId, account);
        if (!keyResult.success()) {
            return keyResult;
        }
        const QStringList ids = remoteKey.split(QLatin1Char('|'));
        if (ids.size() != 2) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        }

        const auto graphItem = m_graphClient.getItemById(account->accessToken(), ids.at(0), ids.at(1));
        if (!graphItem.success) {
            if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            if (graphItem.httpStatus == 404) {
                return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage);
        }

        const KIO::UDSEntry entry = driveItemToEntry(graphItem.item);
        statEntry(entry);
        return KIO::WorkerResult::pass();
    }

    return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
}

KIO::WorkerResult KIOGDrive::get(const QUrl &url)
{
    qCDebug(ONEDRIVE) << "Fetching content of" << url;

    const auto gdriveUrl = GDriveUrl(url);
    const QString accountId = gdriveUrl.account();
    const auto account = getAccount(accountId);

    if (gdriveUrl.isRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }
    if (gdriveUrl.isAccountRoot()) {
        // You cannot GET an account folder!
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED, url.path());
    }

    if (gdriveUrl.isSharedDrivesRoot() || gdriveUrl.isSharedDrive()) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Shared drives are not supported yet."));
    }

    if (gdriveUrl.isSharedWithMe()) {
        const auto [keyResult, remoteKey] = resolveSharedWithMeKey(url, accountId, account);
        if (!keyResult.success()) {
            return keyResult;
        }

        const QStringList ids = remoteKey.split(QLatin1Char('|'));
        if (ids.size() != 2) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        }

        const auto graphItem = m_graphClient.getItemById(account->accessToken(), ids.at(0), ids.at(1));
        if (!graphItem.success) {
            if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            if (graphItem.httpStatus == 404) {
                return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage);
        }

        if (graphItem.item.isFolder) {
            return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.path());
        }

        if (!graphItem.item.mimeType.isEmpty()) {
            mimeType(graphItem.item.mimeType);
        } else {
            QMimeDatabase db;
            const auto mime = db.mimeTypeForFile(graphItem.item.name, QMimeDatabase::MatchExtension);
            mimeType(mime.name());
        }

        auto currentAccount = account;
        auto tryDownload = [&](const QString &token) {
            return m_graphClient.downloadItem(token, graphItem.item.id, graphItem.item.downloadUrl, graphItem.item.driveId);
        };
        auto downloadResult = tryDownload(currentAccount->accessToken());
        if (!downloadResult.success && (downloadResult.httpStatus == 401 || downloadResult.httpStatus == 403)) {
            currentAccount = m_accountManager->refreshAccount(currentAccount);
            if (currentAccount && !currentAccount->accessToken().isEmpty()) {
                downloadResult = tryDownload(currentAccount->accessToken());
            }
        }
        if (!downloadResult.success) {
            if (downloadResult.httpStatus == 401 || downloadResult.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, downloadResult.errorMessage);
        }

        const QByteArray contentData = downloadResult.data;
        processedSize(contentData.size());
        totalSize(contentData.size());
        int transferred = 0;
        do {
            const QByteArray chunk = contentData.mid(transferred, 1024 * 8);
            data(chunk);
            transferred += chunk.size();
        } while (transferred < contentData.size());
        data(QByteArray());

        return KIO::WorkerResult::pass();
    }

    if (!gdriveUrl.isSharedWithMe() && !gdriveUrl.isSharedWithMeRoot() && !gdriveUrl.isSharedDrivesRoot() && !gdriveUrl.isSharedDrive()
        && !gdriveUrl.isTrashDir() && !gdriveUrl.isTrashed()) {
        const QString relativePath = gdriveUrl.pathComponents().mid(1).join(QStringLiteral("/"));
        const auto graphItem = m_graphClient.getItemByPath(account->accessToken(), relativePath);
        if (!graphItem.success) {
            qCWarning(ONEDRIVE) << "Graph getItemByPath failed for" << accountId << relativePath << graphItem.httpStatus << graphItem.errorMessage;
            if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            if (graphItem.httpStatus == 404) {
                return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage);
        }

        if (graphItem.item.isFolder) {
            return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.path());
        }

        if (!graphItem.item.mimeType.isEmpty()) {
            mimeType(graphItem.item.mimeType);
        } else {
            QMimeDatabase db;
            const auto mime = db.mimeTypeForFile(graphItem.item.name, QMimeDatabase::MatchExtension);
            mimeType(mime.name());
        }

        auto currentAccount = account;
        auto tryDownload = [&](const QString &token) {
            return m_graphClient.downloadItem(token, graphItem.item.id, graphItem.item.downloadUrl, graphItem.item.driveId);
        };
        auto downloadResult = tryDownload(currentAccount->accessToken());
        if (!downloadResult.success && (downloadResult.httpStatus == 401 || downloadResult.httpStatus == 403)) {
            currentAccount = m_accountManager->refreshAccount(currentAccount);
            if (currentAccount && !currentAccount->accessToken().isEmpty()) {
                downloadResult = tryDownload(currentAccount->accessToken());
            }
        }
        if (!downloadResult.success) {
            qCWarning(ONEDRIVE) << "Failed downloading" << relativePath << downloadResult.httpStatus << downloadResult.errorMessage;
            if (downloadResult.httpStatus == 401 || downloadResult.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, downloadResult.errorMessage);
        }

        const QByteArray contentData = downloadResult.data;

        processedSize(contentData.size());
        totalSize(contentData.size());

        int transferred = 0;
        do {
            const QByteArray chunk = contentData.mid(transferred, 1024 * 8);
            data(chunk);
            transferred += chunk.size();
        } while (transferred < contentData.size());
        data(QByteArray());

        return KIO::WorkerResult::pass();
    }

    return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
}

KIO::WorkerResult KIOGDrive::readPutData(QTemporaryFile &tempFile, const QString &fileName, QString *detectedMimeType)
{
    // TODO: Instead of using a temp file, upload directly the raw data (requires
    // support in LibKGAPI)

    // TODO: For large files, switch to resumable upload and upload the file in
    // reasonably large chunks (requires support in LibKGAPI)

    // TODO: Support resumable upload (requires support in LibKGAPI)

    if (!tempFile.open()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, tempFile.fileName());
    }

    int result;
    do {
        QByteArray buffer;
        dataReq();
        result = readData(buffer);
        if (!buffer.isEmpty()) {
            qint64 size = tempFile.write(buffer);
            if (size != buffer.size()) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, tempFile.fileName());
            }
        }
    } while (result > 0);

    const QMimeType mime = QMimeDatabase().mimeTypeForFileNameAndData(fileName, &tempFile);
    if (detectedMimeType) {
        *detectedMimeType = mime.name();
    }

    if (!tempFile.seek(0)) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, tempFile.fileName());
    }

    if (result == -1) {
        qCWarning(ONEDRIVE) << "Could not read source file" << tempFile.fileName();
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, QString());
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::putUpdate(const QUrl &url)
{
    const QString fileId = QUrlQuery(url).queryItemValue(QStringLiteral("id"));
    qCDebug(ONEDRIVE) << Q_FUNC_INFO << url << fileId;

    const auto gdriveUrl = GDriveUrl(url);
    const auto accountId = gdriveUrl.account();

    if (fileId.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    auto isPersonalPath = [](const GDriveUrl &path) {
        return !path.isSharedWithMeRoot() && !path.isSharedWithMe() && !path.isSharedDrivesRoot() && !path.isSharedDrive() && !path.isTrashDir()
            && !path.isTrashed();
    };

    if (!isPersonalPath(gdriveUrl)) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Only personal OneDrive content can be modified for now."));
    }

    const auto account = getAccount(accountId);
    if (account->accountName().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("%1 isn't a known GDrive account", accountId));
    }

    QTemporaryFile tmpFile;
    QString mimeType;
    if (auto result = readPutData(tmpFile, gdriveUrl.filename(), &mimeType); !result.success()) {
        return result;
    }
    const auto uploadResult = m_graphClient.uploadItemById(account->accessToken(), QString(), fileId, &tmpFile, mimeType);
    tmpFile.close();
    if (!uploadResult.success) {
        if (uploadResult.httpStatus == 401 || uploadResult.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        if (uploadResult.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, uploadResult.errorMessage);
    }

    const QString normalizedPath = url.adjusted(QUrl::StripTrailingSlash).path();
    if (!normalizedPath.isEmpty()) {
        const QString cachedId = uploadResult.item.id.isEmpty() ? fileId : uploadResult.item.id;
        m_cache.insertPath(normalizedPath, cachedId);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::putCreate(const QUrl &url)
{
    qCDebug(ONEDRIVE) << Q_FUNC_INFO << url;
    const auto gdriveUrl = GDriveUrl(url);
    if (gdriveUrl.isRoot() || gdriveUrl.isAccountRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED, url.path());
    }

    auto isPersonalPath = [](const GDriveUrl &path) {
        return !path.isSharedWithMeRoot() && !path.isSharedWithMe() && !path.isSharedDrivesRoot() && !path.isSharedDrive() && !path.isTrashDir()
            && !path.isTrashed();
    };

    if (!isPersonalPath(gdriveUrl)) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Only personal OneDrive content can be modified for now."));
    }

    const auto accountId = gdriveUrl.account();
    const auto account = getAccount(accountId);
    if (account->accountName().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("%1 isn't a known GDrive account", accountId));
    }

    const QStringList components = gdriveUrl.pathComponents();
    if (components.size() < 2) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    const QString relativePath = components.mid(1).join(QStringLiteral("/"));
    auto relativeParentPath = [](const QStringList &parts) {
        if (parts.size() <= 2) {
            return QString();
        }
        return parts.mid(1, parts.size() - 2).join(QStringLiteral("/"));
    };
    const QString parentPath = relativeParentPath(components);
    if (!parentPath.isEmpty()) {
        const auto parentResult = m_graphClient.getItemByPath(account->accessToken(), parentPath);
        if (!parentResult.success) {
            if (parentResult.httpStatus == 401 || parentResult.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            if (parentResult.httpStatus == 404) {
                return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, gdriveUrl.parentPath());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, parentResult.errorMessage);
        }
    }

    QTemporaryFile tmpFile;
    QString mimeType;
    if (auto result = readPutData(tmpFile, gdriveUrl.filename(), &mimeType); !result.success()) {
        return result;
    }
    const auto uploadResult = m_graphClient.uploadItemByPath(account->accessToken(), relativePath, &tmpFile, mimeType);
    tmpFile.close();
    if (!uploadResult.success) {
        if (uploadResult.httpStatus == 401 || uploadResult.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        if (uploadResult.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, uploadResult.errorMessage);
    }

    const QString normalizedPath = url.adjusted(QUrl::StripTrailingSlash).path();
    if (!normalizedPath.isEmpty() && !uploadResult.item.id.isEmpty()) {
        m_cache.insertPath(normalizedPath, uploadResult.item.id);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::put(const QUrl &url, int permissions, KIO::JobFlags flags)
{
    // NOTE: We deliberately ignore the permissions field here, because GDrive
    // does not recognize any privileges that could be mapped to standard UNIX
    // file permissions.
    Q_UNUSED(permissions)
    Q_UNUSED(flags)

    qCDebug(ONEDRIVE) << Q_FUNC_INFO << url;

    const auto gdriveUrl = GDriveUrl(url);

    if (gdriveUrl.isSharedDrive()) {
        qCDebug(ONEDRIVE) << "Can't create files in Shared Drives root" << url;
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_WRITE, url.path());
    }

    auto isPersonalPath = [](const GDriveUrl &path) {
        return !path.isSharedWithMeRoot() && !path.isSharedWithMe() && !path.isSharedDrivesRoot() && !path.isSharedDrive() && !path.isTrashDir()
            && !path.isTrashed();
    };

    if (!isPersonalPath(gdriveUrl)) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Only personal OneDrive content can be modified for now."));
    }

    if (QUrlQuery(url).hasQueryItem(QStringLiteral("id"))) {
        if (auto result = putUpdate(url); !result.success()) {
            return result;
        }
    } else {
        if (auto result = putCreate(url); !result.success()) {
            return result;
        }
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::copy(const QUrl &src, const QUrl &dest, int permissions, KIO::JobFlags flags)
{
    qCDebug(ONEDRIVE) << "Going to copy" << src << "to" << dest;

    // NOTE: We deliberately ignore the permissions field here, because GDrive
    // does not recognize any privileges that could be mapped to standard UNIX
    // file permissions.
    Q_UNUSED(permissions);

    // NOTE: We deliberately ignore the flags field here, because the "overwrite"
    // flag would have no effect on GDrive, since file name don't have to be
    // unique. IOW if there is a file "foo.bar" and user copy-pastes into the
    // same directory, the FileCopyJob will succeed and a new file with the same
    // name will be created.
    Q_UNUSED(flags);

    const auto srcGDriveUrl = GDriveUrl(src);
    const auto destGDriveUrl = GDriveUrl(dest);
    const QString sourceAccountId = srcGDriveUrl.account();
    const QString destAccountId = destGDriveUrl.account();

    // TODO: Does this actually happen, or does KIO treat our account name as host?
    if (sourceAccountId != destAccountId) {
        // KIO will fallback to get+post
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, src.path());
    }

    if (srcGDriveUrl.isRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
    }
    if (srcGDriveUrl.isAccountRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED, src.path());
    }

    auto isPersonalPath = [](const GDriveUrl &url) {
        return !url.isSharedWithMeRoot() && !url.isSharedWithMe() && !url.isSharedDrivesRoot() && !url.isSharedDrive() && !url.isTrashDir() && !url.isTrashed();
    };

    if (!isPersonalPath(srcGDriveUrl) || !isPersonalPath(destGDriveUrl)) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Only personal OneDrive content can be copied for now."));
    }

    const auto account = getAccount(sourceAccountId);
    if (account->accountName().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("%1 isn't a known GDrive account", sourceAccountId));
    }

    const QStringList srcComponents = srcGDriveUrl.pathComponents();
    if (srcComponents.size() < 2) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
    }
    const QString srcRelativePath = srcComponents.mid(1).join(QStringLiteral("/"));
    const auto sourceItem = m_graphClient.getItemByPath(account->accessToken(), srcRelativePath);
    if (!sourceItem.success) {
        if (sourceItem.httpStatus == 401 || sourceItem.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, src.toDisplayString());
        }
        if (sourceItem.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, sourceItem.errorMessage);
    }

    if (destGDriveUrl.isRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED, dest.path());
    }

    const QString destName = destGDriveUrl.filename();
    if (destName.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, dest.path());
    }

    const QStringList destComponents = destGDriveUrl.pathComponents();
    auto relativeParentPath = [](const QStringList &components) {
        if (components.size() <= 2) {
            return QString();
        }
        return components.mid(1, components.size() - 2).join(QStringLiteral("/"));
    };
    const QString destParentPath = relativeParentPath(destComponents);
    const auto destParentItem = m_graphClient.getItemByPath(account->accessToken(), destParentPath);
    if (!destParentItem.success) {
        if (destParentItem.httpStatus == 401 || destParentItem.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, dest.toDisplayString());
        }
        if (destParentItem.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, destGDriveUrl.parentPath());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, destParentItem.errorMessage);
    }

    if (!destParentItem.item.isFolder) {
        return KIO::WorkerResult::fail(KIO::ERR_IS_FILE, destGDriveUrl.parentPath());
    }

    QString parentGraphPath;
    if (destParentPath.isEmpty()) {
        parentGraphPath = QStringLiteral("/drive/root:");
    } else {
        parentGraphPath = QStringLiteral("/drive/root:/%1").arg(destParentPath);
    }

    const QString destRelativePath = destComponents.mid(1).join(QStringLiteral("/"));
    const auto copyResult = m_graphClient.copyItem(account->accessToken(), QString(), sourceItem.item.id, destName, parentGraphPath, destRelativePath);
    const QString copiedItemId = copyResult.item.id;
    if (!copyResult.success) {
        qCWarning(ONEDRIVE) << "Graph copyItem failed for" << src << "->" << dest << copyResult.httpStatus << copyResult.errorMessage;
        if (copyResult.httpStatus == 409) {
            return KIO::WorkerResult::fail(KIO::ERR_FILE_ALREADY_EXIST, dest.path());
        }
        if (copyResult.httpStatus == 401 || copyResult.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, src.toDisplayString());
        }
        if (copyResult.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, copyResult.errorMessage);
    }

    const QString normalizedDestPath = dest.adjusted(QUrl::StripTrailingSlash).path();
    if (!normalizedDestPath.isEmpty() && !copiedItemId.isEmpty()) {
        m_cache.insertPath(normalizedDestPath, copiedItemId);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::del(const QUrl &url, bool isfile)
{
    Q_UNUSED(isfile)
    const auto gdriveUrl = GDriveUrl(url);

    if (gdriveUrl.isSharedDrivesRoot() || gdriveUrl.isSharedDrive()) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Shared drives are not supported yet."));
    }
    if (gdriveUrl.isSharedWithMe()) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Deleting shared items is not supported yet."));
    }

    if (gdriveUrl.isRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    const QString accountId = gdriveUrl.account();
    const auto account = getAccount(accountId);

    if (gdriveUrl.isAccountRoot()) {
        if (account->accountName().isEmpty()) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, accountId);
        }
        m_accountManager->removeAccount(accountId);
        return KIO::WorkerResult::pass();
    }

    const QString relativePath = gdriveUrl.pathComponents().mid(1).join(QStringLiteral("/"));
    const auto graphItem = m_graphClient.getItemByPath(account->accessToken(), relativePath);
    if (!graphItem.success) {
        if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        if (graphItem.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage);
    }

    const QString itemId = graphItem.item.id;
    const QString driveId = graphItem.item.driveId;

    if (graphItem.item.isFolder && metaData(QStringLiteral("recurse")) != QLatin1String("true")) {
        const auto children = m_graphClient.listDriveChildren(account->accessToken(), driveId, itemId);
        if (!children.success) {
            if (children.httpStatus == 401 || children.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, children.errorMessage);
        }
        if (!children.items.isEmpty()) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_RMDIR, url.path());
        }
    }

    const auto deleteResult = m_graphClient.deleteItem(account->accessToken(), itemId, driveId);
    if (!deleteResult.success) {
        if (deleteResult.httpStatus == 401 || deleteResult.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        if (deleteResult.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, deleteResult.errorMessage);
    }

    m_cache.removePath(url.path());
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::rename(const QUrl &src, const QUrl &dest, KIO::JobFlags flags)
{
    Q_UNUSED(flags)
    qCDebug(ONEDRIVE) << "Renaming" << src << "to" << dest;

    const auto srcGDriveUrl = GDriveUrl(src);
    const auto destGDriveUrl = GDriveUrl(dest);
    const QString sourceAccountId = srcGDriveUrl.account();
    const QString destAccountId = destGDriveUrl.account();

    // TODO: Does this actually happen, or does KIO treat our account name as host?
    if (sourceAccountId != destAccountId) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, src.path());
    }

    if (srcGDriveUrl.isRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, dest.path());
    }
    if (srcGDriveUrl.isAccountRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED, dest.path());
    }
    if (destGDriveUrl.isRoot() || destGDriveUrl.isAccountRoot() || destGDriveUrl.isNewAccountPath()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, dest.path());
    }

    auto isPersonalPath = [](const GDriveUrl &url) {
        return !url.isSharedWithMeRoot() && !url.isSharedWithMe() && !url.isSharedDrivesRoot() && !url.isSharedDrive() && !url.isTrashDir() && !url.isTrashed();
    };

    if (!isPersonalPath(srcGDriveUrl) || !isPersonalPath(destGDriveUrl)) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Only personal OneDrive content can be renamed for now."));
    }

    const auto account = getAccount(sourceAccountId);
    if (account->accountName().isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("%1 isn't a known GDrive account", sourceAccountId));
    }

    const QStringList srcComponents = srcGDriveUrl.pathComponents();
    const QStringList destComponents = destGDriveUrl.pathComponents();
    if (srcComponents.size() < 2 || destComponents.size() < 2) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, dest.path());
    }

    const QString srcRelativePath = srcComponents.mid(1).join(QStringLiteral("/"));
    const auto graphItem = m_graphClient.getItemByPath(account->accessToken(), srcRelativePath);
    if (!graphItem.success) {
        if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, src.toDisplayString());
        }
        if (graphItem.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage);
    }

    const QString destName = destGDriveUrl.filename();
    if (destName.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, dest.path());
    }

    auto relativeParentPath = [](const QStringList &components) {
        if (components.size() <= 2) {
            return QString();
        }
        return components.mid(1, components.size() - 2).join(QStringLiteral("/"));
    };

    const QString sourceParentRelativePath = relativeParentPath(srcComponents);
    const QString destParentRelativePath = relativeParentPath(destComponents);

    const bool renameNeeded = destName != graphItem.item.name;
    const bool moveNeeded = destParentRelativePath != sourceParentRelativePath;

    if (!renameNeeded && !moveNeeded) {
        return KIO::WorkerResult::pass();
    }

    QString parentPathArgument;
    if (moveNeeded) {
        if (destParentRelativePath.isEmpty()) {
            parentPathArgument = QStringLiteral("/drive/root:");
        } else {
            parentPathArgument = QStringLiteral("/drive/root:/") + destParentRelativePath;
        }
    }

    const QString newNameArgument = renameNeeded ? destName : QString();
    const auto updateResult = m_graphClient.updateItem(account->accessToken(), graphItem.item.driveId, graphItem.item.id, newNameArgument, parentPathArgument);
    if (!updateResult.success) {
        if (updateResult.httpStatus == 401 || updateResult.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, src.toDisplayString());
        }
        if (updateResult.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
        }
        if (updateResult.httpStatus == 409) {
            return KIO::WorkerResult::fail(KIO::ERR_FILE_ALREADY_EXIST, dest.path());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, updateResult.errorMessage);
    }

    const QString normalizedSrcPath = src.adjusted(QUrl::StripTrailingSlash).path();
    if (!normalizedSrcPath.isEmpty()) {
        m_cache.removePath(normalizedSrcPath);
    }
    const QString normalizedDestPath = dest.adjusted(QUrl::StripTrailingSlash).path();
    if (!normalizedDestPath.isEmpty()) {
        const QString updatedId = updateResult.item.id.isEmpty() ? graphItem.item.id : updateResult.item.id;
        m_cache.insertPath(normalizedDestPath, updatedId);
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::mimetype(const QUrl &url)
{
    qCDebug(ONEDRIVE) << Q_FUNC_INFO << url;

    const auto gdriveUrl = GDriveUrl(url);
    const QString accountId = gdriveUrl.account();
    const auto account = getAccount(accountId);

    if (gdriveUrl.isRoot() || gdriveUrl.isAccountRoot() || gdriveUrl.isNewAccountPath()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }
    if (gdriveUrl.isSharedDrivesRoot() || gdriveUrl.isSharedDrive()) {
        return KIO::WorkerResult::fail(KIO::ERR_UNSUPPORTED_ACTION, i18n("Shared drives are not supported yet."));
    }

    auto emitMime = [&](const QString &name) {
        if (!name.isEmpty()) {
            mimeType(name);
            return true;
        }
        return false;
    };

    if (gdriveUrl.isSharedWithMe()) {
        const auto [keyResult, remoteKey] = resolveSharedWithMeKey(url, accountId, account);
        if (!keyResult.success()) {
            return keyResult;
        }
        const QStringList ids = remoteKey.split(QLatin1Char('|'));
        if (ids.size() != 2) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        }

        const auto graphItem = m_graphClient.getItemById(account->accessToken(), ids.at(0), ids.at(1));
        if (!graphItem.success) {
            if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            if (graphItem.httpStatus == 404) {
                return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage);
        }

        if (graphItem.item.isFolder) {
            return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.path());
        }

        if (!emitMime(graphItem.item.mimeType)) {
            QMimeDatabase db;
            emitMime(db.mimeTypeForFile(graphItem.item.name, QMimeDatabase::MatchExtension).name());
        }
        return KIO::WorkerResult::pass();
    }

    const QString relativePath = gdriveUrl.pathComponents().mid(1).join(QStringLiteral("/"));
    const auto graphItem = m_graphClient.getItemByPath(account->accessToken(), relativePath);
    if (!graphItem.success) {
        qCWarning(ONEDRIVE) << "Graph getItemByPath failed for" << accountId << relativePath << graphItem.httpStatus << graphItem.errorMessage;
        if (graphItem.httpStatus == 401 || graphItem.httpStatus == 403) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        if (graphItem.httpStatus == 404) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        }
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, graphItem.errorMessage);
    }

    if (graphItem.item.isFolder) {
        return KIO::WorkerResult::fail(KIO::ERR_IS_DIRECTORY, url.path());
    }

    if (!emitMime(graphItem.item.mimeType)) {
        QMimeDatabase db;
        emitMime(db.mimeTypeForFile(graphItem.item.name, QMimeDatabase::MatchExtension).name());
    }
    return KIO::WorkerResult::pass();
}

#include "kio_gdrive.moc"
