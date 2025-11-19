/*
 * SPDX-FileCopyrightText: 2013-2014 Daniel Vrátil <dvratil@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "kio_gdrive.h"
#include "gdrive_udsentry.h"
#include "gdrivebackend.h"
#include "gdrivehelper.h"
#include "gdriveurl.h"
#include "onedrivedebug.h"
#include "onedriveversion.h"

#include <QApplication>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QUrlQuery>
#include <QUuid>

#include <KGAPI/AuthJob>
#include <KGAPI/Drive/About>
#include <KGAPI/Drive/AboutFetchJob>
#include <KGAPI/Drive/ChildReference>
#include <KGAPI/Drive/ChildReferenceCreateJob>
#include <KGAPI/Drive/ChildReferenceFetchJob>
#include <KGAPI/Drive/Drives>
#include <KGAPI/Drive/DrivesCreateJob>
#include <KGAPI/Drive/DrivesDeleteJob>
#include <KGAPI/Drive/DrivesFetchJob>
#include <KGAPI/Drive/DrivesModifyJob>
#include <KGAPI/Drive/File>
#include <KGAPI/Drive/FileCopyJob>
#include <KGAPI/Drive/FileCreateJob>
#include <KGAPI/Drive/FileFetchContentJob>
#include <KGAPI/Drive/FileFetchJob>
#include <KGAPI/Drive/FileModifyJob>
#include <KGAPI/Drive/FileSearchQuery>
#include <KGAPI/Drive/FileTrashJob>
#include <KGAPI/Drive/ParentReference>
#include <KGAPI/Drive/Permission>
#include <KIO/Job>
#include <KLocalizedString>

using namespace KGAPI2;
using namespace Drive;

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

KIOGDrive::Result KIOGDrive::handleError(const KGAPI2::Job &job, const QUrl &url)
{
    qCDebug(ONEDRIVE) << "Completed job" << (&job) << "error code:" << job.error() << "- message:" << job.errorString();

    switch (job.error()) {
    case KGAPI2::OK:
    case KGAPI2::NoError:
        return Result::success();
    case KGAPI2::AuthCancelled:
    case KGAPI2::AuthError:
        return Result::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
    case KGAPI2::Unauthorized: {
        const AccountPtr oldAccount = job.account();
        const AccountPtr account = m_accountManager->refreshAccount(oldAccount);
        if (!account) {
            return Result::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
        }
        return Result::restart();
    }
    case KGAPI2::Forbidden:
        return Result::fail(KIO::ERR_ACCESS_DENIED, url.toDisplayString());
    case KGAPI2::NotFound:
        return Result::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
    case KGAPI2::NoContent:
        return Result::fail(KIO::ERR_NO_CONTENT, url.toDisplayString());
    case KGAPI2::QuotaExceeded:
        return Result::fail(KIO::ERR_DISK_FULL, url.toDisplayString());
    default:
        return Result::fail(KIO::ERR_WORKER_DEFINED, job.errorString());
    }

    return Result::fail(KIO::ERR_WORKER_DEFINED, i18n("Unknown error"));
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
    AboutFetchJob aboutFetch(getAccount(accountId));
    aboutFetch.setFields({
        About::Fields::Kind,
        About::Fields::QuotaBytesTotal,
        About::Fields::QuotaBytesUsedAggregate,
    });
    if (auto result = runJob(aboutFetch, url, accountId); result.success()) {
        const AboutPtr about = aboutFetch.aboutData();
        if (about) {
            setMetaData(QStringLiteral("total"), QString::number(about->quotaBytesTotal()));
            setMetaData(QStringLiteral("available"), QString::number(about->quotaBytesTotal() - about->quotaBytesUsedAggregate()));
            return KIO::WorkerResult::pass();
        }
    } else {
        return result;
    }

    return KIO::WorkerResult::fail();
}

AccountPtr KIOGDrive::getAccount(const QString &accountName)
{
    return m_accountManager->account(accountName);
}

KIO::UDSEntry KIOGDrive::fileToUDSEntry(const FilePtr &origFile, const QString &path) const
{
    KIO::UDSEntry entry;
    bool isFolder = false;

    FilePtr file = origFile;
    if (GDriveHelper::isGDocsDocument(file)) {
        GDriveHelper::convertFromGDocs(file);
    }

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, file->title());
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, file->title());
    entry.fastInsert(KIO::UDSEntry::UDS_COMMENT, file->description());

    if (file->isFolder()) {
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
        entry.fastInsert(KIO::UDSEntry::UDS_SIZE, 0);
        isFolder = true;
    } else {
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG);
        entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, file->mimeType());
        entry.fastInsert(KIO::UDSEntry::UDS_SIZE, file->fileSize());

        entry.fastInsert(KIO::UDSEntry::UDS_URL, fileToUrl(origFile, path).toString());
    }

    entry.fastInsert(KIO::UDSEntry::UDS_CREATION_TIME, file->createdDate().toSecsSinceEpoch());
    entry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, file->modifiedDate().toSecsSinceEpoch());
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS_TIME, file->lastViewedByMeDate().toSecsSinceEpoch());
    if (!file->ownerNames().isEmpty()) {
        entry.fastInsert(KIO::UDSEntry::UDS_USER, file->ownerNames().first());
    }

    if (!isFolder) {
        if (file->editable()) {
            entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        } else {
            entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IRGRP | S_IROTH);
        }
    } else {
        entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH);
    }

    entry.fastInsert(GDriveUDSEntryExtras::Id, file->id());
    entry.fastInsert(GDriveUDSEntryExtras::Url, file->alternateLink().toString());
    entry.fastInsert(GDriveUDSEntryExtras::Version, QString::number(file->version()));
    entry.fastInsert(GDriveUDSEntryExtras::Md5, file->md5Checksum());
    entry.fastInsert(GDriveUDSEntryExtras::LastModifyingUser, file->lastModifyingUserName());
    entry.fastInsert(GDriveUDSEntryExtras::Owners, file->ownerNames().join(QStringLiteral(", ")));
    if (file->sharedWithMeDate().isValid()) {
        entry.fastInsert(GDriveUDSEntryExtras::SharedWithMeDate, QLocale::system().toString(file->sharedWithMeDate(), QLocale::LongFormat));
    }

    return entry;
}

QUrl KIOGDrive::fileToUrl(const FilePtr &file, const QString &path) const
{
    QUrl url;
    url.setScheme(GDriveUrl::Scheme);
    url.setPath(path + QLatin1Char('/') + file->title());

    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("id"), file->id());
    url.setQuery(urlQuery);

    return url;
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

KIO::UDSEntry KIOGDrive::sharedDriveToUDSEntry(const DrivesPtr &sharedDrive)
{
    KIO::UDSEntry entry;

    qlonglong udsAccess = S_IRUSR | S_IXUSR | S_IRGRP;
    if (sharedDrive->capabilities()->canRenameDrive() || sharedDrive->capabilities()->canDeleteDrive()) {
        udsAccess |= S_IWUSR;
    }

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, sharedDrive->id());
    entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, sharedDrive->name());
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    entry.fastInsert(KIO::UDSEntry::UDS_SIZE, 0);
    entry.fastInsert(KIO::UDSEntry::UDS_CREATION_TIME, sharedDrive->createdDate().toSecsSinceEpoch());
    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, udsAccess);
    entry.fastInsert(KIO::UDSEntry::UDS_HIDDEN, sharedDrive->hidden());
    entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("onedrive"));

    return entry;
}

KIO::WorkerResult KIOGDrive::createAccount()
{
    const KGAPI2::AccountPtr account = m_accountManager->createAccount();
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
    const auto gdriveUrl = GDriveUrl(url);
    const QString accountId = gdriveUrl.account();
    DrivesDeleteJob sharedDriveDeleteJob(gdriveUrl.filename(), getAccount(accountId));
    return runJob(sharedDriveDeleteJob, url, accountId);
}

KIO::WorkerResult KIOGDrive::statSharedDrive(const QUrl &url)
{
    const auto gdriveUrl = GDriveUrl(url);
    const QString accountId = gdriveUrl.account();
    const auto account = getAccount(accountId);

    const QString sharedDriveId = m_cache.idForPath(url.path());
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
    // Not every user is allowed to create shared Drives,
    // check with About resource.
    bool canCreateDrives = false;
    AboutFetchJob aboutFetch(getAccount(accountId));
    aboutFetch.setFields({
        About::Fields::Kind,
        About::Fields::CanCreateDrives,
    });
    QEventLoop eventLoop;
    QObject::connect(&aboutFetch, &KGAPI2::Job::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    if (aboutFetch.error() == KGAPI2::OK || aboutFetch.error() == KGAPI2::NoError) {
        const AboutPtr about = aboutFetch.aboutData();
        if (about) {
            canCreateDrives = about->canCreateDrives();
        }
    }
    qCDebug(ONEDRIVE) << "Account" << accountId << (canCreateDrives ? "can" : "can't") << "create Shared Drives";

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

    QString parentId;
    if (!gdriveUrl.isSharedWithMeTopLevel()) {
        // Try to recursively resolve ID of parent path - either from cache, or by querying Google
        const auto [result, id] = resolveFileIdFromPath(gdriveUrl.parentPath(), KIOGDrive::PathIsFolder);

        if (!result.success()) {
            return {result, QString()};
        }
        parentId = id;

        if (parentId.isEmpty()) {
            // We failed to resolve parent -> error
            return {KIO::WorkerResult::pass(), QString()};
        }
        qCDebug(ONEDRIVE) << "Getting ID for" << gdriveUrl.filename() << "in parent with ID" << parentId;
    } else {
        qCDebug(ONEDRIVE) << "Getting ID for" << gdriveUrl.filename() << "(top-level shared-with-me file without a parentId)";
    }

    FileSearchQuery query;
    if (flags != KIOGDrive::None) {
        query.addQuery(FileSearchQuery::MimeType,
                       (flags & KIOGDrive::PathIsFolder ? FileSearchQuery::Equals : FileSearchQuery::NotEquals),
                       GDriveHelper::folderMimeType());
    }
    query.addQuery(FileSearchQuery::Title, FileSearchQuery::Equals, gdriveUrl.filename());
    if (!parentId.isEmpty()) {
        query.addQuery(FileSearchQuery::Parents, FileSearchQuery::In, parentId);
    }
    query.addQuery(FileSearchQuery::Trashed, FileSearchQuery::Equals, gdriveUrl.isTrashed());

    const QString accountId = gdriveUrl.account();
    FileFetchJob fetchJob(query, getAccount(accountId));
    fetchJob.setFields({File::Fields::Id, File::Fields::Title, File::Fields::Labels});
    if (auto result = runJob(fetchJob, url, accountId); !result.success()) {
        return {result, QString()};
    }

    const ObjectsList objects = fetchJob.items();
    if (objects.isEmpty()) {
        qCWarning(ONEDRIVE) << "Failed to resolve" << path;
        return {KIO::WorkerResult::pass(), QString()};
    }

    const FilePtr file = objects[0].dynamicCast<File>();

    m_cache.insertPath(path, file->id());

    qCDebug(ONEDRIVE) << "Resolved" << path << "to" << file->id() << "(from network)";
    return {KIO::WorkerResult::pass(), file->id()};
}

QString KIOGDrive::resolveSharedDriveId(const QString &idOrName, const QString &accountId)
{
    qCDebug(ONEDRIVE) << "Resolving shared drive id for" << idOrName;

    const auto idOrNamePath = GDriveUrl::buildSharedDrivePath(accountId, idOrName);
    QString fileId = m_cache.idForPath(idOrNamePath);
    if (!fileId.isEmpty()) {
        qCDebug(ONEDRIVE) << "Resolved shared drive id" << idOrName << "to" << fileId << "(from cache)";
        return fileId;
    }

    // We start by trying to fetch a shared drive with the filename as id
    DrivesFetchJob searchByIdJob(idOrName, getAccount(accountId));
    searchByIdJob.setFields({
        Drives::Fields::Kind,
        Drives::Fields::Id,
        Drives::Fields::Name,
    });
    QEventLoop eventLoop;
    QObject::connect(&searchByIdJob, &KGAPI2::Job::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    if (searchByIdJob.error() == KGAPI2::OK || searchByIdJob.error() == KGAPI2::NoError) {
        // A Shared Drive with that id exists so we return it
        const auto objects = searchByIdJob.items();
        const DrivesPtr sharedDrive = objects.at(0).dynamicCast<Drives>();
        fileId = sharedDrive->id();
        qCDebug(ONEDRIVE) << "Resolved shared drive id" << idOrName << "to" << fileId;

        const auto idPath = idOrNamePath;
        const auto namePath = GDriveUrl::buildSharedDrivePath(accountId, sharedDrive->name());
        m_cache.insertPath(idPath, fileId);
        m_cache.insertPath(namePath, fileId);

        return fileId;
    }

    // The gdriveUrl's filename is not a shared drive id, we must
    // search for a shared drive with the filename name.
    // Unfortunately searching by name is only allowed for admin
    // accounts (i.e. useDomainAdminAccess=true) so we retrieve all
    // shared drives and search by name here
    DrivesFetchJob sharedDrivesFetchJob(getAccount(accountId));
    sharedDrivesFetchJob.setFields({
        Drives::Fields::Kind,
        Drives::Fields::Id,
        Drives::Fields::Name,
    });
    QObject::connect(&sharedDrivesFetchJob, &KGAPI2::Job::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
    if (sharedDrivesFetchJob.error() == KGAPI2::OK || sharedDrivesFetchJob.error() == KGAPI2::NoError) {
        const auto objects = sharedDrivesFetchJob.items();
        for (const auto &object : objects) {
            const DrivesPtr sharedDrive = object.dynamicCast<Drives>();

            // If we have one or more hits we will take the first as good because we
            // don't have any other measures for picking the correct drive
            if (sharedDrive->name() == idOrName) {
                fileId = sharedDrive->id();
                qCDebug(ONEDRIVE) << "Resolved shared drive id" << idOrName << "to" << fileId;

                const auto idPath = GDriveUrl::buildSharedDrivePath(accountId, fileId);
                const auto namePath = idOrNamePath;
                m_cache.insertPath(idPath, fileId);
                m_cache.insertPath(namePath, fileId);

                return fileId;
            }
        }
    }

    // We couldn't find any shared drive with that id or name
    qCDebug(ONEDRIVE) << "Failed resolving shared drive" << idOrName << "(couldn't find drive with that id or name)";
    return QString();
}

std::pair<KIO::WorkerResult, QString> KIOGDrive::rootFolderId(const QString &accountId)
{
    auto it = m_rootIds.constFind(accountId);
    if (it == m_rootIds.cend()) {
        qCDebug(ONEDRIVE) << "Getting root ID for" << accountId;
        AboutFetchJob aboutFetch(getAccount(accountId));
        aboutFetch.setFields({About::Fields::Kind, About::Fields::RootFolderId});
        QUrl url;
        if (auto result = runJob(aboutFetch, url, accountId); !result.success()) {
            return {result, QString()};
        }

        const AboutPtr about = aboutFetch.aboutData();
        if (!about || about->rootFolderId().isEmpty()) {
            qCWarning(ONEDRIVE) << "Failed to obtain root ID";
            return {KIO::WorkerResult::pass(), QString()};
        }

        auto v = m_rootIds.insert(accountId, about->rootFolderId());
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

KIO::WorkerResult KIOGDrive::listAccountRoot(const QUrl &url, const QString &accountId, const KGAPI2::AccountPtr &account)
{
    auto sharedDrivesEntry = fetchSharedDrivesRootEntry(accountId);
    listEntry(sharedDrivesEntry);

    auto sharedWithMeEntry = sharedWithMeUDSEntry();
    listEntry(sharedWithMeEntry);

    return listFolderByPath(url, accountId, account, QString());
}

KIO::WorkerResult KIOGDrive::listFolderByPath(const QUrl &url, const QString &accountId, const KGAPI2::AccountPtr &account, const QString &relativePath)
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

    if (gdriveUrl.isSharedWithMeRoot()) {
        const auto sharedItems = m_graphClient.listSharedWithMe(account->accessToken());
        if (!sharedItems.success) {
            qCWarning(ONEDRIVE) << "Graph sharedWithMe failed for" << accountId << sharedItems.httpStatus << sharedItems.errorMessage;
            if (sharedItems.httpStatus == 401 || sharedItems.httpStatus == 403) {
                return KIO::WorkerResult::fail(KIO::ERR_CANNOT_LOGIN, url.toDisplayString());
            }
            return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, sharedItems.errorMessage);
        }

        const QString pathPrefix = url.path().endsWith(QLatin1Char('/')) ? url.path() : url.path() + QLatin1Char('/');
        for (const auto &item : sharedItems.items) {
            const KIO::UDSEntry entry = driveItemToEntry(item);
            listEntry(entry);
            m_cache.insertPath(pathPrefix + item.name, QStringLiteral("%1|%2").arg(item.remoteDriveId, item.remoteItemId));
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
        const QString remoteKey = m_cache.idForPath(url.path());
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
        qCDebug(ONEDRIVE) << "Directory is shared drive, creating that instead" << url;
        return createSharedDrive(url);
    }

    QString parentId;
    if (gdriveUrl.isTopLevel()) {
        const auto [result, id] = rootFolderId(accountId);
        if (!result.success()) {
            return result;
        }
        parentId = id;

    } else {
        const auto [result, id] = resolveFileIdFromPath(gdriveUrl.parentPath(), KIOGDrive::PathIsFolder);
        if (!result.success()) {
            return result;
        }
        parentId = id;
    }

    if (parentId.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    FilePtr file(new File());
    file->setTitle(gdriveUrl.filename());
    file->setMimeType(File::folderMimeType());

    ParentReferencePtr parent(new ParentReference(parentId));
    file->setParents(ParentReferencesList() << parent);

    FileCreateJob createJob(file, getAccount(accountId));
    return runJob(createJob, url, accountId);
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
    if (gdriveUrl.isSharedWithMe()) {
        const QString remoteKey = m_cache.idForPath(url.path());
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

    const QUrlQuery urlQuery(url);
    QString fileId;

    if (urlQuery.hasQueryItem(QStringLiteral("id"))) {
        fileId = urlQuery.queryItemValue(QStringLiteral("id"));
    } else {
        const auto [result, id] = resolveFileIdFromPath(url.adjusted(QUrl::StripTrailingSlash).path(), KIOGDrive::None);

        if (!result.success()) {
            return result;
        }
        fileId = id;
    }

    if (fileId.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    FileFetchJob fileFetchJob(fileId, getAccount(accountId));
    if (auto result = runJob(fileFetchJob, url, accountId); !result.success()) {
        qCDebug(ONEDRIVE) << "Failed stat()ing file" << fileFetchJob.errorString();
        return result;
    }

    const ObjectsList objects = fileFetchJob.items();
    if (objects.count() != 1) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    const FilePtr file = objects.first().dynamicCast<File>();
    if (file->labels()->trashed()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    const KIO::UDSEntry entry = fileToUDSEntry(file, gdriveUrl.parentPath());

    statEntry(entry);
    return KIO::WorkerResult::pass();
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

    if (gdriveUrl.isSharedWithMe()) {
        const QString remoteKey = m_cache.idForPath(url.path());
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

        const auto downloadResult = m_graphClient.downloadItem(account->accessToken(), graphItem.item.id, graphItem.item.downloadUrl);
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

        const auto downloadResult = m_graphClient.downloadItem(account->accessToken(), graphItem.item.id, graphItem.item.downloadUrl);
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

    const QUrlQuery urlQuery(url);
    QString fileId;

    if (urlQuery.hasQueryItem(QStringLiteral("id"))) {
        fileId = urlQuery.queryItemValue(QStringLiteral("id"));
    } else {
        const auto [result, id] = resolveFileIdFromPath(url.adjusted(QUrl::StripTrailingSlash).path(), KIOGDrive::PathIsFile);

        if (!result.success()) {
            return result;
        }
        fileId = id;
    }
    if (fileId.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    FileFetchJob fileFetchJob(fileId, getAccount(accountId));
    fileFetchJob.setFields({File::Fields::Id, File::Fields::MimeType, File::Fields::ExportLinks, File::Fields::DownloadUrl});
    if (auto result = runJob(fileFetchJob, url, accountId); !result.success()) {
        return result;
    }

    const ObjectsList objects = fileFetchJob.items();
    if (objects.count() != 1) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.fileName());
    }

    FilePtr file = objects.first().dynamicCast<File>();
    QUrl downloadUrl;
    if (GDriveHelper::isGDocsDocument(file)) {
        downloadUrl = GDriveHelper::convertFromGDocs(file);
    } else {
        downloadUrl = GDriveHelper::downloadUrl(file);
    }

    mimeType(file->mimeType());

    FileFetchContentJob contentJob(downloadUrl, getAccount(accountId));
    QObject::connect(&contentJob, &KGAPI2::Job::progress, [this](KGAPI2::Job *, int processed, int total) {
        processedSize(processed);
        totalSize(total);
    });
    if (auto result = runJob(contentJob, url, accountId); !result.success()) {
        return result;
    }

    QByteArray contentData = contentJob.data();

    processedSize(contentData.size());
    totalSize(contentData.size());

    // data() has a maximum transfer size of 14 MiB so we need to send it in chunks.
    // See TransferJob::slotDataReq.
    int transferred = 0;
    // do-while loop to call data() even for empty files.
    do {
        const size_t nextChunk = qMin(contentData.size() - transferred, 14 * 1024 * 1024);
        data(QByteArray::fromRawData(contentData.constData() + transferred, nextChunk));
        transferred += nextChunk;
    } while (transferred < contentData.size());
    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::readPutData(QTemporaryFile &tempFile, FilePtr &fileMetaData)
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

    const QMimeType mime = QMimeDatabase().mimeTypeForFileNameAndData(fileMetaData->title(), &tempFile);
    fileMetaData->setMimeType(mime.name());

    tempFile.close();

    if (result == -1) {
        qCWarning(ONEDRIVE) << "Could not read source file" << tempFile.fileName();
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, QString());
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::runJob(KGAPI2::Job &job, const QUrl &url, const QString &accountId)
{
    auto account = getAccount(accountId);
    if (account->accessToken().isEmpty()) {
        qCWarning(ONEDRIVE) << "Expired or missing access/refresh token for account" << accountId;
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("Expired or missing access tokens for account %1", accountId));
    }

    Q_FOREVER {
        qCDebug(ONEDRIVE) << "Running job" << (&job) << "with accessToken" << GDriveHelper::elideToken(job.account()->accessToken());
        QEventLoop eventLoop;
        QObject::connect(&job, &KGAPI2::Job::finished, &eventLoop, &QEventLoop::quit);
        eventLoop.exec();
        Result result = handleError(job, url);
        if (result.action == KIOGDrive::Success) {
            break;
        } else if (result.action == KIOGDrive::Fail) {
            return KIO::WorkerResult::fail(result.error, result.errorString);
        }
        job.setAccount(account);
        job.restart();
    };

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::putUpdate(const QUrl &url)
{
    const QString fileId = QUrlQuery(url).queryItemValue(QStringLiteral("id"));
    qCDebug(ONEDRIVE) << Q_FUNC_INFO << url << fileId;

    const auto gdriveUrl = GDriveUrl(url);
    const auto accountId = gdriveUrl.account();

    FileFetchJob fetchJob(fileId, getAccount(accountId));
    if (auto result = runJob(fetchJob, url, accountId); !result.success()) {
        return result;
    }

    const ObjectsList objects = fetchJob.items();
    if (objects.size() != 1) {
        return putCreate(url);
    }

    FilePtr file = objects[0].dynamicCast<File>();

    QTemporaryFile tmpFile;
    if (auto result = readPutData(tmpFile, file); !result.success()) {
        return result;
    }

    FileModifyJob modifyJob(tmpFile.fileName(), file, getAccount(accountId));
    modifyJob.setUpdateModifiedDate(true);
    if (auto result = runJob(modifyJob, url, accountId); !result.success()) {
        return result;
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KIOGDrive::putCreate(const QUrl &url)
{
    qCDebug(ONEDRIVE) << Q_FUNC_INFO << url;
    ParentReferencesList parentReferences;

    const auto gdriveUrl = GDriveUrl(url);
    if (gdriveUrl.isRoot() || gdriveUrl.isAccountRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED, url.path());
    }

    if (!gdriveUrl.isTopLevel()) {
        // Not creating in root directory, fill parent references
        QString parentId;

        const auto [result, id] = resolveFileIdFromPath(gdriveUrl.parentPath());

        if (!result.success()) {
            return result;
        }
        parentId = id;

        if (parentId.isEmpty()) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).path());
        }
        parentReferences << ParentReferencePtr(new ParentReference(parentId));
    }

    FilePtr file(new File);
    file->setTitle(gdriveUrl.filename());
    file->setParents(parentReferences);
    /*
    if (hasMetaData(QLatin1String("modified"))) {
        const QString modified = metaData(QLatin1String("modified"));
        qCDebug(ONEDRIVE) << modified;
        file->setModifiedDate(KDateTime::fromString(modified, KDateTime::ISODate));
    }
    */

    QTemporaryFile tmpFile;
    if (auto result = readPutData(tmpFile, file); !result.success()) {
        return result;
    }

    const auto accountId = gdriveUrl.account();
    FileCreateJob createJob(tmpFile.fileName(), file, getAccount(accountId));
    if (auto result = runJob(createJob, url, accountId); !result.success()) {
        return result;
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

    if (QUrlQuery(url).hasQueryItem(QStringLiteral("id"))) {
        if (auto result = putUpdate(url); !result.success()) {
            return result;
        }
    } else {
        if (auto result = putCreate(url); !result.success()) {
            return result;
        }
    }

    // FIXME: Update the cache now!

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

    const QUrlQuery urlQuery(src);
    QString sourceFileId;

    if (urlQuery.hasQueryItem(QStringLiteral("id"))) {
        sourceFileId = urlQuery.queryItemValue(QStringLiteral("id"));
    } else {
        const auto [result, id] = resolveFileIdFromPath(src.adjusted(QUrl::StripTrailingSlash).path());

        if (!result.success()) {
            return result;
        }
        sourceFileId = id;
    }

    if (sourceFileId.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
    }
    FileFetchJob sourceFileFetchJob(sourceFileId, getAccount(sourceAccountId));
    sourceFileFetchJob.setFields({File::Fields::Id, File::Fields::ModifiedDate, File::Fields::LastViewedByMeDate, File::Fields::Description});
    if (auto result = runJob(sourceFileFetchJob, src, sourceAccountId); !result.success()) {
        return result;
    }

    const ObjectsList objects = sourceFileFetchJob.items();
    if (objects.count() != 1) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
    }

    const FilePtr sourceFile = objects[0].dynamicCast<File>();

    ParentReferencesList destParentReferences;
    if (destGDriveUrl.isRoot()) {
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED, dest.path());
    }

    QString destDirId;
    if (destGDriveUrl.isTopLevel()) {
        const auto [result, id] = rootFolderId(destAccountId);

        if (!result.success()) {
            return result;
        }
        destDirId = id;
    } else {
        const auto [result, id] = resolveFileIdFromPath(destGDriveUrl.parentPath(), KIOGDrive::PathIsFolder);

        if (!result.success()) {
            return result;
        }
        destDirId = id;
    }
    destParentReferences << ParentReferencePtr(new ParentReference(destDirId));

    FilePtr destFile(new File);
    destFile->setTitle(destGDriveUrl.filename());
    destFile->setModifiedDate(sourceFile->modifiedDate());
    destFile->setLastViewedByMeDate(sourceFile->lastViewedByMeDate());
    destFile->setDescription(sourceFile->description());
    destFile->setParents(destParentReferences);

    FileCopyJob copyJob(sourceFile, destFile, getAccount(sourceAccountId));
    return runJob(copyJob, dest, sourceAccountId);
}

KIO::WorkerResult KIOGDrive::del(const QUrl &url, bool isfile)
{
    // FIXME: Verify that a single file cannot actually have multiple parent
    // references. If it can, then we need to be more careful: currently this
    // implementation will simply remove the file from all it's parents but
    // it actually should just remove the current parent reference

    // FIXME: Because of the above, we are not really deleting the file, but only
    // moving it to trash - so if users really really really wants to delete the
    // file, they have to go to GDrive web interface and delete it there. I think
    // that we should do the DELETE operation here, because for trash people have
    // their local trashes. This however requires fixing the first FIXME first,
    // otherwise we are risking severe data loss.

    const auto gdriveUrl = GDriveUrl(url);

    // Trying to delete the Team Drive root is pointless
    if (gdriveUrl.isSharedDrivesRoot()) {
        qCDebug(ONEDRIVE) << "Tried deleting Shared Drives root.";
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("Can't delete Shared Drives root."));
    }

    qCDebug(ONEDRIVE) << "Deleting URL" << url << "- is it a file?" << isfile;

    const QUrlQuery urlQuery(url);
    QString fileId;

    if (isfile && urlQuery.hasQueryItem(QStringLiteral("id"))) {
        fileId = urlQuery.queryItemValue(QStringLiteral("id"));
    } else {
        const auto [result, id] =
            resolveFileIdFromPath(url.adjusted(QUrl::StripTrailingSlash).path(), isfile ? KIOGDrive::PathIsFile : KIOGDrive::PathIsFolder);
        if (!result.success()) {
            return result;
        }
        fileId = id;
    }

    if (fileId.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }
    const QString accountId = gdriveUrl.account();

    // If user tries to delete the account folder, remove the account from the keychain
    if (gdriveUrl.isAccountRoot()) {
        const KGAPI2::AccountPtr account = getAccount(accountId);
        if (account->accountName().isEmpty()) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, accountId);
        }
        m_accountManager->removeAccount(accountId);
        return KIO::WorkerResult::pass();
    }

    if (gdriveUrl.isSharedDrive()) {
        qCDebug(ONEDRIVE) << "Deleting Shared Drive" << url;
        return deleteSharedDrive(url);
    }

    // GDrive allows us to delete entire directory even when it's not empty,
    // so we need to emulate the normal behavior ourselves by checking number of
    // child references
    if (!isfile) {
        ChildReferenceFetchJob referencesFetch(fileId, getAccount(accountId));
        if (auto result = runJob(referencesFetch, url, accountId); !result.success()) {
            return result;
        }
        const bool isEmpty = !referencesFetch.items().count();

        if (!isEmpty && metaData(QStringLiteral("recurse")) != QLatin1String("true")) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_RMDIR, url.path());
        }
    }

    FileTrashJob trashJob(fileId, getAccount(accountId));

    auto result = runJob(trashJob, url, accountId);
    m_cache.removePath(url.path());
    return result;
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

    const QUrlQuery urlQuery(src);
    QString sourceFileId;

    if (urlQuery.hasQueryItem(QStringLiteral("id"))) {
        sourceFileId = urlQuery.queryItemValue(QStringLiteral("id"));
    } else {
        const auto [result, id] = resolveFileIdFromPath(src.adjusted(QUrl::StripTrailingSlash).path(), KIOGDrive::PathIsFile);
        if (!result.success()) {
            return result;
        }
        sourceFileId = id;
    }

    if (sourceFileId.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
    }

    if (srcGDriveUrl.isSharedDrive()) {
        qCDebug(ONEDRIVE) << "Renaming Shared Drive" << srcGDriveUrl.filename() << "to" << destGDriveUrl.filename();
        DrivesPtr drives = DrivesPtr::create();
        drives->setId(sourceFileId);
        drives->setName(destGDriveUrl.filename());

        DrivesModifyJob modifyJob(drives, getAccount(sourceAccountId));
        if (auto result = runJob(modifyJob, src, sourceAccountId); !result.success()) {
            return result;
        }

        return KIO::WorkerResult::pass();
    }

    // We need to fetch ALL, so that we can do update later
    FileFetchJob sourceFileFetchJob(sourceFileId, getAccount(sourceAccountId));
    if (auto result = runJob(sourceFileFetchJob, src, sourceAccountId); !result.success()) {
        return result;
    }

    const ObjectsList objects = sourceFileFetchJob.items();
    if (objects.count() != 1) {
        qCDebug(ONEDRIVE) << "FileFetchJob retrieved" << objects.count() << "items, while only one was expected.";
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
    }

    const FilePtr sourceFile = objects[0].dynamicCast<File>();

    ParentReferencesList parentReferences = sourceFile->parents();
    if (destGDriveUrl.isRoot()) {
        // user is trying to move to top-level onedrive:///
        return KIO::WorkerResult::fail(KIO::ERR_ACCESS_DENIED, dest.fileName());
    }
    if (destGDriveUrl.isAccountRoot()) {
        // user is trying to move to root -> we are only renaming
    } else {
        // skip filename and extract the second-to-last component
        const auto [destDirResult, destDirId] = resolveFileIdFromPath(destGDriveUrl.parentPath(), KIOGDrive::PathIsFolder);

        if (!destDirResult.success()) {
            return destDirResult;
        }

        const auto [srcDirResult, srcDirId] = resolveFileIdFromPath(srcGDriveUrl.parentPath(), KIOGDrive::PathIsFolder);

        if (!srcDirResult.success()) {
            return srcDirResult;
        }

        // Remove source from parent references
        auto iter = parentReferences.begin();
        bool removed = false;
        while (iter != parentReferences.end()) {
            const ParentReferencePtr ref = *iter;
            if (ref->id() == srcDirId) {
                parentReferences.erase(iter);
                removed = true;
                break;
            }
            ++iter;
        }
        if (!removed) {
            qCDebug(ONEDRIVE) << "Could not remove" << src << "from parent references.";
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, src.path());
        }

        // Add destination to parent references
        parentReferences << ParentReferencePtr(new ParentReference(destDirId));
    }

    FilePtr destFile(sourceFile);
    destFile->setTitle(destGDriveUrl.filename());
    destFile->setParents(parentReferences);

    FileModifyJob modifyJob(destFile, getAccount(sourceAccountId));
    modifyJob.setUpdateModifiedDate(true);
    return runJob(modifyJob, dest, sourceAccountId);
}

KIO::WorkerResult KIOGDrive::mimetype(const QUrl &url)
{
    qCDebug(ONEDRIVE) << Q_FUNC_INFO << url;

    const QUrlQuery urlQuery(url);
    QString fileId;

    if (urlQuery.hasQueryItem(QStringLiteral("id"))) {
        fileId = urlQuery.queryItemValue(QStringLiteral("id"));
    } else {
        const auto [result, id] = resolveFileIdFromPath(url.adjusted(QUrl::StripTrailingSlash).path());

        if (!result.success()) {
            return result;
        }
        fileId = id;
    }

    if (fileId.isEmpty()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }
    const QString accountId = GDriveUrl(url).account();

    FileFetchJob fileFetchJob(fileId, getAccount(accountId));
    fileFetchJob.setFields({File::Fields::Id, File::Fields::MimeType});
    if (auto result = runJob(fileFetchJob, url, accountId); !result.success()) {
        return result;
    }

    const ObjectsList objects = fileFetchJob.items();
    if (objects.count() != 1) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

    const FilePtr file = objects.first().dynamicCast<File>();
    mimeType(file->mimeType());
    return KIO::WorkerResult::pass();
}

#include "kio_gdrive.moc"
