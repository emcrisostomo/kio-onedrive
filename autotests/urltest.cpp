/*
 * SPDX-FileCopyrightText: 2016 Elvis Angelaccio <elvis.angelaccio@kde.org>
 * SPDX-FileCopyrightText: 2019 David Barchiesi <david@barchie.si>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "../src/onedriveurl.h"

#include <QTest>

class UrlTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testOneDriveUrl_data();
    void testOneDriveUrl();
};

QTEST_GUILESS_MAIN(UrlTest)

void UrlTest::testOneDriveUrl_data()
{
    const auto gdriveUrl = [](const QString &path) {
        QUrl url;
        url.setScheme(OneDriveUrl::Scheme);
        url.setPath(path);
        return url;
    };

    QTest::addColumn<QUrl>("url");
    QTest::addColumn<QString>("expectedToString");
    QTest::addColumn<QString>("expectedAccount");
    QTest::addColumn<QString>("expectedParentPath");
    QTest::addColumn<bool>("expectedIsTrashed");
    QTest::addColumn<bool>("expectedIsTopLevel");
    QTest::addColumn<bool>("expectedIsRoot");
    QTest::addColumn<bool>("expectedIsAccountRoot");
    QTest::addColumn<bool>("expectedIsSharedWithMeRoot");
    QTest::addColumn<bool>("expectedIsSharedWithMeTopLevel");
    QTest::addColumn<bool>("expectedIsSharedWithMe");
    QTest::addColumn<bool>("expectedIsSharedDrivesRoot");
    QTest::addColumn<bool>("expectedIsSharedDrive");
    QTest::addColumn<bool>("expectedIsNewAccountPath");
    QTest::addColumn<bool>("expectedIsTrashDir");
    QTest::addColumn<QStringList>("expectedPathComponents");
    QTest::addColumn<QString>("expectedFilename");

    // clang-format off
    QTest::newRow("root url")
            << gdriveUrl(QStringLiteral("/"))
            << QStringLiteral("onedrive:/")
            << QString()
            << QString()
            << false // expectedIsTrashed
            << false // expectedIsTopLevel
            << true  // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << false // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList()
            << "";

    QTest::newRow("account root url")
            << gdriveUrl(QStringLiteral("/foo@gmail.com"))
            << QStringLiteral("onedrive:/foo@gmail.com")
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/")
            << false // expectedIsTrashed
            << false // expectedIsTopLevel
            << false // expectedIsRoot
            << true  // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << false // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com")}
            << QStringLiteral("foo@gmail.com");

    QTest::newRow("account trash url")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/") + OneDriveUrl::TrashDir)
            << QStringLiteral("onedrive:/foo@gmail.com/") + OneDriveUrl::TrashDir
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com")
            << false // expectedIsTrashed
            << true  // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << false // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << true  // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), OneDriveUrl::TrashDir}
            << OneDriveUrl::TrashDir;

    QTest::newRow("file in trash")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/") + OneDriveUrl::TrashDir + QStringLiteral("/baz.txt"))
            << QStringLiteral("onedrive:/foo@gmail.com/") + OneDriveUrl::TrashDir + QStringLiteral("/baz.txt")
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com/") + OneDriveUrl::TrashDir
            << true  // expectedIsTrashed
            << false // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << false // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), OneDriveUrl::TrashDir, QStringLiteral("baz.txt")}
            << QStringLiteral("baz.txt");

    QTest::newRow("account shared drives url")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/") + OneDriveUrl::SharedDrivesDir)
            << QStringLiteral("onedrive:/foo@gmail.com/") + OneDriveUrl::SharedDrivesDir
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com")
            << false // expectedIsTrashed
            << true  // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << false // expectedIsSharedWithMe
            << true  // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), OneDriveUrl::SharedDrivesDir}
            << OneDriveUrl::SharedDrivesDir;

    QTest::newRow("file in account root")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/bar.txt"))
            << QStringLiteral("onedrive:/foo@gmail.com/bar.txt")
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com")
            << false // expectedIsTrashed
            << true  // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << false // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), QStringLiteral("bar.txt")}
            << QStringLiteral("bar.txt");

    QTest::newRow("folder in account root - no trailing slash")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/bar"))
            << QStringLiteral("onedrive:/foo@gmail.com/bar")
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com")
            << false // expectedIsTrashed
            << true  // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << false // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), QStringLiteral("bar")}
            << QStringLiteral("bar");

    QTest::newRow("folder in account root - trailing slash")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/bar/"))
            << QStringLiteral("onedrive:/foo@gmail.com/bar/")
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com")
            << false // expectedIsTrashed
            << true  // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << false // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), QStringLiteral("bar")}
            << QStringLiteral("bar");

    QTest::newRow("file in subfolder")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/bar/baz.txt"))
            << QStringLiteral("onedrive:/foo@gmail.com/bar/baz.txt")
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com/bar")
            << false // expectedIsTrashed
            << false // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << false // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), QStringLiteral("bar"), QStringLiteral("baz.txt")}
            << QStringLiteral("baz.txt");

    QTest::newRow("account shared with me root")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/") + OneDriveUrl::SharedWithMeDir)
            << QStringLiteral("onedrive:/foo@gmail.com/") + OneDriveUrl::SharedWithMeDir
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com")
            << false // expectedIsTrashed
            << true  // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << true  // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << false // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), OneDriveUrl::SharedWithMeDir}
            << OneDriveUrl::SharedWithMeDir;

    QTest::newRow("shared with me top-level file")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/%1/baz.txt").arg(OneDriveUrl::SharedWithMeDir))
            << QStringLiteral("onedrive:/foo@gmail.com/%1/baz.txt").arg(OneDriveUrl::SharedWithMeDir)
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com/") + OneDriveUrl::SharedWithMeDir
            << false // expectedIsTrashed
            << false // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << true  // expectedIsSharedWithMeTopLevel
            << true  // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), OneDriveUrl::SharedWithMeDir, QStringLiteral("baz.txt")}
            << QStringLiteral("baz.txt");

    QTest::newRow("shared with me top-level folder")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/%1/bar/").arg(OneDriveUrl::SharedWithMeDir))
            << QStringLiteral("onedrive:/foo@gmail.com/%1/bar/").arg(OneDriveUrl::SharedWithMeDir)
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com/") + OneDriveUrl::SharedWithMeDir
            << false // expectedIsTrashed
            << false // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << true  // expectedIsSharedWithMeTopLevel
            << true  // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), OneDriveUrl::SharedWithMeDir, QStringLiteral("bar")}
            << QStringLiteral("bar");

    QTest::newRow("shared with me inner file")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/%1/bar/baz.txt").arg(OneDriveUrl::SharedWithMeDir))
            << QStringLiteral("onedrive:/foo@gmail.com/%1/bar/baz.txt").arg(OneDriveUrl::SharedWithMeDir)
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com/") + OneDriveUrl::SharedWithMeDir + QStringLiteral("/bar")
            << false // expectedIsTrashed
            << false // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << true  // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), OneDriveUrl::SharedWithMeDir, QStringLiteral("bar"), QStringLiteral("baz.txt")}
            << QStringLiteral("baz.txt");

    QTest::newRow("shared with me inner folder")
            << gdriveUrl(QStringLiteral("/foo@gmail.com/%1/bar/baz/").arg(OneDriveUrl::SharedWithMeDir))
            << QStringLiteral("onedrive:/foo@gmail.com/%1/bar/baz/").arg(OneDriveUrl::SharedWithMeDir)
            << QStringLiteral("foo@gmail.com")
            << QStringLiteral("/foo@gmail.com/") + OneDriveUrl::SharedWithMeDir + QStringLiteral("/bar")
            << false // expectedIsTrashed
            << false // expectedIsTopLevel
            << false // expectedIsRoot
            << false // expectedIsAccountRoot
            << false // expectedIsSharedWithMeRoot
            << false // expectedIsSharedWithMeTopLevel
            << true  // expectedIsSharedWithMe
            << false // expectedIsSharedDrivesRoot
            << false // expectedIsSharedDrive
            << false // expectedIsNewAccountPath
            << false // expectedIsTrashDir
            << QStringList {QStringLiteral("foo@gmail.com"), OneDriveUrl::SharedWithMeDir, QStringLiteral("bar"), QStringLiteral("baz")}
            << QStringLiteral("baz");
    // clang-format on
}

void UrlTest::testOneDriveUrl()
{
    QFETCH(QUrl, url);
    const auto gdriveUrl = OneDriveUrl(url);

    QFETCH(QString, expectedToString);
    QCOMPARE(gdriveUrl.url(), QUrl(expectedToString));

    QFETCH(QString, expectedAccount);
    QFETCH(QString, expectedParentPath);
    QFETCH(bool, expectedIsTrashed);
    QFETCH(bool, expectedIsTopLevel);
    QFETCH(bool, expectedIsRoot);
    QFETCH(bool, expectedIsAccountRoot);
    QFETCH(bool, expectedIsSharedWithMeRoot);
    QFETCH(bool, expectedIsSharedWithMeTopLevel);
    QFETCH(bool, expectedIsSharedWithMe);
    QFETCH(bool, expectedIsSharedDrivesRoot);
    QFETCH(bool, expectedIsSharedDrive);
    QFETCH(bool, expectedIsNewAccountPath);
    QFETCH(bool, expectedIsTrashDir);
    QFETCH(QStringList, expectedPathComponents);
    QFETCH(QString, expectedFilename);

    QCOMPARE(gdriveUrl.account(), expectedAccount);
    QCOMPARE(gdriveUrl.parentPath(), expectedParentPath);
    QCOMPARE(gdriveUrl.pathComponents(), expectedPathComponents);
    QCOMPARE(gdriveUrl.isTrashed(), expectedIsTrashed);
    QCOMPARE(gdriveUrl.isTopLevel(), expectedIsTopLevel);
    QCOMPARE(gdriveUrl.isRoot(), expectedIsRoot);
    QCOMPARE(gdriveUrl.isAccountRoot(), expectedIsAccountRoot);
    QCOMPARE(gdriveUrl.isSharedWithMeRoot(), expectedIsSharedWithMeRoot);
    QCOMPARE(gdriveUrl.isSharedWithMeTopLevel(), expectedIsSharedWithMeTopLevel);
    QCOMPARE(gdriveUrl.isSharedWithMe(), expectedIsSharedWithMe);
    QCOMPARE(gdriveUrl.isSharedDrivesRoot(), expectedIsSharedDrivesRoot);
    QCOMPARE(gdriveUrl.isSharedDrive(), expectedIsSharedDrive);
    QCOMPARE(gdriveUrl.isNewAccountPath(), expectedIsNewAccountPath);
    QCOMPARE(gdriveUrl.isTrashDir(), expectedIsTrashDir);
    QCOMPARE(gdriveUrl.filename(), expectedFilename);

    if (expectedPathComponents.isEmpty()) {
        QVERIFY(gdriveUrl.isRoot());
    } else if (expectedPathComponents.count() == 1) {
        QVERIFY(gdriveUrl.isAccountRoot());
    }
}

#include "urltest.moc"
