
#include <test/jtx/Env.h>

#include <xrpld/app/misc/FeeVote.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/tx/apply.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xrpl::test {

struct FeeSettingsFields
{
    std::optional<XRPAmount> baseFeeDrops = std::nullopt;
    std::optional<XRPAmount> reserveBaseDrops = std::nullopt;
    std::optional<XRPAmount> reserveIncrementDrops = std::nullopt;
};

STTx
createFeeTx(std::uint32_t seq, FeeSettingsFields const& fields)
{
    auto fill = [&](auto& obj) {
        obj.setAccountID(sfAccount, AccountID());
        obj.setFieldU32(sfLedgerSequence, seq);

        // XRPFees feature has been retired - all three fields are REQUIRED
        obj.setFieldAmount(
            sfBaseFeeDrops, fields.baseFeeDrops ? *fields.baseFeeDrops : XRPAmount{0});
        obj.setFieldAmount(
            sfReserveBaseDrops, fields.reserveBaseDrops ? *fields.reserveBaseDrops : XRPAmount{0});
        obj.setFieldAmount(
            sfReserveIncrementDrops,
            fields.reserveIncrementDrops ? *fields.reserveIncrementDrops : XRPAmount{0});
    };
    return STTx(ttFEE, fill);
}

// A ttFEE transaction that omits the (now mandatory) *Drops fields.
STTx
createFeeTxMissingRequiredFields(std::uint32_t seq)
{
    return STTx(ttFEE, [&](auto& obj) {
        obj.setAccountID(sfAccount, AccountID());
        obj.setFieldU32(sfLedgerSequence, seq);
    });
}

// A ttFEE transaction that carries all the required *Drops fields, but also
// the pre-XRPFees legacy fields, which are forbidden now that XRPFees has
// been retired.
STTx
createFeeTxWithLegacyFields(std::uint32_t seq)
{
    return STTx(ttFEE, [&](auto& obj) {
        obj.setAccountID(sfAccount, AccountID());
        obj.setFieldU32(sfLedgerSequence, seq);

        obj.setFieldAmount(sfBaseFeeDrops, XRPAmount{10});
        obj.setFieldAmount(sfReserveBaseDrops, XRPAmount{200000});
        obj.setFieldAmount(sfReserveIncrementDrops, XRPAmount{50000});

        obj.setFieldU64(sfBaseFee, 10);
        obj.setFieldU32(sfReserveBase, 200000);
        obj.setFieldU32(sfReserveIncrement, 50000);
        obj.setFieldU32(sfReferenceFeeUnits, 10);
    });
}

TER
applyFeeTx(jtx::Env& env, OpenView& view, STTx const& tx)
{
    return apply(env.app(), view, tx, ApplyFlags::TapNone, env.journal).ter;
}

bool
applyFeeAndTestResult(jtx::Env& env, OpenView& view, STTx const& tx)
{
    return isTesSuccess(applyFeeTx(env, view, tx));
}

bool
verifyFeeObject(std::shared_ptr<Ledger const> const& ledger, FeeSettingsFields const& expected)
{
    auto const feeObject = ledger->read(keylet::feeSettings());
    if (!feeObject)
        return false;

    auto checkEquality = [&](auto const& field, auto const& expected) {
        if (!feeObject->isFieldPresent(field))
            return false;
        return feeObject->at(field) == expected;
    };

    if (feeObject->isFieldPresent(sfBaseFee) || feeObject->isFieldPresent(sfReserveBase) ||
        feeObject->isFieldPresent(sfReserveIncrement) ||
        feeObject->isFieldPresent(sfReferenceFeeUnits))
        return false;

    if (!checkEquality(sfBaseFeeDrops, expected.baseFeeDrops.value_or(XRPAmount{0})))
        return false;
    if (!checkEquality(sfReserveBaseDrops, expected.reserveBaseDrops.value_or(XRPAmount{0})))
        return false;
    if (!checkEquality(
            sfReserveIncrementDrops, expected.reserveIncrementDrops.value_or(XRPAmount{0})))
        return false;

    return true;
}

std::vector<STTx>
getTxs(std::shared_ptr<SHAMap> const& txSet)
{
    std::vector<STTx> txs;
    for (auto i = txSet->begin(); i != txSet->end(); ++i)
    {
        auto const data = i->slice();
        auto serialIter = SerialIter(data);
        txs.emplace_back(serialIter);
    }
    return txs;
};

class FeeVote_test : public beast::unit_test::Suite
{
    void
    testSetup()
    {
        FeeSetup const defaultSetup;
        {
            // defaults
            Section const config;
            auto setup = setupFeeVote(config);
            BEAST_EXPECT(setup.referenceFee == defaultSetup.referenceFee);
            BEAST_EXPECT(setup.accountReserve == defaultSetup.accountReserve);
            BEAST_EXPECT(setup.ownerReserve == defaultSetup.ownerReserve);
        }
        {
            Section config;
            config.append(
                {"reference_fee = 50", "account_reserve = 1234567", "owner_reserve = 1234"});
            auto setup = setupFeeVote(config);
            BEAST_EXPECT(setup.referenceFee == 50);
            BEAST_EXPECT(setup.accountReserve == 1234567);
            BEAST_EXPECT(setup.ownerReserve == 1234);
        }
        {
            Section config;
            config.append(
                {"reference_fee = blah", "account_reserve = yada", "owner_reserve = foo"});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setupFeeVote(config);
            BEAST_EXPECT(setup.referenceFee == defaultSetup.referenceFee);
            BEAST_EXPECT(setup.accountReserve == defaultSetup.accountReserve);
            BEAST_EXPECT(setup.ownerReserve == defaultSetup.ownerReserve);
        }
        {
            Section config;
            config.append(
                {"reference_fee = -50", "account_reserve = -1234567", "owner_reserve = -1234"});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setupFeeVote(config);
            BEAST_EXPECT(setup.referenceFee == defaultSetup.referenceFee);
            BEAST_EXPECT(setup.accountReserve == static_cast<std::uint32_t>(-1234567));
            BEAST_EXPECT(setup.ownerReserve == static_cast<std::uint32_t>(-1234));
        }
        {
            auto const big64 = std::to_string(
                static_cast<std::uint64_t>(std::numeric_limits<XRPAmount::value_type>::max()) + 1);
            Section config;
            config.append(
                {"reference_fee = " + big64,
                 "account_reserve = " + big64,
                 "owner_reserve = " + big64});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setupFeeVote(config);
            BEAST_EXPECT(setup.referenceFee == defaultSetup.referenceFee);
            BEAST_EXPECT(setup.accountReserve == defaultSetup.accountReserve);
            BEAST_EXPECT(setup.ownerReserve == defaultSetup.ownerReserve);
        }
    }

    void
    testBasic()
    {
        testcase("Basic SetFee transaction");

        jtx::Env env(*this);
        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // Create the next ledger to apply transaction to
        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        FeeSettingsFields const fields{
            .baseFeeDrops = XRPAmount{10},
            .reserveBaseDrops = XRPAmount{200000},
            .reserveIncrementDrops = XRPAmount{50000}};
        // Test successful fee transaction with new fields
        auto feeTx = createFeeTx(ledger->seq(), fields);

        OpenView accum(ledger.get());
        BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx));
        accum.apply(*ledger);

        // Verify fee object was created/updated correctly
        BEAST_EXPECT(verifyFeeObject(ledger, fields));
    }

    void
    testTransactionValidation()
    {
        testcase("Fee Transaction Validation");

        jtx::Env env(*this);
        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // Create the next ledger to apply transaction to
        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        OpenView accum(ledger.get());

        // Test transaction with missing required new fields
        auto const missingTx = createFeeTxMissingRequiredFields(ledger->seq());
        BEAST_EXPECT(applyFeeTx(env, accum, missingTx) == temMALFORMED);

        // Test transaction that has all the required fields but also carries
        // the legacy fields. Now that XRPFees is retired those are forbidden,
        // and rejected with temMALFORMED rather than the former temDISABLED.
        auto const disallowedTx = createFeeTxWithLegacyFields(ledger->seq());
        BEAST_EXPECT(applyFeeTx(env, accum, disallowedTx) == temMALFORMED);
    }

    void
    testPseudoTransactionProperties()
    {
        testcase("Pseudo Transaction Properties");

        jtx::Env env(*this);
        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // Create the next ledger to apply transaction to
        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        auto feeTx = createFeeTx(
            ledger->seq(),
            {.baseFeeDrops = XRPAmount{10},
             .reserveBaseDrops = XRPAmount{200000},
             .reserveIncrementDrops = XRPAmount{50000}});

        // Verify pseudo-transaction properties
        BEAST_EXPECT(feeTx.getAccountID(sfAccount) == AccountID());
        BEAST_EXPECT(feeTx.getFieldAmount(sfFee) == XRPAmount{0});
        BEAST_EXPECT(feeTx.getSigningPubKey().empty());
        BEAST_EXPECT(feeTx.getSignature().empty());
        BEAST_EXPECT(!feeTx.isFieldPresent(sfSigners));
        BEAST_EXPECT(feeTx.getFieldU32(sfSequence) == 0);
        BEAST_EXPECT(!feeTx.isFieldPresent(sfPreviousTxnID));

        // But can be applied to a closed ledger
        {
            OpenView closedAccum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, closedAccum, feeTx));
        }
    }

    void
    testMultipleFeeUpdates()
    {
        testcase("Multiple Fee Updates");

        jtx::Env env(*this);
        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        FeeSettingsFields const fields1{
            .baseFeeDrops = XRPAmount{10},
            .reserveBaseDrops = XRPAmount{200000},
            .reserveIncrementDrops = XRPAmount{50000}};
        auto feeTx1 = createFeeTx(ledger->seq(), fields1);

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx1));
            accum.apply(*ledger);
        }

        BEAST_EXPECT(verifyFeeObject(ledger, fields1));

        // Apply second fee transaction with different values
        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        FeeSettingsFields const fields2{
            .baseFeeDrops = XRPAmount{20},
            .reserveBaseDrops = XRPAmount{300000},
            .reserveIncrementDrops = XRPAmount{75000}};
        auto feeTx2 = createFeeTx(ledger->seq(), fields2);

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx2));
            accum.apply(*ledger);
        }

        // Verify second update overwrote the first
        BEAST_EXPECT(verifyFeeObject(ledger, fields2));
    }

    void
    testWrongLedgerSequence()
    {
        testcase("Wrong Ledger Sequence");

        jtx::Env env(*this);
        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        // Test transaction with wrong ledger sequence
        auto feeTx = createFeeTx(
            ledger->seq() + 5,  // Wrong sequence (should be ledger->seq())
            {.baseFeeDrops = XRPAmount{10},
             .reserveBaseDrops = XRPAmount{200000},
             .reserveIncrementDrops = XRPAmount{50000}});

        OpenView accum(ledger.get());

        // The transaction should still succeed as long as other fields are
        // valid
        // The ledger sequence field is only used for informational purposes
        BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx));
    }

    void
    testPartialFieldUpdates()
    {
        testcase("Partial Field Updates");

        jtx::Env env(*this);
        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        FeeSettingsFields const fields1{
            .baseFeeDrops = XRPAmount{10},
            .reserveBaseDrops = XRPAmount{200000},
            .reserveIncrementDrops = XRPAmount{50000}};
        auto feeTx1 = createFeeTx(ledger->seq(), fields1);

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx1));
            accum.apply(*ledger);
        }

        BEAST_EXPECT(verifyFeeObject(ledger, fields1));

        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        // Apply partial update (only some fields)
        FeeSettingsFields const fields2{
            .baseFeeDrops = XRPAmount{20}, .reserveBaseDrops = XRPAmount{200000}};
        auto feeTx2 = createFeeTx(ledger->seq(), fields2);

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx2));
            accum.apply(*ledger);
        }

        // Verify the partial update worked
        BEAST_EXPECT(verifyFeeObject(ledger, fields2));
    }

    void
    testSingleInvalidTransaction()
    {
        testcase("Single Invalid Transaction");

        jtx::Env env(*this);
        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        // Test invalid transaction with non-zero account - this should fail
        // validation
        auto invalidTx = STTx(ttFEE, [&](auto& obj) {
            obj.setAccountID(sfAccount,
                             AccountID(1));  // Should be zero (this makes it invalid)
            obj.setFieldU32(sfLedgerSequence, ledger->seq());
            obj.setFieldAmount(sfBaseFeeDrops, XRPAmount{10});
            obj.setFieldAmount(sfReserveBaseDrops, XRPAmount{200000});
            obj.setFieldAmount(sfReserveIncrementDrops, XRPAmount{50000});
        });

        OpenView accum(ledger.get());
        BEAST_EXPECT(!applyFeeAndTestResult(env, accum, invalidTx));
    }

    void
    testDoValidation()
    {
        testcase("doValidation");

        using namespace jtx;

        FeeSetup setup;
        setup.referenceFee = 42;
        setup.accountReserve = 1234567;
        setup.ownerReserve = 7654321;

        Env env(*this, testableAmendments());
        auto feeVote = makeFeeVote(setup, env.app().getJournal("FeeVote"));

        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        auto sec = randomSecretKey();
        auto pub = derivePublicKey(KeyType::Secp256k1, sec);

        auto val = std::make_shared<STValidation>(
            env.app().getTimeKeeper().now(), pub, sec, calcNodeID(pub), [](STValidation& v) {
                v.setFieldU32(sfLedgerSequence, 12345);
            });

        // Use the current ledger's fees as the "current" fees for
        // doValidation
        auto const& currentFees = ledger->fees();

        feeVote->doValidation(currentFees, ledger->rules(), *val);

        BEAST_EXPECT(val->isFieldPresent(sfBaseFeeDrops));
        BEAST_EXPECT(val->getFieldAmount(sfBaseFeeDrops) == XRPAmount(setup.referenceFee));
    }

    void
    testDoVoting()
    {
        testcase("doVoting");

        using namespace jtx;

        FeeSetup setup;
        setup.referenceFee = 42;
        setup.accountReserve = 1234567;
        setup.ownerReserve = 7654321;

        Env env(*this, testableAmendments());

        // establish what the current fees are
        BEAST_EXPECT(env.current()->fees().base == XRPAmount{UNIT_TEST_REFERENCE_FEE});
        BEAST_EXPECT(env.current()->fees().reserve == XRPAmount{200'000'000});
        BEAST_EXPECT(env.current()->fees().increment == XRPAmount{50'000'000});

        auto feeVote = makeFeeVote(setup, env.app().getJournal("FeeVote"));
        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // doVoting requires a flag ledger (every 256th ledger)
        // We need to create a ledger at sequence 256 to make it a flag ledger
        for (int i = 0; i < 256 - 1; ++i)
        {
            ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());
        }
        BEAST_EXPECT(ledger->isFlagLedger());

        // Create some mock validations with fee votes
        std::vector<std::shared_ptr<STValidation>> validations;

        for (int i = 0; i < 5; i++)
        {
            auto sec = randomSecretKey();
            auto pub = derivePublicKey(KeyType::Secp256k1, sec);

            auto val = std::make_shared<STValidation>(
                env.app().getTimeKeeper().now(), pub, sec, calcNodeID(pub), [&](STValidation& v) {
                    v.setFieldU32(sfLedgerSequence, ledger->seq());
                    // Vote for different fees than current
                    v.setFieldAmount(sfBaseFeeDrops, XRPAmount{setup.referenceFee});
                    v.setFieldAmount(sfReserveBaseDrops, XRPAmount{setup.accountReserve});
                    v.setFieldAmount(sfReserveIncrementDrops, XRPAmount{setup.ownerReserve});
                });
            if ((i % 2) != 0)
                val->setTrusted();
            validations.push_back(val);
        }

        auto txSet = std::make_shared<SHAMap>(SHAMapType::TRANSACTION, env.app().getNodeFamily());

        // This should not throw since we have a flag ledger
        feeVote->doVoting(ledger, validations, txSet);

        auto const txs = getTxs(txSet);
        BEAST_EXPECT(txs.size() == 1);
        auto const& feeTx = txs[0];

        BEAST_EXPECT(feeTx.getTxnType() == ttFEE);

        BEAST_EXPECT(feeTx.getAccountID(sfAccount) == AccountID());
        BEAST_EXPECT(feeTx.getFieldU32(sfLedgerSequence) == ledger->seq() + 1);

        BEAST_EXPECT(feeTx.isFieldPresent(sfBaseFeeDrops));
        BEAST_EXPECT(feeTx.isFieldPresent(sfReserveBaseDrops));
        BEAST_EXPECT(feeTx.isFieldPresent(sfReserveIncrementDrops));

        // The legacy fields should NOT be present
        BEAST_EXPECT(!feeTx.isFieldPresent(sfBaseFee));
        BEAST_EXPECT(!feeTx.isFieldPresent(sfReserveBase));
        BEAST_EXPECT(!feeTx.isFieldPresent(sfReserveIncrement));
        BEAST_EXPECT(!feeTx.isFieldPresent(sfReferenceFeeUnits));

        // Check the values
        BEAST_EXPECT(feeTx.getFieldAmount(sfBaseFeeDrops) == XRPAmount{setup.referenceFee});
        BEAST_EXPECT(feeTx.getFieldAmount(sfReserveBaseDrops) == XRPAmount{setup.accountReserve});
        BEAST_EXPECT(
            feeTx.getFieldAmount(sfReserveIncrementDrops) == XRPAmount{setup.ownerReserve});
    }

    void
    run() override
    {
        testSetup();
        testBasic();
        testTransactionValidation();
        testPseudoTransactionProperties();
        testMultipleFeeUpdates();
        testWrongLedgerSequence();
        testPartialFieldUpdates();
        testSingleInvalidTransaction();
        testDoValidation();
        testDoVoting();
    }
};

BEAST_DEFINE_TESTSUITE(FeeVote, app, xrpl);

}  // namespace xrpl::test
