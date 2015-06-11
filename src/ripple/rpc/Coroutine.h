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

#ifndef RIPPLE_RPC_COROUTINE_H_INCLUDED
#define RIPPLE_RPC_COROUTINE_H_INCLUDED

#include <ripple/rpc/Yield.h>

namespace ripple {
namespace RPC {

/** SuspendCallback: a function that a Coroutine gives to the coroutine
    scheduler so that it gets a callback with a Suspend when it runs.
 */
using SuspendCallback = std::function <void (Suspend const&)>;

/** Runs a function that takes a SuspendCallback as a coroutine. */
class Coroutine
{
public:
    explicit Coroutine (SuspendCallback const&);
    ~Coroutine();

    /** Run the coroutine and guarantee completion. */
    void run ();

private:
    struct Impl;
    std::shared_ptr <Impl> impl_;

    Coroutine (std::shared_ptr <Impl> const&);
};

} // RPC
} // ripple

#endif
