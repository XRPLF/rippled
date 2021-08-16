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

#include <ripple/app/tx/impl/Transactor.h>  // NotTEC
#include <ripple/basics/Expected.h>         //
#include <ripple/beast/utility/Journal.h>   // beast::Journal
#include <ripple/protocol/STTx.h>           // STTx::maxMultiSigners
#include <ripple/protocol/TER.h>            // temMALFORMED
#include <ripple/protocol/UintTypes.h>      // AccountID

namespace ripple {

// Forward declarations
class STObject;

// Support for SignerEntries that is needed by a few Transactors
class SignerEntries
{
public:
    explicit SignerEntries() = default;

    struct SignerEntry
    {
        AccountID account;
        std::uint16_t weight;

        SignerEntry(AccountID const& inAccount, std::uint16_t inWeight)
            : account(inAccount), weight(inWeight)
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
    static Expected<std::vector<SignerEntry>, NotTEC>
    deserialize(
        STObject const& obj,
        beast::Journal journal,
        std::string const& annotation);
};

}  // namespace ripple

#endif  // RIPPLE_TX_IMPL_SIGNER_ENTRIES_H_INCLUDED
