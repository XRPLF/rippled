#include <test/jtx/paths.h>

#include <xrpld/rpc/detail/Pathfinder.h>

#include <xrpl/protocol/jss.h>

#include <optional>

namespace xrpl {
namespace test {
namespace jtx {

void
paths::operator()(Env& env, JTx& jt) const
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
        jv[jss::Paths] = found.getJson(JsonOptions::none);
}

//------------------------------------------------------------------------------

path::path(STPath const& p)
{
    jv_ = p.getJson(JsonOptions::none);
}

Json::Value&
path::create()
{
    return jv_.append(Json::objectValue);
}

void
path::append_one(Account const& account)
{
    append_one(account.id());
}

void
path::append_one(AccountID const& account)
{
    auto& jv = create();
    jv["account"] = toBase58(account);
}

void
path::append_one(IOU const& iou)
{
    auto& jv = create();
    jv["currency"] = to_string(iou.currency);
    jv["account"] = toBase58(iou.account);
}

void
path::append_one(BookSpec const& book)
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
path::operator()(Env& env, JTx& jt) const
{
    jt.jv["Paths"].append(jv_);
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
