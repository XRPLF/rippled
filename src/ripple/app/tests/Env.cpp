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
#include <ripple/app/tests/Env.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/app/tx/TransactionEngine.h>
#include <ripple/basics/Slice.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFlags.h>
// VFALCO TODO Use AnyPublicKey, AnySecretKey, AccountID

namespace ripple {
namespace test {

STAmount
AccountInfo::balance(
    Issue const& issue) const
{
    if (! root_)
        return STAmount(issue, 0, 0);
    if (isXRP(issue))
        return root_->getFieldAmount(sfBalance);
    auto amount = ledger_->fetch(getRippleStateIndex(
        account_, issue.account,
            issue.currency))->getFieldAmount(sfBalance);
    amount.setIssuer(issue.account);
    if (account_.id() > issue.account)
        amount.negate();
    return amount;
}

std::uint32_t
AccountInfo::seq() const
{
    return root_->getFieldU32(sfSequence);
}

std::uint32_t
AccountInfo::flags() const
{
    return root_->getFieldU32(sfFlags);
}

//------------------------------------------------------------------------------

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
Env::lookup (std::string const& base58ID) const
{
    RippleAddress ra;
    if (! ra.setAccountID(base58ID))
        throw std::runtime_error(
            "Env::lookup: invalid account ID");
    return lookup(ra.getAccountID());
}

Account const&
Env::lookup (ripple::Account const& id) const
{
    auto const iter = map_.find(id);
    if (iter == map_.end())
        throw std::runtime_error(
            "Env::lookup:: unknown account ID");
    return iter->second;
}

void
Env::fund (STAmount const& amount,
    Account const& account)
{
    using namespace jtx;
    memoize(account);
    apply(pay(master, account, amount),
        seq(jtx::autofill),
            fee(jtx::autofill),
                sig(jtx::autofill));
}

void
Env::trust (STAmount const& amount,
    Account const& account)
{
    using namespace jtx;
    apply(jtx::trust(account, amount),
        seq(jtx::autofill),
            fee(jtx::autofill),
                sig(jtx::autofill));
}

void
Env::submit (JTx const& tx)
{
    boost::optional<STTx> stx;
    {
        // The parse must succeed, since we
        // generated the JSON ourselves.
        boost::optional<STObject> st;
        try
        {
            st = jtx::parse(tx.jv);
        }
        catch(jtx::parse_error const&)
        {
            test.log << pretty(tx.jv);
            throw;
        }

        try
        {
            stx.emplace(std::move(*st));
        }
        catch(...)
        {
        }
    }

    TER ter;
    bool didApply;
    if (stx)
    {
        TransactionEngine txe (ledger, multisign);
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
    if (! test.expect(ter == tx.ter,
        "apply: " + transToken(ter) +
            " (" + transHuman(ter) + ")"))
        test.log << pretty(tx.jv);
}

void
Env::autofill (JTx& jt)
{
    auto& jv = jt.jv;
    auto const should = [](boost::tribool v, bool b)
    {
        if (boost::indeterminate(v))
            return b;
        return bool(v);
    };

    if(should(jt.fill_fee, fill_fee_))
        jtx::fill_fee(jv, *ledger);
    
    if(should(jt.fill_seq, fill_seq_))
        jtx::fill_seq(jv, *ledger);
    
    // Must come last
    if (jt.signer)
        jt.signer(*this, jt);
    else if(should(jt.fill_sig, fill_sig_))
    {
        auto const account =
            lookup(jv[jss::Account].asString());
        auto const ar =
            ledger->fetch(getAccountRootIndex(account));
        if (ar->isFieldPresent(sfRegularKey))
            jtx::sign(jv, lookup(
                ar->getFieldAccount160(sfRegularKey)));
        else
            jtx::sign(jv, account);
    }
}

} // test
} // ripple
