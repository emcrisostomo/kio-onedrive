/*
 * SPDX-FileCopyrightText: 2017 Elvis Angelaccio <elvis.angelaccio@kde.org>
 * SPDX-FileCopyrightText: 2025 Enrico M. Crisostomo <enrico.crisostomo@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#pragma once

#include "abstractaccountmanager.h"

#include <QMap>

#include <Accounts/Account>

class KAccountsManager : public AbstractAccountManager
{
public:
    KAccountsManager();
    ~KAccountsManager() override;

    OneDriveAccountPtr account(const QString &accountName) override;
    OneDriveAccountPtr createAccount() override;
    OneDriveAccountPtr refreshAccount(const OneDriveAccountPtr &account) override;
    void removeAccount(const QString &accountName) override;
    QSet<QString> accounts() override;

private:
    void loadAccounts();

    OneDriveAccountPtr getAccountCredentials(Accounts::AccountId id, const QString &displayName);

    QMap<Accounts::AccountId, OneDriveAccountPtr> m_accounts;
};
