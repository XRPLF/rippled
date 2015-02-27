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

#include <ripple/app/tests/common_ledger.h>
#include <ripple/protocol/RippleAddress.h>

namespace ripple {
namespace test {

Amount::Amount(double value_, std::string currency_, TestAccount issuer_)
    : value(value_)
    , currency(currency_)
    , issuer(issuer_)
{
}

Json::Value
Amount::getJson() const
{
    Json::Value tx_json;
    tx_json["currency"] = currency;
    tx_json["issuer"] = issuer.pk.humanAccountID();
    tx_json["value"] = std::to_string(value);
    return tx_json;
}

// Helper function to parse a transaction in Json, sign it with account,
// and return it as a STTx
STTx
parseTransaction(TestAccount& account, Json::Value const& tx_json, bool sign)
{
    STParsedJSONObject parsed("tx_json", tx_json);
    std::unique_ptr<STObject> sopTrans = std::move(parsed.object);
    if (sopTrans == nullptr)
        throw std::runtime_error(
        "sopTrans == nullptr");
    sopTrans->setFieldVL(sfSigningPubKey, account.pk.getAccountPublic());
    auto tx = STTx(*sopTrans);
    if (sign)
        tx.sign(account.sk);
    return tx;
}

// Helper function to apply a transaction to a ledger
void
applyTransaction(Ledger::pointer const& ledger, STTx const& tx, bool check)
{
    TransactionEngine engine(ledger);
    bool didApply = false;
    auto r = engine.applyTransaction(tx, tapOPEN_LEDGER | (check ? tapNONE : tapNO_CHECK_SIGN),
        didApply);
    if (r != tesSUCCESS)
        throw std::runtime_error(
        "r != tesSUCCESS");
    if (!didApply)
        throw std::runtime_error(
        "didApply");
}

// Create genesis ledger from a start amount in drops, and the public
// master RippleAddress
Ledger::pointer
createGenesisLedger(std::uint64_t start_amount_drops, TestAccount const& master)
{
    Ledger::pointer ledger = std::make_shared<Ledger>(master.pk,
        start_amount_drops);
    ledger->updateHash();
    ledger->setClosed();
    if (!ledger->assertSane())
        throw std::runtime_error(
        "! ledger->assertSane()");
    return ledger;
}

// Create an account represented by public RippleAddress and private
// RippleAddress
TestAccount
createAccount(std::string const& passphrase, KeyType keyType)
{
    RippleAddress const seed
        = RippleAddress::createSeedGeneric(passphrase);

    auto keyPair = generateKeysFromSeed(keyType, seed);

    return {
        std::move(keyPair.publicKey),
        std::move(keyPair.secretKey),
        std::uint64_t(0)
    };
}

void
freezeAccount(TestAccount& account, Ledger::pointer const& ledger, bool sign)
{
    Json::Value tx_json;
    tx_json["TransactionType"] = "AccountSet";
    tx_json["Fee"] = std::to_string(10);
    tx_json["Account"] = account.pk.humanAccountID();
    tx_json["SetFlag"] = asfGlobalFreeze;
    tx_json["Sequence"] = ++account.sequence;
    STTx tx = parseTransaction(account, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}

void
unfreezeAccount(TestAccount& account, Ledger::pointer const& ledger, bool sign)
{
    Json::Value tx_json;
    tx_json["TransactionType"] = "AccountSet";
    tx_json["Fee"] = std::to_string(10);
    tx_json["Account"] = account.pk.humanAccountID();
    tx_json["ClearFlag"] = asfGlobalFreeze;
    tx_json["Sequence"] = ++account.sequence;
    STTx tx = parseTransaction(account, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}

STTx
getPaymentTx(TestAccount& from, TestAccount const& to,
    std::uint64_t amountDrops,
    bool sign)
{
    Json::Value tx_json;
    tx_json["Account"] = from.pk.humanAccountID();
    tx_json["Amount"] = std::to_string(amountDrops);
    tx_json["Destination"] = to.pk.humanAccountID();
    tx_json["TransactionType"] = "Payment";
    tx_json["Fee"] = std::to_string(10);
    tx_json["Sequence"] = ++from.sequence;
    tx_json["Flags"] = tfUniversal;
    return parseTransaction(from, tx_json, sign);
}

STTx
makeAndApplyPayment(TestAccount& from, TestAccount const& to,
    std::uint64_t amountDrops,
    Ledger::pointer const& ledger, bool sign)
{
    auto tx = getPaymentTx(from, to, amountDrops, sign);
    applyTransaction(ledger, tx, sign);
    return tx;
}

STTx
getPaymentTx(TestAccount& from, TestAccount const& to,
    std::string const& currency, std::string const& amount,
    bool sign)
{
    Json::Value tx_json;
    tx_json["Account"] = from.pk.humanAccountID();
    tx_json["Amount"] = Amount(std::stod(amount), currency, to).getJson();
    tx_json["Destination"] = to.pk.humanAccountID();
    tx_json["TransactionType"] = "Payment";
    tx_json["Fee"] = std::to_string(10);
    tx_json["Sequence"] = ++from.sequence;
    tx_json["Flags"] = tfUniversal;
    return parseTransaction(from, tx_json, sign);
}

STTx
makeAndApplyPayment(TestAccount& from, TestAccount const& to,
    std::string const& currency, std::string const& amount,
    Ledger::pointer const& ledger, bool sign)
{
    auto tx = getPaymentTx(from, to, currency, amount, sign);
    applyTransaction(ledger, tx, sign);
    return tx;
}

void
createOffer(TestAccount& from, Amount const& in, Amount const& out,
    Ledger::pointer ledger, bool sign)
{
    Json::Value tx_json;
    tx_json["TransactionType"] = "OfferCreate";
    tx_json["Fee"] = std::to_string(10);
    tx_json["Account"] = from.pk.humanAccountID();
    tx_json["TakerPays"] = in.getJson();
    tx_json["TakerGets"] = out.getJson();
    tx_json["Sequence"] = ++from.sequence;
    STTx tx = parseTransaction(from, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}

// As currently implemented, this will cancel only the last offer made
// from this account.
void
cancelOffer(TestAccount& from, Ledger::pointer ledger, bool sign)
{
    Json::Value tx_json;
    tx_json["TransactionType"] = "OfferCancel";
    tx_json["Fee"] = std::to_string(10);
    tx_json["Account"] = from.pk.humanAccountID();
    tx_json["OfferSequence"] = from.sequence;
    tx_json["Sequence"] = ++from.sequence;
    STTx tx = parseTransaction(from, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}

void
makeTrustSet(TestAccount& from, TestAccount const& issuer,
    std::string const& currency, double amount,
    Ledger::pointer const& ledger, bool sign)
{
    Json::Value tx_json;
    tx_json["Account"] = from.pk.humanAccountID();
    Json::Value& limitAmount = tx_json["LimitAmount"];
    limitAmount["currency"] = currency;
    limitAmount["issuer"] = issuer.pk.humanAccountID();
    limitAmount["value"] = std::to_string(amount);
    tx_json["TransactionType"] = "TrustSet";
    tx_json["Fee"] = std::to_string(10);
    tx_json["Sequence"] = ++from.sequence;
    tx_json["Flags"] = tfClearNoRipple;
    STTx tx = parseTransaction(from, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}

Ledger::pointer
close_and_advance(Ledger::pointer ledger, Ledger::pointer LCL)
{
    std::shared_ptr<SHAMap> set = ledger->peekTransactionMap();
    CanonicalTXSet retriableTransactions(set->getHash());
    Ledger::pointer newLCL = std::make_shared<Ledger>(false, *LCL);
    // Set up to write SHAMap changes to our database,
    //   perform updates, extract changes
    applyTransactions(set, newLCL, newLCL, retriableTransactions, false);
    newLCL->updateSkipList();
    newLCL->setClosed();
    newLCL->peekAccountStateMap()->flushDirty(
        hotACCOUNT_NODE, newLCL->getLedgerSeq());
    newLCL->peekTransactionMap()->flushDirty(
        hotTRANSACTION_NODE, newLCL->getLedgerSeq());
    using namespace std::chrono;
    auto const epoch_offset = days(10957);  // 2000-01-01
    std::uint32_t closeTime = time_point_cast<seconds>  // now
        (system_clock::now() - epoch_offset).
        time_since_epoch().count();
    int closeResolution = seconds(LEDGER_TIME_ACCURACY).count();
    bool closeTimeCorrect = true;
    newLCL->setAccepted(closeTime, closeResolution, closeTimeCorrect);
    return newLCL;
}


}
}