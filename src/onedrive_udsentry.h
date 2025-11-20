/*
 * SPDX-FileCopyrightText: 2020 David Barchiesi <david@barchie.si>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef ONEDRIVEUDSENTRY_H
#define ONEDRIVEUDSENTRY_H

#include <KIO/UDSEntry>

enum OneDriveUDSEntryExtras {
    Url = KIO::UDSEntry::UDS_EXTRA,
    Id,
    Md5,
    Owners,
    Version,
    LastModifyingUser,
    Description,
    SharedWithMeDate,
};

#endif // ONEDRIVEUDSENTRY_H
