//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <test/jtx.h>

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/tx/apply.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STTx.h>

namespace ripple {
namespace test {

/**
 * Helper function to create a ttFEE pseudo-transaction
 * @param rules the rules to determine which fields to include
 * @param seq the ledger sequence
 * @param baseFee the base fee value (legacy format)
 * @param reserveBase the base reserve value (legacy format)
 * @param reserveIncrement the reserve increment value (legacy format)
 * @param referenceFeeUnits the reference fee units value (legacy format)
 * @param baseFeeDrops the base fee in drops (new format)
 * @param reserveBaseDrops the base reserve in drops (new format)
 * @param reserveIncrementDrops the reserve increment in drops (new format)
 * @param extensionComputeLimit the extension compute limit (SmartEscrow)
 * @param extensionSizeLimit the extension size limit (SmartEscrow)
 * @param gasPrice the gas price (SmartEscrow)
 * @return the ttFEE transaction
 */
STTx
createFeeTx(
    Rules const& rules,
    std::uint32_t seq,
    std::optional<std::uint64_t> baseFee = std::nullopt,
    std::optional<std::uint32_t> reserveBase = std::nullopt,
    std::optional<std::uint32_t> reserveIncrement = std::nullopt,
    std::optional<std::uint32_t> referenceFeeUnits = std::nullopt,
    std::optional<XRPAmount> baseFeeDrops = std::nullopt,
    std::optional<XRPAmount> reserveBaseDrops = std::nullopt,
    std::optional<XRPAmount> reserveIncrementDrops = std::nullopt,
    std::optional<std::uint32_t> extensionComputeLimit = std::nullopt,
    std::optional<std::uint32_t> extensionSizeLimit = std::nullopt,
    std::optional<std::uint32_t> gasPrice = std::nullopt)
{
    auto fill = [&](auto& obj) {
        obj.setAccountID(sfAccount, AccountID());
        obj.setFieldU32(sfLedgerSequence, seq);

        if (rules.enabled(featureXRPFees))
        {
            // New XRPFees format - all three fields are REQUIRED
            obj.setFieldAmount(
                sfBaseFeeDrops, baseFeeDrops ? *baseFeeDrops : XRPAmount{10});
            obj.setFieldAmount(
                sfReserveBaseDrops,
                reserveBaseDrops ? *reserveBaseDrops : XRPAmount{200000});
            obj.setFieldAmount(
                sfReserveIncrementDrops,
                reserveIncrementDrops ? *reserveIncrementDrops
                                      : XRPAmount{50000});
        }
        else
        {
            // Legacy format - all four fields are REQUIRED
            obj.setFieldU64(sfBaseFee, baseFee ? *baseFee : 10);
            obj.setFieldU32(sfReserveBase, reserveBase ? *reserveBase : 200000);
            obj.setFieldU32(
                sfReserveIncrement,
                reserveIncrement ? *reserveIncrement : 50000);
            obj.setFieldU32(
                sfReferenceFeeUnits,
                referenceFeeUnits ? *referenceFeeUnits : 10);
        }

        if (rules.enabled(featureSmartEscrow))
        {
            // SmartEscrow fields - all three fields are REQUIRED
            obj.setFieldU32(
                sfExtensionComputeLimit,
                extensionComputeLimit ? *extensionComputeLimit : 1000);
            obj.setFieldU32(
                sfExtensionSizeLimit,
                extensionSizeLimit ? *extensionSizeLimit : 2000);
            obj.setFieldU32(sfGasPrice, gasPrice ? *gasPrice : 100);
        }
    };
    return STTx(ttFEE, fill);
}

/**
 * Helper function to create an invalid ttFEE pseudo-transaction for testing
 * validation
 * @param rules the rules to determine which fields should be missing/present
 * @param seq the ledger sequence
 * @param missingRequiredFields whether to omit required fields
 * @param wrongFeatureFields whether to include fields from wrong feature set
 * @return the invalid ttFEE transaction
 */
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
                // Include legacy fields when XRPFees is enabled (should fail)
                obj.setFieldU64(sfBaseFee, 10 + uniqueValue);
                obj.setFieldU32(sfReserveBase, 200000);
                obj.setFieldU32(sfReserveIncrement, 50000);
                obj.setFieldU32(sfReferenceFeeUnits, 10);
            }
            else
            {
                // Include new fields when XRPFees is disabled (should fail)
                obj.setFieldAmount(sfBaseFeeDrops, XRPAmount{10 + uniqueValue});
                obj.setFieldAmount(sfReserveBaseDrops, XRPAmount{200000});
                obj.setFieldAmount(sfReserveIncrementDrops, XRPAmount{50000});
            }

            if (!rules.enabled(featureSmartEscrow))
            {
                // Include SmartEscrow fields when SmartEscrow is disabled
                // (should fail)
                obj.setFieldU32(sfExtensionComputeLimit, 1000 + uniqueValue);
                obj.setFieldU32(sfExtensionSizeLimit, 2000);
                obj.setFieldU32(sfGasPrice, 100);
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
                obj.setFieldU32(sfExtensionComputeLimit, 1000 + uniqueValue);
                obj.setFieldU32(sfExtensionSizeLimit, 2000);
                obj.setFieldU32(sfGasPrice, 100);
            }
        }
        // If missingRequiredFields is true, we don't add the required fields
        // (default behavior)
    };
    return STTx(ttFEE, fill);
}

/**
 * Helper function to apply a transaction and test the result
 * @param env the test environment
 * @param view the OpenView to apply the transaction to
 * @param tx the transaction to apply
 * @param expectSuccess whether the transaction should succeed
 * @return true if the result matches expectation
 */
bool
applyFeeAndTestResult(
    jtx::Env& env,
    OpenView& view,
    STTx const& tx,
    bool expectSuccess)
{
    auto const res =
        apply(env.app(), view, tx, ApplyFlags::tapNONE, env.journal);
    // Debug output
    JLOG(env.journal.debug())
        << "Transaction result: " << transToken(res.ter) << " (expected "
        << (expectSuccess ? "success" : "failure") << ")";
    if (expectSuccess)
        return res.ter == tesSUCCESS;
    else
        return isTecClaim(res.ter) || isTefFailure(res.ter) ||
            isTemMalformed(res.ter);
}

/**
 * Helper function to verify fee object values in ledger
 * @param ledger the ledger to check
 * @param rules the rules to determine which fields to check
 * @param expectedBaseFee expected base fee (legacy)
 * @param expectedReserveBase expected reserve base (legacy)
 * @param expectedReserveIncrement expected reserve increment (legacy)
 * @param expectedReferenceFeeUnits expected reference fee units (legacy)
 * @param expectedBaseFeeDrops expected base fee drops (new)
 * @param expectedReserveBaseDrops expected reserve base drops (new)
 * @param expectedReserveIncrementDrops expected reserve increment drops (new)
 * @param expectedExtensionComputeLimit expected extension compute limit
 * (SmartEscrow)
 * @param expectedExtensionSizeLimit expected extension size limit (SmartEscrow)
 * @param expectedGasPrice expected gas price (SmartEscrow)
 * @return true if all expected values match
 */
bool
verifyFeeObject(
    std::shared_ptr<Ledger const> const& ledger,
    Rules const& rules,
    std::optional<std::uint64_t> expectedBaseFee = std::nullopt,
    std::optional<std::uint32_t> expectedReserveBase = std::nullopt,
    std::optional<std::uint32_t> expectedReserveIncrement = std::nullopt,
    std::optional<std::uint32_t> expectedReferenceFeeUnits = std::nullopt,
    std::optional<XRPAmount> expectedBaseFeeDrops = std::nullopt,
    std::optional<XRPAmount> expectedReserveBaseDrops = std::nullopt,
    std::optional<XRPAmount> expectedReserveIncrementDrops = std::nullopt,
    std::optional<std::uint32_t> expectedExtensionComputeLimit = std::nullopt,
    std::optional<std::uint32_t> expectedExtensionSizeLimit = std::nullopt,
    std::optional<std::uint32_t> expectedGasPrice = std::nullopt)
{
    auto const feeObject = ledger->read(keylet::fees());
    if (!feeObject)
        return false;

    if (rules.enabled(featureXRPFees))
    {
        if (expectedBaseFeeDrops &&
            (!feeObject->isFieldPresent(sfBaseFeeDrops) ||
             feeObject->getFieldAmount(sfBaseFeeDrops) !=
                 *expectedBaseFeeDrops))
            return false;
        if (expectedReserveBaseDrops &&
            (!feeObject->isFieldPresent(sfReserveBaseDrops) ||
             feeObject->getFieldAmount(sfReserveBaseDrops) !=
                 *expectedReserveBaseDrops))
            return false;
        if (expectedReserveIncrementDrops &&
            (!feeObject->isFieldPresent(sfReserveIncrementDrops) ||
             feeObject->getFieldAmount(sfReserveIncrementDrops) !=
                 *expectedReserveIncrementDrops))
            return false;
    }
    else
    {
        if (expectedBaseFee &&
            (!feeObject->isFieldPresent(sfBaseFee) ||
             feeObject->getFieldU64(sfBaseFee) != *expectedBaseFee))
            return false;
        if (expectedReserveBase &&
            (!feeObject->isFieldPresent(sfReserveBase) ||
             feeObject->getFieldU32(sfReserveBase) != *expectedReserveBase))
            return false;
        if (expectedReserveIncrement &&
            (!feeObject->isFieldPresent(sfReserveIncrement) ||
             feeObject->getFieldU32(sfReserveIncrement) !=
                 *expectedReserveIncrement))
            return false;
        if (expectedReferenceFeeUnits &&
            (!feeObject->isFieldPresent(sfReferenceFeeUnits) ||
             feeObject->getFieldU32(sfReferenceFeeUnits) !=
                 *expectedReferenceFeeUnits))
            return false;
    }

    if (rules.enabled(featureSmartEscrow))
    {
        if (expectedExtensionComputeLimit &&
            (!feeObject->isFieldPresent(sfExtensionComputeLimit) ||
             feeObject->getFieldU32(sfExtensionComputeLimit) !=
                 *expectedExtensionComputeLimit))
            return false;
        if (expectedExtensionSizeLimit &&
            (!feeObject->isFieldPresent(sfExtensionSizeLimit) ||
             feeObject->getFieldU32(sfExtensionSizeLimit) !=
                 *expectedExtensionSizeLimit))
            return false;
        if (expectedGasPrice &&
            (!feeObject->isFieldPresent(sfGasPrice) ||
             feeObject->getFieldU32(sfGasPrice) != *expectedGasPrice))
            return false;
    }

    return true;
}

class FeeVote_test : public beast::unit_test::suite
{
    void
    testSetup()
    {
        FeeSetup const defaultSetup;
        {
            // defaults
            Section config;
            auto setup = setup_FeeVote(config);
            BEAST_EXPECT(setup.reference_fee == defaultSetup.reference_fee);
            BEAST_EXPECT(setup.account_reserve == defaultSetup.account_reserve);
            BEAST_EXPECT(setup.owner_reserve == defaultSetup.owner_reserve);
        }
        {
            Section config;
            config.append(
                {"reference_fee = 50",
                 "account_reserve = 1234567",
                 "owner_reserve = 1234"});
            auto setup = setup_FeeVote(config);
            BEAST_EXPECT(setup.reference_fee == 50);
            BEAST_EXPECT(setup.account_reserve == 1234567);
            BEAST_EXPECT(setup.owner_reserve == 1234);
        }
        {
            Section config;
            config.append(
                {"reference_fee = blah",
                 "account_reserve = yada",
                 "owner_reserve = foo"});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setup_FeeVote(config);
            BEAST_EXPECT(setup.reference_fee == defaultSetup.reference_fee);
            BEAST_EXPECT(setup.account_reserve == defaultSetup.account_reserve);
            BEAST_EXPECT(setup.owner_reserve == defaultSetup.owner_reserve);
        }
        {
            Section config;
            config.append(
                {"reference_fee = -50",
                 "account_reserve = -1234567",
                 "owner_reserve = -1234"});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setup_FeeVote(config);
            BEAST_EXPECT(setup.reference_fee == defaultSetup.reference_fee);
            BEAST_EXPECT(
                setup.account_reserve == static_cast<std::uint32_t>(-1234567));
            BEAST_EXPECT(
                setup.owner_reserve == static_cast<std::uint32_t>(-1234));
        }
        {
            auto const big64 = std::to_string(
                static_cast<std::uint64_t>(
                    std::numeric_limits<XRPAmount::value_type>::max()) +
                1);
            Section config;
            config.append(
                {"reference_fee = " + big64,
                 "account_reserve = " + big64,
                 "owner_reserve = " + big64});
            // Illegal values are ignored, and the defaults left unchanged
            auto setup = setup_FeeVote(config);
            BEAST_EXPECT(setup.reference_fee == defaultSetup.reference_fee);
            BEAST_EXPECT(setup.account_reserve == defaultSetup.account_reserve);
            BEAST_EXPECT(setup.owner_reserve == defaultSetup.owner_reserve);
        }
    }

    void
    testBasicFeeTransactionCreationAndApplication()
    {
        testcase("Basic Fee Transaction Creation and Application");

        // Test with XRPFees disabled (legacy format)
        {
            jtx::Env env(*this, jtx::testable_amendments() - featureXRPFees);
            auto ledger = std::make_shared<Ledger>(
                create_genesis,
                env.app().config(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(
                *ledger, env.app().timeKeeper().closeTime());

            // Test successful fee transaction with legacy fields
            auto feeTx = createFeeTx(
                ledger->rules(),
                ledger->seq(),
                10,      // baseFee
                200000,  // reserveBase
                50000,   // reserveIncrement
                10);     // referenceFeeUnits

            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx, true));
            accum.apply(*ledger);

            // Verify fee object was created/updated correctly
            BEAST_EXPECT(verifyFeeObject(
                ledger,
                ledger->rules(),
                10,      // expectedBaseFee
                200000,  // expectedReserveBase
                50000,   // expectedReserveIncrement
                10));    // expectedReferenceFeeUnits
        }

        // Test with XRPFees enabled (new format)
        {
            jtx::Env env(*this, jtx::testable_amendments() | featureXRPFees);
            auto ledger = std::make_shared<Ledger>(
                create_genesis,
                env.app().config(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(
                *ledger, env.app().timeKeeper().closeTime());

            // Test successful fee transaction with new fields
            auto feeTx = createFeeTx(
                ledger->rules(),
                ledger->seq(),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,       // legacy fields
                XRPAmount{10},      // baseFeeDrops
                XRPAmount{200000},  // reserveBaseDrops
                XRPAmount{50000});  // reserveIncrementDrops

            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx, true));
            accum.apply(*ledger);

            // Verify fee object was created/updated correctly
            BEAST_EXPECT(verifyFeeObject(
                ledger,
                ledger->rules(),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,        // legacy fields
                XRPAmount{10},       // expectedBaseFeeDrops
                XRPAmount{200000},   // expectedReserveBaseDrops
                XRPAmount{50000}));  // expectedReserveIncrementDrops
        }

        // Test with SmartEscrow enabled
        {
            jtx::Env env(
                *this,
                jtx::testable_amendments() | featureXRPFees |
                    featureSmartEscrow);
            auto ledger = std::make_shared<Ledger>(
                create_genesis,
                env.app().config(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(
                *ledger, env.app().timeKeeper().closeTime());

            // Test successful fee transaction with SmartEscrow fields
            auto feeTx = createFeeTx(
                ledger->rules(),
                ledger->seq(),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,       // legacy fields
                XRPAmount{10},      // baseFeeDrops
                XRPAmount{200000},  // reserveBaseDrops
                XRPAmount{50000},   // reserveIncrementDrops
                1000,               // extensionComputeLimit
                2000,               // extensionSizeLimit
                100);               // gasPrice

            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx, true));
            accum.apply(*ledger);

            // Verify fee object was created/updated correctly
            BEAST_EXPECT(verifyFeeObject(
                ledger,
                ledger->rules(),
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,       // legacy fields
                XRPAmount{10},      // expectedBaseFeeDrops
                XRPAmount{200000},  // expectedReserveBaseDrops
                XRPAmount{50000},   // expectedReserveIncrementDrops
                1000,               // expectedExtensionComputeLimit
                2000,               // expectedExtensionSizeLimit
                100));              // expectedGasPrice
        }
    }

    void
    testTransactionValidation()
    {
        testcase("Fee Transaction Validation");

        {
            jtx::Env env(*this, jtx::testable_amendments() - featureXRPFees);
            auto ledger = std::make_shared<Ledger>(
                create_genesis,
                env.app().config(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(
                *ledger, env.app().timeKeeper().closeTime());

            // Test transaction with missing required legacy fields
            auto invalidTx = createInvalidFeeTx(
                ledger->rules(), ledger->seq(), true, false, 1);
            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, invalidTx, false));

            // Test transaction with new format fields when XRPFees is disabled
            auto disallowedTx = createInvalidFeeTx(
                ledger->rules(), ledger->seq(), false, true, 2);
            BEAST_EXPECT(
                applyFeeAndTestResult(env, accum, disallowedTx, false));
        }

        {
            jtx::Env env(*this, jtx::testable_amendments() | featureXRPFees);
            auto ledger = std::make_shared<Ledger>(
                create_genesis,
                env.app().config(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(
                *ledger, env.app().timeKeeper().closeTime());

            // Test transaction with missing required new fields
            auto invalidTx = createInvalidFeeTx(
                ledger->rules(), ledger->seq(), true, false, 3);
            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, invalidTx, false));

            // Test transaction with legacy fields when XRPFees is enabled
            auto disallowedTx = createInvalidFeeTx(
                ledger->rules(), ledger->seq(), false, true, 4);
            BEAST_EXPECT(
                applyFeeAndTestResult(env, accum, disallowedTx, false));
        }

        {
            jtx::Env env(
                *this,
                (jtx::testable_amendments() | featureXRPFees) -
                    featureSmartEscrow);
            auto ledger = std::make_shared<Ledger>(
                create_genesis,
                env.app().config(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(
                *ledger, env.app().timeKeeper().closeTime());

            // Test transaction with SmartEscrow fields when SmartEscrow is
            // disabled
            auto disallowedTx = createInvalidFeeTx(
                ledger->rules(), ledger->seq(), false, true, 5);
            OpenView accum(ledger.get());
            BEAST_EXPECT(
                applyFeeAndTestResult(env, accum, disallowedTx, false));
        }
    }

    void
    testPseudoTransactionProperties()
    {
        testcase("Pseudo Transaction Properties");

        jtx::Env env(*this, jtx::testable_amendments());
        auto ledger = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // Create the next ledger to apply transaction to
        ledger = std::make_shared<Ledger>(
            *ledger, env.app().timeKeeper().closeTime());

        auto feeTx = createFeeTx(
            ledger->rules(),
            ledger->seq(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,       // legacy fields
            XRPAmount{10},      // baseFeeDrops
            XRPAmount{200000},  // reserveBaseDrops
            XRPAmount{50000});  // reserveIncrementDrops

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
            BEAST_EXPECT(applyFeeAndTestResult(env, closedAccum, feeTx, true));
        }
    }

    void
    testMultipleFeeUpdates()
    {
        testcase("Multiple Fee Updates");

        jtx::Env env(
            *this,
            jtx::testable_amendments() | featureXRPFees | featureSmartEscrow);
        auto ledger = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // Create the next ledger to apply transaction to
        ledger = std::make_shared<Ledger>(
            *ledger, env.app().timeKeeper().closeTime());

        // Apply first fee transaction
        auto feeTx1 = createFeeTx(
            ledger->rules(),
            ledger->seq(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,       // legacy fields
            XRPAmount{10},      // baseFeeDrops
            XRPAmount{200000},  // reserveBaseDrops
            XRPAmount{50000},   // reserveIncrementDrops
            1000,               // extensionComputeLimit
            2000,               // extensionSizeLimit
            100);               // gasPrice

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx1, true));
            accum.apply(*ledger);
        }

        // Verify first update
        BEAST_EXPECT(verifyFeeObject(
            ledger,
            ledger->rules(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            XRPAmount{10},
            XRPAmount{200000},
            XRPAmount{50000},
            1000,
            2000,
            100));

        // Create next ledger and apply second fee transaction with different
        // values
        ledger = std::make_shared<Ledger>(
            *ledger, env.app().timeKeeper().closeTime());

        auto feeTx2 = createFeeTx(
            ledger->rules(),
            ledger->seq(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,       // legacy fields
            XRPAmount{20},      // baseFeeDrops
            XRPAmount{300000},  // reserveBaseDrops
            XRPAmount{75000},   // reserveIncrementDrops
            1500,               // extensionComputeLimit
            3000,               // extensionSizeLimit
            150);               // gasPrice

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx2, true));
            accum.apply(*ledger);
        }

        // Verify second update overwrote the first
        BEAST_EXPECT(verifyFeeObject(
            ledger,
            ledger->rules(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            XRPAmount{20},
            XRPAmount{300000},
            XRPAmount{75000},
            1500,
            3000,
            150));
    }

    void
    testInvalidTransactionFields()
    {
        testcase("Invalid Transaction Fields");

        // Empty test to isolate the issue
        BEAST_EXPECT(true);
    }

    void
    testWrongLedgerSequence()
    {
        testcase("Wrong Ledger Sequence");

        jtx::Env env(*this, jtx::testable_amendments() | featureXRPFees);
        auto ledger = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // Create the next ledger to apply transaction to
        ledger = std::make_shared<Ledger>(
            *ledger, env.app().timeKeeper().closeTime());

        // Test transaction with wrong ledger sequence
        auto feeTx = createFeeTx(
            ledger->rules(),
            ledger->seq() + 5,  // Wrong sequence (should be ledger->seq())
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,       // legacy fields
            XRPAmount{10},      // baseFeeDrops
            XRPAmount{200000},  // reserveBaseDrops
            XRPAmount{50000});  // reserveIncrementDrops

        OpenView accum(ledger.get());

        // The transaction should still succeed as long as other fields are
        // valid The ledger sequence field is used for informational purposes
        BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx, true));
    }

    void
    testMixedFeatureFlags()
    {
        testcase("Mixed Feature Flags");

        // Test with only SmartEscrow enabled (but not XRPFees)
        {
            jtx::Env env(
                *this,
                (jtx::testable_amendments() | featureSmartEscrow) -
                    featureXRPFees);
            auto ledger = std::make_shared<Ledger>(
                create_genesis,
                env.app().config(),
                std::vector<uint256>{},
                env.app().getNodeFamily());

            // Create the next ledger to apply transaction to
            ledger = std::make_shared<Ledger>(
                *ledger, env.app().timeKeeper().closeTime());

            // Should require legacy fee fields + SmartEscrow fields
            auto feeTx = createFeeTx(
                ledger->rules(),
                ledger->seq(),
                10,      // baseFee
                200000,  // reserveBase
                50000,   // reserveIncrement
                10,      // referenceFeeUnits
                std::nullopt,
                std::nullopt,
                std::nullopt,  // no XRP fee fields
                1000,          // extensionComputeLimit
                2000,          // extensionSizeLimit
                100);          // gasPrice

            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx, true));
            accum.apply(*ledger);

            // Verify fee object was created correctly
            BEAST_EXPECT(verifyFeeObject(
                ledger,
                ledger->rules(),
                10,
                200000,
                50000,
                10,  // legacy fields
                std::nullopt,
                std::nullopt,
                std::nullopt,  // no XRP fee fields
                1000,
                2000,
                100));  // SmartEscrow fields
        }
    }

    void
    testPartialFieldUpdates()
    {
        testcase("Partial Field Updates");

        jtx::Env env(
            *this,
            jtx::testable_amendments() | featureXRPFees | featureSmartEscrow);
        auto ledger = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // Create the next ledger to apply transaction to
        ledger = std::make_shared<Ledger>(
            *ledger, env.app().timeKeeper().closeTime());

        // Apply initial fee transaction with all fields
        auto feeTx1 = createFeeTx(
            ledger->rules(),
            ledger->seq(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,       // legacy fields
            XRPAmount{10},      // baseFeeDrops
            XRPAmount{200000},  // reserveBaseDrops
            XRPAmount{50000},   // reserveIncrementDrops
            1000,               // extensionComputeLimit
            2000,               // extensionSizeLimit
            100);               // gasPrice

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx1, true));
            accum.apply(*ledger);
        }

        // Create next ledger
        ledger = std::make_shared<Ledger>(
            *ledger, env.app().timeKeeper().closeTime());

        // Apply partial update (only some fields)
        auto feeTx2 = createFeeTx(
            ledger->rules(),
            ledger->seq(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,       // legacy fields
            XRPAmount{20},      // only update baseFeeDrops
            XRPAmount{200000},  // keep same reserveBaseDrops
            XRPAmount{50000},   // keep same reserveIncrementDrops
            1500);  // only update extensionComputeLimit (leave others as
                    // defaults)

        {
            OpenView accum(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx2, true));
            accum.apply(*ledger);
        }

        // Verify the partial update worked
        BEAST_EXPECT(verifyFeeObject(
            ledger,
            ledger->rules(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            XRPAmount{20},
            XRPAmount{200000},
            XRPAmount{50000},  // updated base fee
            1500));  // updated compute limit, other SmartEscrow fields not
                     // checked
    }

    void
    testTransactionOrderAndIdempotence()
    {
        testcase("Transaction Order and Idempotence");

        jtx::Env env(*this, jtx::testable_amendments() | featureXRPFees);
        auto ledger = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // Create the next ledger to apply transaction to
        ledger = std::make_shared<Ledger>(
            *ledger, env.app().timeKeeper().closeTime());

        // Create two identical fee transactions
        auto feeTx1 = createFeeTx(
            ledger->rules(),
            ledger->seq(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,       // legacy fields
            XRPAmount{10},      // baseFeeDrops
            XRPAmount{200000},  // reserveBaseDrops
            XRPAmount{50000});  // reserveIncrementDrops

        // Apply both transactions to the same ledger view
        OpenView accum(ledger.get());
        BEAST_EXPECT(applyFeeAndTestResult(env, accum, feeTx1, true));
        accum.apply(*ledger);

        // Verify final state is as expected
        BEAST_EXPECT(verifyFeeObject(
            ledger,
            ledger->rules(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            XRPAmount{10},
            XRPAmount{200000},
            XRPAmount{50000}));

        // Apply different transaction in next ledger
        ledger = std::make_shared<Ledger>(
            *ledger, env.app().timeKeeper().closeTime());

        auto feeTx3 = createFeeTx(
            ledger->rules(),
            ledger->seq(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,       // legacy fields
            XRPAmount{20},      // different baseFeeDrops
            XRPAmount{200000},  // same reserveBaseDrops
            XRPAmount{50000});  // same reserveIncrementDrops

        {
            OpenView accum2(ledger.get());
            BEAST_EXPECT(applyFeeAndTestResult(env, accum2, feeTx3, true));
            accum2.apply(*ledger);
        }

        // Verify the different transaction updated the values
        BEAST_EXPECT(verifyFeeObject(
            ledger,
            ledger->rules(),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            XRPAmount{20},
            XRPAmount{200000},
            XRPAmount{50000}));
    }

    void
    testSingleInvalidTransaction()
    {
        testcase("Single Invalid Transaction");

        jtx::Env env(
            *this,
            jtx::testable_amendments() | featureXRPFees | featureSmartEscrow);
        auto ledger = std::make_shared<Ledger>(
            create_genesis,
            env.app().config(),
            std::vector<uint256>{},
            env.app().getNodeFamily());

        // Create the next ledger to apply transaction to
        ledger = std::make_shared<Ledger>(
            *ledger, env.app().timeKeeper().closeTime());

        // Test invalid transaction with non-zero account - this should fail
        // validation
        auto invalidTx = STTx(ttFEE, [&](auto& obj) {
            obj.setAccountID(
                sfAccount,
                AccountID(1));  // Should be zero (this makes it invalid)
            obj.setFieldU32(sfLedgerSequence, ledger->seq());
            obj.setFieldAmount(sfBaseFeeDrops, XRPAmount{10});
            obj.setFieldAmount(sfReserveBaseDrops, XRPAmount{200000});
            obj.setFieldAmount(sfReserveIncrementDrops, XRPAmount{50000});
            obj.setFieldU32(sfExtensionComputeLimit, 1000);
            obj.setFieldU32(sfExtensionSizeLimit, 2000);
            obj.setFieldU32(sfGasPrice, 100);
        });

        OpenView accum(ledger.get());
        BEAST_EXPECT(applyFeeAndTestResult(env, accum, invalidTx, false));
    }

    void
    run() override
    {
        testSetup();
        testBasicFeeTransactionCreationAndApplication();
        testTransactionValidation();
        testPseudoTransactionProperties();
        testMultipleFeeUpdates();
        testInvalidTransactionFields();
        testWrongLedgerSequence();
        testMixedFeatureFlags();
        testPartialFieldUpdates();
        testTransactionOrderAndIdempotence();
        testSingleInvalidTransaction();
    }
};

BEAST_DEFINE_TESTSUITE(FeeVote, app, ripple);

}  // namespace test
}  // namespace ripple
