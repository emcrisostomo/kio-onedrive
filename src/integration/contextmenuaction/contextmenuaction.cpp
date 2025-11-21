/*
 * SPDX-FileCopyrightText: 2020 David Barchiesi <david@barchie.si>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "contextmenuaction.h"
#include "../../onedriveudsentry.h"

#include <QAction>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMenu>

#include <KFileItemListProperties>
#include <KLocalizedString>
#include <KPluginFactory>

K_PLUGIN_CLASS_WITH_JSON(OneDriveContextMenuAction, "contextmenuaction.json")

OneDriveContextMenuAction::OneDriveContextMenuAction(QObject *parent, const QVariantList &)
    : KAbstractFileItemActionPlugin(parent)
{
}

QList<QAction *> OneDriveContextMenuAction::actions(const KFileItemListProperties &fileItemInfos, QWidget *parentWidget)
{
    // Ignore if more than one file is selected
    if (fileItemInfos.items().size() != 1) {
        return {};
    }

    const KFileItem item = fileItemInfos.items().at(0);

    // Ignore if not a OneDrive url
    if (item.url().scheme() != QLatin1String("onedrive")) {
        return {};
    }

    const KIO::UDSEntry entry = item.entry();
    const QString oneDriveLink = entry.stringValue(OneDriveUDSEntryExtras::Url);
    // Ignore if missing a shareable link
    if (oneDriveLink.isEmpty()) {
        return {};
    }

    QMenu *menu = new QMenu(parentWidget);
    menu->addAction(createOpenUrlAction(parentWidget, oneDriveLink));
    menu->addAction(createCopyUrlAction(parentWidget, oneDriveLink));

    QAction *menuAction = new QAction(i18n("Microsoft OneDrive"), parentWidget);
    menuAction->setMenu(menu);
    menuAction->setIcon(QIcon::fromTheme(QStringLiteral("im-msn")));

    return {menuAction};
}

QAction *OneDriveContextMenuAction::createCopyUrlAction(QWidget *parent, const QString &onedriveLink)
{
    const QString name = i18n("Copy URL to clipboard");
    const QIcon icon = QIcon::fromTheme(QStringLiteral("edit-copy"));
    QAction *action = new QAction(icon, name, parent);

    connect(action, &QAction::triggered, this, [onedriveLink]() {
        QGuiApplication::clipboard()->setText(onedriveLink);
    });

    return action;
}

QAction *OneDriveContextMenuAction::createOpenUrlAction(QWidget *parent, const QString &onedriveLink)
{
    const QString name = i18n("Open in browser");
    const QIcon icon = QIcon::fromTheme(QStringLiteral("internet-services"));
    QAction *action = new QAction(icon, name, parent);

    connect(action, &QAction::triggered, this, [onedriveLink]() {
        QDesktopServices::openUrl(QUrl(onedriveLink));
    });

    return action;
}

#include "contextmenuaction.moc"
