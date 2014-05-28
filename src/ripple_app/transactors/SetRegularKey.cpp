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

namespace ripple {

std::uint64_t SetRegularKey::calculateBaseFee ()
{
    if ( mTxnAccount
            && (! (mTxnAccount->getFlags () & lsfPasswordSpent))
            && (mSigningPubKey.getAccountID () == mTxnAccountID))
    {
        // flag is armed and they signed with the right account
        return 0;
    }

    return Transactor::calculateBaseFee ();
}


TER SetRegularKey::doApply ()
{
    std::uint32_t const uTxFlags = mTxn.getFlags ();

    if (uTxFlags & tfUniversalMask)
    {
        m_journal.trace <<
            "Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }

    if (mFeeDue == zero)
    {
        mTxnAccount->setFlag (lsfPasswordSpent);
    }

    if (mTxn.isFieldPresent (sfRegularKey))
    {
        uint160 uAuthKeyID = mTxn.getFieldAccount160 (sfRegularKey);
        mTxnAccount->setFieldAccount (sfRegularKey, uAuthKeyID);
    }
    else
    {
        if (mTxnAccount->isFlag (lsfDisableMaster))
            return tecMASTER_DISABLED;
        mTxnAccount->makeFieldAbsent (sfRegularKey);
    }

    return tesSUCCESS;
}

}
