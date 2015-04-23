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

#include <ripple/protocol/Indexes.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/rpc/impl/RipplePathFind.h>
#include <ripple/json/json_writer.h>

namespace ripple {
namespace test {

Json::Value
TestJson::getJson() const
{
    Json::Value tx_json;
    getJson(tx_json);
    return tx_json;
}

Currency::Currency(std::string currency)
    : currency_(currency)
{
}

void
Currency::getJson(Json::Value& tx_json) const
{
    tx_json[jss::currency] = currency_;
}

std::string
Currency::getCurrency() const
{
    return currency_;
}


Issuer::Issuer(TestAccount issuer)
    :issuer_(issuer)
{
}

void
Issuer::getJson(Json::Value& tx_json) const
{
    tx_json[jss::issuer] = issuer_.pk.humanAccountID();
}

TestAccount const&
Issuer::getAccount() const
{
    return issuer_;
}


Amount::Amount(double value, std::string currency, TestAccount issuer)
    : value_(value)
    , currency_(Currency(currency))
    , issuer_(Issuer(issuer))
{
}

void
Amount::getJson(Json::Value& tx_json) const
{
    currency_.getJson(tx_json);
    issuer_.getJson(tx_json);
    tx_json[jss::value] = std::to_string(value_);
}

double
Amount::getValue() const
{
    return value_;
}

TestAccount const&
Amount::getIssuer() const
{
    return issuer_.getAccount();
}

Currency const&
Amount::getCurrency() const
{
    return currency_;
}


// Helper function to parse a transaction in Json, sign it with account,
// and return it as a STTx
STTx
parseTransaction(TestAccount& account, Json::Value const& tx_json, bool sign)
{
    STParsedJSONObject parsed("tx_json", tx_json);
    if (!parsed.object)
        throw std::runtime_error(
        "object not parseable");
    parsed.object->setFieldVL(sfSigningPubKey, account.pk.getAccountPublic());
    auto tx = STTx(std::move (*parsed.object));
    if (sign)
        tx.sign(account.sk);
    return tx;
}

// Helper function to apply a transaction to a ledger
void
applyTransaction(Ledger::pointer const& ledger, STTx const& tx, bool check)
{
    TransactionEngine engine(ledger);
    auto r = engine.applyTransaction(tx,
        tapOPEN_LEDGER | (check ? tapNONE : tapNO_CHECK_SIGN));
    if (r.first != tesSUCCESS)
        throw std::runtime_error("r != tesSUCCESS");
    if (!r.second)
        throw std::runtime_error("didApply");
}

// Create genesis ledger from a start amount in drops, and the public
// master RippleAddress
std::pair<Ledger::pointer, Ledger::pointer>
createGenesisLedger(std::uint64_t start_amount_drops, TestAccount const& master)
{
    initializePathfinding();
    Ledger::pointer ledger = std::make_shared<Ledger>(master.pk,
        start_amount_drops);
    ledger->updateHash();
    ledger->setClosed();
    if (!ledger->assertSane())
        throw std::runtime_error(
        "! ledger->assertSane()");
    return std::make_pair(std::move(ledger), std::make_shared<Ledger>(false, *ledger));
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

TestAccount
createAndFundAccount(TestAccount& from, std::string const& passphrase,
    KeyType keyType, std::uint64_t amountDrops,
    Ledger::pointer const& ledger, bool sign)
{
    auto to = createAccount(passphrase, keyType);
    pay(from, to, amountDrops, ledger, sign);
    return to;
}

std::map<std::string, TestAccount>
createAndFundAccounts(TestAccount& from, std::vector<std::string> passphrases,
    KeyType keyType, std::uint64_t amountDrops,
    Ledger::pointer const& ledger, bool sign)
{
    std::map<std::string, TestAccount> accounts;
    for (auto const& passphrase : passphrases)
    {
        auto to = createAndFundAccount(from, passphrase, keyType,
            amountDrops, ledger, sign);
        accounts.emplace(passphrase, std::move(to));
    }
    return accounts;
}

std::map<std::string, TestAccount>
createAndFundAccountsWithFlags(TestAccount& from,
    std::vector<std::string> passphrases,
    KeyType keyType, std::uint64_t amountDrops,
    Ledger::pointer& ledger,
    Ledger::pointer& LCL,
    const std::uint32_t flags, bool sign)
{
    auto accounts = createAndFundAccounts(from,
        passphrases, keyType, amountDrops, ledger, sign);
    close_and_advance(ledger, LCL);
    setAllAccountFlags(accounts, ledger, flags);
    close_and_advance(ledger, LCL);
    return accounts;
}

Json::Value
getCommonTransactionJson(TestAccount& account)
{
    Json::Value tx_json;
    tx_json[jss::Account] = account.pk.humanAccountID();
    tx_json[jss::Fee] = std::to_string(10);
    tx_json[jss::Sequence] = ++account.sequence;
    return tx_json;
}

void
setAccountFlags(TestAccount& account, Ledger::pointer const& ledger,
    const std::uint32_t flags, bool sign)
{
    Json::Value tx_json = getCommonTransactionJson(account);
    tx_json[jss::TransactionType] = "AccountSet";
    tx_json[jss::SetFlag] = flags;
    STTx tx = parseTransaction(account, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}

void
setAllAccountFlags(std::vector<TestAccount>& accounts, Ledger::pointer const& ledger,
const std::uint32_t flags, bool sign)
{
    for (auto& account : accounts)
    {
        setAccountFlags(account, ledger, flags, sign);
    }
}

void
clearAccountFlags(TestAccount& account, Ledger::pointer const& ledger,
    const std::uint32_t flags, bool sign)
{
    Json::Value tx_json = getCommonTransactionJson(account);
    tx_json[jss::TransactionType] = "AccountSet";
    tx_json[jss::ClearFlag] = flags;
    STTx tx = parseTransaction(account, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}

void
freezeAccount(TestAccount& account, Ledger::pointer const& ledger, bool sign)
{
    setAccountFlags(account, ledger, asfGlobalFreeze, sign);
}

void
unfreezeAccount(TestAccount& account, Ledger::pointer const& ledger, bool sign)
{
    clearAccountFlags(account, ledger, asfGlobalFreeze, sign);
}

Json::Value
getPaymentJson(TestAccount& from, TestAccount const& to,
    Json::Value amountJson)
{
    Json::Value tx_json = getCommonTransactionJson(from);
    tx_json[jss::Amount] = amountJson;
    tx_json[jss::Destination] = to.pk.humanAccountID();
    tx_json[jss::TransactionType] = "Payment";
    tx_json[jss::Flags] = tfUniversal;
    return tx_json;
}

STTx
getPaymentTx(TestAccount& from, TestAccount const& to,
    std::uint64_t amountDrops,
    bool sign)
{
    Json::Value tx_json = getPaymentJson(from, to,
        std::to_string(amountDrops));
    return parseTransaction(from, tx_json, sign);
}

STTx
pay(TestAccount& from, TestAccount const& to,
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
    Json::Value tx_json = getPaymentJson(from, to,
        Amount(std::stod(amount), currency, to).getJson());
    return parseTransaction(from, tx_json, sign);
}

STTx
pay(TestAccount& from, TestAccount const& to,
    std::string const& currency, std::string const& amount,
    Ledger::pointer const& ledger, bool sign)
{
    auto tx = getPaymentTx(from, to, currency, amount, sign);
    applyTransaction(ledger, tx, sign);
    return tx;
}

STTx
getPaymentTxWithPath(TestAccount& from, TestAccount const& to,
std::string const& currency, std::string const& amount,
Ledger::pointer const& ledger, bool sign)
{
    auto amountJson = Amount(std::stod(amount), currency, to).getJson();
    Json::Value tx_json = getPaymentJson(from, to, amountJson);

    // Find path. Note that the sign command can do this transparently
    // with the "build_path" field, but we don't have that here.
    auto cache = std::make_shared<RippleLineCache>(ledger);
    STPathSet pathSet;
    STPath fullLiquidityPath;
    auto stDstAmount = amountFromJson(sfGeneric, amountJson);
    Issue srcIssue = Issue(stDstAmount.getCurrency(), from.pk.getAccountID());

    auto found = findPathsForOneIssuer(cache, from.pk.getAccountID(), to.pk.getAccountID(),
        srcIssue, stDstAmount, 7, 4, pathSet, fullLiquidityPath);
    if (!found)
        throw std::runtime_error(
        "!found");
    if (pathSet.isDefault())
        throw std::runtime_error(
        "pathSet.isDefault()");

    tx_json[jss::Paths] = pathSet.getJson(0);

    return parseTransaction(from, tx_json, sign);
}

STTx
payWithPath(TestAccount& from, TestAccount const& to,
    std::string const& currency, std::string const& amount,
    Ledger::pointer const& ledger, bool sign)
{
    auto tx = getPaymentTxWithPath(from, to, currency, amount, ledger, sign);
    applyTransaction(ledger, tx, sign);
    return tx;
}

STTx
payWithPath(TestAccount& from, TestAccount const& to,
    std::string const& currency, std::string const& amount,
    Ledger::pointer const& ledger, Json::Value const& path,
    std::uint32_t flags, bool sign)
{
    auto amountJson = Amount(std::stod(amount), currency, to).getJson();
    Json::Value tx_json = getPaymentJson(from, to, amountJson);

    tx_json[jss::Paths] = path;
    tx_json[jss::Flags] = flags;

    auto tx = parseTransaction(from, tx_json, sign);
    applyTransaction(ledger, tx, sign);
    return tx;
}


void
createOffer(TestAccount& from, Amount const& in, Amount const& out,
    Ledger::pointer ledger, bool sign)
{
    Json::Value tx_json = getCommonTransactionJson(from);
    tx_json[jss::TransactionType] = "OfferCreate";
    tx_json[jss::TakerPays] = in.getJson();
    tx_json[jss::TakerGets] = out.getJson();
    STTx tx = parseTransaction(from, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}

void
createOfferWithFlags(TestAccount& from, Amount const& in, Amount const& out,
                     Ledger::pointer ledger, std::uint32_t flags,
                     bool sign)
{
    Json::Value tx_json = getCommonTransactionJson(from);
    tx_json[jss::TransactionType] = "OfferCreate";
    tx_json[jss::TakerPays] = in.getJson();
    tx_json[jss::TakerGets] = out.getJson();
    tx_json[jss::Flags] = flags;
    STTx tx = parseTransaction(from, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}


// As currently implemented, this will cancel only the last offer made
// from this account.
void
cancelOffer(TestAccount& from, Ledger::pointer ledger, bool sign)
{
    auto seq = from.sequence;
    Json::Value tx_json = getCommonTransactionJson(from);
    tx_json[jss::TransactionType] = "OfferCancel";
    tx_json[jss::OfferSequence] = seq;
    STTx tx = parseTransaction(from, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}

void
trust(TestAccount& from, TestAccount const& issuer,
    std::string const& currency, double amount,
    Ledger::pointer const& ledger, bool sign)
{
    Json::Value tx_json = getCommonTransactionJson(from);
    Json::Value& limitAmount = tx_json[jss::LimitAmount];
    limitAmount[jss::currency] = currency;
    limitAmount[jss::issuer] = issuer.pk.humanAccountID();
    limitAmount[jss::value] = std::to_string(amount);
    tx_json[jss::TransactionType] = "TrustSet";
    tx_json[jss::Flags] = 0;    // tfClearNoRipple;
    STTx tx = parseTransaction(from, tx_json, sign);
    applyTransaction(ledger, tx, sign);
}

void
close_and_advance(Ledger::pointer& ledger, Ledger::pointer& LCL)
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

    LCL = newLCL;
    ledger = std::make_shared<Ledger>(false, *LCL);
}

Json::Value findPath(Ledger::pointer ledger, TestAccount const& src,
    TestAccount const& dest, std::vector<Currency> srcCurrencies,
    Amount const& dstAmount, beast::abstract_ostream& log,
    boost::optional<Json::Value> contextPaths)
{
    int const level = 8;

    auto cache = std::make_shared<RippleLineCache>(ledger);

    STAmount saDstAmount;
    if (!amountFromJsonNoThrow(saDstAmount, dstAmount.getJson()))
        throw std::runtime_error(
        "!amountFromJsonNoThrow(saDstAmount, dstAmount.getJson())");
    log << "Dst amount: " << saDstAmount;

    auto jvSrcCurrencies = Json::Value(Json::arrayValue);
    for (auto const& srcCurrency : srcCurrencies)
    {
        jvSrcCurrencies.append(srcCurrency.getJson());
    }
    log << "Source currencies: " << jvSrcCurrencies;

    auto result = ripplePathFind(cache, src.pk, dest.pk, saDstAmount,
        ledger, jvSrcCurrencies, contextPaths, level);
    if(!result.first)
        throw std::runtime_error(
        "ripplePathFind find failed");

    return result.second;
}

SLE::pointer
getLedgerEntryRippleState(Ledger::pointer ledger,
    TestAccount const& account1, TestAccount const& account2,
    Currency currency)
{
    auto uNodeIndex = getRippleStateIndex(
        account1.pk.getAccountID(), account2.pk.getAccountID(),
        to_currency(currency.getCurrency()));

    if (!uNodeIndex.isNonZero())
        throw std::runtime_error(
        "!uNodeIndex.isNonZero()");

    return ledger->getSLEi(uNodeIndex);
}

void
verifyBalance(Ledger::pointer ledger, TestAccount const& account,
    Amount const& amount)
{
    auto sle = getLedgerEntryRippleState(ledger, account,
        amount.getIssuer(), amount.getCurrency());
    if (!sle)
        throw std::runtime_error(
        "!sle");
    STAmount amountReq;
    amountFromJsonNoThrow(amountReq, amount.getJson());

    auto high = sle->getFieldAmount(sfHighLimit);
    auto balance = sle->getFieldAmount(sfBalance);
    if (high.getIssuer() == account.pk.getAccountID())
    {
        balance.negate();
    }
    if (balance != amountReq)
        throw std::runtime_error(
        "balance != amountReq");
}

Json::Value pathNode (TestAccount const& acc)
{
    Json::Value result;
    result["account"] = acc.pk.humanAccountID();
    result["type"] = 1;
    result["type_hex"] = "0000000000000001";
    return result;
}

Json::Value pathNode (OfferPathNode const& offer)
{
    Json::Value result;
    result["currency"] = offer.currency;
    result["type"] = 48;
    result["type_hex"] = "0000000000000030";
    if (offer.issuer)
        result["issuer"] = offer.issuer->pk.humanAccountID();
    return result;
}


}
}
