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

#ifndef RIPPLE_ACCOUNTITEMS_H
#define RIPPLE_ACCOUNTITEMS_H

namespace ripple {

/** A set of AccountItem objects. */
class AccountItems : beast::LeakChecked <AccountItems>
{
public:
    typedef std::shared_ptr <AccountItems> pointer;

    typedef std::vector <AccountItem::pointer> Container;

    // VFALCO TODO Create a typedef uint160 AccountID and replace
    AccountItems (uint160 const& accountID,
                  Ledger::ref ledger,
                  AccountItem::pointer ofType);

    // VFALCO TODO rename to getContainer and make this change in every interface
    //             that exposes the caller to the type of container.
    //
    Container& getItems ()
    {
        return mItems;
    }

    // VFALCO TODO What is the int for?
    Json::Value getJson (int);

private:
    void fillItems (const uint160& accountID, Ledger::ref ledger);

private:
    // VFALCO TODO This looks like its used as an exemplar, rename appropriately
    AccountItem::pointer mOfType;

    Container mItems;
};

} // ripple

#endif
