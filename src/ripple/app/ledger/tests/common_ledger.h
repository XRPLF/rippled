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

#ifndef RIPPLE_APP_TESTS_COMMON_LEDGER_H_INCLUDED
#define RIPPLE_APP_TESTS_COMMON_LEDGER_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerConsensus.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/chrono.h>
#include <ripple/crypto/KeyType.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <beast/unit_test/suite.h>
#include <beast/streams/abstract_ostream.h>
#include <chrono>
#include <string>

namespace ripple {
namespace test {

struct TestAccount
{
    RippleAddress pk;
    RippleAddress sk;
    unsigned sequence;
};

struct TestJson
{
    Json::Value
    getJson() const;

    virtual void
    getJson(Json::Value& tx_json) const = 0;
};

struct Currency : TestJson
{
    Currency(std::string currency);

    void
    getJson(Json::Value& tx_json) const override;

    std::string
    getCurrency() const;

    using TestJson::getJson;

private:
    std::string currency_;
};

struct Issuer : TestJson
{
    Issuer(TestAccount issuer);

    void
    getJson(Json::Value& tx_json) const override;

    TestAccount const&
    getAccount() const;

    using TestJson::getJson;

private:
    TestAccount issuer_;
};

struct Amount : TestJson
{
    Amount(double value, std::string currency, TestAccount issuer);

    void
    getJson(Json::Value& tx_json) const override;

    double
    getValue() const;

    TestAccount const&
    getIssuer() const;

    Currency const&
    getCurrency() const;

    using TestJson::getJson;

private:
    double value_;
    Currency currency_;
    Issuer issuer_;
};

// Helper function to parse a transaction in Json, sign it with account,
// and return it as a STTx
STTx
parseTransaction(TestAccount& account, Json::Value const& tx_json, bool sign = true);

// Helper function to apply a transaction to a ledger
void
applyTransaction(Ledger::pointer const& ledger, STTx const& tx, bool check = true);

// Create genesis ledger from a start amount in drops, and the public
// master RippleAddress
std::pair<std::shared_ptr<Ledger const>, Ledger::pointer>
createGenesisLedger(std::uint64_t start_amount_drops, TestAccount const& master);

// Create an account represented by public RippleAddress and private
// RippleAddress
TestAccount
createAccount(std::string const& passphrase, KeyType keyType);

TestAccount
createAndFundAccount(TestAccount& from, std::string const& passphrase,
    KeyType keyType, std::uint64_t amountDrops,
    Ledger::pointer const& ledger, bool sign = true);

std::map<std::string, TestAccount>
createAndFundAccounts(TestAccount& from, std::vector<std::string> passphrases,
    KeyType keyType, std::uint64_t amountDrops,
    Ledger::pointer const& ledger, bool sign = true);

std::map<std::string, TestAccount>
createAndFundAccountsWithFlags(TestAccount& from,
    std::vector<std::string> passphrases,
    KeyType keyType, std::uint64_t amountDrops,
    Ledger::pointer& ledger,
    std::shared_ptr<Ledger const>& LCL,
    const std::uint32_t flags, bool sign = true);

void
setAccountFlags(TestAccount& account, Ledger::pointer const& ledger,
    const std::uint32_t flags, bool sign = true);

void
setAllAccountFlags(std::vector<TestAccount>& accounts, Ledger::pointer const& ledger,
    const std::uint32_t flags, bool sign = true);

template<class key_t>
void
setAllAccountFlags(std::map<key_t, TestAccount>& accounts, Ledger::pointer const& ledger,
const std::uint32_t flags, bool sign = true)
{
    for (auto& pair : accounts)
    {
        setAccountFlags(pair.second, ledger, flags, sign);
    }
}

void
clearAccountFlags(TestAccount& account, Ledger::pointer const& ledger,
    const std::uint32_t flags, bool sign = true);

void
freezeAccount(TestAccount& account, Ledger::pointer const& ledger, bool sign = true);

void
unfreezeAccount(TestAccount& account, Ledger::pointer const& ledger, bool sign = true);

STTx
getPaymentTx(TestAccount& from, TestAccount const& to,
            std::uint64_t amountDrops,
            bool sign = true);

STTx
pay(TestAccount& from, TestAccount const& to,
            std::uint64_t amountDrops,
            Ledger::pointer const& ledger, bool sign = true);

STTx
getPaymentTx(TestAccount& from, TestAccount const& to,
            std::string const& currency, std::string const& amount,
            bool sign = true);

STTx
pay(TestAccount& from, TestAccount const& to,
            std::string const& currency, std::string const& amount,
            Ledger::pointer const& ledger, bool sign = true);

STTx
getPaymentTxWithPath(TestAccount& from, TestAccount const& to,
    std::string const& currency, std::string const& amount,
    Ledger::pointer const& ledger, bool sign = true);

STTx
payWithPath(TestAccount& from, TestAccount const& to,
    std::string const& currency, std::string const& amount,
    Ledger::pointer const& ledger, bool sign = true);

STTx
payWithPath(TestAccount& from, TestAccount const& to,
    std::string const& currency, std::string const& amount,
    Ledger::pointer const& ledger, Json::Value const& path,
    std::uint32_t flags,
    bool sign = true);

void
createOffer(TestAccount& from, Amount const& in, Amount const& out,
            Ledger::pointer ledger, bool sign = true);

void
createOfferWithFlags(TestAccount& from, Amount const& in, Amount const& out,
                     Ledger::pointer ledger, std::uint32_t flags,
                     bool sign = true);

// As currently implemented, this will cancel only the last offer made
// from this account.
void
cancelOffer(TestAccount& from, Ledger::pointer ledger, bool sign = true);

void
trust(TestAccount& from, TestAccount const& issuer,
                std::string const& currency, double amount,
                Ledger::pointer const& ledger, bool sign = true);

void
close_and_advance(Ledger::pointer& ledger, std::shared_ptr<Ledger const>& LCL);

Json::Value findPath(Ledger::pointer ledger, TestAccount const& src,
    TestAccount const& dest, std::vector<Currency> srcCurrencies,
    Amount const& dstAmount, beast::abstract_ostream& log,
    boost::optional<Json::Value> contextPaths = boost::none);

struct OfferPathNode
{
    std::string currency;
    boost::optional<TestAccount> issuer;
    OfferPathNode(std::string s, TestAccount const& iss)
            :currency(std::move(s)), issuer(iss) {}
};

Json::Value pathNode (TestAccount const& acc);

Json::Value pathNode (OfferPathNode const& offer);

inline void createPathHelper (Json::Value& result)
{
    // base case
}

template<class First, class... Rest>
void createPathHelper (Json::Value& result,
                       First&& first,
                       Rest&&... rest)
{
    result.append (pathNode (std::forward<First> (first)));
    createPathHelper(result, rest...);
}

template<class First, class... Rest>
Json::Value createPath (First&& first,
                        Rest&&... rest)
{
    Json::Value result;
    createPathHelper (result, first, rest...);
    return result;
}


SLE::pointer
get_ledger_entry_ripple_state(Ledger::pointer ledger,
    RippleAddress account1, RippleAddress account2,
    Currency currency);

void
verifyBalance(Ledger::pointer ledger, TestAccount const& account, Amount const& amount);

} // test
} // ripple

#endif
