//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012=2014 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_ACCOUNTFROMSTRING_H_INCLUDED
#define RIPPLE_RPC_ACCOUNTFROMSTRING_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/NetworkOPs.h>

namespace ripple {
namespace RPC {

/** Get an AccountID from an account ID or public key. */
boost::optional<AccountID> accountFromStringStrict (std::string const&);

// --> strIdent: public key, account ID, or regular seed.
// --> bStrict: Only allow account id or public key.
//
// Returns a Json::objectValue, containing error information if there was one.
Json::Value accountFromString (
    AccountID& result,
    std::string const& strIdent,
    bool bStrict = false);

} // RPC
} // ripple

#endif
