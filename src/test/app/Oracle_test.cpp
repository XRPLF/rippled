#include <test/jtx/Oracle.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {
namespace oracle {

struct Oracle_test : public beast::unit_test::suite
{
private:
    void
    testInvalidSet()
    {
        testcase("Invalid Set");

        using namespace jtx;
        Account const owner("owner");

        {
            // Invalid account
            Env env(*this);
            Account const bad("bad");
            env.memoize(bad);
            Oracle oracle(
                env,
                {.owner = bad,
                 .seq = seq(1),
                 .fee = static_cast<int>(env.current()->fees().base.drops()),
                 .err = ter(terNO_ACCOUNT)});
        }

        // Insufficient reserve
        {
            Env env(*this);
            env.fund(env.current()->fees().accountReserve(0), owner);
            Oracle oracle(
                env,
                {.owner = owner,
                 .fee = static_cast<int>(env.current()->fees().base.drops()),
                 .err = ter(tecINSUFFICIENT_RESERVE)});
        }
        // Insufficient reserve if the data series extends to greater than 5
        {
            Env env(*this);
            env.fund(
                env.current()->fees().accountReserve(1) +
                    env.current()->fees().base * 2,
                owner);
            Oracle oracle(
                env,
                {.owner = owner,
                 .fee = static_cast<int>(env.current()->fees().base.drops())});
            BEAST_EXPECT(oracle.exists());
            oracle.set(UpdateArg{
                .series =
                    {
                        {"XRP", "EUR", 740, 1},
                        {"XRP", "GBP", 740, 1},
                        {"XRP", "CNY", 740, 1},
                        {"XRP", "CAD", 740, 1},
                        {"XRP", "AUD", 740, 1},
                    },
                .fee = static_cast<int>(env.current()->fees().base.drops()),
                .err = ter(tecINSUFFICIENT_RESERVE)});
        }

        {
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner, .fee = baseFee}, false);

            // Invalid flag
            oracle.set(CreateArg{
                .flags = tfSellNFToken,
                .fee = baseFee,
                .err = ter(temINVALID_FLAG)});

            // Duplicate token pair
            oracle.set(CreateArg{
                .series = {{"XRP", "USD", 740, 1}, {"XRP", "USD", 750, 1}},
                .fee = baseFee,
                .err = ter(temMALFORMED)});

            // Price is not included
            oracle.set(CreateArg{
                .series =
                    {{"XRP", "USD", 740, 1}, {"XRP", "EUR", std::nullopt, 1}},
                .fee = baseFee,
                .err = ter(temMALFORMED)});

            // Token pair is in update and delete
            oracle.set(CreateArg{
                .series =
                    {{"XRP", "USD", 740, 1}, {"XRP", "USD", std::nullopt, 1}},
                .fee = baseFee,
                .err = ter(temMALFORMED)});
            // Token pair is in add and delete
            oracle.set(CreateArg{
                .series =
                    {{"XRP", "EUR", 740, 1}, {"XRP", "EUR", std::nullopt, 1}},
                .fee = baseFee,
                .err = ter(temMALFORMED)});

            // Array of token pair is 0 or exceeds 10
            oracle.set(CreateArg{
                .series =
                    {{"XRP", "US1", 740, 1},
                     {"XRP", "US2", 750, 1},
                     {"XRP", "US3", 740, 1},
                     {"XRP", "US4", 750, 1},
                     {"XRP", "US5", 740, 1},
                     {"XRP", "US6", 750, 1},
                     {"XRP", "US7", 740, 1},
                     {"XRP", "US8", 750, 1},
                     {"XRP", "US9", 740, 1},
                     {"XRP", "U10", 750, 1},
                     {"XRP", "U11", 740, 1}},
                .fee = baseFee,
                .err = ter(temARRAY_TOO_LARGE)});
            oracle.set(CreateArg{
                .series = {}, .fee = baseFee, .err = ter(temARRAY_EMPTY)});
        }

        // Array of token pair exceeds 10 after update
        {
            Env env{*this};
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);

            Oracle oracle(
                env,
                CreateArg{
                    .owner = owner,
                    .series = {{{"XRP", "USD", 740, 1}}},
                    .fee = baseFee});
            oracle.set(UpdateArg{
                .series =
                    {
                        {"XRP", "US1", 740, 1},
                        {"XRP", "US2", 750, 1},
                        {"XRP", "US3", 740, 1},
                        {"XRP", "US4", 750, 1},
                        {"XRP", "US5", 740, 1},
                        {"XRP", "US6", 750, 1},
                        {"XRP", "US7", 740, 1},
                        {"XRP", "US8", 750, 1},
                        {"XRP", "US9", 740, 1},
                        {"XRP", "U10", 750, 1},
                    },
                .fee = baseFee,
                .err = ter(tecARRAY_TOO_LARGE)});
        }

        {
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner, .fee = baseFee}, false);

            // Asset class or provider not included on create
            oracle.set(CreateArg{
                .assetClass = std::nullopt,
                .provider = "provider",
                .fee = baseFee,
                .err = ter(temMALFORMED)});
            oracle.set(CreateArg{
                .assetClass = "currency",
                .provider = std::nullopt,
                .uri = "URI",
                .fee = baseFee,
                .err = ter(temMALFORMED)});

            // Asset class or provider are included on update
            // and don't match the current values
            oracle.set(CreateArg{
                .fee = static_cast<int>(env.current()->fees().base.drops())});
            BEAST_EXPECT(oracle.exists());
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", 740, 1}},
                .provider = "provider1",
                .fee = baseFee,
                .err = ter(temMALFORMED)});
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", 740, 1}},
                .assetClass = "currency1",
                .fee = baseFee,
                .err = ter(temMALFORMED)});
        }

        {
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner, .fee = baseFee}, false);

            // Fields too long
            // Asset class
            std::string assetClass(17, '0');
            oracle.set(CreateArg{
                .assetClass = assetClass,
                .fee = baseFee,
                .err = ter(temMALFORMED)});
            // provider
            std::string const large(257, '0');
            oracle.set(CreateArg{
                .provider = large, .fee = baseFee, .err = ter(temMALFORMED)});
            // URI
            oracle.set(CreateArg{
                .uri = large, .fee = baseFee, .err = ter(temMALFORMED)});
            // Empty field
            // Asset class
            oracle.set(CreateArg{
                .assetClass = "", .fee = baseFee, .err = ter(temMALFORMED)});
            // provider
            oracle.set(CreateArg{
                .provider = "", .fee = baseFee, .err = ter(temMALFORMED)});
            // URI
            oracle.set(
                CreateArg{.uri = "", .fee = baseFee, .err = ter(temMALFORMED)});
        }

        {
            // Different owner creates a new object and fails because
            // of missing fields currency/provider
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            Account const some("some");
            env.fund(XRP(1'000), owner);
            env.fund(XRP(1'000), some);
            Oracle oracle(env, {.owner = owner, .fee = baseFee});
            BEAST_EXPECT(oracle.exists());
            oracle.set(UpdateArg{
                .owner = some,
                .series = {{"XRP", "USD", 740, 1}},
                .fee = baseFee,
                .err = ter(temMALFORMED)});
        }

        {
            // Invalid update time
            using namespace std::chrono;
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            auto closeTime = [&]() {
                return duration_cast<seconds>(
                           env.current()->info().closeTime.time_since_epoch() -
                           10'000s)
                    .count();
            };
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner, .fee = baseFee});
            BEAST_EXPECT(oracle.exists());
            env.close(seconds(400));
            // Less than the last close time - 300s
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", 740, 1}},
                .lastUpdateTime = static_cast<std::uint32_t>(closeTime() - 301),
                .fee = baseFee,
                .err = ter(tecINVALID_UPDATE_TIME)});
            // Greater than last close time + 300s
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", 740, 1}},
                .lastUpdateTime = static_cast<std::uint32_t>(closeTime() + 311),
                .fee = baseFee,
                .err = ter(tecINVALID_UPDATE_TIME)});
            oracle.set(
                UpdateArg{.series = {{"XRP", "USD", 740, 1}}, .fee = baseFee});
            BEAST_EXPECT(oracle.expectLastUpdateTime(
                static_cast<std::uint32_t>(testStartTime.count() + 450)));
            // Less than the previous lastUpdateTime
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", 740, 1}},
                .lastUpdateTime = static_cast<std::uint32_t>(449),
                .fee = baseFee,
                .err = ter(tecINVALID_UPDATE_TIME)});
            // Less than the epoch time
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", 740, 1}},
                .lastUpdateTime = static_cast<int>(epoch_offset.count() - 1),
                .fee = baseFee,
                .err = ter(tecINVALID_UPDATE_TIME)});
        }

        {
            // delete token pair that doesn't exist
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner, .fee = baseFee});
            BEAST_EXPECT(oracle.exists());
            oracle.set(UpdateArg{
                .series = {{"XRP", "EUR", std::nullopt, std::nullopt}},
                .fee = baseFee,
                .err = ter(tecTOKEN_PAIR_NOT_FOUND)});
            // delete all token pairs
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", std::nullopt, std::nullopt}},
                .fee = baseFee,
                .err = ter(tecARRAY_EMPTY)});
        }

        {
            // same BaseAsset and QuoteAsset
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);
            Oracle oracle(
                env,
                {.owner = owner,
                 .series = {{"USD", "USD", 740, 1}},
                 .fee = baseFee,
                 .err = ter(temMALFORMED)});
        }

        {
            // Scale is greater than maxPriceScale
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);
            Oracle oracle(
                env,
                {.owner = owner,
                 .series = {{"USD", "BTC", 740, maxPriceScale + 1}},
                 .fee = baseFee,
                 .err = ter(temMALFORMED)});
        }

        {
            // Updating token pair to add and delete
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);
            Oracle oracle(env, {.owner = owner, .fee = baseFee});
            oracle.set(UpdateArg{
                .series =
                    {{"XRP", "EUR", std::nullopt, std::nullopt},
                     {"XRP", "EUR", 740, 1}},
                .fee = baseFee,
                .err = ter(temMALFORMED)});
            // Delete token pair that doesn't exist in this oracle
            oracle.set(UpdateArg{
                .series = {{"XRP", "EUR", std::nullopt, std::nullopt}},
                .fee = baseFee,
                .err = ter(tecTOKEN_PAIR_NOT_FOUND)});
            // Delete token pair in oracle, which is not in the ledger
            oracle.set(UpdateArg{
                .documentID = 10,
                .series = {{"XRP", "EUR", std::nullopt, std::nullopt}},
                .fee = baseFee,
                .err = ter(temMALFORMED)});
        }

        {
            // Bad fee
            Env env(*this);
            env.fund(XRP(1'000), owner);
            Oracle oracle(
                env, {.owner = owner, .fee = -1, .err = ter(temBAD_FEE)});
            Oracle oracle1(
                env,
                {.owner = owner,
                 .fee = static_cast<int>(env.current()->fees().base.drops())});
            oracle.set(
                UpdateArg{.owner = owner, .fee = -1, .err = ter(temBAD_FEE)});
        }
    }

    void
    testCreate(FeatureBitset features)
    {
        testcase("Create");
        using namespace jtx;
        Account const owner("owner");

        auto test = [&](Env& env, DataSeries const& series, std::uint16_t adj) {
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);
            auto const count = ownerCount(env, owner);
            Oracle oracle(
                env, {.owner = owner, .series = series, .fee = baseFee});
            BEAST_EXPECT(oracle.exists());
            BEAST_EXPECT(ownerCount(env, owner) == (count + adj));
            auto const entry = oracle.ledgerEntry();
            BEAST_EXPECT(entry[jss::node][jss::Owner] == owner.human());
            if (features[fixIncludeKeyletFields])
            {
                BEAST_EXPECT(
                    entry[jss::node][jss::OracleDocumentID] ==
                    oracle.documentID());
            }
            else
            {
                BEAST_EXPECT(!entry[jss::node].isMember(jss::OracleDocumentID));
            }
            BEAST_EXPECT(oracle.expectLastUpdateTime(946694810));
        };

        {
            // owner count is adjusted by 1
            Env env(*this, features);
            test(env, {{"XRP", "USD", 740, 1}}, 1);
        }

        {
            // owner count is adjusted by 2
            Env env(*this, features);
            test(
                env,
                {{"XRP", "USD", 740, 1},
                 {"BTC", "USD", 740, 1},
                 {"ETH", "USD", 740, 1},
                 {"CAN", "USD", 740, 1},
                 {"YAN", "USD", 740, 1},
                 {"GBP", "USD", 740, 1}},
                2);
        }

        {
            // Different owner creates a new object
            Env env(*this, features);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            Account const some("some");
            env.fund(XRP(1'000), owner);
            env.fund(XRP(1'000), some);
            Oracle oracle(env, {.owner = owner, .fee = baseFee});
            BEAST_EXPECT(oracle.exists());
            oracle.set(CreateArg{
                .owner = some,
                .series = {{"912810RR9", "USD", 740, 1}},
                .fee = baseFee});
            BEAST_EXPECT(Oracle::exists(env, some, oracle.documentID()));
        }
    }

    void
    testInvalidDelete()
    {
        testcase("Invalid Delete");

        using namespace jtx;
        Env env(*this);
        auto const baseFee =
            static_cast<int>(env.current()->fees().base.drops());
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle oracle(env, {.owner = owner, .fee = baseFee});
        BEAST_EXPECT(oracle.exists());

        {
            // Invalid account
            Account const bad("bad");
            env.memoize(bad);
            oracle.remove(
                {.owner = bad,
                 .seq = seq(1),
                 .fee = baseFee,
                 .err = ter(terNO_ACCOUNT)});
        }

        // Invalid DocumentID
        oracle.remove(
            {.documentID = 2, .fee = baseFee, .err = ter(tecNO_ENTRY)});

        // Invalid owner
        Account const invalid("invalid");
        env.fund(XRP(1'000), invalid);
        oracle.remove(
            {.owner = invalid, .fee = baseFee, .err = ter(tecNO_ENTRY)});

        // Invalid flags
        oracle.remove(
            {.flags = tfSellNFToken,
             .fee = baseFee,
             .err = ter(temINVALID_FLAG)});

        // Bad fee
        oracle.remove({.fee = -1, .err = ter(temBAD_FEE)});
    }

    void
    testDelete()
    {
        testcase("Delete");
        using namespace jtx;
        Account const owner("owner");

        auto test = [&](Env& env, DataSeries const& series, std::uint16_t adj) {
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);
            Oracle oracle(
                env, {.owner = owner, .series = series, .fee = baseFee});
            auto const count = ownerCount(env, owner);
            BEAST_EXPECT(oracle.exists());
            oracle.remove({.fee = baseFee});
            BEAST_EXPECT(!oracle.exists());
            BEAST_EXPECT(ownerCount(env, owner) == (count - adj));
        };

        {
            // owner count is adjusted by 1
            Env env(*this);
            test(env, {{"XRP", "USD", 740, 1}}, 1);
        }

        {
            // owner count is adjusted by 2
            Env env(*this);
            test(
                env,
                {
                    {"XRP", "USD", 740, 1},
                    {"BTC", "USD", 740, 1},
                    {"ETH", "USD", 740, 1},
                    {"CAN", "USD", 740, 1},
                    {"YAN", "USD", 740, 1},
                    {"GBP", "USD", 740, 1},
                },
                2);
        }

        {
            // deleting the account deletes the oracles
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());

            auto const alice = Account("alice");
            auto const acctDelFee{drops(env.current()->fees().increment)};
            env.fund(XRP(1'000), owner);
            env.fund(XRP(1'000), alice);
            Oracle oracle(
                env,
                {.owner = owner,
                 .series = {{"XRP", "USD", 740, 1}},
                 .fee = baseFee});
            Oracle oracle1(
                env,
                {.owner = owner,
                 .documentID = 2,
                 .series = {{"XRP", "EUR", 740, 1}},
                 .fee = baseFee});
            BEAST_EXPECT(ownerCount(env, owner) == 2);
            BEAST_EXPECT(oracle.exists());
            BEAST_EXPECT(oracle1.exists());
            auto const index = env.closed()->seq();
            auto const hash = env.closed()->info().hash;
            for (int i = 0; i < 256; ++i)
                env.close();
            env(acctdelete(owner, alice), fee(acctDelFee));
            env.close();
            BEAST_EXPECT(!oracle.exists());
            BEAST_EXPECT(!oracle1.exists());

            // can still get the oracles via the ledger index or hash
            auto verifyLedgerData = [&](auto const& field, auto const& value) {
                Json::Value jvParams;
                jvParams[field] = value;
                jvParams[jss::binary] = false;
                jvParams[jss::type] = jss::oracle;
                Json::Value jrr =
                    env.rpc("json", "ledger_data", to_string(jvParams));
                BEAST_EXPECT(jrr[jss::result][jss::state].size() == 2);
            };
            verifyLedgerData(jss::ledger_index, index);
            verifyLedgerData(jss::ledger_hash, to_string(hash));
        }
    }

    void
    testUpdate()
    {
        testcase("Update");
        using namespace jtx;
        Account const owner("owner");

        {
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(XRP(1'000), owner);
            auto count = ownerCount(env, owner);
            Oracle oracle(env, {.owner = owner, .fee = baseFee});
            BEAST_EXPECT(oracle.exists());

            // update existing pair
            oracle.set(
                UpdateArg{.series = {{"XRP", "USD", 740, 2}}, .fee = baseFee});
            BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 740, 2}}));
            // owner count is increased by 1 since the oracle object is added
            // with one token pair
            count += 1;
            BEAST_EXPECT(ownerCount(env, owner) == count);

            // add new pairs, not-included pair is reset
            oracle.set(
                UpdateArg{.series = {{"XRP", "EUR", 700, 2}}, .fee = baseFee});
            BEAST_EXPECT(oracle.expectPrice(
                {{"XRP", "USD", 0, 0}, {"XRP", "EUR", 700, 2}}));
            // owner count is not changed since the number of pairs is 2
            BEAST_EXPECT(ownerCount(env, owner) == count);

            // update both pairs
            oracle.set(UpdateArg{
                .series = {{"XRP", "USD", 741, 2}, {"XRP", "EUR", 710, 2}},
                .fee = baseFee});
            BEAST_EXPECT(oracle.expectPrice(
                {{"XRP", "USD", 741, 2}, {"XRP", "EUR", 710, 2}}));
            // owner count is not changed since the number of pairs is 2
            BEAST_EXPECT(ownerCount(env, owner) == count);

            // owner count is increased by 1 since the number of pairs is 6
            oracle.set(UpdateArg{
                .series =
                    {
                        {"BTC", "USD", 741, 2},
                        {"ETH", "EUR", 710, 2},
                        {"YAN", "EUR", 710, 2},
                        {"CAN", "EUR", 710, 2},
                    },
                .fee = baseFee});
            count += 1;
            BEAST_EXPECT(ownerCount(env, owner) == count);

            // update two pairs and delete four
            oracle.set(UpdateArg{
                .series = {{"BTC", "USD", std::nullopt, std::nullopt}},
                .fee = baseFee});
            oracle.set(UpdateArg{
                .series =
                    {{"XRP", "USD", 742, 2},
                     {"XRP", "EUR", 711, 2},
                     {"ETH", "EUR", std::nullopt, std::nullopt},
                     {"YAN", "EUR", std::nullopt, std::nullopt},
                     {"CAN", "EUR", std::nullopt, std::nullopt}},
                .fee = baseFee});
            BEAST_EXPECT(oracle.expectPrice(
                {{"XRP", "USD", 742, 2}, {"XRP", "EUR", 711, 2}}));
            // owner count is decreased by 1 since the number of pairs is 2
            count -= 1;
            BEAST_EXPECT(ownerCount(env, owner) == count);
        }

        // Min reserve to create and update
        {
            Env env(*this);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());
            env.fund(
                env.current()->fees().accountReserve(1) +
                    env.current()->fees().base * 2,
                owner);
            Oracle oracle(env, {.owner = owner, .fee = baseFee});
            oracle.set(
                UpdateArg{.series = {{"XRP", "USD", 742, 2}}, .fee = baseFee});
        }

        for (bool const withFixOrder : {false, true})
        {
            // Should be same order as creation
            Env env(
                *this,
                withFixOrder ? testable_amendments()
                             : testable_amendments() - fixPriceOracleOrder);
            auto const baseFee =
                static_cast<int>(env.current()->fees().base.drops());

            auto test = [&](Env& env, DataSeries const& series) {
                env.fund(XRP(1'000), owner);
                Oracle oracle(
                    env, {.owner = owner, .series = series, .fee = baseFee});
                BEAST_EXPECT(oracle.exists());
                auto sle = env.le(keylet::oracle(owner, oracle.documentID()));
                BEAST_EXPECT(
                    sle->getFieldArray(sfPriceDataSeries).size() ==
                    series.size());

                auto const beforeQuoteAssetName1 =
                    sle->getFieldArray(sfPriceDataSeries)[0]
                        .getFieldCurrency(sfQuoteAsset)
                        .getText();
                auto const beforeQuoteAssetName2 =
                    sle->getFieldArray(sfPriceDataSeries)[1]
                        .getFieldCurrency(sfQuoteAsset)
                        .getText();

                oracle.set(UpdateArg{.series = series, .fee = baseFee});
                sle = env.le(keylet::oracle(owner, oracle.documentID()));

                auto const afterQuoteAssetName1 =
                    sle->getFieldArray(sfPriceDataSeries)[0]
                        .getFieldCurrency(sfQuoteAsset)
                        .getText();
                auto const afterQuoteAssetName2 =
                    sle->getFieldArray(sfPriceDataSeries)[1]
                        .getFieldCurrency(sfQuoteAsset)
                        .getText();

                if (env.current()->rules().enabled(fixPriceOracleOrder))
                {
                    BEAST_EXPECT(afterQuoteAssetName1 == beforeQuoteAssetName1);
                    BEAST_EXPECT(afterQuoteAssetName2 == beforeQuoteAssetName2);
                }
                else
                {
                    BEAST_EXPECT(afterQuoteAssetName1 != beforeQuoteAssetName1);
                    BEAST_EXPECT(afterQuoteAssetName2 != beforeQuoteAssetName2);
                }
            };
            test(env, {{"XRP", "USD", 742, 2}, {"XRP", "EUR", 711, 2}});
        }
    }

    void
    testMultisig(FeatureBitset features)
    {
        testcase("Multisig");
        using namespace jtx;
        Oracle::setFee(100'000);

        Env env(*this, features);
        auto const baseFee =
            static_cast<int>(env.current()->fees().base.drops());

        Account const alice{"alice", KeyType::secp256k1};
        Account const bogie{"bogie", KeyType::secp256k1};
        Account const ed{"ed", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const zelda{"zelda", KeyType::secp256k1};
        Account const bob{"bob", KeyType::secp256k1};
        env.fund(XRP(10'000), alice, becky, zelda, ed, bob);

        // alice uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), sig(alice));

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {bogie, 1}, {ed, 2}}), sig(alie));
        env.close();
        // if multiSignReserve disabled then its 2 + 1 per signer
        int const signerListOwners{features[featureMultiSignReserve] ? 1 : 5};
        env.require(owners(alice, signerListOwners));

        // Create
        // Force close (true) and time advancement because the close time
        // is no longer 0.
        Oracle oracle(
            env,
            CreateArg{.owner = alice, .fee = baseFee, .close = true},
            false);
        oracle.set(CreateArg{
            .msig = msig(becky), .fee = baseFee, .err = ter(tefBAD_QUORUM)});
        oracle.set(CreateArg{
            .msig = msig(zelda), .fee = baseFee, .err = ter(tefBAD_SIGNATURE)});
        oracle.set(CreateArg{.msig = msig(becky, bogie), .fee = baseFee});
        BEAST_EXPECT(oracle.exists());

        // Update
        oracle.set(UpdateArg{
            .series = {{"XRP", "USD", 740, 1}},
            .msig = msig(becky),
            .fee = baseFee,
            .err = ter(tefBAD_QUORUM)});
        oracle.set(UpdateArg{
            .series = {{"XRP", "USD", 740, 1}},
            .msig = msig(zelda),
            .fee = baseFee,
            .err = ter(tefBAD_SIGNATURE)});
        oracle.set(UpdateArg{
            .series = {{"XRP", "USD", 741, 1}},
            .msig = msig(becky, bogie),
            .fee = baseFee});
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 741, 1}}));
        // remove the signer list
        env(signers(alice, jtx::none), sig(alie));
        env.close();
        env.require(owners(alice, 1));
        // create new signer list
        env(signers(alice, 2, {{zelda, 1}, {bob, 1}, {ed, 2}}), sig(alie));
        env.close();
        // old list fails
        oracle.set(UpdateArg{
            .series = {{"XRP", "USD", 740, 1}},
            .msig = msig(becky, bogie),
            .fee = baseFee,
            .err = ter(tefBAD_SIGNATURE)});
        // updated list succeeds
        oracle.set(UpdateArg{
            .series = {{"XRP", "USD", 7412, 2}},
            .msig = msig(zelda, bob),
            .fee = baseFee});
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 7412, 2}}));
        oracle.set(UpdateArg{
            .series = {{"XRP", "USD", 74245, 3}},
            .msig = msig(ed),
            .fee = baseFee});
        BEAST_EXPECT(oracle.expectPrice({{"XRP", "USD", 74245, 3}}));

        // Remove
        oracle.remove(
            {.msig = msig(bob), .fee = baseFee, .err = ter(tefBAD_QUORUM)});
        oracle.remove(
            {.msig = msig(becky),
             .fee = baseFee,
             .err = ter(tefBAD_SIGNATURE)});
        oracle.remove({.msig = msig(ed), .fee = baseFee});
        BEAST_EXPECT(!oracle.exists());
    }

    void
    testAmendment()
    {
        testcase("Amendment");
        using namespace jtx;

        auto const features = testable_amendments() - featurePriceOracle;
        Account const owner("owner");
        Env env(*this, features);
        auto const baseFee =
            static_cast<int>(env.current()->fees().base.drops());

        env.fund(XRP(1'000), owner);
        {
            Oracle oracle(
                env, {.owner = owner, .fee = baseFee, .err = ter(temDISABLED)});
        }

        {
            Oracle oracle(env, {.owner = owner, .fee = baseFee}, false);
            oracle.remove({.fee = baseFee, .err = ter(temDISABLED)});
        }
    }

public:
    void
    run() override
    {
        using namespace jtx;
        auto const all = testable_amendments();
        testInvalidSet();
        testInvalidDelete();
        testCreate(all);
        testCreate(all - fixIncludeKeyletFields);
        testDelete();
        testUpdate();
        testAmendment();
        for (auto const& features :
             {all,
              all - featureMultiSignReserve - featureExpandedSignerList,
              all - featureExpandedSignerList})
            testMultisig(features);
    }
};

BEAST_DEFINE_TESTSUITE(Oracle, app, ripple);

}  // namespace oracle

}  // namespace jtx

}  // namespace test

}  // namespace ripple
