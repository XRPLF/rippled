#include <test/jtx.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>

#include <string>
#include <vector>

namespace xrpl {
namespace test {

class VaultHelpers_test : public beast::unit_test::suite
{
    jtx::Env* env_ = nullptr;
    AccountID const issuerID{0x2};
    std::uint32_t const seq{1};
    MPTID const shareMPTID = makeMptID(seq, issuerID);
    Issue const assetIssue{Currency(0x1), AccountID(0x2)};
    MPTIssue const shareIssue{shareMPTID};

    SLE::pointer
    makeVault(
        Number const& assetsTotal,
        Number const& lossUnrealized,
        Number const& interestUnrealized,
        std::int32_t scale)
    {
        auto sle = std::make_shared<SLE>(keylet::vault(AccountID(0x1), 1));
        sle->setFieldIssue(sfAsset, STIssue{sfAsset, assetIssue});
        sle->at(sfShareMPTID) = shareMPTID;
        sle->at(sfAssetsTotal) = assetsTotal;
        sle->at(sfLossUnrealized) = lossUnrealized;
        sle->at(sfInterestUnrealized) = interestUnrealized;
        sle->at(sfScale) = scale;
        return sle;
    }

    SLE::pointer
    makeIssuance(std::uint64_t outstandingAmount)
    {
        auto sle = std::make_shared<SLE>(keylet::mptIssuance(shareMPTID));
        sle->setFieldU64(sfOutstandingAmount, outstandingAmount);
        return sle;
    }

    Rules
    rules() const
    {
        return env_->current()->rules();
    }

    STAmount
    asset(Number const& value) const
    {
        return STAmount{assetIssue, value};
    }

    STAmount
    shares(Number const& value) const
    {
        return STAmount{shareIssue, value};
    }

    void
    testAssetsToSharesDeposit()
    {
        using namespace vault;

        struct TestCase
        {
            std::string name;
            Number assetsTotal;
            Number interestUnrealized;
            std::uint64_t shareTotal;
            std::int32_t scale;
            Number depositAssets;
            STAmount expectedShares;
        };

        auto const testCases = std::vector<TestCase>{
            // Initial: shares = Number(assets.mantissa, assets.exponent + scale).truncate()
            {
                .name = "Initial deposit IOU scale=6",
                .assetsTotal = 0,
                .interestUnrealized = 0,
                .shareTotal = 0,
                .scale = 6,
                .depositAssets = 100,
                .expectedShares =
                    shares(Number(Number{100}.mantissa(), Number{100}.exponent() + 6).truncate()),
            },
            {
                .name = "Initial deposit scale=0",
                .assetsTotal = 0,
                .interestUnrealized = 0,
                .shareTotal = 0,
                .scale = 0,
                .depositAssets = 10'000'000,
                .expectedShares =
                    shares(Number(Number{10'000'000}.mantissa(), Number{10'000'000}.exponent() + 0)
                               .truncate()),
            },
            {
                .name = "Initial deposit scale=2",
                .assetsTotal = 0,
                .interestUnrealized = 0,
                .shareTotal = 0,
                .scale = 2,
                .depositAssets = 50,
                .expectedShares =
                    shares(Number(Number{50}.mantissa(), Number{50}.exponent() + 2).truncate()),
            },
            {
                .name = "Initial deposit scale=18",
                .assetsTotal = 0,
                .interestUnrealized = 0,
                .shareTotal = 0,
                .scale = 18,
                .depositAssets = 1,
                .expectedShares =
                    shares(Number(Number{1}.mantissa(), Number{1}.exponent() + 18).truncate()),
            },
            // Subsequent: shares = floor(shareTotal * assets / depositNAV)
            {
                .name = "Subsequent proportional",
                .assetsTotal = 100,
                .interestUnrealized = 0,
                .shareTotal = 100'000'000,
                .scale = 6,
                .depositAssets = 50,
                .expectedShares = shares(Number{(100'000'000LL * 50) / 100}.truncate()),
            },
            {
                .name = "With unrealized interest",
                .assetsTotal = 1000,
                .interestUnrealized = 50,
                .shareTotal = 950,
                .scale = 0,
                .depositAssets = 95,
                // depositNAV = 1000 - 50 = 950
                .expectedShares = shares(Number{(950 * 95) / 950}.truncate()),
            },
            {
                .name = "Deposit with interest+loss (deposit ignores loss)",
                .assetsTotal = 1000,
                .interestUnrealized = 50,
                .shareTotal = 950,
                .scale = 0,
                .depositAssets = 1,
                // depositNAV = 1000 - 50 = 950
                .expectedShares = shares(Number{(950 * 1) / 950}.truncate()),
            },
            {
                .name = "Floor rounds down (inflated vault)",
                .assetsTotal = 7,
                .interestUnrealized = 0,
                .shareTotal = 3,
                .scale = 0,
                .depositAssets = 3,
                // shares = floor(3 * 3 / 7) = floor(9/7) = 1
                .expectedShares = shares(Number{(3 * 3) / 7}.truncate()),
            },
            {
                .name = "Tiny into large vault",
                .assetsTotal = Number{1, 9},
                .interestUnrealized = 0,
                .shareTotal = UINT64_C(1'000'000'000'000'000),
                .scale = 6,
                .depositAssets = Number{1, -6},
                .expectedShares =
                    shares(((Number{1, 15} * Number{1, -6}) / Number{1, 9}).truncate()),
            },
            {
                .name = "Extreme 1e15:1 ratio",
                .assetsTotal = Number{1, 15},
                .interestUnrealized = 0,
                .shareTotal = UINT64_C(1'000'000'000'000'000),
                .scale = 0,
                .depositAssets = 1,
                .expectedShares = shares(((Number{1, 15} * 1) / Number{1, 15}).truncate()),
            },
            {
                .name = "Inflated vault — fewer shares per asset",
                .assetsTotal = 110,
                .interestUnrealized = 0,
                .shareTotal = 100,
                .scale = 0,
                .depositAssets = 100,
                .expectedShares = shares(Number{(100 * 100) / 110}.truncate()),
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("assetsToSharesDeposit v2: " + tc.name);

            auto const vaultSle = makeVault(tc.assetsTotal, 0, tc.interestUnrealized, tc.scale);
            auto const issuanceSle = makeIssuance(tc.shareTotal);

            auto const result = vault::detail::assetsToSharesDeposit(
                vaultSle, issuanceSle, asset(tc.depositAssets));

            BEAST_EXPECTS(
                result == tc.expectedShares,
                "expected " + to_string(tc.expectedShares) + ", got " + to_string(result));
        }
    }

    void
    testSharesToAssetsDeposit()
    {
        using namespace vault;

        struct TestCase
        {
            std::string name;
            Number assetsTotal;
            Number interestUnrealized;
            std::uint64_t shareTotal;
            std::int32_t scale;
            Number depositShares;
            STAmount expectedAssets;
        };

        auto const testCases = std::vector<TestCase>{
            // Initial: assets = Number(shares.mantissa, shares.exponent - scale)
            {
                .name = "Initial (assetTotal=0) scale=6",
                .assetsTotal = 0,
                .interestUnrealized = 0,
                .shareTotal = 0,
                .scale = 6,
                .depositShares = Number{1, 8},
                .expectedAssets =
                    asset(Number(Number{1, 8}.mantissa(), Number{1, 8}.exponent() - 6)),
            },
            {
                .name = "Initial (assetTotal=0) scale=0",
                .assetsTotal = 0,
                .interestUnrealized = 0,
                .shareTotal = 0,
                .scale = 0,
                .depositShares = 500,
                .expectedAssets = asset(Number(Number{500}.mantissa(), Number{500}.exponent() - 0)),
            },
            // Subsequent: assets = depositNAV * shares / shareTotal
            {
                .name = "Proportional back-calc",
                .assetsTotal = 100,
                .interestUnrealized = 0,
                .shareTotal = 100'000'000,
                .scale = 6,
                .depositShares = Number{5, 7},
                .expectedAssets = asset((Number{100} * Number{5, 7}) / 100'000'000),
            },
            {
                .name = "With interest",
                .assetsTotal = 1000,
                .interestUnrealized = 50,
                .shareTotal = 950,
                .scale = 0,
                .depositShares = 95,
                // depositNAV = 950
                .expectedAssets = asset(Number{950 * 95} / 950),
            },
            {
                .name = "Non-integer back-calc (7/3)",
                .assetsTotal = 7,
                .interestUnrealized = 0,
                .shareTotal = 3,
                .scale = 0,
                .depositShares = 1,
                // assets = 7 * 1 / 3
                .expectedAssets = asset(Number{7} / 3),
            },
            {
                .name = "Floor invariant: actualAssets <= requested",
                .assetsTotal = 10,
                .interestUnrealized = 0,
                .shareTotal = 7,
                .scale = 0,
                .depositShares = 2,
                // assets = 10 * 2 / 7
                .expectedAssets = asset(Number{10 * 2} / 7),
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("sharesToAssetsDeposit v2: " + tc.name);

            auto const vaultSle = makeVault(tc.assetsTotal, 0, tc.interestUnrealized, tc.scale);
            auto const issuanceSle = makeIssuance(tc.shareTotal);

            auto const result = vault::detail::sharesToAssetsDeposit(
                vaultSle, issuanceSle, shares(tc.depositShares));

            BEAST_EXPECTS(
                result == tc.expectedAssets,
                "expected " + to_string(tc.expectedAssets) + ", got " + to_string(result));
        }
    }

    void
    testAssetsToSharesWithdraw()
    {
        using namespace vault;

        struct TestCase
        {
            std::string name;
            Number assetsTotal;
            Number interestUnrealized;
            Number lossUnrealized;
            std::uint64_t shareTotal;
            Number withdrawAssets;
            TruncateShares truncate;
            STAmount expectedShares;
        };

        // shares = shareTotal * assets / withdrawalNAV
        // withdrawalNAV = assetsTotal - interestUnrealized - lossUnrealized

        auto const testCases = std::vector<TestCase>{
            {
                .name = "Basic no loss",
                .assetsTotal = 100,
                .interestUnrealized = 0,
                .lossUnrealized = 0,
                .shareTotal = 100'000'000,
                .withdrawAssets = 50,
                .truncate = TruncateShares::no,
                // NAV = 100
                .expectedShares = shares(Number{100'000'000LL * 50} / 100),
            },
            {
                .name = "With paper loss",
                .assetsTotal = 10,
                .interestUnrealized = 0,
                .lossUnrealized = 3,
                .shareTotal = 10,
                .withdrawAssets = 1,
                .truncate = TruncateShares::yes,
                // NAV = 7; shares = floor(10 * 1 / 7)
                .expectedShares = shares(Number{(10 * 1) / 7}.truncate()),
            },
            {
                .name = "Fractional still floors same",
                .assetsTotal = 10,
                .interestUnrealized = 0,
                .lossUnrealized = 3,
                .shareTotal = 10,
                .withdrawAssets = Number{11, -1},
                .truncate = TruncateShares::yes,
                // NAV = 7; shares = floor(10 * 1.1 / 7)
                .expectedShares = shares(((Number{10} * Number{11, -1}) / 7).truncate()),
            },
            {
                .name = "Floor at 0.5 boundary (confirms floor, not round)",
                .assetsTotal = 10,
                .interestUnrealized = 0,
                .lossUnrealized = 0,
                .shareTotal = 10,
                .withdrawAssets = Number{15, -1},
                .truncate = TruncateShares::yes,
                // NAV = 10; shares = floor(10 * 1.5 / 10)
                .expectedShares = shares(((Number{10} * Number{15, -1}) / 10).truncate()),
            },
            {
                .name = "Zero NAV returns zero shares",
                .assetsTotal = 100,
                .interestUnrealized = 50,
                .lossUnrealized = 50,
                .shareTotal = 500,
                .withdrawAssets = 10,
                .truncate = TruncateShares::no,
                .expectedShares = shares(0),
            },
            {
                .name = "With loss — assetsOut <= requested",
                .assetsTotal = 10,
                .interestUnrealized = 0,
                .lossUnrealized = 2,
                .shareTotal = 10,
                .withdrawAssets = 3,
                .truncate = TruncateShares::yes,
                // NAV = 8; shares = floor(10 * 3 / 8)
                .expectedShares = shares(Number{(10 * 3) / 8}.truncate()),
            },
            {
                .name = "With losses and interest (no truncation)",
                .assetsTotal = 1000,
                .interestUnrealized = 100,
                .lossUnrealized = 100,
                .shareTotal = 500,
                .withdrawAssets = 100,
                .truncate = TruncateShares::no,
                // NAV = 800; raw = 62.5, assigned to MPT STAmount
                .expectedShares = shares(Number{500 * 100} / 800),
            },
            {
                .name = "With losses and interest (truncated)",
                .assetsTotal = 1000,
                .interestUnrealized = 100,
                .lossUnrealized = 100,
                .shareTotal = 500,
                .withdrawAssets = 100,
                .truncate = TruncateShares::yes,
                // NAV = 800; shares = floor(500 * 100 / 800)
                .expectedShares = shares(Number{(500 * 100) / 800}.truncate()),
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("assetsToSharesWithdraw v2: " + tc.name);

            auto const vaultSle =
                makeVault(tc.assetsTotal, tc.lossUnrealized, tc.interestUnrealized, 0);
            auto const issuanceSle = makeIssuance(tc.shareTotal);

            auto const result = vault::detail::assetsToSharesWithdraw(
                vaultSle, issuanceSle, asset(tc.withdrawAssets), tc.truncate);

            BEAST_EXPECTS(
                result == tc.expectedShares,
                "expected " + to_string(tc.expectedShares) + ", got " + to_string(result));
        }
    }

    void
    testSharesToAssetsWithdraw()
    {
        using namespace vault;

        struct TestCase
        {
            std::string name;
            Number assetsTotal;
            Number interestUnrealized;
            Number lossUnrealized;
            std::uint64_t shareTotal;
            Number withdrawShares;
            STAmount expectedAssets;
        };

        // assets = withdrawalNAV * shares / shareTotal
        // withdrawalNAV = assetsTotal - interestUnrealized - lossUnrealized

        auto const testCases = std::vector<TestCase>{
            {
                .name = "Basic redeem half",
                .assetsTotal = 100,
                .interestUnrealized = 0,
                .lossUnrealized = 0,
                .shareTotal = 100'000'000,
                .withdrawShares = Number{5, 7},
                .expectedAssets = asset((Number{100} * Number{5, 7}) / 100'000'000),
            },
            {
                .name = "Redeem all",
                .assetsTotal = 100,
                .interestUnrealized = 0,
                .lossUnrealized = 0,
                .shareTotal = 100'000'000,
                .withdrawShares = Number{1, 8},
                .expectedAssets = asset((Number{100} * Number{1, 8}) / 100'000'000),
            },
            {
                .name = "With loss",
                .assetsTotal = 1050,
                .interestUnrealized = 50,
                .lossUnrealized = 100,
                .shareTotal = 1000,
                .withdrawShares = 100,
                // NAV = 900
                .expectedAssets = asset(Number{900 * 100} / 1000),
            },
            {
                .name = "Both interest+loss (spec example)",
                .assetsTotal = 1001,
                .interestUnrealized = 50,
                .lossUnrealized = 100,
                .shareTotal = 951,
                .withdrawShares = 1,
                // NAV = 851
                .expectedAssets = asset(Number{851} / 951),
            },
            {
                .name = "Loss only (no interest)",
                .assetsTotal = 1100,
                .interestUnrealized = 0,
                .lossUnrealized = 200,
                .shareTotal = 1100,
                .withdrawShares = 100,
                // NAV = 900
                .expectedAssets = asset(Number{900 * 100} / 1100),
            },
            {
                .name = "After hard default",
                .assetsTotal = 600,
                .interestUnrealized = 0,
                .lossUnrealized = 0,
                .shareTotal = 1000,
                .withdrawShares = 500,
                .expectedAssets = asset(Number{600 * 500} / 1000),
            },
            {
                .name = "Non-terminating fraction (1/3)",
                .assetsTotal = 1,
                .interestUnrealized = 0,
                .lossUnrealized = 0,
                .shareTotal = 3,
                .withdrawShares = 1,
                .expectedAssets = asset(Number{1} / 3),
            },
            {
                .name = "Zero NAV returns zero assets",
                .assetsTotal = 100,
                .interestUnrealized = 50,
                .lossUnrealized = 50,
                .shareTotal = 500,
                .withdrawShares = 10,
                .expectedAssets = asset(0),
            },
            {
                .name = "High precision with loss",
                .assetsTotal = Number{1, 12},
                .interestUnrealized = 0,
                .lossUnrealized = Number{2, 11},
                .shareTotal = UINT64_C(1'000'000'000'000),
                .withdrawShares = 1,
                // NAV = 8e11
                .expectedAssets = asset(Number{8, 11} / Number{1, 12}),
            },
            {
                .name = "Inflated vault redeem — captures yield",
                .assetsTotal = 209,
                .interestUnrealized = 0,
                .lossUnrealized = 0,
                .shareTotal = 190,
                .withdrawShares = 100,
                .expectedAssets = asset(Number{209 * 100} / 190),
            },
        };

        for (auto const& tc : testCases)
        {
            testcase("sharesToAssetsWithdraw v2: " + tc.name);

            auto const vaultSle =
                makeVault(tc.assetsTotal, tc.lossUnrealized, tc.interestUnrealized, 0);
            auto const issuanceSle = makeIssuance(tc.shareTotal);

            auto const result = vault::detail::sharesToAssetsWithdraw(
                vaultSle, issuanceSle, shares(tc.withdrawShares));

            BEAST_EXPECTS(
                result == tc.expectedAssets,
                "expected " + to_string(tc.expectedAssets) + ", got " + to_string(result));
        }
    }

    void
    testComputeDeposit()
    {
        using namespace vault;

        testcase("computeDeposit: normal");
        {
            auto const vaultSle = makeVault(100, 0, 0, 6);
            auto const issuanceSle = makeIssuance(100'000'000);

            auto const result =
                computeDeposit(rules(), vaultSle, issuanceSle, asset(50), env_->journal);
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                BEAST_EXPECT(result->shares != beast::zero);
                BEAST_EXPECT(result->assets <= asset(50));
            }
        }

        testcase("computeDeposit: initial deposit");
        {
            auto const vaultSle = makeVault(0, 0, 0, 6);
            auto const issuanceSle = makeIssuance(0);

            auto const result =
                computeDeposit(rules(), vaultSle, issuanceSle, asset(100), env_->journal);
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                BEAST_EXPECT(result->shares != beast::zero);
                BEAST_EXPECT(result->assets == asset(100));
            }
        }

        testcase("computeDeposit: precision loss returns tecPRECISION_LOSS");
        {
            // Huge vault, tiny deposit — shares truncate to zero.
            auto const vaultSle = makeVault(Number{1, 15}, 0, 0, 0);
            auto const issuanceSle = makeIssuance(1);

            auto const result = computeDeposit(
                rules(), vaultSle, issuanceSle, asset(Number{1, -15}), env_->journal);
            BEAST_EXPECT(!result.has_value());
            if (!result)
                BEAST_EXPECT(result.error() == tecPRECISION_LOSS);
        }
    }

    void
    testComputeWithdrawByAssets()
    {
        using namespace vault;

        testcase("computeWithdrawByAssets: normal");
        {
            auto const vaultSle = makeVault(100, 0, 0, 0);
            auto const issuanceSle = makeIssuance(100);

            auto const result =
                computeWithdrawByAssets(rules(), vaultSle, issuanceSle, asset(50), env_->journal);
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                BEAST_EXPECT(result->shares != beast::zero);
                BEAST_EXPECT(result->assets != beast::zero);
            }
        }

        testcase("computeWithdrawByAssets: precision loss");
        {
            auto const vaultSle = makeVault(Number{1, 15}, 0, 0, 0);
            auto const issuanceSle = makeIssuance(1);

            auto const result = computeWithdrawByAssets(
                rules(), vaultSle, issuanceSle, asset(Number{1, -15}), env_->journal);
            BEAST_EXPECT(!result.has_value());
            if (!result)
                BEAST_EXPECT(result.error() == tecPRECISION_LOSS);
        }
    }

    void
    testComputeWithdrawByShares()
    {
        using namespace vault;

        testcase("computeWithdrawByShares: normal");
        {
            auto const vaultSle = makeVault(100, 0, 0, 0);
            auto const issuanceSle = makeIssuance(100);

            auto const result =
                computeWithdrawByShares(rules(), vaultSle, issuanceSle, shares(50), env_->journal);
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                BEAST_EXPECT(result->assets != beast::zero);
                BEAST_EXPECT(result->shares == shares(50));
            }
        }
    }

    void
    testComputeClawback()
    {
        using namespace vault;

        testcase("computeClawback: normal");
        {
            auto const vaultSle = makeVault(1000, 0, 0, 0);
            auto const issuanceSle = makeIssuance(1000);

            auto const result = computeClawback(
                rules(), vaultSle, issuanceSle, asset(100), Number{1000}, env_->journal);
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                BEAST_EXPECT(result->assets != beast::zero);
                BEAST_EXPECT(result->shares != beast::zero);
            }
        }

        testcase("computeClawback: clamped to assetsAvailable");
        {
            // assetsAvailable is small — clawback should be clamped.
            auto const vaultSle = makeVault(1000, 0, 0, 0);
            auto const issuanceSle = makeIssuance(1000);

            auto const result = computeClawback(
                rules(), vaultSle, issuanceSle, asset(500), Number{10}, env_->journal);
            BEAST_EXPECT(result.has_value());
            if (result)
                BEAST_EXPECT(result->assets <= asset(10));
        }
    }

    // Creates a Sandbox with vault and issuance SLEs inserted.
    Sandbox
    makeSandbox(SLE::pointer vault, SLE::pointer issuance)
    {
        Sandbox sb(env_->current().get(), tapNONE);
        sb.insert(vault);
        sb.insert(issuance);
        return sb;
    }

    void
    testBorrowFromVault()
    {
        using namespace vault;

        testcase("borrowFromVault: normal");
        {
            auto vaultSle = makeVault(1000, 0, 0, 0);
            vaultSle->at(sfAssetsAvailable) = 1000;
            vaultSle->at(sfAssetsMaximum) = 0;
            auto issuanceSle = makeIssuance(1000);

            auto sb = makeSandbox(vaultSle, issuanceSle);
            auto const ter = borrowFromVault(sb, vaultSle, 100, 10, env_->journal);
            BEAST_EXPECT(ter == tesSUCCESS);
            BEAST_EXPECT(vaultSle->at(sfAssetsAvailable) == 900);
            BEAST_EXPECT(vaultSle->at(sfAssetsTotal) == 1010);
            BEAST_EXPECT(vaultSle->at(sfInterestUnrealized) == 10);
        }

        testcase("borrowFromVault: zero yield");
        {
            auto vaultSle = makeVault(1000, 0, 0, 0);
            vaultSle->at(sfAssetsAvailable) = 1000;
            vaultSle->at(sfAssetsMaximum) = 0;
            auto issuanceSle = makeIssuance(1000);

            auto sb = makeSandbox(vaultSle, issuanceSle);
            auto const ter = borrowFromVault(sb, vaultSle, 100, 0, env_->journal);
            BEAST_EXPECT(ter == tesSUCCESS);
            BEAST_EXPECT(vaultSle->at(sfAssetsAvailable) == 900);
            BEAST_EXPECT(vaultSle->at(sfAssetsTotal) == 1000);
            BEAST_EXPECT(vaultSle->at(sfInterestUnrealized) == 0);
        }

        testcase("borrowFromVault: invalid amount");
        {
            auto vaultSle = makeVault(1000, 0, 0, 0);
            vaultSle->at(sfAssetsAvailable) = 1000;
            vaultSle->at(sfAssetsMaximum) = 0;
            auto issuanceSle = makeIssuance(1000);

            auto sb = makeSandbox(vaultSle, issuanceSle);
            BEAST_EXPECT(borrowFromVault(sb, vaultSle, 0, 10, env_->journal) == tecINTERNAL);
            BEAST_EXPECT(borrowFromVault(sb, vaultSle, -1, 10, env_->journal) == tecINTERNAL);
        }

        testcase("borrowFromVault: negative yield");
        {
            auto vaultSle = makeVault(1000, 0, 0, 0);
            vaultSle->at(sfAssetsAvailable) = 1000;
            vaultSle->at(sfAssetsMaximum) = 0;
            auto issuanceSle = makeIssuance(1000);

            auto sb = makeSandbox(vaultSle, issuanceSle);
            BEAST_EXPECT(borrowFromVault(sb, vaultSle, 100, -1, env_->journal) == tecINTERNAL);
        }

        testcase("borrowFromVault: insufficient available");
        {
            auto vaultSle = makeVault(1000, 0, 0, 0);
            vaultSle->at(sfAssetsAvailable) = 50;
            vaultSle->at(sfAssetsMaximum) = 0;
            auto issuanceSle = makeIssuance(1000);

            auto sb = makeSandbox(vaultSle, issuanceSle);
            BEAST_EXPECT(borrowFromVault(sb, vaultSle, 100, 10, env_->journal) == tecINTERNAL);
        }
    }

    void
    testV1Routing()
    {
        using namespace vault;

        // Use non-zero interestUnrealized to distinguish v1 from v2.
        // v1 ignores interestUnrealized in NAV, v2 subtracts it.
        // With assetsTotal=100, interestUnrealized=20, shares=100, scale=0:
        //   v1 deposit NAV  = 100 (raw assetsTotal)
        //   v2 deposit NAV  = 80  (assetsTotal - interestUnrealized)
        //   v1 withdraw NAV = 100 (assetsTotal - lossUnrealized)
        //   v2 withdraw NAV = 80  (assetsTotal - interestUnrealized - lossUnrealized)

        testcase("v1 routing: computeDeposit");
        {
            auto const vaultSle = makeVault(100, 0, 20, 0);
            auto const issuanceSle = makeIssuance(100);

            auto const result =
                computeDeposit(rules(), vaultSle, issuanceSle, asset(40), env_->journal);
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                // v1: 100 * 40 / 100 = 40 shares (v2 would give 50)
                BEAST_EXPECT(result->shares == shares(40));
            }
        }

        testcase("v1 routing: computeWithdrawByShares");
        {
            auto const vaultSle = makeVault(100, 0, 20, 0);
            auto const issuanceSle = makeIssuance(100);

            auto const result =
                computeWithdrawByShares(rules(), vaultSle, issuanceSle, shares(50), env_->journal);
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                // v1: 100 * 50 / 100 = 50 assets (v2 would give 40)
                BEAST_EXPECT(result->assets == asset(50));
            }
        }

        testcase("v1 routing: computeWithdrawByAssets");
        {
            auto const vaultSle = makeVault(100, 0, 20, 0);
            auto const issuanceSle = makeIssuance(100);

            auto const result =
                computeWithdrawByAssets(rules(), vaultSle, issuanceSle, asset(50), env_->journal);
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                // v1: 100 * 50 / 100 = 50 shares (v2 would give 62.5 → 62)
                BEAST_EXPECT(result->shares == shares(50));
            }
        }

        testcase("v1 routing: computeClawback");
        {
            auto const vaultSle = makeVault(100, 0, 20, 0);
            auto const issuanceSle = makeIssuance(100);

            auto const result = computeClawback(
                rules(), vaultSle, issuanceSle, asset(50), Number{100}, env_->journal);
            BEAST_EXPECT(result.has_value());
            if (result)
            {
                // v1 uses same withdraw NAV (100), so shares = 50
                BEAST_EXPECT(result->shares == shares(50));
                BEAST_EXPECT(result->assets == asset(50));
            }
        }
    }

public:
    void
    run() override
    {
        using namespace jtx;
        Env env{*this};
        env_ = &env;

        // v2 tests (amendment enabled by default)
        testAssetsToSharesDeposit();
        testSharesToAssetsDeposit();
        testAssetsToSharesWithdraw();
        testSharesToAssetsWithdraw();

        // High-level API tests
        testComputeDeposit();
        testComputeWithdrawByAssets();
        testComputeWithdrawByShares();
        testComputeClawback();
        testBorrowFromVault();

        // v1 routing tests (amendment disabled)
        {
            Env v1Env{*this};
            v1Env.disableFeature(featureLendingProtocolV1_1);
            env_ = &v1Env;
            testV1Routing();
            env_ = &env;
        }
    }
};

BEAST_DEFINE_TESTSUITE(VaultHelpers, app, xrpl);

}  // namespace test
}  // namespace xrpl
