
#include <test/jtx/Env.h>

#include <xrpld/app/misc/FeeVote.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
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
#include <source_location>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

struct FeeSettingsFields
{
    std::optional<std::uint64_t> baseFee = std::nullopt;
    std::optional<std::uint32_t> reserveBase = std::nullopt;
    std::optional<std::uint32_t> reserveIncrement = std::nullopt;
    std::optional<std::uint32_t> referenceFeeUnits = std::nullopt;
    std::optional<XRPAmount> baseFeeDrops = std::nullopt;
    std::optional<XRPAmount> reserveBaseDrops = std::nullopt;
    std::optional<XRPAmount> reserveIncrementDrops = std::nullopt;
    std::optional<std::uint32_t> gasLimit = std::nullopt;
    std::optional<std::uint32_t> bytecodeSizeLimit = std::nullopt;
    std::optional<std::uint32_t> gasPrice = std::nullopt;
};

STTx
createFeeTx(
    Rules const& rules,
    std::uint32_t seq,
    FeeSettingsFields const& fields,
    bool forceAllFields = false)
{
    auto fill = [&](auto& obj) {
        obj.setAccountID(sfAccount, AccountID());
        obj.setFieldU32(sfLedgerSequence, seq);

        if (rules.enabled(featureXRPFees))
        {
            // New XRPFees format - all three fields are REQUIRED
            obj.setFieldAmount(
                sfBaseFeeDrops, fields.baseFeeDrops ? *fields.baseFeeDrops : XRPAmount{0});
            obj.setFieldAmount(
                sfReserveBaseDrops,
                fields.reserveBaseDrops ? *fields.reserveBaseDrops : XRPAmount{0});
            obj.setFieldAmount(
                sfReserveIncrementDrops,
                fields.reserveIncrementDrops ? *fields.reserveIncrementDrops : XRPAmount{0});
        }
        else
        {
            // Legacy format - all four fields are REQUIRED
            obj.setFieldU64(sfBaseFee, fields.baseFee ? *fields.baseFee : 0);
            obj.setFieldU32(sfReserveBase, fields.reserveBase ? *fields.reserveBase : 0);
            obj.setFieldU32(
                sfReserveIncrement, fields.reserveIncrement ? *fields.reserveIncrement : 0);
            obj.setFieldU32(
                sfReferenceFeeUnits, fields.referenceFeeUnits ? *fields.referenceFeeUnits : 0);
        }
        if (rules.enabled(featureSmartEscrow) || forceAllFields)
        {
            obj.setFieldU32(sfGasLimit, fields.gasLimit ? *fields.gasLimit : 0);
            obj.setFieldU32(
                sfBytecodeSizeLimit, fields.bytecodeSizeLimit ? *fields.bytecodeSizeLimit : 0);
            obj.setFieldU32(sfGasPrice, fields.gasPrice ? *fields.gasPrice : 0);
        }
    };
    return STTx(ttFEE, fill);
}

STTx
createInvalidFeeTx(
    Rules const& rules,
    std::uint32_t seq,
    bool missingRequiredFields = true,
    bool wrongFeatureFields = false,
    std::uint32_t uniqueValue = 42)
{
    auto fill = [&](auto& obj) {
        obj.setAccountID(sfAccount, AccountID());
        obj.setFieldU32(sfLedgerSequence, seq);

        if (wrongFeatureFields)
        {
            if (rules.enabled(featureXRPFees))
            {
                obj.setFieldU64(sfBaseFee, 10 + uniqueValue);
                obj.setFieldU32(sfReserveBase, 200000);
                obj.setFieldU32(sfReserveIncrement, 50000);
                obj.setFieldU32(sfReferenceFeeUnits, 10);
            }
            else
            {
                obj.setFieldAmount(sfBaseFeeDrops, XRPAmount{10 + uniqueValue});
                obj.setFieldAmount(sfReserveBaseDrops, XRPAmount{200000});
                obj.setFieldAmount(sfReserveIncrementDrops, XRPAmount{50000});
            }
        }
        else if (!missingRequiredFields)
        {
            // Create valid transaction (all required fields present)
            if (rules.enabled(featureXRPFees))
            {
                obj.setFieldAmount(sfBaseFeeDrops, XRPAmount{10 + uniqueValue});
                obj.setFieldAmount(sfReserveBaseDrops, XRPAmount{200000});
                obj.setFieldAmount(sfReserveIncrementDrops, XRPAmount{50000});
            }
            else
            {
                obj.setFieldU64(sfBaseFee, 10 + uniqueValue);
                obj.setFieldU32(sfReserveBase, 200000);
                obj.setFieldU32(sfReserveIncrement, 50000);
                obj.setFieldU32(sfReferenceFeeUnits, 10);
            }
            if (rules.enabled(featureSmartEscrow))
            {
                obj.setFieldU32(sfGasLimit, 100 + uniqueValue);
                obj.setFieldU32(sfBytecodeSizeLimit, 200 + uniqueValue);
                obj.setFieldU32(sfGasPrice, 300 + uniqueValue);
            }
        }
        // If missingRequiredFields is true, we don't add the required fields
        // (default behavior)
    };
    return STTx(ttFEE, fill);
}

TER
applyFeeAndTestResult(jtx::Env& env, OpenView& view, STTx const& tx)
{
    auto const res = apply(env.app(), view, tx, ApplyFlags::TapNone, env.journal);
    return res.ter;
}

bool
verifyFeeObject(
    std::shared_ptr<Ledger const> const& ledger,
    Rules const& rules,
    FeeSettingsFields const& expected)
{
    auto const feeObject = ledger->read(keylet::feeSettings());
    if (!feeObject)
        return false;

    auto checkEquality = [&](auto const& field, auto const& expected) {
        if (!feeObject->isFieldPresent(field))
            return false;
        return feeObject->at(field) == expected;
    };

    if (rules.enabled(featureXRPFees))
    {
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
    }
    else
    {
        if (feeObject->isFieldPresent(sfBaseFeeDrops) ||
            feeObject->isFieldPresent(sfReserveBaseDrops) ||
            feeObject->isFieldPresent(sfReserveIncrementDrops))
            return false;

        // Read sfBaseFee as a hex string and compare to expected.baseFee
        if (!checkEquality(sfBaseFee, expected.baseFee))
            return false;
        if (!checkEquality(sfReserveBase, expected.reserveBase))
            return false;
        if (!checkEquality(sfReserveIncrement, expected.reserveIncrement))
            return false;
        if (!checkEquality(sfReferenceFeeUnits, expected.referenceFeeUnits))
            return false;
    }
    if (rules.enabled(featureSmartEscrow))
    {
        if (!checkEquality(sfGasLimit, expected.gasLimit.value_or(0)))
            return false;
        if (!checkEquality(sfBytecodeSizeLimit, expected.bytecodeSizeLimit.value_or(0)))
            return false;
        if (!checkEquality(sfGasPrice, expected.gasPrice.value_or(0)))
            return false;
    }
    else
    {
        if (feeObject->isFieldPresent(sfGasLimit) ||
            feeObject->isFieldPresent(sfBytecodeSizeLimit) || feeObject->isFieldPresent(sfGasPrice))
            return false;
    }

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
        testcase("FeeVote setup");
        FeeSetup const defaultSetup;
        {
            // defaults
            Section const config;
            auto setup = setupFeeVote(config);
            BEAST_EXPECT(setup.referenceFee == defaultSetup.referenceFee);
            BEAST_EXPECT(setup.accountReserve == defaultSetup.accountReserve);
            BEAST_EXPECT(setup.ownerReserve == defaultSetup.ownerReserve);
            BEAST_EXPECT(setup.gasLimit == defaultSetup.gasLimit);
            BEAST_EXPECT(setup.bytecodeSizeLimit == defaultSetup.bytecodeSizeLimit);
            BEAST_EXPECT(setup.gasPrice == defaultSetup.gasPrice);
        }
        {
            Section config;
            config.append(
                {"reference_fee = 50",
                 "account_reserve = 1234567",
                 "owner_reserve = 1234",
                 "gas_limit = 100",
                 "bytecode_size_limit = 200",
                 "gas_price = 300"});
            auto setup = setupFeeVote(config);
            BEAST_EXPECT(setup.referenceFee == 50);
            BEAST_EXPECT(setup.accountReserve == 1234567);
            BEAST_EXPECT(setup.ownerReserve == 1234);
            BEAST_EXPECT(setup.gasLimit == 100);
            BEAST_EXPECT(setup.bytecodeSizeLimit == 200);
            BEAST_EXPECT(setup.gasPrice == 300);
        }
        {
            Section config;
            config.append(
                {"reference_fee = blah",
                 "account_reserve = yada",
                 "owner_reserve = foo",
                 "gas_limit = bar",
                 "bytecode_size_limit = baz",
                 "gas_price = qux"});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setupFeeVote(config);
            BEAST_EXPECT(setup.referenceFee == defaultSetup.referenceFee);
            BEAST_EXPECT(setup.accountReserve == defaultSetup.accountReserve);
            BEAST_EXPECT(setup.ownerReserve == defaultSetup.ownerReserve);
            BEAST_EXPECT(setup.gasLimit == defaultSetup.gasLimit);
            BEAST_EXPECT(setup.bytecodeSizeLimit == defaultSetup.bytecodeSizeLimit);
            BEAST_EXPECT(setup.gasPrice == defaultSetup.gasPrice);
        }
        {
            Section config;
            config.append(
                {"reference_fee = -50",
                 "account_reserve = -1234567",
                 "owner_reserve = -1234",
                 "gas_limit = -100",
                 "bytecode_size_limit = -200",
                 "gas_price = -300"});
            // Negative gas/bytecode limit values wrap past their maximum and are
            // ignored. Other uint32_t fields keep the existing behavior.
            auto setup = setupFeeVote(config);
            BEAST_EXPECT(setup.referenceFee == defaultSetup.referenceFee);
            BEAST_EXPECT(setup.accountReserve == static_cast<std::uint32_t>(-1234567));
            BEAST_EXPECT(setup.ownerReserve == static_cast<std::uint32_t>(-1234));
            BEAST_EXPECT(setup.gasLimit == defaultSetup.gasLimit);
            BEAST_EXPECT(setup.bytecodeSizeLimit == defaultSetup.bytecodeSizeLimit);
            BEAST_EXPECT(setup.gasPrice == static_cast<std::uint32_t>(-300));
        }
        {
            auto const big64 = std::to_string(
                static_cast<std::uint64_t>(std::numeric_limits<XRPAmount::value_type>::max()) + 1);
            Section config;
            config.append(
                {"reference_fee = " + big64,
                 "account_reserve = " + big64,
                 "owner_reserve = " + big64,
                 "gas_limit = " + big64,
                 "bytecode_size_limit = " + big64,
                 "gas_price = " + big64});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setupFeeVote(config);
            BEAST_EXPECT(setup.referenceFee == defaultSetup.referenceFee);
            BEAST_EXPECT(setup.accountReserve == defaultSetup.accountReserve);
            BEAST_EXPECT(setup.ownerReserve == defaultSetup.ownerReserve);
            BEAST_EXPECT(setup.gasLimit == defaultSetup.gasLimit);
            BEAST_EXPECT(setup.bytecodeSizeLimit == defaultSetup.bytecodeSizeLimit);
            BEAST_EXPECT(setup.gasPrice == defaultSetup.gasPrice);
        }
        {
            Section config;
            config.append(
                {"gas_limit = " + std::to_string(kMaxGasLimit + 1),
                 "bytecode_size_limit = " + std::to_string(kMaxBytecodeSizeLimit + 1)});
            auto const setup = setupFeeVote(config);
            BEAST_EXPECT(setup.gasLimit == defaultSetup.gasLimit);
            BEAST_EXPECT(setup.bytecodeSizeLimit == defaultSetup.bytecodeSizeLimit);
        }
        {
            Section config;
            config.append(
                {"gas_limit = " + std::to_string(kMaxGasLimit),
                 "bytecode_size_limit = " + std::to_string(kMaxBytecodeSizeLimit)});
            auto const setup = setupFeeVote(config);
            BEAST_EXPECT(setup.gasLimit == kMaxGasLimit);
            BEAST_EXPECT(setup.bytecodeSizeLimit == kMaxBytecodeSizeLimit);
        }
    }

    void
    testBasic()
    {
        testcase("Basic SetFee transaction");

        // Test with XRPFees disabled (legacy format)
        {
            jtx::Env env(*this, jtx::testableAmendments() - featureXRPFees - featureSmartEscrow);
            auto ledger = std::make_shared<Ledger>(
                kCreateGenesis,
                Rules{env.app().config().features},
                env.app().config().fees.toFees(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

            // Test successful fee transaction with legacy fields

            FeeSettingsFields const fields{
                .baseFee = 10,
                .reserveBase = 200000,
                .reserveIncrement = 50000,
                .referenceFeeUnits = 10};
            auto feeTx = createFeeTx(ledger->rules(), ledger->seq(), fields);

            OpenView accum(ledger.get());
            BEAST_EXPECT(isTesSuccess(applyFeeAndTestResult(env, accum, feeTx)));
            accum.apply(*ledger);

            // Verify fee object was created/updated correctly
            BEAST_EXPECT(verifyFeeObject(ledger, ledger->rules(), fields));
        }

        // Test with XRPFees enabled (new format)
        {
            jtx::Env env(*this, jtx::testableAmendments() - featureSmartEscrow);
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
            auto feeTx = createFeeTx(ledger->rules(), ledger->seq(), fields);

            OpenView accum(ledger.get());
            BEAST_EXPECT(isTesSuccess(applyFeeAndTestResult(env, accum, feeTx)));
            accum.apply(*ledger);

            // Verify fee object was created/updated correctly
            BEAST_EXPECT(verifyFeeObject(ledger, ledger->rules(), fields));
        }

        // Test with both XRPFees and SmartEscrow enabled
        {
            jtx::Env env(*this, jtx::testableAmendments());
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
                .reserveIncrementDrops = XRPAmount{50000},
                .gasLimit = 100,
                .bytecodeSizeLimit = 200,
                .gasPrice = 300};
            // Test successful fee transaction with new fields
            auto feeTx = createFeeTx(ledger->rules(), ledger->seq(), fields);

            OpenView accum(ledger.get());
            BEAST_EXPECT(isTesSuccess(applyFeeAndTestResult(env, accum, feeTx)));
            accum.apply(*ledger);

            // Verify fee object was created/updated correctly
            BEAST_EXPECT(verifyFeeObject(ledger, ledger->rules(), fields));
        }

        // Test that Smart Escrow limits reject values above their maximums.
        {
            jtx::Env env(*this, jtx::testableAmendments());
            auto ledger = std::make_shared<Ledger>(
                kCreateGenesis,
                Rules{env.app().config().features},
                env.app().config().fees.toFees(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

            auto testBadFields = [&](FeeSettingsFields const& fields) {
                auto feeTx = createFeeTx(ledger->rules(), ledger->seq(), fields);
                OpenView accum(ledger.get());
                BEAST_EXPECT(!isTesSuccess(applyFeeAndTestResult(env, accum, feeTx)));
            };

            testBadFields(
                {.baseFeeDrops = XRPAmount{10},
                 .reserveBaseDrops = XRPAmount{200000},
                 .reserveIncrementDrops = XRPAmount{50000},
                 .gasLimit = kMaxGasLimit + 1,
                 .bytecodeSizeLimit = kMaxBytecodeSizeLimit,
                 .gasPrice = 300});
            testBadFields(
                {.baseFeeDrops = XRPAmount{10},
                 .reserveBaseDrops = XRPAmount{200000},
                 .reserveIncrementDrops = XRPAmount{50000},
                 .gasLimit = kMaxGasLimit,
                 .bytecodeSizeLimit = kMaxBytecodeSizeLimit + 1,
                 .gasPrice = 300});
        }

        // Test that the Smart Escrow fields are rejected if the
        // feature is disabled
        {
            jtx::Env env(*this, jtx::testableAmendments() - featureSmartEscrow);
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
                .reserveIncrementDrops = XRPAmount{50000},
                .gasLimit = 100,
                .bytecodeSizeLimit = 200,
                .gasPrice = 300};
            // Test successful fee transaction with new fields
            auto feeTx = createFeeTx(ledger->rules(), ledger->seq(), fields, true);

            OpenView accum(ledger.get());
            BEAST_EXPECT(!isTesSuccess(applyFeeAndTestResult(env, accum, feeTx)));
        }
    }

    void
    testTransactionValidation()
    {
        testcase("Fee Transaction Validation");

        {
            jtx::Env env(*this, jtx::testableAmendments() - featureXRPFees - featureSmartEscrow);
            auto ledger = std::make_shared<Ledger>(
                kCreateGenesis,
                Rules{env.app().config().features},
                env.app().config().fees.toFees(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

            // Test transaction with missing required legacy fields
            auto invalidTx = createInvalidFeeTx(ledger->rules(), ledger->seq(), true, false, 1);
            OpenView accum(ledger.get());
            BEAST_EXPECT(!isTesSuccess(applyFeeAndTestResult(env, accum, invalidTx)));

            // Test transaction with new format fields when XRPFees is disabled
            auto disallowedTx = createInvalidFeeTx(ledger->rules(), ledger->seq(), false, true, 2);
            BEAST_EXPECT(!isTesSuccess(applyFeeAndTestResult(env, accum, disallowedTx)));
        }

        {
            jtx::Env env(*this, jtx::testableAmendments() - featureSmartEscrow);
            auto ledger = std::make_shared<Ledger>(
                kCreateGenesis,
                Rules{env.app().config().features},
                env.app().config().fees.toFees(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

            // Test transaction with missing required new fields
            auto invalidTx = createInvalidFeeTx(ledger->rules(), ledger->seq(), true, false, 3);
            OpenView accum(ledger.get());
            BEAST_EXPECT(!isTesSuccess(applyFeeAndTestResult(env, accum, invalidTx)));

            // Test transaction with legacy fields when XRPFees is enabled
            auto disallowedTx = createInvalidFeeTx(ledger->rules(), ledger->seq(), false, true, 4);
            BEAST_EXPECT(!isTesSuccess(applyFeeAndTestResult(env, accum, disallowedTx)));
        }

        {
            jtx::Env env(*this, jtx::testableAmendments() | featureXRPFees | featureSmartEscrow);
            auto ledger = std::make_shared<Ledger>(
                kCreateGenesis,
                Rules{env.app().config().features},
                env.app().config().fees.toFees(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

            // Test transaction with missing required new fields
            auto invalidTx = createInvalidFeeTx(ledger->rules(), ledger->seq(), true, false, 5);
            OpenView accum(ledger.get());
            BEAST_EXPECT(!isTesSuccess(applyFeeAndTestResult(env, accum, invalidTx)));

            // Test transaction with legacy fields when XRPFees is enabled
            auto disallowedTx = createInvalidFeeTx(ledger->rules(), ledger->seq(), false, true, 6);
            BEAST_EXPECT(!isTesSuccess(applyFeeAndTestResult(env, accum, disallowedTx)));
        }
    }

    void
    testPseudoTransactionProperties()
    {
        testcase("Pseudo Transaction Properties");

        jtx::Env env(*this, jtx::testableAmendments() - featureSmartEscrow);
        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // Create the next ledger to apply transaction to
        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        auto feeTx = createFeeTx(
            ledger->rules(),
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
            BEAST_EXPECT(isTesSuccess(applyFeeAndTestResult(env, closedAccum, feeTx)));
        }
    }

    void
    testMultipleFeeUpdates()
    {
        testcase("Multiple Fee Updates");

        jtx::Env env(*this, jtx::testableAmendments() - featureSmartEscrow);
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
        auto feeTx1 = createFeeTx(ledger->rules(), ledger->seq(), fields1);

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(isTesSuccess(applyFeeAndTestResult(env, accum, feeTx1)));
            accum.apply(*ledger);
        }

        BEAST_EXPECT(verifyFeeObject(ledger, ledger->rules(), fields1));

        // Apply second fee transaction with different values
        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        FeeSettingsFields const fields2{
            .baseFeeDrops = XRPAmount{20},
            .reserveBaseDrops = XRPAmount{300000},
            .reserveIncrementDrops = XRPAmount{75000}};
        auto feeTx2 = createFeeTx(ledger->rules(), ledger->seq(), fields2);

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(isTesSuccess(applyFeeAndTestResult(env, accum, feeTx2)));
            accum.apply(*ledger);
        }

        // Verify second update overwrote the first
        BEAST_EXPECT(verifyFeeObject(ledger, ledger->rules(), fields2));
    }

    void
    testWrongLedgerSequence()
    {
        testcase("Wrong Ledger Sequence");

        jtx::Env env(*this, jtx::testableAmendments() - featureSmartEscrow);
        auto ledger = std::make_shared<Ledger>(
            kCreateGenesis,
            Rules{env.app().config().features},
            env.app().config().fees.toFees(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        // Test transaction with wrong ledger sequence
        auto feeTx = createFeeTx(
            ledger->rules(),
            ledger->seq() + 5,  // Wrong sequence (should be ledger->seq())
            {.baseFeeDrops = XRPAmount{10},
             .reserveBaseDrops = XRPAmount{200000},
             .reserveIncrementDrops = XRPAmount{50000}});

        OpenView accum(ledger.get());

        // The transaction should still succeed as long as other fields are
        // valid
        // The ledger sequence field is only used for informational purposes
        BEAST_EXPECT(isTesSuccess(applyFeeAndTestResult(env, accum, feeTx)));
    }

    void
    testPartialFieldUpdates()
    {
        testcase("Partial Field Updates");

        jtx::Env env(*this, jtx::testableAmendments() - featureSmartEscrow);
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
        auto feeTx1 = createFeeTx(ledger->rules(), ledger->seq(), fields1);

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(isTesSuccess(applyFeeAndTestResult(env, accum, feeTx1)));
            accum.apply(*ledger);
        }

        BEAST_EXPECT(verifyFeeObject(ledger, ledger->rules(), fields1));

        ledger = std::make_shared<Ledger>(*ledger, env.app().getTimeKeeper().closeTime());

        // Apply partial update (only some fields)
        FeeSettingsFields const fields2{
            .baseFeeDrops = XRPAmount{20}, .reserveBaseDrops = XRPAmount{200000}};
        auto feeTx2 = createFeeTx(ledger->rules(), ledger->seq(), fields2);

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(isTesSuccess(applyFeeAndTestResult(env, accum, feeTx2)));
            accum.apply(*ledger);
        }

        // Verify the partial update worked
        BEAST_EXPECT(verifyFeeObject(ledger, ledger->rules(), fields2));
    }

    void
    testSingleInvalidTransaction()
    {
        testcase("Single Invalid Transaction");

        jtx::Env env(*this, jtx::testableAmendments() - featureSmartEscrow);
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
        BEAST_EXPECT(!isTesSuccess(applyFeeAndTestResult(env, accum, invalidTx)));
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

        // Test with XRPFees enabled
        {
            Env env(*this, testableAmendments() - featureSmartEscrow);
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

        // Test with XRPFees disabled (legacy format)
        {
            Env env(*this, testableAmendments() - featureXRPFees - featureSmartEscrow);
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

            auto const& currentFees = ledger->fees();

            feeVote->doValidation(currentFees, ledger->rules(), *val);

            // In legacy mode, should vote using legacy fields
            BEAST_EXPECT(val->isFieldPresent(sfBaseFee));
            BEAST_EXPECT(val->getFieldU64(sfBaseFee) == setup.referenceFee);
        }
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

        Env env(*this, testableAmendments() - featureSmartEscrow);

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
    testDoVotingSmartEscrow()
    {
        testcase("doVoting with Smart Escrow");

        using namespace jtx;

        Env env(*this, testableAmendments() | featureXRPFees | featureSmartEscrow);

        // establish what the current fees are
        BEAST_EXPECT(env.current()->fees().base == XRPAmount{UNIT_TEST_REFERENCE_FEE});
        BEAST_EXPECT(env.current()->fees().reserve == XRPAmount{200'000'000});
        BEAST_EXPECT(env.current()->fees().increment == XRPAmount{50'000'000});
        BEAST_EXPECT(env.current()->fees().gasLimit == 0);
        BEAST_EXPECT(env.current()->fees().bytecodeSizeLimit == 0);
        BEAST_EXPECT(env.current()->fees().gasPrice == 0);

        auto const createFeeTxFromVoting =
            [&](FeeSetup const& setup) -> std::pair<STTx, std::shared_ptr<Ledger>> {
            auto feeVote = makeFeeVote(setup, env.app().getJournal("FeeVote"));
            auto ledger = std::make_shared<Ledger>(
                kCreateGenesis,
                Rules{env.app().config().features},
                env.app().config().fees.toFees(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // doVoting requires a flag ledger (every 256th ledger)
            // We need to create a ledger at sequence 256 to make it a flag
            // ledger
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
                    env.app().getTimeKeeper().now(),
                    pub,
                    sec,
                    calcNodeID(pub),
                    [&](STValidation& v) {
                        v.setFieldU32(sfLedgerSequence, ledger->seq());
                        // Vote for different fees than current
                        v.setFieldAmount(sfBaseFeeDrops, XRPAmount{setup.referenceFee});
                        v.setFieldAmount(sfReserveBaseDrops, XRPAmount{setup.accountReserve});
                        v.setFieldAmount(sfReserveIncrementDrops, XRPAmount{setup.ownerReserve});
                        v.setFieldU32(sfGasLimit, setup.gasLimit);
                        v.setFieldU32(sfBytecodeSizeLimit, setup.bytecodeSizeLimit);
                        v.setFieldU32(sfGasPrice, setup.gasPrice);
                    });
                if (i % 2)
                    val->setTrusted();
                validations.push_back(val);
            }

            auto txSet =
                std::make_shared<SHAMap>(SHAMapType::TRANSACTION, env.app().getNodeFamily());

            // This should not throw since we have a flag ledger
            feeVote->doVoting(ledger, validations, txSet);

            auto const txs = getTxs(txSet);
            BEAST_EXPECT(txs.size() == 1);
            return {txs[0], ledger};
        };

        auto checkFeeTx = [&](FeeSetup const& setup,
                              STTx const& feeTx,
                              std::shared_ptr<Ledger> const& ledger,
                              std::source_location const loc = std::source_location::current()) {
            auto const line = " (" + std::to_string(loc.line()) + ")";
            BEAST_EXPECTS(feeTx.getTxnType() == ttFEE, line);

            BEAST_EXPECTS(feeTx.getAccountID(sfAccount) == AccountID(), line);
            BEAST_EXPECTS(feeTx.getFieldU32(sfLedgerSequence) == ledger->seq() + 1, line);

            BEAST_EXPECTS(feeTx.isFieldPresent(sfBaseFeeDrops), line);
            BEAST_EXPECTS(feeTx.isFieldPresent(sfReserveBaseDrops), line);
            BEAST_EXPECTS(feeTx.isFieldPresent(sfReserveIncrementDrops), line);

            // The legacy fields should NOT be present
            BEAST_EXPECTS(!feeTx.isFieldPresent(sfBaseFee), line);
            BEAST_EXPECTS(!feeTx.isFieldPresent(sfReserveBase), line);
            BEAST_EXPECTS(!feeTx.isFieldPresent(sfReserveIncrement), line);
            BEAST_EXPECTS(!feeTx.isFieldPresent(sfReferenceFeeUnits), line);

            // Check the values
            BEAST_EXPECTS(
                feeTx.getFieldAmount(sfBaseFeeDrops) == XRPAmount{setup.referenceFee}, line);
            BEAST_EXPECTS(
                feeTx.getFieldAmount(sfReserveBaseDrops) == XRPAmount{setup.accountReserve}, line);
            BEAST_EXPECTS(
                feeTx.getFieldAmount(sfReserveIncrementDrops) == XRPAmount{setup.ownerReserve},
                line);
            BEAST_EXPECTS(feeTx.getFieldU32(sfGasLimit) == setup.gasLimit, line);
            BEAST_EXPECTS(feeTx.getFieldU32(sfBytecodeSizeLimit) == setup.bytecodeSizeLimit, line);
            BEAST_EXPECTS(feeTx.getFieldU32(sfGasPrice) == setup.gasPrice, line);
        };

        {
            FeeSetup setup;
            setup.referenceFee = 42;
            setup.accountReserve = 1234567;
            setup.ownerReserve = 7654321;
            setup.gasLimit = 100;
            setup.bytecodeSizeLimit = 200;
            setup.gasPrice = 300;
            auto const [feeTx, ledger] = createFeeTxFromVoting(setup);

            checkFeeTx(setup, feeTx, ledger);
        }

        {
            FeeSetup setup;
            setup.referenceFee = 42;
            setup.accountReserve = 1234567;
            setup.ownerReserve = 7654321;
            setup.gasLimit = 0;
            setup.bytecodeSizeLimit = 0;
            setup.gasPrice = 300;
            auto const [feeTx, ledger] = createFeeTxFromVoting(setup);

            checkFeeTx(setup, feeTx, ledger);
        }

        {
            FeeSetup setup;
            setup.referenceFee = 42;
            setup.accountReserve = 1234567;
            setup.ownerReserve = 7654321;
            setup.gasLimit = kMaxGasLimit + 1;
            setup.bytecodeSizeLimit = kMaxBytecodeSizeLimit + 1;
            setup.gasPrice = 300;
            auto const [feeTx, ledger] = createFeeTxFromVoting(setup);

            setup.gasLimit = ledger->fees().gasLimit;
            setup.bytecodeSizeLimit = ledger->fees().bytecodeSizeLimit;
            checkFeeTx(setup, feeTx, ledger);
        }
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
        testDoVotingSmartEscrow();
    }
};

BEAST_DEFINE_TESTSUITE(FeeVote, app, xrpl);

}  // namespace xrpl::test
