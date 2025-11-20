/*
 * SPDX-FileCopyrightText: 2025 Enrico M. Crisostomo <enrico.crisostomo@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QStringList>
#include <memory>

struct OneDriveAccount {
    QString name;
    QString token;
    QString refresh;
    QStringList scopes;

    QString accountName() const
    {
        return name;
    }
    QString accessToken() const
    {
        return token;
    }
    QString refreshToken() const
    {
        return refresh;
    }
    bool isValid() const
    {
        return !name.isEmpty();
    }
};

using OneDriveAccountPtr = std::shared_ptr<OneDriveAccount>;
