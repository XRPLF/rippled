#include <test/jtx/paths.h>

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>

#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/Pathfinder.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <optional>

namespace xrpl::test::jtx {

void
Paths::operator()(Env& env, JTx& jt) const
{
    auto& jv = jt.jv;
    auto const from = env.lookup(jv[jss::Account].asString());
    auto const to = env.lookup(jv[jss::Destination].asString());
    auto const amount = amountFromJson(sfAmount, jv[jss::Amount]);

    std::optional<uint256> domain;
    if (jv.isMember(sfDomainID.jsonName))
    {
        if (!jv[sfDomainID.jsonName].isString())
            return;
        uint256 num;
        auto const s = jv[sfDomainID.jsonName].asString();
        if (num.parseHex(s))
            domain = num;
    }

    Pathfinder pf(
        std::make_shared<AssetCache>(env.current(), env.app().getJournal("AssetCache")),
        from,
        to,
        in_,
        in_.getIssuer(),
        amount,
        std::nullopt,
        domain,
        env.app());
    if (!pf.findPaths(depth_))
        return;

    STPath fp;
    pf.computePathRanks(limit_);
    auto const found = pf.getBestPaths(limit_, fp, {}, in_.getIssuer());

    // VFALCO TODO API to allow caller to examine the STPathSet
    // VFALCO isDefault should be renamed to empty()
    if (!found.isDefault())
        jv[jss::Paths] = found.getJson(JsonOptions::Values::None);
}

//------------------------------------------------------------------------------

Path::Path(STPath const& p)
{
    jv_ = p.getJson(JsonOptions::Values::None);
}

json::Value&
Path::create()
{
    return jv_.append(json::ValueType::Object);
}

void
Path::appendOne(Account const& account)
{
    appendOne(account.id());
}

void
Path::appendOne(AccountID const& account)
{
    auto& jv = create();
    jv["account"] = toBase58(account);
}

void
Path::appendOne(IOU const& iou)
{
    auto& jv = create();
    jv["currency"] = to_string(iou.currency);
    jv["account"] = toBase58(iou.account);
}

void
Path::appendOne(BookSpec const& book)
{
    auto& jv = create();
    book.asset.visit(
        [&](Issue const& issue) {
            jv["currency"] = to_string(issue.currency);
            jv["issuer"] = toBase58(issue.account);
        },
        [&](MPTIssue const& issue) { jv["mpt_issuance_id"] = to_string(issue.getMptID()); });
}

void
Path::operator()(Env& env, JTx& jt) const
{
    jt.jv["Paths"].append(jv_);
}

}  // namespace xrpl::test::jtx
