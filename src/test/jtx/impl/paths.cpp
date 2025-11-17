#include <test/jtx/paths.h>

#include <xrpld/app/paths/Pathfinder.h>

#include <xrpl/protocol/jss.h>

#include <optional>

namespace ripple {
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
        std::make_shared<RippleLineCache>(
            env.current(), env.app().journal("RippleLineCache")),
        from,
        to,
        in_.currency,
        in_.account,
        amount,
        std::nullopt,
        domain,
        env.app());
    if (!pf.findPaths(depth_))
        return;

    STPath fp;
    pf.computePathRanks(limit_);
    auto const found = pf.getBestPaths(limit_, fp, {}, in_.account);

    // VFALCO TODO API to allow caller to examine the STPathSet
    // VFALCO isDefault should be renamed to empty()
    if (!found.isDefault())
        jv[jss::Paths] = found.getJson(JsonOptions::none);
}

//------------------------------------------------------------------------------

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
    jv["currency"] = to_string(iou.issue().currency);
    jv["account"] = toBase58(iou.issue().account);
}

void
path::append_one(BookSpec const& book)
{
    auto& jv = create();
    jv["currency"] = to_string(book.currency);
    jv["issuer"] = toBase58(book.account);
}

void
path::operator()(Env& env, JTx& jt) const
{
    jt.jv["Paths"].append(jv_);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
