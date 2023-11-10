//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/json/json_value.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/shamap/SHAMapInnerNode.h>
#include <ripple/shamap/SHAMapLeafNode.h>
#include <ripple/shamap/SHAMapTreeNode.h>

namespace ripple {

Json::Value
doSHAMapInfo(RPC::JsonContext& context)
{
    Json::Value jvResult;
    auto& composition = jvResult["composition"] = Json::objectValue;
    std::uint32_t inners = 0;
    std::uint32_t leaves = 0;
    std::array<std::uint32_t, SHAMapInnerNode::branchFactor> children = {};

    auto validated = context.ledgerMaster.getValidatedLedger();
    auto& state = validated->stateMap();
    state.visitNodes([&](SHAMapTreeNode& node) {
        if (node.isInner())
        {
            inners++;
            auto& inner = static_cast<SHAMapInnerNode&>(node);
            auto count = inner.getBranchCount();
            assert(count > 0);
            children[count - 1]++;
        }
        else
        {
            leaves++;
        }
        return true;
    });

    composition["inners_count"] = inners;
    composition["leaves_count"] = leaves;
    // Not super readable without a lot of faffing around so just export an
    // array
    auto& branches = composition["inners_count_per_child_count"] =
        Json::arrayValue;
    for (const auto& count : children)
    {
        branches.append(count);
    }

    return jvResult;
}

}  // namespace ripple
