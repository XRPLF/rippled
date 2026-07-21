#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/domain.h>
#include <test/jtx/offer.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_dex.h>
#include <test/jtx/sendmax.h>

#include <xrpld/rpc/BookChanges.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace xrpl::test {

class BookChanges_test : public beast::unit_test::Suite
{
public:
    void
    testConventionalLedgerInputStrings()
    {
        testcase("Specify well-known strings as ledger input");
        jtx::Env env(*this);
        json::Value params, resp;

        // As per convention in XRPL, ledgers can be specified with strings
        // "closed", "validated" or "current"
        params["ledger"] = "validated";
        resp = env.rpc("json", "book_changes", to_string(params));
        BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
        BEAST_EXPECT(resp[jss::result][jss::status] == "success");
        BEAST_EXPECT(resp[jss::result][jss::validated] == true);

        params["ledger"] = "current";
        resp = env.rpc("json", "book_changes", to_string(params));
        BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
        BEAST_EXPECT(resp[jss::result][jss::status] == "success");
        BEAST_EXPECT(resp[jss::result][jss::validated] == false);

        params["ledger"] = "closed";
        resp = env.rpc("json", "book_changes", to_string(params));
        BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
        BEAST_EXPECT(resp[jss::result][jss::status] == "success");

        // In the unit-test framework, requesting for "closed" ledgers appears
        // to yield "validated" ledgers. This is not new behavior. It is also
        // observed in the unit tests for the LedgerHeader class.
        BEAST_EXPECT(resp[jss::result][jss::validated] == true);

        // non-conventional ledger input should throw an error
        params["ledger"] = "non_conventional_ledger_input";
        resp = env.rpc("json", "book_changes", to_string(params));
        BEAST_EXPECT(resp[jss::result].isMember(jss::error));
        BEAST_EXPECT(resp[jss::result][jss::status] != "success");
    }

    void
    testLedgerInputDefaultBehavior()
    {
        testcase(
            "If ledger_hash or ledger_index is not specified, the behavior "
            "must default to the `current` ledger");
        jtx::Env env(*this);

        // As per convention in XRPL, ledgers can be specified with strings
        // "closed", "validated" or "current"
        json::Value const resp = env.rpc("json", "book_changes", to_string(json::Value{}));
        BEAST_EXPECT(!resp[jss::result].isMember(jss::error));
        BEAST_EXPECT(resp[jss::result][jss::status] == "success");

        // I dislike asserting the below statement, because its dependent on the
        // unit-test framework BEAST_EXPECT(resp[jss::result][jss::ledger_index]
        // == 3);
    }

    void
    testDomainOffer()
    {
        testcase("Domain Offer");
        using namespace jtx;

        FeatureBitset const all{
            jtx::testableAmendments() | featurePermissionedDomains | featureCredentials |
            featurePermissionedDEX};

        Env env(*this, all);
        PermissionedDEX const permDex(env);
        auto const& [gw, domainOwner, alice, bob, carol, USD, domainID, credType] = permDex;

        auto wsc = makeWSClient(env.app().config());

        env(offer(alice, XRP(10), USD(10)), Domain(domainID));
        env.close();

        env(pay(bob, carol, USD(10)), Path(~USD), Sendmax(XRP(10)), Domain(domainID));
        env.close();

        std::string const txHash{
            env.tx()->getJson(JsonOptions::Values::None)[jss::hash].asString()};

        json::Value const txResult = env.rpc("tx", txHash)[jss::result];
        auto const ledgerIndex = txResult[jss::ledger_index].asInt();

        json::Value jvParams;
        jvParams[jss::ledger_index] = ledgerIndex;

        auto jv = wsc->invoke("book_changes", jvParams);
        auto jrr = jv[jss::result];

        BEAST_EXPECT(jrr[jss::changes].size() == 1);
        BEAST_EXPECT(jrr[jss::changes][0u][jss::domain].asString() == to_string(domainID));
    }

    void
    testSkipsOverflowingRate()
    {
        testcase("book_changes skips overflowing rate");
        using namespace jtx;

        Env env(*this);
        Account const gw{"gw"};
        Account const iouGw{"iouGw"};

        auto const big = MPT{gw.id(), 1};
        auto const usd = iouGw["USD"];

        // This metadata represents a partial MPT/IOU offer fill whose deltas
        // make divide(deltaGets, deltaPays) overflow before MPTokensV2 skips
        // the unrepresentable book-change rate.
        STObject finalFields = STObject::makeInnerObject(sfFinalFields);
        finalFields.setFieldU32(sfSequence, 1);
        finalFields.setFieldAmount(sfTakerGets, big(1'800'000'000'000'000'000ull));
        finalFields.setFieldAmount(sfTakerPays, usd(9));

        STObject previousFields = STObject::makeInnerObject(sfPreviousFields);
        previousFields.setFieldU32(sfSequence, 1);
        previousFields.setFieldAmount(sfTakerGets, big(3'600'000'000'000'000'000ull));
        previousFields.setFieldAmount(sfTakerPays, usd(18));

        STObject modifiedOffer{sfModifiedNode};
        modifiedOffer.setFieldU16(sfLedgerEntryType, ltOFFER);
        modifiedOffer.setFieldObject(sfFinalFields, finalFields);
        modifiedOffer.setFieldObject(sfPreviousFields, previousFields);

        STArray affectedNodes{sfAffectedNodes};
        affectedNodes.pushBack(std::move(modifiedOffer));

        auto metadata = std::make_shared<STObject>(sfTransactionMetaData);
        metadata->setFieldArray(sfAffectedNodes, affectedNodes);

        auto tx = std::make_shared<STTx const>(ttOFFER_CREATE, [](STObject&) {});

        auto const test = [&](std::unordered_set<uint256, beast::Uhash<>> const& features) {
            auto ledger = std::make_shared<Ledger>(
                2,
                NetClock::time_point{},
                Rules{features},
                env.current()->fees(),
                env.app().getNodeFamily());

            auto txSerializer = std::make_shared<Serializer>();
            tx->add(*txSerializer);

            auto metaSerializer = std::make_shared<Serializer>();
            metadata->add(*metaSerializer);

            ledger->rawTxInsert(uint256{1}, txSerializer, metaSerializer);
            ledger->setImmutable();
            ledger->setValidated();

            try
            {
                auto const result =
                    RPC::computeBookChanges(std::static_pointer_cast<Ledger const>(ledger));
                BEAST_EXPECT(result[jss::type] == "bookChanges");
                BEAST_EXPECT(result[jss::changes].size() == 0);
            }
            catch (std::overflow_error const&)
            {
                fail("Overflowing book-change rate shouldn't throw");
            }
        };

        test(std::unordered_set<uint256, beast::Uhash<>>{});
        test(std::unordered_set<uint256, beast::Uhash<>>{featureMPTokensV2});
    }

    void
    run() override
    {
        testConventionalLedgerInputStrings();
        testLedgerInputDefaultBehavior();

        testDomainOffer();
        testSkipsOverflowingRate();
        // Note: Other aspects of the book_changes rpc are fertile grounds
        // for unit-testing purposes. It can be included in future work
    }
};

BEAST_DEFINE_TESTSUITE(BookChanges, rpc, xrpl);

}  // namespace xrpl::test
