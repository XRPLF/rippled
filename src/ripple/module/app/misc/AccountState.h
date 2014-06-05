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

#ifndef RIPPLE_ACCOUNTSTATE_H
#define RIPPLE_ACCOUNTSTATE_H

namespace ripple {

//
// Provide abstract access to an account's state, such that access to the serialized format is hidden.
//

class AccountState : beast::LeakChecked <AccountState>
{
public:
    typedef std::shared_ptr<AccountState> pointer;

public:
    // For new accounts
    explicit AccountState (RippleAddress const& naAccountID);

    // For accounts in a ledger
    AccountState (SLE::ref ledgerEntry, RippleAddress const& naAccountI);

    bool haveAuthorizedKey ()
    {
        return mLedgerEntry->isFieldPresent (sfRegularKey);
    }

    RippleAddress getAuthorizedKey ()
    {
        return mLedgerEntry->getFieldAccount (sfRegularKey);
    }

    STAmount getBalance () const
    {
        return mLedgerEntry->getFieldAmount (sfBalance);
    }

    std::uint32_t getSeq () const
    {
        return mLedgerEntry->getFieldU32 (sfSequence);
    }

    SerializedLedgerEntry::pointer getSLE ()
    {
        return mLedgerEntry;
    }

    SerializedLedgerEntry const& peekSLE () const
    {
        return *mLedgerEntry;
    }

    SerializedLedgerEntry& peekSLE ()
    {
        return *mLedgerEntry;
    }

    Blob getRaw () const;

    void addJson (Json::Value& value);

    void dump ();

    static std::string createGravatarUrl (uint128 uEmailHash);

private:
    RippleAddress const mAccountID;
    RippleAddress                  mAuthorizedKey;
    SerializedLedgerEntry::pointer mLedgerEntry;

    bool                           mValid;
};

} // ripple

#endif
