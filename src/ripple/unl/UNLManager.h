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

#ifndef RIPPLE_UNL_MANAGER_H_INCLUDED
#define RIPPLE_UNL_MANAGER_H_INCLUDED

#include "ripple.pb.h"
#include <ripple/protocol/Protocol.h>
#include <ripple/unl/Horizon.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/protocol/STValidation.h>
#include <beast/threads/Stoppable.h>
#include <beast/http/URL.h>
#include <beast/module/core/files/File.h>
#include <beast/utility/PropertyStream.h>

namespace ripple {
namespace unl {

/** Maintains the list of chosen validators.
    The algorithm for acquiring, building, and calculating metadata on
    the list of chosen validators is critical to the health of the network.
    All operations are performed asynchronously on an internal thread.
*/
class Manager : public beast::PropertyStream::Source
{
protected:
    Manager();

public:
    /** Destroy the object.
        Any pending source fetch operations are aborted. This will block
        until any pending database I/O has completed and the thread has
        stopped.
    */
    virtual ~Manager() = default;

    virtual
    std::unique_ptr<Horizon>
    insert (int id, Horizon::Kind kind) = 0;

    virtual
    void
    erase (std::unique_ptr<Horizon> c) = 0;

    /** Called when a signed validation is received
        Preconditions:
            Signature check passed
        Effects:
    */
    virtual
    void
    onMessage (std::unique_ptr<Horizon> const& h,
        protocol::TMValidation const& m,
            STValidation const& v) = 0;

    /** Called when a ledger is built. */
    virtual
    void
    onLedgerClosed (LedgerIndex index,
        LedgerHash const& hash, LedgerHash const& parent) = 0;
};

}
}

#endif
