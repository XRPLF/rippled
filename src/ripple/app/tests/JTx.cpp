//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/tests/JTx.h>
#include <ripple/app/tests/Env.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/basics/Slice.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {
namespace test {

namespace jtx {

Json::Value
fset (Account const& account,
    std::uint32_t on, std::uint32_t off)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "AccountSet";
    if (on != 0)
        jv[jss::SetFlag] = on;
    if (off != 0)
        jv[jss::ClearFlag] = off;
    return jv;
}

Json::Value
pay (Account const& account,
    Account const& to,
        MaybeAnyAmount amount)
{
    amount.to(to);
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::Amount] = amount.value.getJson(0);
    jv[jss::Destination] = to.human();
    jv[jss::TransactionType] = "Payment";
    jv[jss::Flags] = tfUniversal;
    return jv;
}

Json::Value
offer (Account const& account,
    STAmount const& in, STAmount const& out)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TakerPays] = in.getJson(0);
    jv[jss::TakerGets] = out.getJson(0);
    jv[jss::TransactionType] = "OfferCreate";
    return jv;
}

Json::Value
rate (Account const& account, double multiplier)
{
    if (multiplier > 4)
        throw std::runtime_error(
            "rate multiplier out of range");
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransferRate] = std::uint32_t(
        1000000000 * multiplier);
    jv[jss::TransactionType] = "AccountSet";
    return jv;
}

Json::Value
regkey (Account const& account,
    disabled_t)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "SetRegularKey";
    return jv;
}

Json::Value
regkey (Account const& account,
    Account const& signer)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv["RegularKey"] = to_string(signer.id());
    jv[jss::TransactionType] = "SetRegularKey";
    return jv;
}

Json::Value
signers (Account const& account,
    std::uint32_t quorum,
        std::vector<signer> const& v)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "SignerListSet";
    jv["SignerQuorum"] = quorum;
    auto& ja = jv["SignerEntries"];
    ja.resize(v.size());
    for(std::size_t i = 0; i < v.size(); ++i)
    {
        auto const& e = v[i];
        auto& je = ja[i]["SignerEntry"];
        je[jss::Account] = e.account.human();
        je["SignerWeight"] = e.weight;
    }
    return jv;
}

Json::Value
signers (Account const& account, none_t)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "SignerListSet";
    return jv;
}

namespace ticket {

namespace detail {

Json::Value
create (Account const& account,
    boost::optional<Account> const& target,
        boost::optional<std::uint32_t> const& expire)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = "TicketCreate";
    if (expire)
        jv["Expiration"] = *expire;
    if (target)
        jv["Target"] = target->human();
    return jv;
}

} // detail

} // ticket

Json::Value
trust (Account const& account,
    STAmount const& amount)
{
    if (isXRP(amount))
        throw std::runtime_error(
            "trust() requires IOU");
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::LimitAmount] = amount.getJson(0);
    jv[jss::TransactionType] = "TrustSet";
    jv[jss::Flags] = 0;    // tfClearNoRipple;
    return jv;
}

void
fill_fee (Json::Value& jv,
    Ledger const& ledger)
{
    if (jv.isMember(jss::Fee))
        return;
    jv[jss::Fee] = std::to_string(
        ledger.getBaseFee());
}

void
fill_seq (Json::Value& jv,
    Ledger const& ledger)
{
    if (jv.isMember(jss::Sequence))
        return;
    RippleAddress ra;
    ra.setAccountID(jv[jss::Account].asString());
    auto const ar = ledger.fetch(
        getAccountRootIndex(ra.getAccountID()));

    jv[jss::Sequence] =
        ar->getFieldU32(sfSequence);
}

void
sign (Json::Value& jv,
    Account const& account)
{
    jv[jss::SigningPubKey] =
        strHex(make_Slice(
            account.pk().getAccountPublic()));
    Serializer ss;
    ss.add32 (HashPrefix::txSign);
    parse(jv).add(ss);
    jv[jss::TxnSignature] = strHex(make_Slice(
        account.sk().accountPrivateSign(
            ss.getData())));
}

STObject
parse (Json::Value const& jv)
{
    STParsedJSONObject p("tx_json", jv);
    if (! p.object)
        throw parse_error(
            rpcErrorString(p.error));
    return std::move(*p.object);
}

void
fee::operator()(Env const&, JTx& jt) const
{
    if (boost::indeterminate(b_))
        jt[jss::Fee] =
            v_.getJson(0);
    else
        jt.fill_fee = b_;
}

void
paths::operator()(Env const& env, JTx& jt) const
{
    auto& jv = jt.jv;
    auto const from = env.lookup(
        jv[jss::Account].asString());
    auto const to = env.lookup(
        jv[jss::Destination].asString());
    auto const amount = amountFromJson(
        sfAmount, jv[jss::Amount]);
    STPath fp;
    STPathSet ps;
    auto const found = findPathsForOneIssuer(
        std::make_shared<RippleLineCache>(
            env.ledger), from, to,
                in_, amount,
                    depth_, limit_, ps, fp);
    // VFALCO TODO API to allow caller to examine the STPathSet
    // VFALCO isDefault should be renamed to empty()
    if (found && ! ps.isDefault())
        jv[jss::Paths] = ps.getJson(0);
}

void
sendmax::operator()(Env const& env, JTx& jt) const
{
    jt.jv[jss::SendMax] = amount_.getJson(0);
}

void
txflags::operator()(Env const&, JTx& jt) const
{
    jt[jss::Flags] =
        v_ /*| tfUniversal*/;
}

void
msig::operator()(Env const& env, JTx& jt) const
{
    // VFALCO Inefficient pre-C++14
    auto accounts = accounts_;
    std::sort(accounts.begin(), accounts.end(),
        [](Account const& lhs, Account const& rhs)
        {
            return lhs.id() < rhs.id();
        });
    jt.signer = [accounts, &env](Env&, JTx& jt)
    {
        jt["SigningPubKey"] = "";
        boost::optional<STObject> st;
        try
        {
            st = parse(jt.jv);
        }
        catch(parse_error const&)
        {
            env.test.log << pretty(jt.jv);
            throw;
        }
        auto const signingForID = [](Json::Value const& jv)
            {
                RippleAddress ra;
                ra.setAccountID(jv[jss::Account].asString());
                return ra.getAccountID();
            }(jt.jv);
        auto& jv = jt["MultiSigners"][0u]["SigningFor"];
        jv[jss::Account] = jt[jss::Account];
        auto& js = jv["SigningAccounts"];
        js.resize(accounts.size());
        for(std::size_t i = 0; i < accounts.size(); ++i)
        {
            auto const& e = accounts[i];
            auto& jo = js[i]["SigningAccount"];
            jo[jss::Account] = e.human();
            jo[jss::SigningPubKey] = strHex(make_Slice(
                e.pk().getAccountPublic()));

            Serializer ss;
            ss.add32 (HashPrefix::txMultiSign);
            st->addWithoutSigningFields(ss);
            ss.add160(signingForID);
            ss.add160(e.id());
            jo["MultiSignature"] = strHex(make_Slice(
                e.sk().accountPrivateSign(ss.getData())));

        }
    };
}

msig2_t::msig2_t (std::vector<std::pair<
    Account, Account>> sigs)
{
    for (auto& sig : sigs)
    {
        auto result = sigs_.emplace(
            std::piecewise_construct,
                std::make_tuple(std::move(sig.first)),
                    std::make_tuple());
        result.first->second.emplace(
            std::move(sig.second));
    }
}

void
msig2_t::operator()(Env const& env, JTx& jt) const
{
    // VFALCO Inefficient pre-C++14
    auto const sigs = sigs_;
    jt.signer = [sigs, &env](Env&, JTx& jt)
    {
        jt["SigningPubKey"] = "";
        boost::optional<STObject> st;
        try
        {
            st = parse(jt.jv);
        }
        catch(parse_error const&)
        {
            env.test.log << pretty(jt.jv);
            throw;
        }
        auto& ja = jt["MultiSigners"];
        ja.resize(sigs.size());
        for (auto i = std::make_pair(0, sigs.begin());
            i.first < sigs.size(); ++i.first, ++i.second)
        {
            auto const& sign_for = i.second->first;
            auto const& list = i.second->second;
            auto& ji = ja[i.first]["SigningFor"];
            ji[jss::Account] = sign_for.human();
            auto& js = ji["SigningAccounts"];
            js.resize(list.size());
            for (auto j = std::make_pair(0, list.begin());
                j.first < list.size(); ++j.first, ++j.second)
            {
                auto& jj = js[j.first]["SigningAccount"];
                jj[jss::Account] = j.second->human();
                jj[jss::SigningPubKey] = strHex(make_Slice(
                    j.second->pk().getAccountPublic()));

                Serializer ss;
                ss.add32 (HashPrefix::txMultiSign);
                st->addWithoutSigningFields(ss);
                ss.add160(sign_for.id());
                ss.add160(j.second->id());
                jj["MultiSignature"] = strHex(make_Slice(
                    j.second->sk().accountPrivateSign(
                        ss.getData())));
            }
        }
    };
}

void
seq::operator()(Env const&, JTx& jt) const
{
    if (boost::indeterminate(b_))
        jt[jss::Sequence] = v_;
    else
        jt.fill_seq = b_;
}

void
sig::operator()(Env const&, JTx& jt) const
{
    if(boost::indeterminate(b_))
    {
        // VFALCO Inefficient pre-C++14
        auto const account = account_;
        jt.signer = [account](Env&, JTx& jt)
        {
            jtx::sign(jt.jv, account);
        };
    }
    else
    {
        jt.fill_sig = b_;
    }
}

//------------------------------------------------------------------------------
//
// Conditions
//
//------------------------------------------------------------------------------

namespace cond {

void
balance::operator()(Env const& env) const
{
    if (isXRP(value_.issue()))
    {
        auto const sle = env.le(account_);

        if (none_)
            env.test.expect(! sle);
        else
            env.test.expect(sle->getFieldAmount(
                sfBalance) == value_);
    }
    else
    {
        auto const sle = env.le(
            getRippleStateIndex(account_.id(),
                value_.issue()));
        if (none_)
        {
            env.test.expect(! sle);
        }
        else if (env.test.expect(sle))
        {
            auto amount =
                sle->getFieldAmount(sfBalance);
            amount.setIssuer(
                value_.issue().account);
            if (account_.id() >
                    value_.issue().account)
                amount.negate();
            env.test.expect(amount == value_);
        }
    }
}

void
flags::operator()(Env const& env) const
{
    auto const sle = env.le(account_);
    if (sle->isFieldPresent(sfFlags))
        env.test.expect((sle->getFieldU32(sfFlags) &
            mask_) == mask_);
    else
        env.test.expect(mask_ == 0);
}

void
nflags::operator()(Env const& env) const
{
    auto const sle = env.le(account_);
    if (sle->isFieldPresent(sfFlags))
        env.test.expect((sle->getFieldU32(sfFlags) &
            mask_) == 0);
    else
        env.test.pass();
}

namespace detail {

std::uint32_t
owned_count_of(Ledger const& ledger,
    ripple::Account const& id,
        LedgerEntryType type)
{
    std::uint32_t count = 0;
    forEachItem(ledger, id, getApp().getSLECache(),
        [&count, type](std::shared_ptr<SLE const> const& sle)
        {
            if (sle->getType() == type)
                ++count;
        });
    return count;
}

void
owned_count_helper(Env const& env,
    ripple::Account const& id,
        LedgerEntryType type,
            std::uint32_t value)
{
    env.test.expect(owned_count_of(
        *env.ledger, id, type) == value);
}

} // detail

void
owners::operator()(Env const& env) const
{
    env.test.expect(env.le(
        account_)->getFieldU32(sfOwnerCount) ==
            value_);
}

} // cond

} // jtx

} // test
} // ripple
