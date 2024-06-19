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

#ifndef RIPPLE_PEERCLIENT_SHAMAPREQUESTER_H_INCLUDED
#define RIPPLE_PEERCLIENT_SHAMAPREQUESTER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/peerclient/BasicSHAMapRequester.h>
#include <ripple/shamap/SHAMap.h>

#include <memory>

namespace ripple {

class SHAMapRequester : public BasicSHAMapRequester<std::shared_ptr<SHAMap>>
{
private:
    std::shared_ptr<SHAMap> shamap_;

public:
    SHAMapRequester(
        Application& app,
        Scheduler& jscheduler,
        protocol::TMLedgerInfoType type,
        LedgerDigest&& digest)
        : BasicSHAMapRequester(app, jscheduler, type, std::move(digest))
    {
    }

protected:
    void
    onNode(SHAMapNodeID& id, SHAMapTreeNode& node);

    bool
    onInner(SHAMapNodeID& id, SHAMapInnerNode& inner) override
    {
        onNode(id, inner);
        return false;
    }

    bool
    onLeaf(SHAMapNodeID& id, SHAMapLeafNode& leaf) override
    {
        onNode(id, leaf);
        return false;
    }

    void
    onComplete() override;
};
}  // namespace ripple

#endif
