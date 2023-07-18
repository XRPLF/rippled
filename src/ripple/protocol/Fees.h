//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_FEES_H_INCLUDED
#define RIPPLE_PROTOCOL_FEES_H_INCLUDED

#include <ripple/basics/XRPAmount.h>

namespace ripple {

/** Reflects the fee settings for a particular ledger.

    The fees are always the same for any transactions applied
    to a ledger. Changes to fees occur in between ledgers.
*/
struct Fees
{
    XRPAmount base{0};       // Reference tx cost (drops)
    XRPAmount reserve{0};    // Reserve base (drops)
    XRPAmount increment{0};  // Reserve increment (drops)

    explicit Fees() = default;
    Fees(Fees const&) = default;
    Fees&
    operator=(Fees const&) = default;

    /** Returns the account reserve given the owner count, in drops.

        The reserve is calculated as the reserve base plus
        the reserve increment times the number of increments.
    */
    XRPAmount
    accountReserve(std::size_t ownerCount) const
    {
        return reserve + ownerCount * increment;
    }
};

}  // namespace ripple

#endif
