//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Transia, LLC.

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

#include <test/jtx/firewall.h>
#include <test/jtx/utility.h>
// #include <xrpl/protocol/Batch.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <optional>
#include <sstream>

namespace ripple {
namespace test {
namespace jtx {
namespace firewall {

Json::Value
set(Account const& account, std::uint32_t const& seq, STAmount const& fee)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = jss::FirewallSet;
    jv[jss::Sequence] = seq;
    jv[jss::Fee] = fee.getJson(JsonOptions::none);
    jv[jss::SigningPubKey] = strHex(account.pk().slice());
    return jv;
}

void
time_period::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfTimePeriod.jsonName] = value_;
}

void
amt::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfAmount.jsonName] = amt_.getJson(JsonOptions::none);
}

void
issuer::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfIssuer.jsonName] = issuer_.human();
}

void
auth::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfAuthorize.jsonName] = auth_.human();
}

sig::sig(std::vector<sig::Reg> signers_) : signers(std::move(signers_))
{
    // Signatures must be applied in sorted order.
    std::sort(
        signers.begin(),
        signers.end(),
        [](sig::Reg const& lhs, sig::Reg const& rhs) {
            return lhs.acct.id() < rhs.acct.id();
        });
}

void
sig::operator()(Env& env, JTx& jt) const
{
    std::optional<STObject> st;
    try
    {
        st = parse(jt.jv);
    }
    catch (parse_error const&)
    {
        env.test.log << pretty(jt.jv) << std::endl;
        Rethrow();
    }
    auto const mySigners = signers;
    auto& js = jt[sfFirewallSigners.getJsonName()];
    for (std::size_t i = 0; i < mySigners.size(); ++i)
    {
        auto const& e = mySigners[i];
        auto& jo = js[i][sfFirewallSigner.getJsonName()];
        jo[jss::Account] = e.acct.human();
        jo[jss::SigningPubKey] = strHex(e.sig.pk().slice());

        Serializer ss;
        ss.add32(HashPrefix::txSign);
        st->addWithoutSigningFields(ss);
        auto const sig = ripple::sign(*publicKeyType(e.sig.pk().slice()), e.sig.sk(), ss.slice());
        jo[jss::TxnSignature] = strHex(Slice{sig.data(), sig.size()});
    }
}

msig::msig(Account const& masterAccount, std::vector<msig::Reg> signers_)
    : master(masterAccount), signers(std::move(signers_))
{
    std::sort(
        signers.begin(),
        signers.end(),
        [](msig::Reg const& lhs, msig::Reg const& rhs) {
            return lhs.acct.id() < rhs.acct.id();
        });
}

void
msig::operator()(Env& env, JTx& jt) const
{
    auto const mySigners = signers;
    std::optional<STObject> st;
    try
    {
        st = parse(jt.jv);
    }
    catch (parse_error const&)
    {
        env.test.log << pretty(jt.jv) << std::endl;
        Rethrow();
    }
    auto& bs = jt[sfFirewallSigners.getJsonName()];
    auto const index = jt[sfFirewallSigners.jsonName].size();
    auto& bso = bs[index][sfFirewallSigner.getJsonName()];
    bso[jss::Account] = master.human();
    bso[jss::SigningPubKey] = "";
    auto& is = bso[sfSigners.getJsonName()];
    for (std::size_t i = 0; i < mySigners.size(); ++i)
    {
        auto const& e = mySigners[i];
        auto& iso = is[i][sfSigner.getJsonName()];
        iso[jss::Account] = e.acct.human();
        iso[jss::SigningPubKey] = strHex(e.sig.pk().slice());

        Serializer msg;
        // serializeBatch(msg, st->getFlags(), st->getFieldV256(sfTxIDs));
        // auto const sig = ripple::sign(
        //     *publicKeyType(e.sig.pk().slice()), e.sig.sk(), msg.slice());
        // iso[sfTxnSignature.getJsonName()] =
        //     strHex(Slice{sig.data(), sig.size()});
    }
}

}  // namespace firewall
}  // namespace jtx
}  // namespace test
}  // namespace ripple
