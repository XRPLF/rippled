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

#ifndef RIPPLE_PEERCLIENT_TXSETREQUESTER_H_INCLUDED
#define RIPPLE_PEERCLIENT_TXSETREQUESTER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/ledger/LedgerIdentifier.h>
#include <ripple/peerclient/BasicSHAMapRequester.h>
#include <ripple/protocol/STTx.h>
#include <ripple/shamap/SHAMapNodeID.h>

#include <map>
#include <memory>

namespace ripple {

// This is the type used in `LedgerReplay`,
// which is the type expected by `buildLedger(...)`.
// TODO: Refactor.
using TxSet = std::map<std::uint32_t, std::shared_ptr<STTx const>>;

class TxSetRequester : public BasicSHAMapRequester<TxSet>
{
protected:
    using Journaler::journal_;

public:
    using Named::name;

private:
    TxSet txns_;

public:
    TxSetRequester(
        Application& app,
        Scheduler& jscheduler,
        LedgerDigest&& digest)
        : BasicSHAMapRequester(
              app,
              jscheduler,
              "TxSetRequester",
              protocol::liTX_NODE,
              std::move(digest))
    {
    }

protected:
    bool
    onLeaf(SHAMapNodeID& id, SHAMapLeafNode& leaf) override;

    void
    onComplete() override;
};

}  // namespace ripple

#endif
