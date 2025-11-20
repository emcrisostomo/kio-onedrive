/*
 * SPDX-FileCopyrightText: 2017 Elvis Angelaccio <elvis.angelaccio@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#pragma once

#include "onedriveaccount.h"

#include <QSet>

class AbstractAccountManager
{
public:
    virtual ~AbstractAccountManager();

    /**
     * @return Pointer to the account for @p accountName.
     * The account is valid only if @p accountName is in accounts().
     * @see accounts()
     */
    virtual OneDriveAccountPtr account(const QString &accountName) = 0;

    /**
     * Creates a new account.
     * @return The new account if a new account has been created, an invalid account otherwise.
     */
    virtual OneDriveAccountPtr createAccount() = 0;

    virtual OneDriveAccountPtr refreshAccount(const OneDriveAccountPtr &account) = 0;

    /**
     * Remove @p accountName from accounts().
     * @see accounts()
     */
    virtual void removeAccount(const QString &accountName) = 0;

    /**
     * @return The OneDrive accounts managed by this object.
     */
    virtual QSet<QString> accounts() = 0;
};
