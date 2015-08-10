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

/** Default account objects returned per request from the
    account_objects command when no limit param is specified
*/
static unsigned int const defaultObjectsPerRequest = 200;

/** Minimum account objects returned per request from the
    account_objects command. Specified in the limit param.
*/
static unsigned int const minObjectsPerRequest = 10;

/** Maximum account objects returned per request from the
    account_objects command. Specified in the limit param.
*/
static unsigned int const maxObjectsPerRequest = 400;

/** Default account lines returned per request from the
    account_lines command when no limit param is specified
*/
static unsigned int const defaultLinesPerRequest = 200;

/** Minimum account lines returned per request from the
    account_lines command. Specified in the limit param.
*/
static unsigned int const minLinesPerRequest = 10;

/** Maximum account lines returned per request from the
    account_lines command. Specified in the limit param.
*/
static unsigned int const maxLinesPerRequest = 400;

/** Default offers returned per request from the
    account_offers command when no limit param is specified.
*/
static unsigned int const defaultOffersPerRequest = 200;

/** Minimum offers returned per request from the
    account_offers command. Specified in the limit param.
*/
static unsigned int const minOffersPerRequest = 10;

/** Maximum offers returned per request from the
    account_lines command. Specified in the limit param.
*/
static unsigned int const maxOffersPerRequest = 400;

static int const defaultAutoFillFeeMultiplier = 10;
static int const maxPathfindsInProgress = 2;
static int const maxPathfindJobCount = 50;
static int const maxJobQueueClients = 500;
static int const maxValidatedLedgerAge = 120;
static int const maxRequestSize = 1000000;

/** Maximum number of pages in one response from a binary LedgerData request. */
static int const binaryPageLength = 2048;

/** Maximum number of pages in one response from a Json LedgerData request. */
static int const jsonPageLength = 256;

/** Maximum number of pages in a LedgerData response. */
inline int pageLength(bool isBinary)
{
    return isBinary ? binaryPageLength : jsonPageLength;
}

} // Tuning
/** @} */

} // RPC
} // ripple

#endif
