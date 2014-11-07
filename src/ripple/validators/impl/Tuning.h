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

#ifndef RIPPLE_VALIDATORS_TUNING_H_INCLUDED
#define RIPPLE_VALIDATORS_TUNING_H_INCLUDED

namespace ripple {
namespace Validators {

// Tunable constants
//
enum
{
#if 1
    // We will fetch a source at this interval
    hoursBetweenFetches = 24
    ,secondsBetweenFetches = hoursBetweenFetches * 60 * 60
    // We check Source expirations on this time interval
    ,checkEverySeconds = 60 * 60
#else
     secondsBetweenFetches = 59
    ,checkEverySeconds = 60
#endif

    // This tunes the preallocated arrays
    ,expectedNumberOfResults    = 1000

    // Number of entries in the recent validations cache
    ,recentValidationsCacheSize = 1000

    // Number of entries in the recent ledgers cache
    ,recentLedgersCacheSize     = 1000 // about half an hour at 2/sec

    // Number of closed Ledger entries per Validator
    ,ledgersPerValidator        =  100 // this shouldn't be too large
};

}
}

#endif
