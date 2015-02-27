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
#include <ripple/app/consensus/LedgerConsensus.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/basics/seconds_clock.h>
#include <ripple/crypto/KeyType.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/TxFlags.h>
#include <beast/unit_test/suite.h>
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

struct Amount
{
    Amount(double value_, std::string currency_, TestAccount issuer_);

    Json::Value
    getJson() const;

private:
    double value;
    std::string currency;
    TestAccount issuer;
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
Ledger::pointer
createGenesisLedger(std::uint64_t start_amount_drops, TestAccount const& master);

// Create an account represented by public RippleAddress and private
// RippleAddress
TestAccount
createAccount(std::string const& passphrase, KeyType keyType);

void
freezeAccount(TestAccount& account, Ledger::pointer const& ledger, bool sign = true);

void
unfreezeAccount(TestAccount& account, Ledger::pointer const& ledger, bool sign = true);

STTx
getPaymentTx(TestAccount& from, TestAccount const& to,
            std::uint64_t amountDrops,
            bool sign = true);

STTx
makeAndApplyPayment(TestAccount& from, TestAccount const& to,
            std::uint64_t amountDrops,
            Ledger::pointer const& ledger, bool sign = true);

STTx
getPaymentTx(TestAccount& from, TestAccount const& to,
            std::string const& currency, std::string const& amount,
            bool sign = true);

STTx
makeAndApplyPayment(TestAccount& from, TestAccount const& to,
            std::string const& currency, std::string const& amount,
            Ledger::pointer const& ledger, bool sign = true);

void
createOffer(TestAccount& from, Amount const& in, Amount const& out,
            Ledger::pointer ledger, bool sign = true);

// As currently implemented, this will cancel only the last offer made
// from this account.
void
cancelOffer(TestAccount& from, Ledger::pointer ledger, bool sign = true);

void
makeTrustSet(TestAccount& from, TestAccount const& issuer,
                std::string const& currency, double amount,
                Ledger::pointer const& ledger, bool sign = true);

Ledger::pointer
close_and_advance(Ledger::pointer ledger, Ledger::pointer LCL);

} // test
} // ripple

#endif
