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
#include <ripple/test/jtx/balance.h>
#include <ripple/test/jtx/Env.h>
#include <ripple/test/jtx/fee.h>
#include <ripple/test/jtx/flags.h>
#include <ripple/test/jtx/pay.h>
#include <ripple/test/jtx/trust.h>
#include <ripple/test/jtx/require.h>
#include <ripple/test/jtx/seq.h>
#include <ripple/test/jtx/sig.h>
#include <ripple/test/jtx/utility.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/app/tx/TransactionEngine.h>
#include <ripple/basics/Slice.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFlags.h>
#include <memory>
// VFALCO TODO Use AnyPublicKey, AnySecretKey, AccountID

namespace ripple {
namespace test {

namespace jtx {

Env::Env (beast::unit_test::suite& test_)
    : test(test_)
    , master("master", generateKeysFromSeed(
        KeyType::secp256k1, RippleAddress::createSeedGeneric(
            "masterpassphrase")))
{
    memoize(master);
    initializePathfinding();
    ledger = std::make_shared<Ledger>(
        master.pk(), SYSTEM_CURRENCY_START);
}

void
Env::memoize (Account const& account)
{
    map_.emplace(account.id(), account);
}

Account const&
Env::lookup (AccountID const& id) const
{
    auto const iter = map_.find(id);
    if (iter == map_.end())
        throw std::runtime_error(
            "Env::lookup:: unknown account ID");
    return iter->second;
}

Account const&
Env::lookup (std::string const& base58ID) const
{
    RippleAddress ra;
    if (! ra.setAccountID(base58ID))
        throw std::runtime_error(
            "Env::lookup: invalid account ID");
    return lookup(ra.getAccountID());
}

PrettyAmount
Env::balance (Account const& account) const
{
    auto const sle = le(account);
    if (! sle)
        return XRP(0);
    return {
        sle->getFieldAmount(sfBalance),
            "" };
}

PrettyAmount
Env::balance (Account const& account,
    Issue const& issue) const
{
    if (isXRP(issue.currency))
        return balance(account);
    auto const sle = le(getRippleStateIndex(
        account.id(), issue));
    if (! sle)
        return { STAmount( issue, 0 ),
            account.name() };
    auto amount = sle->getFieldAmount(sfBalance);
    amount.setIssuer(issue.account);
    if (account.id() > issue.account)
        amount.negate();
    return { amount,
        lookup(issue.account).name() };
}

std::uint32_t
Env::seq (Account const& account) const
{
    auto const sle = le(account);
    if (! sle)
        throw std::runtime_error(
            "missing account root");
    return sle->getFieldU32(sfSequence);
}

std::shared_ptr<SLE const>
Env::le (Account const& account) const
{
    return ledger->fetch(
        getAccountRootIndex(account.id()));
}

std::shared_ptr<SLE const>
Env::le (uint256 const& key) const
{
    return ledger->fetch(key);
}

void
Env::fund (bool setDefaultRipple,
    STAmount const& amount,
        Account const& account)
{
    memoize(account);
    if (setDefaultRipple)
    {
        // VFALCO NOTE Is the fee formula correct?
        apply(pay(master, account, amount +
            drops(ledger->getBaseFee())),
                jtx::seq(jtx::autofill),
                    fee(jtx::autofill),
                        sig(jtx::autofill));
        apply(fset(account, asfDefaultRipple),
            jtx::seq(jtx::autofill),
                fee(jtx::autofill),
                    sig(jtx::autofill));
        require(flags(account, asfDefaultRipple));
    }
    else
    {
        apply(pay(master, account, amount),
            jtx::seq(jtx::autofill),
                fee(jtx::autofill),
                    sig(jtx::autofill));
        require(nflags(account, asfDefaultRipple));
    }
    require(jtx::balance(account, amount));
}

void
Env::trust (STAmount const& amount,
    Account const& account)
{
    auto const start = balance(account);
    apply(jtx::trust(account, amount),
        jtx::seq(jtx::autofill),
            fee(jtx::autofill),
                sig(jtx::autofill));
    apply(pay(master, account,
        drops(ledger->getBaseFee())),
            jtx::seq(jtx::autofill),
                fee(jtx::autofill),
                    sig(jtx::autofill));
    test.expect(balance(account) == start);
}

void
Env::submit (JTx const& jt)
{
    auto const stx = st(jt);
    TER ter;
    bool didApply;
    if (stx)
    {
        TransactionEngine txe (ledger,
            tx_enable_test);
        std::tie(ter, didApply) = txe.applyTransaction(
            *stx, tapOPEN_LEDGER |
                (true ? tapNONE : tapNO_CHECK_SIGN));
    }
    else
    {
        // Convert the exception into a TER so that
        // callers can expect it using ter(temMALFORMED)
        ter = temMALFORMED;
        didApply = false;
    }
    if (! test.expect(ter == jt.ter,
        "apply: " + transToken(ter) +
            " (" + transHuman(ter) + ")"))
    {
        test.log << pretty(jt.jv);
        // Don't check postconditions if
        // we didn't get the expected result.
        return;
    }
    if (trace_)
    {
        if (trace_ > 0)
            --trace_;
        test.log << pretty(jt.jv);
    }
    for (auto const& f : jt.requires)
        f(*this);
}

void
Env::autofill_sig (JTx& jt)
{
    auto& jv = jt.jv;
    if (jt.signer)
        jt.signer(*this, jt);
    else if(jt.fill_sig)
    {
        auto const account =
            lookup(jv[jss::Account].asString());
        auto const ar = le(account);
        if (ar && ar->isFieldPresent(sfRegularKey))
            jtx::sign(jv, lookup(
                ar->getFieldAccount160(sfRegularKey)));
        else
            jtx::sign(jv, account);
    }
}

void
Env::autofill (JTx& jt)
{
    auto& jv = jt.jv;
    if(jt.fill_fee)
        jtx::fill_fee(jv, *ledger);
    if(jt.fill_seq)
        jtx::fill_seq(jv, *ledger);
    // Must come last
    try
    {
        autofill_sig(jt);
    }
    catch (parse_error const&)
    {
        test.log <<
            "parse failed:\n" <<
            pretty(jv);
        throw;
    }
}

std::shared_ptr<STTx>
Env::st (JTx const& jt)
{
    // The parse must succeed, since we
    // generated the JSON ourselves.
    boost::optional<STObject> obj;
    try
    {
        obj = jtx::parse(jt.jv);
    }
    catch(jtx::parse_error const&)
    {
        test.log <<
            "Exception: parse_error\n" <<
            pretty(jt.jv);
        throw;
    }

    try
    {
        return std::make_shared<STTx>(
            std::move(*obj));
    }
    catch(...)
    {
    }
    return nullptr;
}

} // jtx

} // test
} // ripple
