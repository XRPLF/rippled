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

AccountState::AccountState (RippleAddress const& naAccountID)
    : mAccountID (naAccountID)
    , mValid (false)
{
    if (naAccountID.isValid ())
    {
        mValid = true;

        mLedgerEntry = std::make_shared <SerializedLedgerEntry> (
                           ltACCOUNT_ROOT, Ledger::getAccountRootIndex (naAccountID));

        mLedgerEntry->setFieldAccount (sfAccount, naAccountID.getAccountID ());
    }
}

AccountState::AccountState (SLE::ref ledgerEntry, const RippleAddress& naAccountID) :
    mAccountID (naAccountID), mLedgerEntry (ledgerEntry), mValid (false)
{
    if (!mLedgerEntry)
        return;

    if (mLedgerEntry->getType () != ltACCOUNT_ROOT)
        return;

    mValid = true;
}

// VFALCO TODO Make this a generic utility function of some container class
//
std::string AccountState::createGravatarUrl (uint128 uEmailHash)
{
    Blob    vucMD5 (uEmailHash.begin (), uEmailHash.end ());
    std::string                 strMD5Lower = strHex (vucMD5);
    boost::to_lower (strMD5Lower);

    // VFALCO TODO Give a name and move this constant to a more visible location.
    //             Also shouldn't this be https?
    return str (boost::format ("http://www.gravatar.com/avatar/%s") % strMD5Lower);
}

void AccountState::addJson (Json::Value& val)
{
    val = mLedgerEntry->getJson (0);

    if (mValid)
    {
        if (mLedgerEntry->isFieldPresent (sfEmailHash))
            val["urlgravatar"]  = createGravatarUrl (mLedgerEntry->getFieldH128 (sfEmailHash));
    }
    else
    {
        val["Invalid"] = true;
    }
}

void AccountState::dump ()
{
    Json::Value j (Json::objectValue);
    addJson (j);
    Log (lsINFO) << j;
}

} // ripple
