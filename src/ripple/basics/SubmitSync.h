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

#ifndef RIPPLE_BASICS_SUBMITSYNC_H_INCLUDED
#define RIPPLE_BASICS_SUBMITSYNC_H_INCLUDED

namespace ripple {
namespace RPC {

/**
 * Possible values for defining synchronous behavior of the transaction
 * submission API.
 *   1) sync (default): Process transactions in a batch immediately,
 *       and return only once the transaction has been processed.
 *   2) async: Put transaction into the batch for the next processing
 *       interval and return immediately.
 *   3) wait: Put transaction into the batch for the next processing
 *       interval and return only after it is processed.
 */
enum class SubmitSync { sync, async, wait };

}  // namespace RPC
}  // namespace ripple

#endif