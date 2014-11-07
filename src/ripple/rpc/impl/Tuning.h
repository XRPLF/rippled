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

#ifndef RIPPLE_RPC_TUNING_H_INCLUDED
#define RIPPLE_RPC_TUNING_H_INCLUDED

namespace ripple {
namespace RPC {

/** Tuned constants. */
/** @{ */
namespace Tuning {

/** Default account lines return per request to the
account_lines command when no limit param is specified
*/
unsigned int const defaultLinesPerRequest (200);

/** Minimum account lines return per request to the
account_lines command. Specified in the limit param.
*/
unsigned int const minLinesPerRequest (10);

/** Maximum account lines return per request to the
account_lines command. Specified in the limit param.
*/
unsigned int const maxLinesPerRequest (400);

/** Default offers return per request to the account_offers command
when no limit param is specified
*/
unsigned int const defaultOffersPerRequest (200);

/** Minimum offers return per request to the account_offers command.
Specified in the limit param.
*/
unsigned int const minOffersPerRequest (10);

/** Maximum offers return per request to the account_lines command.
Specified in the limit param.
*/
unsigned int const maxOffersPerRequest (400);

int const defaultAutoFillFeeMultiplier (10);
int const maxPathfindsInProgress (2);
int const maxPathfindJobCount (50);
int const maxJobQueueClients (500);
int const maxValidatedLedgerAge (120);
int const maxRequestSize (1000000);

} // Tuning
/** @} */

} // RPC
} // ripple

#endif
