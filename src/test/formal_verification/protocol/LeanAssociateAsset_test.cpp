#include <test/formal_verification/ffi/protocol/NumberFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/LoanBrokerFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/LoanFFI.h>
#include <test/formal_verification/ffi/protocol/ledger_entries/VaultFFI.h>
#include <test/formal_verification/ledger/LedgerDataHelpers.h>
#include <test/formal_verification/ledger/LedgerSuite.h>

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STTakesAsset.h>

#include <exception>
#include <limits>
#include <optional>
#include <sstream>
#include <type_traits>

namespace xrpl::test {

using namespace formal_verification;

class LeanAssociateAsset_test : public LedgerSuite
{
    Asset const xrp{xrpIssue()};
    Asset const iou = iouAsset();
    Asset const mpt = mptAsset();

    static ledger_entries::Vault
    makeVault(
        Asset const& asset,
        std::optional<Number> const& total,
        std::optional<Number> const& available,
        std::optional<Number> const& maximum,
        std::optional<Number> const& loss)
    {
        AccountID const owner = fillId<AccountID>(0x73);
        ledger_entries::VaultBuilder b(
            fillId<uint256>(0x75),
            66,
            5,
            3,
            owner,
            fillId<AccountID>(0x72),
            asset,
            makeMptID(1, owner),
            1);
        if (total)
            b.setAssetsTotal(*total);
        if (available)
            b.setAssetsAvailable(*available);
        if (maximum)
            b.setAssetsMaximum(*maximum);
        if (loss)
            b.setLossUnrealized(*loss);
        return b.build(keylet::vault(owner, 5).key);
    }

    static ledger_entries::LoanBroker
    makeBroker(
        std::optional<Number> const& debtTotal,
        std::optional<Number> const& debtMaximum,
        std::optional<Number> const& coverAvailable)
    {
        AccountID const owner = fillId<AccountID>(0x90);
        ledger_entries::LoanBrokerBuilder b(
            fillId<uint256>(0x93),
            88,
            5,
            3,
            4,
            fillId<uint256>(0x92),
            fillId<AccountID>(0x91),
            owner,
            9);
        if (debtTotal)
            b.setDebtTotal(*debtTotal);
        if (debtMaximum)
            b.setDebtMaximum(*debtMaximum);
        if (coverAvailable)
            b.setCoverAvailable(*coverAvailable);
        return b.build(keylet::loanbroker(owner, 5).key);
    }

    static ledger_entries::Loan
    makeLoan(
        Number const& periodicPayment,
        std::optional<Number> const& originationFee,
        std::optional<Number> const& principal,
        std::optional<Number> const& totalValue,
        std::optional<Number> const& managementFee)
    {
        uint256 const brokerID = fillId<uint256>(0xA0);
        ledger_entries::LoanBuilder b(
            fillId<uint256>(0xA2),
            88,
            3,
            4,
            brokerID,
            7,
            fillId<AccountID>(0xA1),
            100,
            200,
            periodicPayment);
        if (originationFee)
            b.setLoanOriginationFee(*originationFee);
        if (principal)
            b.setPrincipalOutstanding(*principal);
        if (totalValue)
            b.setTotalValueOutstanding(*totalValue);
        if (managementFee)
            b.setManagementFeeOutstanding(*managementFee);
        return b.build(keylet::loan(brokerID, 7).key);
    }

    template <class FFIBuilder, class Entry>
    void
    runAssociateAsset(
        FFIBuilder builder,
        Entry const& input,
        std::optional<std::type_identity_t<Entry>> const& expected,
        Asset const& asset,
        Number::RoundingMode mode,
        char const* label)
    {
        beginCase(label);

        SLE cppSle(*input.getSle());
        bool cppThrew = false;
        try
        {
            NumberRoundModeGuard const g(mode);
            associateAsset(cppSle, asset);
        }
        catch (std::exception const&)
        {
            cppThrew = true;
        }

        auto const lean =
            builder.fromCpp(input).build(input.getKey()).associateAsset(asset, toLeanMode(mode));

        bool const shouldSucceed = expected.has_value();
        bool const cppOk = BEAST_EXPECTS(cppThrew != shouldSucceed, label);
        bool const leanOk = BEAST_EXPECTS(
            lean.value.has_value() == shouldSucceed,
            lean.error.empty() ? std::string(label) : lean.error);
        if (!shouldSucceed || !cppOk || !leanOk)
            return;
        expectSameSle(*expected->getSle(), cppSle, label, "cpp");
        expectSameSle(*expected->getSle(), *lean.value->toCpp().getSle(), label, "lean");
    }

    // Vault carries {2.5, 3.5, -0.5, 2.500000000000001}: half-even gives
    // 2 / 4 / removed / 3
    void
    testAssociateAssetToNearest()
    {
        auto const in = [&](Asset const& a) {
            return makeVault(
                a,
                Number{25, -1},
                Number{35, -1},
                Number{-5, -1},
                Number{2'500'000'000'000'001, -15});
        };
        auto const rounded = [&](Asset const& a) {
            return makeVault(a, Number{2}, Number{4}, std::nullopt, Number{3});
        };

        runAssociateAsset(
            VaultFFIBuilder(),
            in(xrp),
            rounded(xrp),
            xrp,
            Number::RoundingMode::ToNearest,
            "associateAsset.nearest_xrp");
        runAssociateAsset(
            VaultFFIBuilder(),
            in(iou),
            in(iou),
            iou,
            Number::RoundingMode::ToNearest,
            "associateAsset.nearest_iou");
        runAssociateAsset(
            VaultFFIBuilder(),
            in(mpt),
            rounded(mpt),
            mpt,
            Number::RoundingMode::ToNearest,
            "associateAsset.nearest_mpt");
    }

    // LoanBroker carries {0.999999999999999, -1.999999999999999, 999999999999999.5}:
    // magnitude down gives removed / -1 / 999999999999999
    void
    testAssociateAssetDownward()
    {
        auto const in = [&] {
            return makeBroker(
                Number{999'999'999'999'999, -15},
                Number{-1'999'999'999'999'999, -15},
                Number{9'999'999'999'999'995, -1});
        };
        auto const rounded = [&] {
            return makeBroker(std::nullopt, Number{-1}, Number{999'999'999'999'999});
        };

        runAssociateAsset(
            LoanBrokerFFIBuilder(),
            in(),
            rounded(),
            xrp,
            Number::RoundingMode::Downward,
            "associateAsset.downward_xrp");
        runAssociateAsset(
            LoanBrokerFFIBuilder(),
            in(),
            in(),
            iou,
            Number::RoundingMode::Downward,
            "associateAsset.downward_iou");
        runAssociateAsset(
            LoanBrokerFFIBuilder(),
            in(),
            rounded(),
            mpt,
            Number::RoundingMode::Downward,
            "associateAsset.downward_mpt");
    }

    // Loan trio carries {1.000000000000001, -1e-15, 999999999999999.5}: any epsilon
    // rounds away from zero, giving 2 / -1 / 1e15 (near-zero is NOT removed); the
    // non-NeedsAsset periodicPayment (2.5) and originationFee (1.5) never round
    void
    testAssociateAssetUpward()
    {
        Number const periodic{25, -1};
        Number const origFee{15, -1};
        auto const in = [&] {
            return makeLoan(
                periodic,
                origFee,
                Number{1'000'000'000'000'001, -15},
                Number{-1, -15},
                Number{9'999'999'999'999'995, -1});
        };
        auto const rounded = [&] {
            return makeLoan(
                periodic, origFee, Number{2}, Number{-1}, Number{1'000'000'000'000'000});
        };

        runAssociateAsset(
            LoanFFIBuilder(),
            in(),
            rounded(),
            xrp,
            Number::RoundingMode::Upward,
            "associateAsset.upward_xrp");
        runAssociateAsset(
            LoanFFIBuilder(),
            in(),
            in(),
            iou,
            Number::RoundingMode::Upward,
            "associateAsset.upward_iou");
        runAssociateAsset(
            LoanFFIBuilder(),
            in(),
            rounded(),
            mpt,
            Number::RoundingMode::Upward,
            "associateAsset.upward_mpt");
    }

    // Vault carries {1.999999999999999, 2.5, -2.999999999999999, 0.9}: towards zero
    // gives 1 / 2 / -2 / removed
    void
    testAssociateAssetTowardsZero()
    {
        auto const in = [&](Asset const& a) {
            return makeVault(
                a,
                Number{1'999'999'999'999'999, -15},
                Number{25, -1},
                Number{-2'999'999'999'999'999, -15},
                Number{9, -1});
        };
        auto const rounded = [&](Asset const& a) {
            return makeVault(a, Number{1}, Number{2}, Number{-2}, std::nullopt);
        };

        runAssociateAsset(
            VaultFFIBuilder(),
            in(xrp),
            rounded(xrp),
            xrp,
            Number::RoundingMode::TowardsZero,
            "associateAsset.towards_zero_xrp");
        runAssociateAsset(
            VaultFFIBuilder(),
            in(iou),
            in(iou),
            iou,
            Number::RoundingMode::TowardsZero,
            "associateAsset.towards_zero_iou");
        runAssociateAsset(
            VaultFFIBuilder(),
            in(mpt),
            rounded(mpt),
            mpt,
            Number::RoundingMode::TowardsZero,
            "associateAsset.towards_zero_mpt");
    }

    void
    testAssociateAssetExtremes()
    {
        // IOU only clamps exponents: max mantissa at the max exponent (80) and a
        // negative mid-range value stay exact, 1e-96 underflows to removed
        runAssociateAsset(
            LoanBrokerFFIBuilder(),
            makeBroker(
                Number{9'999'999'999'999'999, 80},
                Number{1, -96},
                Number{-5'555'555'555'555'555, -30}),
            makeBroker(
                Number{9'999'999'999'999'999, 80},
                std::nullopt,
                Number{-5'555'555'555'555'555, -30}),
            iou,
            Number::RoundingMode::ToNearest,
            "associateAsset.iou_exponent_clamp");

        // near-max drops with a full 16-digit mantissa (int64-wrap regression)
        {
            auto const entry = makeVault(
                xrp, Number{9'999'999'999'999'999, 1}, std::nullopt, std::nullopt, std::nullopt);
            runAssociateAsset(
                VaultFFIBuilder(),
                entry,
                entry,
                xrp,
                Number::RoundingMode::ToNearest,
                "associateAsset.xrp_large_mantissa");
        }

        // mantissa at the int64 carrier limit (kMaxRep): MPT holds +-(2^63 - 1)
        // exactly, IOU rounds the 19-digit mantissa to 16 digits, XRP overflows
        constexpr int64_t int64max = std::numeric_limits<int64_t>::max();
        {
            auto const entry = makeBroker(Number{int64max}, Number{-int64max}, std::nullopt);
            runAssociateAsset(
                LoanBrokerFFIBuilder(),
                entry,
                entry,
                mpt,
                Number::RoundingMode::ToNearest,
                "associateAsset.mpt_int64_mantissa");
        }
        runAssociateAsset(
            VaultFFIBuilder(),
            makeVault(iou, Number{int64max}, Number{-int64max}, std::nullopt, std::nullopt),
            makeVault(
                iou,
                Number{9'223'372'036'854'776, 3},
                Number{-9'223'372'036'854'776, 3},
                std::nullopt,
                std::nullopt),
            iou,
            Number::RoundingMode::ToNearest,
            "associateAsset.iou_int64_mantissa");
        runAssociateAsset(
            VaultFFIBuilder(),
            makeVault(xrp, Number{int64max}, std::nullopt, std::nullopt, std::nullopt),
            std::nullopt,
            xrp,
            Number::RoundingMode::ToNearest,
            "associateAsset.xrp_int64_mantissa");

        // each asset's maximum exceeded -> both sides fail
        runAssociateAsset(
            VaultFFIBuilder(),
            makeVault(xrp, Number{1, 30}, std::nullopt, std::nullopt, std::nullopt),
            std::nullopt,
            xrp,
            Number::RoundingMode::ToNearest,
            "associateAsset.xrp_overflow");
        runAssociateAsset(
            LoanBrokerFFIBuilder(),
            makeBroker(Number{1, 19}, std::nullopt, std::nullopt),
            std::nullopt,
            mpt,
            Number::RoundingMode::ToNearest,
            "associateAsset.mpt_overflow");
        runAssociateAsset(
            LoanFFIBuilder(),
            makeLoan(
                Number{25, -1},
                std::nullopt,
                Number{9'999'999'999'999'999, 100},
                std::nullopt,
                std::nullopt),
            std::nullopt,
            iou,
            Number::RoundingMode::ToNearest,
            "associateAsset.iou_overflow");
    }

    void
    runTests() override
    {
        testAssociateAssetToNearest();
        testAssociateAssetDownward();
        testAssociateAssetUpward();
        testAssociateAssetTowardsZero();
        testAssociateAssetExtremes();
    }
};

BEAST_DEFINE_TESTSUITE(LeanAssociateAsset, formal_verification, xrpl);

}  // namespace xrpl::test
