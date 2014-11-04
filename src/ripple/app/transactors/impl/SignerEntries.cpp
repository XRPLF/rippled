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

#include <ripple/app/transactors/impl/SignerEntries.h>
#include <cstdint>

namespace ripple {

SignerEntries::SignerEntriesDecode
SignerEntries::deserializeSignerEntries (
    STObject const& obj, beast::Journal& journal, char const* annotation)
{
    SignerEntriesDecode s;
    auto& accountVec (s.vec);
    accountVec.reserve (maxSignerEntries);

    if (!obj.isFieldPresent (sfSignerEntries))
    {
        if (journal.trace) journal.trace <<
            "Malformed " << annotation << ": Need signer entry array.";
        s.ter = temMALFORMED;
        return s;
    }

    STArray const& sEntries (obj.getFieldArray (sfSignerEntries));
    for (STObject const& sEntry : sEntries)
    {
        // Validate the SignerEntry.
        // SSCHURR NOTE it would be good to do the validation with
        // STObject::setType().  But setType is a non-const method and we have
        // a const object in our hands.  So we do the validation manually.
        if (sEntry.getFName () != sfSignerEntry)
        {
            journal.trace <<
                "Malformed " << annotation << ": Expected signer entry.";
            s.ter = temMALFORMED;
            return s;
        }

        // Extract SignerEntry fields.
        bool gotAccount (false);
        Account account;
        bool gotWeight (false);
        std::uint16_t weight (0);
        for (SerializedType const& sType : sEntry)
        {
            SField::ref const type = sType.getFName ();
            if (type == sfAccount)
            {
                auto const accountPtr =
                    dynamic_cast <STAccount const*> (&sType);
                if (!accountPtr)
                {
                    if (journal.trace) journal.trace <<
                        "Malformed " << annotation << ": Expected account.";
                    s.ter = temMALFORMED;
                    return s;
                }
                if (!accountPtr->getValueH160 (account))
                {
                    if (journal.trace) journal.trace <<
                        "Malformed " << annotation <<
                        ": Expected 160 bit account ID.";
                    s.ter = temMALFORMED;
                    return s;
                }
                gotAccount = true;
            }
            else if (type == sfSignerWeight)
            {
                auto const weightPtr = dynamic_cast <STUInt16 const*> (&sType);
                if (!weightPtr)
                {
                    if (journal.trace) journal.trace <<
                        "Malformed " << annotation << ": Expected weight.";
                    s.ter = temMALFORMED;
                    return s;
                }
                weight = weightPtr->getValue ();
                gotWeight = true;
            }
            else
            {
                if (journal.trace) journal.trace <<
                    "Malformed " << annotation <<
                    ": Unexpected field in signer entry.";
                s.ter = temMALFORMED;
                return s;
            }
        }
        if (gotAccount && gotWeight)
        {
            // We have deserialized the pair.  Put them in the vector.
            accountVec.push_back ( {account, weight} );
        }
        else
        {
            if (journal.trace) journal.trace <<
                "Malformed " << annotation <<
                ": Missing field in signer entry.";
            s.ter = temMALFORMED;
            return s;
        }
    }

    s.ter = tesSUCCESS;
    return s;
}

} // riople
