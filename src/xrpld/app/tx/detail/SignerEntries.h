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

#ifndef RIPPLE_TX_IMPL_SIGNER_ENTRIES_H_INCLUDED
#define RIPPLE_TX_IMPL_SIGNER_ENTRIES_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>  // NotTEC
#include <xrpl/basics/Expected.h>            //
#include <xrpl/beast/utility/Journal.h>      // beast::Journal
#include <xrpl/protocol/Rules.h>             // Rules
#include <xrpl/protocol/STTx.h>              // STTx::maxMultiSigners
#include <xrpl/protocol/TER.h>               // temMALFORMED
#include <xrpl/protocol/UintTypes.h>         // AccountID

#include <optional>
#include <string_view>

namespace ripple {

// Forward declarations
class STObject;

// Support for SignerEntries that is needed by a few Transactors.
//
// SignerEntries is represented as a std::vector<SignerEntries::SignerEntry>.
// There is no direct constructor for SignerEntries.
//
//  o A std::vector<SignerEntries::SignerEntry> is a SignerEntries.
//  o More commonly, SignerEntries are extracted from an STObject by
//    calling SignerEntries::deserialize().
class SignerEntries
{
public:
    explicit SignerEntries() = delete;

    struct SignerEntry
    {
        AccountID account;
        std::uint16_t weight;
        std::optional<uint256> tag;

        SignerEntry(
            AccountID const& inAccount,
            std::uint16_t inWeight,
            std::optional<uint256> inTag)
            : account(inAccount), weight(inWeight), tag(inTag)
        {
        }

        // For sorting to look for duplicate accounts
        friend bool
        operator<(SignerEntry const& lhs, SignerEntry const& rhs)
        {
            return lhs.account < rhs.account;
        }

        friend bool
        operator==(SignerEntry const& lhs, SignerEntry const& rhs)
        {
            return lhs.account == rhs.account;
        }
    };

    // Deserialize a SignerEntries array from the network or from the ledger.
    //
    // obj Contains a SignerEntries field that is an STArray.
    // journal For reporting error conditions.
    // annotation Source of SignerEntries, like "ledger" or "transaction".
    static Expected<std::vector<SignerEntry>, NotTEC>
    deserialize(
        STObject const& obj,
        beast::Journal journal,
        std::string_view annotation);
};

}  // namespace ripple

#endif  // RIPPLE_TX_IMPL_SIGNER_ENTRIES_H_INCLUDED
