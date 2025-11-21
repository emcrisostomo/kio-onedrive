/*
 * SPDX-FileCopyrightText: 2017 Elvis Angelaccio <elvis.angelaccio@kde.org>
 * SPDX-FileCopyrightText: 2025 Enrico M. Crisostomo <enrico.crisostomo@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "kaccountsmanager.h"
#include "onedriveaccount.h"
#include "onedrivedebug.h"

#include <Accounts/Manager>
#include <Accounts/Provider>
#include <KAccounts/Core>
#include <KAccounts/GetCredentialsJob>
#include <QProcess>
#include <QStandardPaths>

KAccountsManager::KAccountsManager()
{
    loadAccounts();
}

KAccountsManager::~KAccountsManager() = default;

OneDriveAccountPtr KAccountsManager::account(const QString &accountName)
{
    const auto accounts = m_accounts.values();
    for (const auto &account : accounts) {
        if (account->accountName() == accountName) {
            return account;
        }
    }

    return std::make_shared<OneDriveAccount>();
}

OneDriveAccountPtr KAccountsManager::createAccount()
{
    if (QStandardPaths::findExecutable(QStringLiteral("kcmshell6")).isEmpty()) {
        return std::make_shared<OneDriveAccount>();
    }

    const auto oldAccounts = accounts();

    QProcess process;
    process.start(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_kaccounts")});
    qCDebug(ONEDRIVE) << "Waiting for kcmshell process...";
    if (process.waitForFinished(-1)) {
        loadAccounts();
    }

    const auto newAccounts = accounts();
    for (const auto &accountName : newAccounts) {
        if (oldAccounts.contains(accountName)) {
            continue;
        }

        // The KCM allows to add more than one account, but we can return only one from here.
        // So we just return the first new account in the set.
        qCDebug(ONEDRIVE) << "New account successfully created:" << accountName;
        return account(accountName);
    }

    // No accounts at all or no new account(s).
    qCDebug(ONEDRIVE) << "No new account created.";
    return std::make_shared<OneDriveAccount>();
}

OneDriveAccountPtr KAccountsManager::refreshAccount(const OneDriveAccountPtr &account)
{
    const QString accountName = account->accountName();
    for (auto it = m_accounts.constBegin(); it != m_accounts.constEnd(); ++it) {
        if (it.value()->accountName() != accountName) {
            continue;
        }

        const auto id = it.key();
        qCDebug(ONEDRIVE) << "Refreshing" << accountName;
        auto cloudAccount = getAccountCredentials(id, accountName);
        m_accounts.insert(id, cloudAccount);
        return cloudAccount;
    }

    return {};
}

void KAccountsManager::removeAccount(const QString &accountName)
{
    if (!accounts().contains(accountName)) {
        return;
    }

    for (auto it = m_accounts.constBegin(); it != m_accounts.constEnd(); ++it) {
        if (it.value()->accountName() != accountName) {
            continue;
        }

        auto manager = KAccounts::accountsManager();
        auto account = Accounts::Account::fromId(manager, it.key());
        Q_ASSERT(account->displayName() == accountName);
        qCDebug(ONEDRIVE) << "Going to remove account:" << account->displayName();
        account->selectService(manager->service(QStringLiteral("onedrive")));
        account->setEnabled(false);
        account->sync();
        return;
    }
}

QSet<QString> KAccountsManager::accounts()
{
    auto accountNames = QSet<QString>();

    const auto accounts = m_accounts.values();
    for (const auto &account : accounts) {
        accountNames << account->accountName();
    }

    return accountNames;
}

void KAccountsManager::loadAccounts()
{
    m_accounts.clear();

    auto manager = KAccounts::accountsManager();
    const auto enabledIDs = manager->accountListEnabled();
    for (const auto id : enabledIDs) {
        auto account = manager->account(id);
        auto providerName = account->providerName();

        qCDebug(ONEDRIVE) << "Checking account:" << account->displayName() << ", provider name:" << providerName;

        if (providerName != QLatin1String("microsoft")) {
            continue;
        }

        qCDebug(ONEDRIVE) << "Found Microsoft-provided account:" << account->displayName();

        const auto services = account->enabledServices();
        for (const auto &service : services) {
            if (service.name() != QLatin1String("onedrive")) {
                continue;
            }
            qCDebug(ONEDRIVE) << account->displayName() << "supports OneDrive service.";

            auto cloudAccount = getAccountCredentials(id, account->displayName());
            m_accounts.insert(id, cloudAccount);
        }
    }
}

static QString elideToken(const QString &token)
{
    if (token.size() <= 8) {
        return token;
    }
    return token.left(4) + QStringLiteral("...") + token.right(4);
}

OneDriveAccountPtr KAccountsManager::getAccountCredentials(Accounts::AccountId id, const QString &displayName) const
{
    auto job = new KAccounts::GetCredentialsJob(id, nullptr);
    job->exec();
    if (job->error()) {
        qCWarning(ONEDRIVE) << "GetCredentialsJob failed:" << job->errorString();
    }

    auto cloudAccount = std::make_shared<OneDriveAccount>();
    cloudAccount->name = displayName;
    cloudAccount->token = job->credentialsData().value(QStringLiteral("AccessToken")).toString();
    cloudAccount->refresh = job->credentialsData().value(QStringLiteral("RefreshToken")).toString();
    cloudAccount->scopes = job->credentialsData().value(QStringLiteral("Scope")).toStringList();

    qCDebug(ONEDRIVE) << "Got account credentials for:" << cloudAccount->accountName() << ", accessToken:" << elideToken(cloudAccount->accessToken())
                      << ", refreshToken:" << elideToken(cloudAccount->refreshToken()) << ", scopes:" << cloudAccount->scopes;

    return cloudAccount;
}
