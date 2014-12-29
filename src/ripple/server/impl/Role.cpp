//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/server/Role.h>

namespace ripple {

Role
adminRole (HTTP::Port const& port, Json::Value const& params,
    beast::IP::Endpoint const& remoteIp,
        std::vector<beast::IP::Endpoint> const& admin_allow)
{
    Role role (Role::FORBID);

    bool const bPasswordSupplied =
        params.isMember ("admin_user") ||
        params.isMember ("admin_password");

    bool const bPasswordRequired =
        ! port.admin_user.empty() || ! port.admin_password.empty();

    bool bPasswordWrong;

    if (bPasswordSupplied)
    {
        if (bPasswordRequired)
        {
            // Required, and supplied, check match
            bPasswordWrong =
                (port.admin_user !=
                    (params.isMember ("admin_user") ? params["admin_user"].asString () : ""))
                ||
                (port.admin_password !=
                    (params.isMember ("admin_user") ? params["admin_password"].asString () : ""));
        }
        else
        {
            // Not required, but supplied
            bPasswordWrong = false;
        }
    }
    else
    {
        // Required but not supplied,
        bPasswordWrong = bPasswordRequired;
    }

    // Meets IP restriction for admin.
    beast::IP::Endpoint const remote_addr (remoteIp.at_port (0));
    bool bAdminIP = false;

    // VFALCO TODO Don't use this!
    for (auto const& allow_addr : admin_allow)
    {
        if (allow_addr == remote_addr)
        {
            bAdminIP = true;
            break;
        }
    }

    if (bPasswordWrong                          // Wrong
            || (bPasswordSupplied && !bAdminIP))    // Supplied and doesn't meet IP filter.
    {
        role   = Role::FORBID;
    }
    // If supplied, password is correct.
    else
    {
        // Allow admin, if from admin IP and no password is required or it was supplied and correct.
        role = bAdminIP && (!bPasswordRequired || bPasswordSupplied) ? Role::ADMIN : Role::GUEST;
    }

    return role;
}

}
