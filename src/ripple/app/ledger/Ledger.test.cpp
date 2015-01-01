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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/consensus/LedgerConsensus.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/basics/seconds_clock.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/basics/TestSuite.h>

// Added
#include <ripple/json/json_reader.h>
#include <fstream>
#include <ripple/app/paths/RippleState.h>

// These are undefined at the end of the file. At first they were defined in the
// scope of a function, but of course, as useful as they are, I missed them,
// soon as I started writing a test for something else. There's a certain
// gravity towards defining them (or something like them) available to all
// tests.

#define FILE_POSITION() to_string(__FILE__) + ":" + to_string(__LINE__)
#define ANNOTATE_MSG(msg) "`" + to_string(#msg) + "` @ " + FILE_POSITION()
// #2 failed: `!c.makePayment(root, gw2, xrp(4000))` @ src/ripple/app/ledger/Ledger.test.cpp:392:

// With message passing ability, using 2,3 etc, to refer to number of args.
#define EXPECT2(expr, msg) expect((expr), (ANNOTATE_MSG(expr)) + ": " + #msg);
#define EXPECT_EQ3(a, b, msg) expectEquals(a, b, (ANNOTATE_MSG(a == b)) + ": " + #msg);

// Note the trailing comma! Silent msg
#define EXPECT(expr) EXPECT2((expr), );
// #13 failed: `aliceLimit.getText() == "12"` @ src/ripple/app/ledger/Ledger.test.cpp:423:
// Actual: 1
// Expected: 12
#define EXPECT_EQ(a, b) EXPECT_EQ3(a, b, );

/* -------------------------------------------------------------------------- */

// DSL implemented in C++. What? You think some custom lang is going to be less
// prone to errors??? You wanna messe around with json declarations ??? and
// constantly wondering if the darn thing is buggy. This is pretty simple to
// understand, is `just C++` and gives useful source specific error messages.

// TODO: some way of defining/undefining per test
#define AFFECTED(name, index, is_type) EXPECT(nodes.contains(index));\
        Affected& name = nodes[index];\
        EXPECT(name.is_type);

#define CHANGEDP(affected, fieldName, prop, beforeValue, afterValue) \
        EXPECT(!affected.is_created) \
        EXPECT_EQ((affected.b4()[fieldName])prop, (beforeValue)) \
        EXPECT_EQ((affected.aft()[fieldName])prop, (afterValue));

// Note the `, ,` empty prop value forwarded to CHANGEDP
#define CHANGED(affected, fieldName, beforeValue, afterValue) \
        CHANGEDP(affected, fieldName, ,beforeValue, afterValue);

#define UNCHANGED(affected, fieldName) \
        EXPECT(!affected.is_created) \
        EXPECT_EQ(affected.b4()[fieldName], affected.aft()[fieldName]);

#define NEW(affected, fieldName, value) \
        EXPECT(affected.is_created) \
        EXPECT_EQ(affected.aft()[fieldName], (value));

// Repeating the `name` 3 times is a pain.
#define TRANSACTOR_TEST(name, block) \
        class name##_test : public TestSuite, public test::TestContext {\
            void \
            run () \
            { \
                using namespace test; \
                auto restore (logSuppressor()); \
                testcase(#name); \
                block \
            } \
        }; \
        BEAST_DEFINE_TESTSUITE(name,ripple_app,ripple);

/* -------------------------------------------------------------------------- */

namespace ripple {
namespace test {

typedef std::function<void (STTx& ref)> TxConfigurator;

static const TxConfigurator NOOPCONF = [](STTx& c){};

std::vector<std::string>
split(std::string const & s, char delim)
{
  std::stringstream ss(s);
  std::string item;
  std::vector<std::string> elems;
  while (std::getline(ss, item, delim))
  {
    elems.push_back(item);
  }
  return elems;
}

struct SeverityRestorator
{
    beast::Journal::Severity to;
    ~SeverityRestorator() {
        deprecatedLogs().severity(to);
    }
};

SeverityRestorator
logSuppressor(beast::Journal::Severity allowed = beast::Journal::kFatal)
{
    auto to = deprecatedLogs().severity();
    deprecatedLogs().severity(allowed);
    return {to};
}

STAmount
xrp(double n)
{
    bool negative (n < 0);
    if (negative) n = -n;
    return STAmount(static_cast<std::int64_t>(n * 1000000), negative);
}

/// Wraps LedgerEntrySet iterator elements.
struct LESIView
{
    std::pair<uint256 const&, LedgerEntrySetEntry> const& it;
    uint256 const& index() {return it.first; }
    SLE::ref entry() {return it.second.mEntry; }
    LedgerEntryType type() {return it.second.mEntry->getType(); }
    LedgerEntryAction action() {return it.second.mAction; }
};

// Create genesis ledger from a start amount in drops, and the public
// root RippleAddress
Ledger::pointer
createGenesisLedger(std::uint64_t start_amount_drops,
                    RippleAddress const& root)
{
    Ledger::pointer ledger =
        std::make_shared<Ledger>(root, start_amount_drops);
    ledger->updateHash();
    ledger->setClosed();
    return ledger;
}

Ledger::pointer
closeAndAdvance(Ledger::pointer& ledger, Ledger::pointer& LCL,
                std::uint32_t closeTime = 0)
{
    SHAMap::pointer set = ledger->peekTransactionMap();
    CanonicalTXSet retriableTransactions{set->getHash()};
    Ledger::pointer newLCL = std::make_shared<Ledger>(false, *LCL);
    applyTransactions(set, newLCL, newLCL, retriableTransactions, false);
    newLCL->updateSkipList();
    newLCL->setClosed();

    using namespace std::chrono;
    if (closeTime == 0)
    {
        auto const epoch_offset = days{10957};  // 2000-01-01
        closeTime = time_point_cast<seconds>  // now
                                    (system_clock::now()-epoch_offset).
                                     time_since_epoch().count();
    }
    int CloseResolution = seconds{LEDGER_TIME_ACCURACY}.count();
    bool closeTimeUnrounded = true;
    newLCL->setAccepted(closeTime, CloseResolution, closeTimeUnrounded);
    return newLCL;
}

void
closeAndAdvanceByRef(Ledger::pointer& ledger,
                     Ledger::pointer& LCL,
                     std::uint32_t closeTime = 0)
{
    LCL = closeAndAdvance(ledger, LCL, closeTime);
    ledger = std::make_shared<Ledger>(false, *LCL);
}

STAmount
xrpChange (STTx& tx, Ledger::ref beforeTx, LedgerEntrySet& view)
{
   auto xrpChange = tx[sfFee];

   for (auto& it : view)
   {
       test::LESIView v {it};
       SLE& sle = (*v.entry());
       auto action = v.action();

       if (v.type() == ltACCOUNT_ROOT)
       {
           if (action == taaMODIFY || action == taaCREATE)
                xrpChange += sle[sfBalance];
           if (action == taaMODIFY)
               xrpChange -= (*beforeTx->getSLE(v.index()))[sfBalance];
       }
   }
   return xrpChange;
}

struct TestAccount
{
    std::string aliasAndPassPhrase;
    std::uint32_t sequence;
    RippleAddress address;
    Account id;
    std::string idHuman;
    Blob pubKey;
    uint256 ledgerIndex;
    // TODO: secretKey, regularKey, sign etc

    TestAccount(RippleAddress ad,
               std::string al) :
                aliasAndPassPhrase(al),
                sequence(0),
                address(ad),
                id(address.getAccountID()),
                idHuman(address.humanAccountID()),
                pubKey(address.getAccountPublic()),
                ledgerIndex(getAccountRootIndex(id)) {}

    Issue
    issue (std::string const& currency) const
    {
        return Issue(to_currency(currency), id);
    }

    STAmount
    amount(std::string const& valueAndCurrency) const
    {
        auto parts = test::split(valueAndCurrency, '/');
        assert(parts.size() == 2);
        auto amt = STAmount(issue(parts[1]), 0, 0);
        amt.setValue(parts[0]);
        return amt;
    }

    uint256
    indexForlineTo(TestAccount const& other, std::string const& currency)
    const
    {
        return indexForlineTo(other, to_currency(currency));
    }

    uint256
    indexForlineTo(TestAccount const& other, Currency const& currency)
    const
    {
        return getRippleStateIndex(id, other.id, currency);
    }

    uint256
    ownerDirIndex()
    const
    {
        return getOwnerDirIndex(id);
    }
};

class Affected
{
public:
    bool is_created;
    bool is_modified;
    bool is_deleted;
    LedgerEntryType type;
    uint256 index;
private:
    SLE::pointer aft_ /*er the transaction */;
    SLE::pointer b4_ /* the transaction, possibly nullptr */;

public:
    SLE&
    b4()
    {
        if (is_created)
        {
            throw std::runtime_error(
                "tried to access LES state b4 transaction of new entry");
        }
        return *b4_;
    }

    SLE&
    aft()
    {
        return *aft_;
    }

    Affected() : is_created(false), is_modified(false), is_deleted(false),
                 type(ltINVALID), index(uint256(0)),
                 aft_(nullptr), b4_(nullptr) {}

    /*Owners of SLE::ref must outlast Affected*/
    Affected (SLE::ref b4,
              SLE::ref aft,
              LedgerEntryAction action_) :
                is_created (action_ == taaCREATE),
                is_modified ( action_ == taaMODIFY),
                is_deleted ( action_ == taaDELETE),
                type (aft->getType()),
                index (aft->getIndex()),
                aft_ (aft),
                b4_ (b4)
    {
    }
};

struct AffectedNodes
{
    bool
    contains(uint256 const& index)
    {
        auto find = nodes.find(index);
        return find != nodes.end();
    }

    // Give us what we want or blow up, wrap calls in WITHREF
    Affected&
    operator[] (uint256 const& index)
    {
        return nodes.at(index);
    }

    size_t
    size ()
    {
        return nodes.size();
    }

    void
    insertNode (LESIView& v, SLE::ref b4)
    {
        nodes.insert(std::make_pair(v.index(),
                                    Affected(b4,v.entry(), v.action())));
    }

private:
    std::map<uint256, Affected> nodes;
};

struct TxResult {
    STTx::pointer tx;
    std::shared_ptr<LedgerEntrySet> ledgerView_;
    // Immutable snapshot
    Ledger::pointer ledgerPrior;
    TER engineResult;
    bool applied;

    // we can expect() a result directly
    operator bool ()
    {
        return fullyApplied();
    }

    LedgerEntrySet&
    ledgerView()
    {
        return *ledgerView_;
    }

    SLE&
    entryBefore(uint256 const& index)
    {
        // the cache maintains ownership, we just borrow
        return *ledgerPrior->getSLEi(index);
    }

    bool
    fullyApplied()
    {
        return applied && success();
    }

    bool
    success()
    {
        return engineResult == tesSUCCESS;
    }

    STAmount
    xrpDelta()
    {
        return xrpChange(*tx, ledgerPrior, ledgerView());
    }

    void
    getAffected(AffectedNodes& affected)
    {

        for (auto& it : ledgerView())
        {
            test::LESIView v {it};

            switch (v.action())
            {
                case taaCREATE:
                case taaMODIFY:
                case taaDELETE:
                    affected.insertNode(v, ledgerPrior->getSLEi(v.index()));
                default: // ignore these
                    break;

            }
        }
    }
};

class TestContext
{
public:
    /*Members*/
    TestAccount rootAccount;
    Ledger::pointer LCL;
    Ledger::pointer ledger;

    void
    populateMissing(STTx& tx, TestAccount& account)
    {
        // TODO: for somethings we need to check sig
        static const Blob sig {0x11, 0x22, 0x33, 0x44};

        tx(sfSigningPubKey, account.pubKey);
        tx(sfAccount,       account.id);
        tx(sfFee,           10);
        tx(sfSequence,      ++account.sequence);
        tx(sfTxnSignature,  sig);

        // TODO: check format properly somehow
        if (!tx.isValidForType())
        {
            throw std::runtime_error (
                to_string("transaction not valid:\n") +
                to_string(tx.getJson(0)) );
        }
    }

    Ledger::pointer
    snapShotLedger()
    {
        Ledger::pointer snap = ledger;
        snap->setImmutable();
        ledger = std::make_shared<Ledger> (std::ref(*snap), /*mutable=*/true);
        return snap;
    }

    TxResult
    applyTransaction(STTx& tx,
                     TestAccount& account,
                     TxConfigurator const& configure)
    {
        // Adds the Account, Sequence fields etc
        populateMissing(tx, account);
        // Here the configurator can override certain fields
        configure(tx);
        return applyTransaction(tx);
    }

    TxResult
    applyTransaction(STTx& tx)
    {
        auto b4TxApplied = snapShotLedger();
        TransactionEngine engine(ledger);

        bool didApply = false;
        TER r = engine.applyTransaction(tx,  ( tapNO_RESET |
                                               tapOPEN_LEDGER |
                                               tapNO_CHECK_SIGN ), didApply);

        return {std::make_shared<STTx>(tx), /* TODO: unique ?*/
                std::make_shared<LedgerEntrySet>(engine.view()),
                b4TxApplied,
                r,
                didApply};
    }

    TestAccount
    createAccount(std::string const& passphrase)
    {
        RippleAddress const seed
                = RippleAddress::createSeedGeneric (passphrase);
        RippleAddress const generator
                = RippleAddress::createGeneratorPublic (seed);
        return {RippleAddress::createAccountPublic(generator, 0), passphrase};
    }

    TxResult
    freezeAccount(TestAccount& account, TxConfigurator const& conf = NOOPCONF)
    {
        STTx tx (ttACCOUNT_SET);
        tx(sfSetFlag, asfGlobalFreeze);
        return applyTransaction(tx, account, conf);
    }

    TxResult
    unfreezeAccount(TestAccount& account, TxConfigurator const& conf = NOOPCONF)
    {
        STTx tx (ttACCOUNT_SET);
        tx(sfClearFlag, asfGlobalFreeze);
        return applyTransaction(tx, account, conf);
    }

    TxResult
    makePayment(TestAccount& from, TestAccount const& to,
                STAmount const& amount, TxConfigurator const& conf = NOOPCONF)
    {
        STTx tx (ttPAYMENT);
        tx(sfAmount, amount);
        tx(sfDestination, to.id);
        return applyTransaction(tx, from, conf);
    }

    TxResult
    makePayment(TestAccount& from, TestAccount const& to,
                std::string const& valueAndCurrency,
                TxConfigurator const& conf = NOOPCONF)
    {
        STTx tx (ttPAYMENT);
        tx(sfAmount, to.amount(valueAndCurrency));
        tx(sfDestination, to.id);
        return applyTransaction(tx, from, conf);
    }

    TxResult
    createOffer(TestAccount& creator,
                STAmount const& in,
                STAmount const& out,
                TxConfigurator const& conf = NOOPCONF)
    {
        STTx tx (ttOFFER_CREATE);
        tx(sfTakerPays, in);
        tx(sfTakerGets, out);
        return applyTransaction(tx, creator, conf);
    }

    TxResult
    createTicket(TestAccount& creator,
                 TxConfigurator const& conf = NOOPCONF)
    {
        STTx tx (ttTICKET_CREATE);
        return applyTransaction(tx, creator, conf);
    }

    TxResult
    cancelTicket(TestAccount& account,
                 uint256 const& ticketID,
                 TxConfigurator const& conf = NOOPCONF)
    {
        STTx tx (ttTICKET_CANCEL);
        tx(sfTicketID, ticketID);
        return applyTransaction(tx, account, conf);
    }

    TxResult
    cancelOffer(TestAccount& from, std::uint32_t offerSequence,
                TxConfigurator const& conf = NOOPCONF)
    {
        STTx tx (ttOFFER_CANCEL);
        tx(sfOfferSequence, offerSequence);
        return applyTransaction(tx, from, conf);
    }

    TxResult
    makeTrustSet(TestAccount& from, STAmount const& limitAmount,
                 TxConfigurator const& conf = NOOPCONF)
    {
        STTx tx (ttTRUST_SET);
        tx(sfLimitAmount, limitAmount);
        return applyTransaction(tx, from, conf);
    }

    // TODO: rename to something less getThisJavaEsquE
    SLE&
    entry(uint256 const& index)
    {
        // We use getSLEi, as it may give us a tiny performance boost, we never
        // mess with the entries internals, only inspect them, and also, it
        // allows us to return a reference, as the sle cache maintains owner
        // ship of the pointer. If we tried to return a derefd getSLE it would
        // blow up in our faaaa-eeee-sssce [1]

        // ([1]: http://www.adultswim.com/videos/xavier-renegade-angel/)

        return *ledger->getSLEi(index);
    }

    // TODO: relative time
    std::uint32_t
    ledgerAccept(std::uint32_t closeTime=0)
    {
        closeAndAdvanceByRef(ledger, LCL, closeTime);
        return ledger->getLedgerSeq();
    };

    TestContext () :
        rootAccount(createAccount("masterpassphrase")),
        // Create genesis ledger
        LCL(createGenesisLedger(100000000000000000ull, rootAccount.address)),
        // Create open scratch ledger
        ledger(std::make_shared<Ledger>(false, *LCL)) {}

    TestContext (Ledger::ref lcl) :
        rootAccount(createAccount("masterpassphrase")),
        LCL(lcl),
        // Create open scratch ledger
        ledger(std::make_shared<Ledger>(false, *LCL)) {}
};

class LedgerDumpSuite : public TestSuite
{
public:
    bool
    parseJsonObject (std::string const& json, Json::Value& to)
    {
        Json::Reader reader;
        return (reader.parse(json, to) && !to.isNull() && to.isObject());
    }

    STTx
    loadTransaction(std::string const& txString)
    {

        // Parse the transaction json string
        Json::Value tx_json;
        EXPECT2(parseJsonObject(txString, tx_json),
              "failed to parse json string from: " + txString);

        STParsedJSONObject parsed ("", tx_json);
        EXPECT2(parsed.object != nullptr,
               "failed to parse STObject from Json::Value: " +
               to_string(tx_json));

        STTx tx {*parsed.object};

        if (tx_json["hash"] != Json::nullValue)
        {
            std::string id (to_string(tx.getTransactionID()));
            EXPECT_EQ3(id, tx_json["hash"].asString(),
                        "computed tx hash different than expected");
        }

        return tx;
    }

    bool
    loadFixtureJSON(Json::Value& json,
                    std::string const& fixtureFile) {
        const char* path = std::getenv("TEST_FIXTURES");

        if (path == nullptr)
        {
            log << "TEST_FIXTURES environment var not declared";
            return false;
        }
        else
        {
            Json::Reader reader;
            std::string fullPath = std::string(path) +"/"+ fixtureFile;
            std::ifstream instream (fullPath, std::ios::in);
            EXPECT2(instream.good(), "IO not good for " +  fullPath);
            return reader.parse (instream, json, false);
        }
    }

    void
    loadLedger (std::string ledgerFile,
                Ledger::pointer& closed)
    {
        Json::Value ledger (Json::objectValue);
        EXPECT2(loadFixtureJSON(ledger, ledgerFile),
               "failed to load Json::Value from ledger fixture: " +
               ledgerFile);

        closed = parseLedgerFromJSON(ledger);
        EXPECT2(closed != nullptr,
               // we don't dump the json in the message as it could be HUGE
               "failed to create a Ledger from Json::Value from " +
               ledgerFile);

        // Ensure the ledger loaded properly,
        std::string actualAccountHash (
            to_string(closed->peekAccountStateMap ()->getHash ()));

        EXPECT_EQ3(actualAccountHash, ledger["account_hash"].asString(),
                   "account state hash differs from dumps");
    }

    void
    applyTransactionToDump (std::string const& ledgerFn,
                            std::string const& txString,
                            std::function<void (STTx&,
                                                TestContext&,
                                                TxResult& )>
                                                         onResult)
    {
        Ledger::pointer oldLedger;
        loadLedger(ledgerFn, oldLedger);
        TestContext c (oldLedger);
        STTx tx = loadTransaction(txString);
        auto r = c.applyTransaction(tx);
        onResult(tx, c, r);
    }
};

}; // namespace: test

class DiscrepancyTestExample_test : public test::LedgerDumpSuite
{
    void
    testDiscrepancyInHistoricalTransaction ()
    {
        using namespace test;
        auto restore (logSuppressor());

        testcase ("testDiscrepancyInHistoricalTransaction");

        std::string const& dump ("ledger-for-discrepancy-test-example.json");

        applyTransactionToDump(dump,  R"({
            "hash" : "062E94FFCE80B08C0FACF6910E0CDC04377AEE64CB2CBEF63EE9A22A414A2A8A",
            "Account": "r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K",
            "Amount": {
              "currency": "BTC",
              "issuer": "r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K",
              "value": "20"
            },
            "Destination": "r9hEDb4xBGRfBCcX3E4FirDWQBAYtpxC8K",
            "Fee": "12",
            "Flags": 0,
            "Paths": [
              [
                {
                  "currency": "BTC",
                  "issuer": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B"
                },
                {
                  "account": "rvYAfWj5gh67oV6fW32ZzP3Aw4Eubs59B"
                }
              ],
              [
                {
                  "currency": "BTC",
                  "issuer": "ra9eZxMbJrUcgV8ui7aPc161FgrqWScQxV"
                },
                {
                  "account": "ra9eZxMbJrUcgV8ui7aPc161FgrqWScQxV"
                }
              ],
              [
                {
                  "currency": "BTC",
                  "issuer": "rJHygWcTLVpSXkowott6kzgZU6viQSVYM1"
                },
                {
                  "account": "rJHygWcTLVpSXkowott6kzgZU6viQSVYM1"
                }
              ]
            ],
            "SendMax": "1660340193217",
            "Sequence": 2113,
            "SigningPubKey": "03CEB1BE2B8E444847DC13415378325666BF2F63A0549B3F9D844E7C73F8564E2E",
            "TransactionType": "Payment",
            "TxnSignature": "30450220373EBC6E46C3A403F79338F0F9E8C3FE28CEA4BC63E78D7A8365366C501DD5F2022100FC4BC4C610A6739A221A9538FD0FB50A9184443FC68A4F3F34D546DB2BD7013A"
        })", [this](STTx& tx, TestContext& c, TxResult& r){
            EXPECT_EQ3(r.xrpDelta(), 0, "transaction created/destroyed xrp");
        });
    }

public:
    void run ()
    {
        if (std::getenv("TEST_FIXTURES"))
        {
            testDiscrepancyInHistoricalTransaction();
        }
        else {
            fail("TEST_FIXTURES path not set to abspath($REPO/test/fixtures)");
        }
    }
};

#if RIPPLE_ENABLE_TICKETS
TRANSACTOR_TEST(Tickets, {

   auto& root = rootAccount;
   TestAccount alice = createAccount("alice");

   std::uint32_t rippleTime (473568150);

   // Set up the accounts
   EXPECT(makePayment(root, alice, xrp(10000)));

   // We can control the time of the ledger
   // Lucky, as we need to control Expiration
   ledgerAccept(rippleTime);
   EXPECT_EQ(LCL->getCloseTimeNC(), rippleTime);

   // Create a ticket to expire 40 seconds in the future
   TxResult r;
   // The TxResult has a bool () operator
   EXPECT(r = createTicket(root, [&](STTx& tx) {
       // we use typed fields and operator() hack
       tx(sfExpiration, rippleTime + 40);
   }));

   uint256 ticketID = getTicketIndex(root.id, root.sequence);
   /*sequence happens to be the sequence prior, TODO: */
   {
       AffectedNodes nodes;
       r.getAffected(nodes);
       {
           AFFECTED(r, root.ledgerIndex, is_modified);
           CHANGED(r, sfOwnerCount, 0, 1);
       }
       {
           AFFECTED(t, ticketID, is_created);
           NEW(t, sfAccount, root.id);
       }
       {
           AFFECTED(dir, root.ownerDirIndex(), is_created);
           NEW(dir, sfOwner, root.id);
           auto& indexes = dir.aft()[sfIndexes];
           EXPECT_EQ(indexes.size(), 1);
           EXPECT_EQ(indexes[0], ticketID);
       }
       // Nothing weird going on here
       EXPECT_EQ(nodes.size(), 3);
   }

   // Trying to cancel the ticket isn't permitted by randoms until it has
   // expired.
   r = cancelTicket(alice, ticketID);
   EXPECT_EQ(r.engineResult, tecNO_PERMISSION);

   // Pass 60 seconds
   ledgerAccept((rippleTime += 60));

   // This time we have success! as we're past the 40 second expiry
   r = cancelTicket(alice, ticketID);
   EXPECT_EQ(r.engineResult, tesSUCCESS);

   {
       AffectedNodes nodes;
       r.getAffected(nodes);
       {
           // // The root account's owner count is updated
           AFFECTED(r, root.ledgerIndex, is_modified);
           CHANGED(r, sfOwnerCount, 1, 0);
       }
       // Alice's owner count is not decremented
       {
           AFFECTED(a, alice.ledgerIndex, is_modified);
           UNCHANGED(a, sfOwnerCount);
       }
       // The ticket is deleted
       {
           AFFECTED(t, ticketID, is_deleted);
       }
       // The owner directory is deleted
       {
           AFFECTED(dir, root.ownerDirIndex(), is_deleted);
           CHANGEDP(dir, sfIndexes, .size(), 1, 0);
       }
       // Nothing weird going on here
       EXPECT_EQ(nodes.size(), 4);
   }

   ledgerAccept((rippleTime+=35));

   // A nice `something` changed detector
   uint256 timeOfWriting (
       "67F972F462D18C123B90926EB62FBBF236D65EF46779EF606C5DBE6950BFF2B0" );

   // TODO: auto assert
   EXPECT_EQ(LCL->getHash(), timeOfWriting);
});
#endif

class Ledger_test : public TestSuite
{
    void
    test_TestContext ()
    {
        using namespace test;
        auto restore (logSuppressor());
        TestContext c;
        auto& root = c.rootAccount;

        #define TEST_ACCOUNT(name) auto name = c.createAccount(#name);
        // Create user accounts
        TEST_ACCOUNT(alice);
        TEST_ACCOUNT(mark);
        TEST_ACCOUNT(gw1);
        TEST_ACCOUNT(gw2);
        TEST_ACCOUNT(gw3);
        #undef TEST_ACCOUNT

        auto fiveK (xrp(5000));

        // Fund gw1, gw2, gw3, alice, mark from root
        EXPECT(c.makePayment(root, gw1, fiveK));
        EXPECT(c.makePayment(root, gw2, xrp(4000)));
        EXPECT(c.makePayment(root, gw3, xrp(3000)));
        EXPECT(c.makePayment(root, alice, xrp(2000)));
        EXPECT(c.makePayment(root, mark, xrp(1000)));

        // Example of using API with transactions that aren't tesSUCCESS
        TxResult r = c.makePayment(gw1, root, xrp(15000));
        EXPECT(r.applied);
        EXPECT_EQ(r.engineResult, tecUNFUNDED_PAYMENT);

        // We can get prior version
        // We could also add an immutable snapshot of the txn after application
        EXPECT_EQ(r.entryBefore(gw1.ledgerIndex)[sfBalance], fiveK);

        LedgerEntryAction action;
        // We have access to the LedgerEntrySet
        // we could add better helpers here, but the gist is tapNO_REST
        auto le = r.ledgerView().getEntry(gw1.ledgerIndex, action);
        EXPECT_EQ(action, taaMODIFY);
        EXPECT_EQ((*le)[sfBalance], (fiveK - 10 /*drops for Fee*/ ));

        // Close the ledger bob ;)
        EXPECT_EQ(c.ledgerAccept(), 3);

        // alice trusts FOO/gw1
        c.makeTrustSet(alice, gw1.amount("1/FOO"));
        // get the ripple state line
        auto& line = c.entry(alice.indexForlineTo(gw1, "FOO"));
        // alice is the low limit
        EXPECT(alice.id < gw1.id);
        EXPECT_EQ(line[sfLowLimit].getText(), "1");
        EXPECT_EQ(line[sfHighLimit].getText(), "0");

        // mark trusts FOO/gw2
        EXPECT(c.makeTrustSet(mark, gw2.amount("1/FOO")));

        // mark trusts FOO/gw3
        EXPECT(c.makeTrustSet(mark, gw3.amount("1/FOO")));

        // gw2 pays mark with FOO
        EXPECT(c.makePayment(gw2, mark, "0.1/FOO", [](STTx& tx){
            // This is just a configurator lambda, so can configure non standard
            // stuff and junk. Is this a sensible API or crackheaded?
            tx(sfFlags, tfPartialPayment);
        }));

        // gw3 pays mark with FOO
        EXPECT(c.makePayment(gw3, mark, "0.2/FOO"));

        // gw1 pays alice with FOO
        EXPECT(c.makePayment(gw1, alice, "0.3/FOO"));

        EXPECT_EQ(c.ledgerAccept(), 4);

        EXPECT(c.createOffer(mark, gw1.amount("1/FOO"),  gw2.amount("1/FOO")));

        // Example of using TxConfigurator
        EXPECT(c.createOffer(mark, /*in*/  gw2.amount("1/FOO"),
                                   /*out*/ gw3.amount("1/FOO"),  [](STTx& tx){
            tx(sfFee, 12000 /*drops*/);
            tx(sfFlags, tfSell);
        }));

        EXPECT(c.cancelOffer(mark, mark.sequence));
        EXPECT(c.freezeAccount(alice));

        EXPECT_EQ(c.ledgerAccept(), 5);

        auto b4 (c.entry(mark.ledgerIndex)[sfBalance]);
        EXPECT(c.makePayment(alice, mark, xrp(1)));
        EXPECT_EQ(c.entry(mark.ledgerIndex)[sfBalance], b4 + xrp(1));

        EXPECT_EQ(c.ledgerAccept(), 6);
    }

    void
    test_parseLedgerFromJSON ()
    {
        testcase ("test_parseLedgerFromJSON");
        std::string const& ledgerJson (R"({
           "result" : {
               "ledger" : {
                  "accepted" : true,
                  "accountState" : [
                     {
                        "Account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
                        "Balance" : "100000000000000000",
                        "Flags" : 0,
                        "LedgerEntryType" : "AccountRoot",
                        "OwnerCount" : 0,
                        "PreviousTxnID" : "0000000000000000000000000000000000000000000000000000000000000000",
                        "PreviousTxnLgrSeq" : 0,
                        "Sequence" : 1,
                        "index" : "2B6AC232AA4C4BE41BF49D2459FA4A0347E1B543A4C92FCEE0821C0201E2E9A8"
                     }
                  ],
                  "account_hash" : "A21ED30C04C88046FC61DB9DC19375EEDBD365FD8C17286F27127DF804E9CAA6",
                  "close_time" : 473568150,
                  "close_time_human" : "2015-Jan-03 02:42:30",
                  "close_time_resolution" : 30,
                  "closed" : true,
                  "hash" : "DCCFC96D5C233B5BCF26AB456F727C0FB417861E068A442FFE8235C54D032225",
                  "ledger_hash" : "DCCFC96D5C233B5BCF26AB456F727C0FB417861E068A442FFE8235C54D032225",
                  "ledger_index" : "2",
                  "parent_hash" : "AB868A6CFEEC779C2FF845C0AF00A642259986AF40C01976A7F842B6918936C7",
                  "seqNum" : "2",
                  "totalCoins" : "100000000000000000",
                  "total_coins" : "100000000000000000",
                  "transaction_hash" : "0000000000000000000000000000000000000000000000000000000000000000",
                  "transactions" : []
               },
               "ledger_index" : 2,
               "status" : "success",
               "validated" : true
        })");

        uint256 nullHash (0);
        Json::Value json;
        Json::Reader ().parse(ledgerJson, json);
        Ledger::pointer ledger (parseLedgerFromJSON(json));

        EXPECT_EQ(ledger->getAccountHash(), uint256(
            "A21ED30C04C88046FC61DB9DC19375EE"
            "DBD365FD8C17286F27127DF804E9CAA6"));

        EXPECT_EQ(ledger->getCloseTimeNC(), 473568150);
        EXPECT(ledger->isClosed());
        EXPECT_EQ(ledger->getHash(), uint256(
            "DCCFC96D5C233B5BCF26AB456F727C0F"
            "B417861E068A442FFE8235C54D032225"));
        EXPECT_EQ(ledger->getLedgerSeq(), 2);
        EXPECT_EQ(ledger->getParentHash(), uint256(
            "AB868A6CFEEC779C2FF845C0AF00A642"
            "259986AF40C01976A7F842B6918936C7"));
        EXPECT_EQ(ledger->getTotalCoins(), 100000000000000000);
        EXPECT_EQ(ledger->getTransHash(), nullHash);

        EXPECT(ledger->isValidated());
        EXPECT(ledger->isImmutable());
    }

    void
    test_getQuality ()
    {
        // Each ledger entry stored in the account state ShaMap has an index,
        // which is an enduring identifier, that never changes from ledger to
        // ledger. It's created by hashing static elements.

        // For DirectoryNode's that enumerate available Offer's, the pays/gets
        // Issue pair are used to create this index. However, rather than dump
        // all offers of a pair in just the one DirectoryNode, the last (right
        // most) 64 bits has a quality overlayed (so as to store offers of the
        // same quality in the same DirectoryNode(s). (Actually, the directory
        // nodes are paginated, with only 32 entries per page, and IndexNext,
        // IndexPrev pointers to other nodes. Only the root directory for each
        // quality has the common prefix.))

        // This quality is essentially TakerPays/TakerGets, i.e. how much you
        // must put `in` of TakerPays issue to get one of TakerGets issue `out`.

        // This allows easy walking of offers for a given Issue pair, with ever
        // worse rates for the taker, via using a tree.findNextIndex(after) api.
        // To start with the bookbase is used, which is the 192 bit common
        // prefix, with zero in the last 64 bits.

        // In any case, getQuality simply gets the last (right most) 64 bits of
        // a uint256 index.

        uint256 uBig (
            "D2DC44E5DC189318DB36EF87D2104CDF0A0FE3A4B698BEEE"
                                           "55038D7EA4C68000");
                                          // ^ same same but different V
        EXPECT_EQ(getQuality (uBig),      0x55038D7EA4C68000ull);
    }

public:
    void
    run ()
    {
        test_TestContext ();
        test_getQuality ();
        test_parseLedgerFromJSON ();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(DiscrepancyTestExample,ripple_app,ripple);
BEAST_DEFINE_TESTSUITE(Ledger,ripple_app,ripple);

#undef AFFECTED
#undef UNCHANGED
#undef CHANGEDP
#undef CHANGED
#undef NEW
#undef EXPECT
#undef EXPECT2
#undef EXPECT_EQ
#undef EXPECT_EQ3
#undef ANNOTATE_MSG
#undef FILE_POSITION

} // ripple
