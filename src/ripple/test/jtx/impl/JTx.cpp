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

#include <BeastConfig.h>
#include <ripple/test/jtx/JTx.h>
#include <ripple/test/jtx/Env.h>
#include <ripple/app/main/Application.h>
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

} // jtx
} // test
} // ripple
