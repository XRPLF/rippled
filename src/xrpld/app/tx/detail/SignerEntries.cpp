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

#include <ripple/app/tx/impl/SignerEntries.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STObject.h>
#include <cstdint>
#include <optional>

namespace ripple {

Expected<std::vector<SignerEntries::SignerEntry>, NotTEC>
SignerEntries::deserialize(
    STObject const& obj,
    beast::Journal journal,
    std::string const& annotation)
{
    std::pair<std::vector<SignerEntry>, NotTEC> s;

    if (!obj.isFieldPresent(sfSignerEntries))
    {
        JLOG(journal.trace())
            << "Malformed " << annotation << ": Need signer entry array.";
        return Unexpected(temMALFORMED);
    }

    std::vector<SignerEntry> accountVec;
    accountVec.reserve(STTx::maxMultiSigners());

    STArray const& sEntries(obj.getFieldArray(sfSignerEntries));
    for (STObject const& sEntry : sEntries)
    {
        // Validate the SignerEntry.
        if (sEntry.getFName() != sfSignerEntry)
        {
            JLOG(journal.trace())
                << "Malformed " << annotation << ": Expected SignerEntry.";
            return Unexpected(temMALFORMED);
        }

        // Extract SignerEntry fields.
        AccountID const account = sEntry.getAccountID(sfAccount);
        std::uint16_t const weight = sEntry.getFieldU16(sfSignerWeight);
        std::optional<uint256> const tag = sEntry.at(~sfWalletLocator);

        accountVec.emplace_back(account, weight, tag);
    }
    return accountVec;
}

}  // namespace ripple
