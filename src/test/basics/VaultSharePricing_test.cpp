#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <cassert>

namespace xrpl {

// Result types for vault operations
struct DepositResult
{
    STAmount shares;
    STAmount assets;
};

struct WithdrawResult
{
    STAmount shares;
    STAmount assets;
};

// Proof-of-concept Vault implementing the XLS-0065 share pricing model.
// Uses STAmount for typed asset/share values and Number for arithmetic.
class Vault
{
    Asset asset_;
    MPTIssue shareAsset_;
    std::uint8_t scale_;
    Number assetsTotal_{0};
    Number assetsAvailable_{0};
    Number sharesTotal_{0};
    Number interestUnrealized_{0};
    Number lossUnrealized_{0};

    static inline std::uint32_t nextShareSeq_ = 1;

public:
    Vault(Asset asset, std::uint8_t scale = 6)
        : asset_(asset)
        , shareAsset_(makeMptID(nextShareSeq_++, AccountID(0xFACE)))
        , scale_(asset.native() || asset.integral() ? 0 : scale)
    {
        // XRP and MPT force scale to 0 per spec
        // IOU uses caller-provided scale (default 6)
    }

    Expected<DepositResult, TER>
    deposit(STAmount const& assets)
    {
        Number shares;
        Number actualAssets;

        if (assetsTotal_ == 0 && sharesTotal_ == 0)
        {
            // Initial deposit: shares = assets * 10^scale
            shares = Number(assets.mantissa(), assets.exponent() + scale_).truncate();
            actualAssets = assets;
        }
        else
        {
            // Subsequent deposit: shares = floor(assets * sharesTotal / depositNAV)
            Number const depositNAV = assetsTotal_ - interestUnrealized_;
            shares = ((assets * sharesTotal_) / depositNAV).truncate();

            // Recalculate actual assets taken
            actualAssets = (shares * depositNAV) / sharesTotal_;
        }

        if (shares <= 0)
            return Unexpected(tecPRECISION_LOSS);

        assetsTotal_ += actualAssets;
        assetsAvailable_ += actualAssets;
        sharesTotal_ += shares;

        return DepositResult{
            STAmount{shareAsset_, shares},
            STAmount{asset_, actualAssets},
        };
    }

    Expected<STAmount, TER>
    redeem(STAmount const& shares)
    {
        if (shares <= beast::zero)
            return Unexpected(tecPRECISION_LOSS);

        // assets = shares * withdrawalNAV / sharesTotal
        Number const withdrawalNAV = assetsTotal_ - interestUnrealized_ - lossUnrealized_;
        Number const assetsOut = (shares * withdrawalNAV) / sharesTotal_;

        if (assetsOut > assetsAvailable_)
            return Unexpected(tecINSUFFICIENT_FUNDS);

        assetsTotal_ -= assetsOut;
        assetsAvailable_ -= assetsOut;
        sharesTotal_ -= shares;

        return STAmount{asset_, assetsOut};
    }

    Expected<WithdrawResult, TER>
    withdraw(STAmount const& assetsRequested)
    {
        Number const withdrawalNAV = assetsTotal_ - interestUnrealized_ - lossUnrealized_;

        // shares = floor(requested * sharesTotal / withdrawalNAV)
        Number const shares = ((assetsRequested * sharesTotal_) / withdrawalNAV).truncate();

        if (shares <= 0)
            return Unexpected(tecPRECISION_LOSS);

        // Recalculate actual assets out
        Number const assetsOut = (shares * withdrawalNAV) / sharesTotal_;

        if (assetsOut > assetsAvailable_)
            return Unexpected(tecINSUFFICIENT_FUNDS);

        assetsTotal_ -= assetsOut;
        assetsAvailable_ -= assetsOut;
        sharesTotal_ -= shares;

        return WithdrawResult{
            STAmount{shareAsset_, shares},
            STAmount{asset_, assetsOut},
        };
    }

    void
    borrow(Number const& principal, Number const& interest)
    {
        assert(principal > 0 && interest >= 0);
        assert(principal <= assetsAvailable_);

        assetsAvailable_ -= principal;

        interestUnrealized_ += interest;
        assetsTotal_ += interest;
    }

    void
    repay(
        Number const& principal,
        Number const& interest,
        std::optional<Number const> extraInterest = std::nullopt)
    {
        assert(principal > 0 && interest >= 0 && (!extraInterest || *extraInterest >= 0));
        assert(principal + interest + assetsAvailable_ <= assetsTotal_);

        assetsAvailable_ += principal + interest;
        interestUnrealized_ -= interest;

        if (extraInterest)
        {
            assetsTotal_ += extraInterest.value();
            assetsAvailable_ += extraInterest.value();
        }
    }

    void
    addPaperLoss(Number const& amount)
    {
        assert(amount > 0);
        // Spec invariant: lossUnrealized <= assetsTotal - assetsAvailable
        assert(lossUnrealized_ + amount <= assetsTotal_ - assetsAvailable_);

        lossUnrealized_ += amount;
    }

    void
    clearPaperLoss(Number const& amount)
    {
        assert(amount > 0);
        assert(amount <= lossUnrealized_);

        lossUnrealized_ -= amount;
    }

    void
    defaultLoan(Number const& principal, Number const& interest, bool hasPaperLoss = false)
    {
        assert(principal > 0 && interest >= 0);
        assert(principal + interest <= assetsTotal_);
        assert(interest <= interestUnrealized_);

        assetsTotal_ -= principal + interest;
        interestUnrealized_ -= interest;
        if (hasPaperLoss)
            clearPaperLoss(principal + interest);
    }

    STAmount
    assetsTotal() const
    {
        return STAmount{asset_, assetsTotal_};
    }

    STAmount
    assetsAvailable() const
    {
        return STAmount{asset_, assetsAvailable_};
    }

    STAmount
    sharesTotal() const
    {
        return STAmount{shareAsset_, sharesTotal_};
    }

    MPTIssue const&
    shareAsset() const
    {
        return shareAsset_;
    }

    Number
    interestUnrealized() const
    {
        return interestUnrealized_;
    }

    Number
    lossUnrealized() const
    {
        return lossUnrealized_;
    }

    Number
    depositNAV() const
    {
        return assetsTotal_ - interestUnrealized_;
    }

    Number
    withdrawalNAV() const
    {
        return assetsTotal_ - interestUnrealized_ - lossUnrealized_;
    }

    uint8_t
    scale() const
    {
        return scale_;
    }

    // Expected shares for a deposit of the given amount
    Number
    depositShares(Number const& amount) const
    {
        return ((amount * sharesTotal_) / depositNAV()).truncate();
    }

    // Expected assets actually taken for a deposit that yields the given shares
    Number
    depositAssets(Number const& shares) const
    {
        return (shares * depositNAV()) / sharesTotal_;
    }

    // Expected assets out for redeeming the given number of shares
    Number
    redeemAssets(Number const& shares) const
    {
        return (shares * withdrawalNAV()) / sharesTotal_;
    }

    // Expected shares burned for a withdrawal of the given asset amount
    Number
    withdrawShares(Number const& amount) const
    {
        return ((amount * sharesTotal_) / withdrawalNAV()).truncate();
    }

    // Expected assets out for a withdrawal that burns the given shares
    Number
    withdrawAssets(Number const& shares) const
    {
        return (shares * withdrawalNAV()) / sharesTotal_;
    }
};

struct ExpectedState
{
    Number assetsTotal;
    Number assetsAvailable;
    Number sharesTotal;
    Number interestOutstanding = 0;
    Number lossUnrealized = 0;
};

class VaultSharePricing_test : public beast::unit_test::suite
{
    Issue const usd_{Currency(0x5553440000000000), AccountID(0x4985601)};

    void
    expectState(Vault const& vault, ExpectedState const& e)
    {
        BEAST_EXPECT(vault.assetsTotal() == e.assetsTotal);
        BEAST_EXPECT(vault.assetsAvailable() == e.assetsAvailable);
        BEAST_EXPECT(vault.sharesTotal() == e.sharesTotal);
        BEAST_EXPECT(vault.interestUnrealized() == e.interestOutstanding);
        BEAST_EXPECT(vault.lossUnrealized() == e.lossUnrealized);
    }

public:
    void
    testInitialDepositIOU()
    {
        testcase("Initial deposit IOU (scale=6)");

        // An empty IOU vault uses the seeding formula: shares = assets * 10^scale.
        // Verifies the share count, the assets-taken amount, and the resulting vault state.
        Vault vault{usd_};  // scale defaults to 6

        Number const depositAmt = 100;

        // scale=6: initial deposit gives shares = assets * 10^scale = 100 * 10^6 = 1e8
        auto const expectedShares =
            Number{depositAmt.mantissa(), depositAmt.exponent() + vault.scale()};
        auto const [actualShares, actualAssets] = vault.deposit(STAmount{usd_, depositAmt}).value();

        BEAST_EXPECT(actualShares == expectedShares);
        BEAST_EXPECT(actualAssets == depositAmt);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = expectedShares,
            });
    }

    void
    testInitialDepositXRP()
    {
        testcase("Initial deposit XRP (scale=0)");

        // XRP forces scale=0, so the seeding formula simplifies to shares = drops 1:1.
        // Verifies the share count, the assets-taken amount, and the resulting vault state.
        Vault vault{xrpIssue()};

        auto const depositAmt = 10'000'000;

        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{xrpIssue(), depositAmt}).value();

        // scale=0 so shares = drops 1:1
        BEAST_EXPECT(Number(actualShares) == depositAmt);
        BEAST_EXPECT(Number(actualAssets) == depositAmt);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });
    }

    void
    testInitialDepositMPT()
    {
        testcase("Initial deposit MPT (scale=0)");

        // MPT forces scale=0, so the seeding formula simplifies to shares = token units 1:1.
        // Verifies the share count, the assets-taken amount, and the resulting vault state.
        MPTIssue const mptAsset{makeMptID(100, AccountID(0x5678))};
        Vault vault{mptAsset};

        auto const depositAmt = 500;

        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{mptAsset, depositAmt}).value();

        // scale=0, shares = assets 1:1
        BEAST_EXPECT(Number(actualShares) == depositAmt);
        BEAST_EXPECT(Number(actualAssets) == depositAmt);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });
    }

    void
    testSubsequentDeposit()
    {
        testcase("Subsequent deposit (proportional)");

        // A second deposit uses shares = floor(assets * sharesTotal / depositNAV).
        // Verifies that the share count is floored and the actual assets taken are
        // back-calculated from the floored share count.
        Vault vault{usd_};

        Number const depositAmt1 = 100;
        auto const expectedShares1 =
            Number{depositAmt1.mantissa(), depositAmt1.exponent() + vault.scale()};

        Number const depositAmt2 = 50;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt1}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt1,
                .assetsAvailable = depositAmt1,
                .sharesTotal = expectedShares1,
            });

        // No loan outstanding: depositNAV = assetsTotal, shares proportional to assets
        // shares = floor(depositAmt2 * sharesTotal / depositNAV)
        Number const expectedShares2 = vault.depositShares(depositAmt2);
        Number const expectedAssets2 = vault.depositAssets(expectedShares2);

        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{usd_, depositAmt2}).value();

        BEAST_EXPECT(actualAssets == expectedAssets2);
        BEAST_EXPECT(actualShares == expectedShares2);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt1 + expectedAssets2,
                .assetsAvailable = depositAmt1 + expectedAssets2,
                .sharesTotal = expectedShares1 + expectedShares2,
            });
    }

    void
    testRedeemBasic()
    {
        testcase("Redeem basic");

        // Redeem half the outstanding shares from a plain IOU vault.
        // Verifies assets returned are proportional to the share fraction and vault
        // state decrements correctly.
        Vault vault{usd_};

        // scale=6: 100 assets -> 100 * 10^6 = 100_000_000 shares
        Number const depositAmt = 100;
        auto const depositShares =
            Number{depositAmt.mantissa(), depositAmt.exponent() + vault.scale()};
        auto const redeemShares = depositShares / 2;
        auto const expectedOut = depositAmt / 2;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());

        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositShares,
            });

        // Redeem half the shares
        auto const assetsOut = vault.redeem(STAmount{vault.shareAsset(), redeemShares}).value();

        BEAST_EXPECT(assetsOut == expectedOut);
        expectState(
            vault,
            {
                .assetsTotal = expectedOut,
                .assetsAvailable = expectedOut,
                .sharesTotal = redeemShares,
            });
    }

    void
    testWithdrawBasic()
    {
        testcase("Withdraw basic");

        // Withdraw a fixed asset amount from a plain IOU vault.
        // Verifies the correct number of shares are burned and vault state decrements
        // correctly.
        Vault vault{usd_};

        Number const depositAmt = 100;
        auto const depositShares =
            Number{depositAmt.mantissa(), depositAmt.exponent() + vault.scale()};
        auto const withdrawAmt = depositAmt / 2;
        auto const withdrawShares = depositShares / 2;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());

        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositShares,
            });

        // Withdraw 50 assets
        auto const [actualShares, actualAssets] =
            vault.withdraw(STAmount{usd_, withdrawAmt}).value();

        BEAST_EXPECT(actualShares == withdrawShares);
        BEAST_EXPECT(actualAssets == withdrawAmt);
        expectState(
            vault,
            {
                .assetsTotal = withdrawAmt,
                .assetsAvailable = withdrawAmt,
                .sharesTotal = withdrawShares,
            });
    }

    void
    testAsymmetricDepositWithInterest()
    {
        testcase("Asymmetric pricing - deposit with unrealized interest");

        // depositNAV excludes interestUnrealized, so a depositor joining while a loan
        // is outstanding cannot capture existing yield — they receive fewer shares per
        // asset than the initial depositors did.
        Vault vault{usd_, 0};  // scale=0 for simpler math

        auto const depositAmt = 950;
        auto const depositAmt2 = 95;
        auto const principal = 500;
        auto const interest = 50;

        // Seed vault: 950 assets, 950 shares
        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        vault.borrow(principal, interest);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
            });

        Number const expectedShares = vault.depositShares(Number(depositAmt2));
        Number const expectedAssets = vault.depositAssets(expectedShares);

        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{usd_, depositAmt2}).value();

        BEAST_EXPECT(actualShares == expectedShares);
        BEAST_EXPECT(actualAssets == expectedAssets);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest + expectedAssets,
                .assetsAvailable = depositAmt - principal + expectedAssets,
                .sharesTotal = depositAmt + expectedShares,
                .interestOutstanding = interest,
            });
    }

    void
    testAsymmetricWithdrawWithLoss()
    {
        testcase("Asymmetric pricing - withdraw with unrealized loss");

        // withdrawalNAV is discounted by lossUnrealized, so redeemers receive fewer
        // assets per share while a paper loss is active. The loss remains on the books
        // until cleared or confirmed as a hard default.
        Vault vault{usd_, 0};

        auto const depositAmt = 1000;
        auto const principal = 500;
        auto const interest = 50;
        auto const paperLoss = 100;
        auto const redeemShares = 100;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        vault.borrow(principal, interest);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
            });

        vault.addPaperLoss(paperLoss);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
                .lossUnrealized = paperLoss,
            });

        Number const expectedAssets = vault.redeemAssets(redeemShares);
        auto const assetsOut = vault.redeem(STAmount{vault.shareAsset(), redeemShares}).value();

        BEAST_EXPECT(assetsOut == expectedAssets);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest - assetsOut,
                .assetsAvailable = depositAmt - principal - assetsOut,
                .sharesTotal = depositAmt - redeemShares,
                .interestOutstanding = interest,
                .lossUnrealized = paperLoss,
            });
    }

    void
    testSpecExample()
    {
        testcase("Spec example: deposit at full NAV, redeem at loss-adjusted NAV");

        // Reproduces the spec's worked example with both interestUnrealized and
        // lossUnrealized active simultaneously. A new deposit uses depositNAV (ignores
        // loss) while redemption uses the lower withdrawalNAV (subtracts both).
        Vault vault{usd_, 0};

        auto const depositAmt = 950;
        auto const principal = 500;
        auto const interest = 50;
        auto const paperLoss = 100;
        auto const depositAmt2 = 1;
        auto const redeemShares = 1;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        vault.borrow(principal, interest);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
            });

        vault.addPaperLoss(paperLoss);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
                .lossUnrealized = paperLoss,
            });

        // Deposit at depositNAV (excludes interest, ignores loss)
        {
            auto const [actualShares, actualAssets] =
                vault.deposit(STAmount{usd_, depositAmt2}).value();
            BEAST_EXPECT(Number(actualShares) == depositAmt2);
            BEAST_EXPECT(Number(actualAssets) == depositAmt2);
        }

        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest + depositAmt2,
                .assetsAvailable = depositAmt - principal + depositAmt2,
                .sharesTotal = depositAmt + depositAmt2,
                .interestOutstanding = interest,
                .lossUnrealized = paperLoss,
            });

        // Redeem at withdrawalNAV (subtracts both interest and loss)
        {
            Number const expectedAssets = vault.redeemAssets(redeemShares);
            // Compare as STAmount: redeem() returns STAmount which truncates
            // to 16 significant digits, so comparing directly to the
            // full-precision Number would fail for non-terminating fractions.
            STAmount const expectedAmt{usd_, expectedAssets};
            auto const redeemAssets =
                vault.redeem(STAmount{vault.shareAsset(), redeemShares}).value();
            BEAST_EXPECT(redeemAssets == expectedAmt);

            STAmount const expectedTotal{
                usd_, depositAmt + interest + depositAmt2 - expectedAssets};
            STAmount const expectedAvail{
                usd_, depositAmt - principal + depositAmt2 - expectedAssets};
            BEAST_EXPECT(vault.assetsTotal() == expectedTotal);
            BEAST_EXPECT(vault.assetsAvailable() == expectedAvail);
            BEAST_EXPECT(Number(vault.sharesTotal()) == depositAmt + depositAmt2 - redeemShares);
            BEAST_EXPECT(vault.interestUnrealized() == interest);
            BEAST_EXPECT(vault.lossUnrealized() == paperLoss);
        }
    }

    void
    testDepositRoundingDown()
    {
        testcase("Deposit rounding - shares floor");

        // When depositNAV > sharesTotal (inflated vault), the share-per-asset ratio is
        // less than 1, so floor(shares) discards the fractional part. Verifies the floored
        // share count and the back-calculated actual assets taken.
        Vault vault{usd_, 0};

        auto const seedAmt = 3;
        auto const extraInterest = 4;
        auto const depositAmt = 3;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, seedAmt}).has_value());
        // Borrow 1 then repay with 4 extra interest to inflate depositNAV
        vault.borrow(1, 0);
        vault.repay(1, 0, extraInterest);

        expectState(
            vault,
            {
                .assetsTotal = seedAmt + extraInterest,
                .assetsAvailable = seedAmt + extraInterest,
                .sharesTotal = seedAmt,
            });

        Number const expectedShares = vault.depositShares(Number(depositAmt));
        Number const expectedAssets = vault.depositAssets(expectedShares);
        // Compare as STAmount: deposit() returns STAmount which truncates
        // to 16 significant digits, so comparing directly to the
        // full-precision Number would fail for non-terminating fractions.
        STAmount const expectedSharesAmt{vault.shareAsset(), expectedShares};
        STAmount const expectedAssetsAmt{usd_, expectedAssets};

        auto const [actualShares, actualAssets] = vault.deposit(STAmount{usd_, depositAmt}).value();
        BEAST_EXPECT(actualShares == expectedSharesAmt);
        BEAST_EXPECT(actualAssets == expectedAssetsAmt);

        STAmount const expectedTotal{usd_, seedAmt + extraInterest + expectedAssets};
        STAmount const expectedAvail{usd_, seedAmt + extraInterest + expectedAssets};
        BEAST_EXPECT(vault.assetsTotal() == expectedTotal);
        BEAST_EXPECT(vault.assetsAvailable() == expectedAvail);
        BEAST_EXPECT(Number(vault.sharesTotal()) == seedAmt + 1);
    }

    void
    testWithdrawRoundingFloor()
    {
        testcase("Withdraw rounding - shares floor");

        // shares = floor(requested * sharesTotal / withdrawalNAV) is always floored,
        // so assetsOut is always <= requested. Two scenarios: integer requested amount
        // and fractional requested amount (1.1), both floor to the same share count.

        // Scenario 1: withdrawalNAV < sharesTotal due to paper loss
        {
            Vault vault{usd_, 0};

            auto const depositAmt = 10;
            auto const borrowAmt = 3;
            auto const paperLoss = 3;
            auto const withdrawRequested = 1;

            BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
            vault.borrow(borrowAmt, 0);
            vault.addPaperLoss(paperLoss);
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt - borrowAmt,
                    .sharesTotal = depositAmt,
                    .lossUnrealized = paperLoss,
                });

            Number const expectedShares = vault.withdrawShares(Number(withdrawRequested));
            Number const expectedAssetsOut = vault.withdrawAssets(expectedShares);

            auto const [actualShares, actualAssets] =
                vault.withdraw(STAmount{usd_, withdrawRequested}).value();
            BEAST_EXPECT(actualShares == expectedShares);
            BEAST_EXPECT(actualAssets == expectedAssetsOut);

            expectState(
                vault,
                {
                    .assetsTotal = depositAmt - expectedAssetsOut,
                    .assetsAvailable = depositAmt - borrowAmt - expectedAssetsOut,
                    .sharesTotal = depositAmt - 1,
                    .lossUnrealized = paperLoss,
                });
        }

        // Scenario 2: fractional withdrawal (1.1) still floors to 1 share
        {
            Vault vault2{usd_, 0};

            auto const depositAmt = 10;
            auto const borrowAmt = 3;
            auto const paperLoss = 3;
            STAmount const withdrawRequested{usd_, 11, -1};  // 1.1

            vault2.deposit(STAmount{usd_, depositAmt}).value();
            vault2.borrow(borrowAmt, 0);
            vault2.addPaperLoss(paperLoss);
            expectState(
                vault2,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt - borrowAmt,
                    .sharesTotal = depositAmt,
                    .lossUnrealized = paperLoss,
                });

            Number const expectedShares2 = vault2.withdrawShares(Number(withdrawRequested));
            Number const expectedAssetsOut2 = vault2.withdrawAssets(expectedShares2);

            auto const [shares2, assets2] = vault2.withdraw(withdrawRequested).value();
            BEAST_EXPECT(shares2 == expectedShares2);
            BEAST_EXPECT(assets2 == expectedAssetsOut2);

            expectState(
                vault2,
                {
                    .assetsTotal = depositAmt - expectedAssetsOut2,
                    .assetsAvailable = depositAmt - borrowAmt - expectedAssetsOut2,
                    .sharesTotal = depositAmt - 1,
                    .lossUnrealized = paperLoss,
                });
        }
    }

    void
    testRedeemAll()
    {
        testcase("Redeem all shares - vault empties");

        // Redeem every outstanding share in a single call. Verifies the vault reaches
        // the empty state (assetsTotal = assetsAvailable = sharesTotal = 0).
        Vault vault{usd_};

        auto const depositAmt = 100;
        auto const depositShares = 100'000'000;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositShares,
            });

        auto const assetsOut = vault.redeem(STAmount{vault.shareAsset(), depositShares}).value();

        BEAST_EXPECT(Number(assetsOut) == depositAmt);
        expectState(vault, {.assetsTotal = 0, .assetsAvailable = 0, .sharesTotal = 0});
    }

    void
    testWithdrawAll()
    {
        testcase("Withdraw all assets by amount - vault empties");

        // Complement to testRedeemAll: uses withdraw-by-asset-amount rather than
        // redeem-by-share-count. With no outstanding loan both routes empty the vault.
        Vault vault{usd_};

        // scale=6: 100 assets -> 100 * 10^6 = 100_000_000 shares
        auto const depositAmt = 100;
        auto const depositShares = 100'000'000;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositShares,
            });

        // Withdraw the full deposit amount
        // shares = floor(100 * 100_000_000 / 100) = 100_000_000; assetsOut = 100
        auto const [burnedShares, assetsOut] = vault.withdraw(STAmount{usd_, depositAmt}).value();

        BEAST_EXPECT(Number(burnedShares) == depositShares);
        BEAST_EXPECT(Number(assetsOut) == depositAmt);
        expectState(vault, {.assetsTotal = 0, .assetsAvailable = 0, .sharesTotal = 0});
    }

    void
    testLossOnlyVault()
    {
        testcase("Vault with lossUnrealized only (no interestUnrealized)");

        // depositNAV = assetsTotal - interestUnrealized, so a paper loss alone does
        // not affect the deposit price. Verifies that a new deposit gets the full
        // proportional share count while redemption is discounted by the paper loss.
        Vault vault{usd_, 0};

        auto const depositAmt = 1000;
        auto const borrowAmt = 200;
        auto const depositAmt2 = 100;
        auto const redeemShares = 100;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        // Borrow with no interest, then mark as paper loss
        vault.borrow(borrowAmt, 0);
        vault.addPaperLoss(borrowAmt);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt - borrowAmt,
                .sharesTotal = depositAmt,
                .lossUnrealized = borrowAmt,
            });

        // Loss doesn't affect depositNAV
        auto const [depShares, depAssets] = vault.deposit(STAmount{usd_, depositAmt2}).value();
        BEAST_EXPECT(Number(depShares) == depositAmt2);
        BEAST_EXPECT(Number(depAssets) == depositAmt2);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + depositAmt2,
                .assetsAvailable = depositAmt - borrowAmt + depositAmt2,
                .sharesTotal = depositAmt + depositAmt2,
                .lossUnrealized = borrowAmt,
            });

        // Loss reduces withdrawalNAV but not depositNAV
        Number const expectedAssets = vault.redeemAssets(redeemShares);
        // Compare as STAmount: redeem() returns STAmount which truncates
        // to 16 significant digits, so comparing directly to the
        // full-precision Number would fail for non-terminating fractions.
        STAmount const expectedAmt{usd_, expectedAssets};
        auto const redeemAssets = vault.redeem(STAmount{vault.shareAsset(), redeemShares}).value();
        BEAST_EXPECT(redeemAssets == expectedAmt);

        STAmount const expectedTotal{usd_, depositAmt + depositAmt2 - expectedAssets};
        STAmount const expectedAvail{usd_, depositAmt - borrowAmt + depositAmt2 - expectedAssets};
        BEAST_EXPECT(vault.assetsTotal() == expectedTotal);
        BEAST_EXPECT(vault.assetsAvailable() == expectedAvail);
        BEAST_EXPECT(Number(vault.sharesTotal()) == depositAmt + depositAmt2 - redeemShares);
        BEAST_EXPECT(vault.lossUnrealized() == borrowAmt);
    }

    void
    testMultipleDepositorsIOU()
    {
        testcase("Multiple depositors with yield (IOU)");

        // User A deposits at par, yield accrues via repay with extra interest, then
        // user B deposits at the inflated price. Verifies each depositor redeems the
        // correct pro-rata amount of the total assets.
        Vault vault{usd_, 0};  // scale=0 for clean integer math

        auto const depositA = 100;
        auto const extraInterest = 10;

        // User A deposits 100 -> 100 shares
        auto const [sharesA, assetsA] = vault.deposit(STAmount{usd_, depositA}).value();
        BEAST_EXPECT(Number(sharesA) == depositA);
        expectState(
            vault,
            {
                .assetsTotal = depositA,
                .assetsAvailable = depositA,
                .sharesTotal = depositA,
            });

        vault.borrow(1, 0);
        expectState(
            vault,
            {
                .assetsTotal = depositA,
                .assetsAvailable = depositA - 1,
                .sharesTotal = depositA,
            });

        // Loan repaid with extra interest: assetsTotal becomes 110
        vault.repay(1, 0, extraInterest);
        expectState(
            vault,
            {
                .assetsTotal = depositA + extraInterest,
                .assetsAvailable = depositA + extraInterest,
                .sharesTotal = depositA,
            });

        // User B deposits 100: shares = floor(deposit * sharesTotal / depositNAV)
        Number const expectedSharesB = vault.depositShares(Number(depositA));
        Number const expectedAssetsB = vault.depositAssets(expectedSharesB);
        auto const [sharesB, assetsB] = vault.deposit(STAmount{usd_, depositA}).value();
        BEAST_EXPECT(sharesB == expectedSharesB);
        BEAST_EXPECT(assetsB == expectedAssetsB);
        expectState(
            vault,
            {
                // assetsTotal = 110 + 99 = 209
                .assetsTotal = depositA + extraInterest + Number(assetsB),
                .assetsAvailable = depositA + extraInterest + Number(assetsB),
                // sharesTotal = 100 + 90 = 190
                .sharesTotal = depositA + Number(sharesB),
            });

        // A redeems 100 shares: assetsOut = 100 * 209 / 190 = 110
        auto const outA = vault.redeem(STAmount{vault.shareAsset(), depositA}).value();
        BEAST_EXPECT(Number(outA) == depositA + extraInterest);
        expectState(
            vault,
            {
                .assetsTotal = depositA + extraInterest + Number(assetsB) - Number(outA),
                .assetsAvailable = depositA + extraInterest + Number(assetsB) - Number(outA),
                .sharesTotal = Number(sharesB),
            });

        // B redeems 90 shares: assetsOut = 90 * 99 / 90 = 99
        auto const outB = vault.redeem(STAmount{vault.shareAsset(), Number(sharesB)}).value();
        BEAST_EXPECT(outB == assetsB);
        expectState(vault, {.assetsTotal = 0, .assetsAvailable = 0, .sharesTotal = 0});
    }

    void
    testPrecisionLoss()
    {
        testcase("Precision loss - zero shares condition");

        // When a deposit is too small relative to the share price, floor(shares) = 0.
        // Verifies tecPRECISION_LOSS is returned and vault state is left unchanged.
        Vault vault{usd_, 0};

        auto const depositAmt = 1;

        // Initial deposit: 1 asset -> 1 share
        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        // Deposit 0.0001 assets: shares = floor(0.0001 * 1 / 1) = 0 -> tecPRECISION_LOSS
        auto const tinyResult = vault.deposit(STAmount{usd_, 1, -4});
        BEAST_EXPECT(!tinyResult);
        BEAST_EXPECT(tinyResult.error() == tecPRECISION_LOSS);

        // Vault state unchanged after failed deposit
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });
    }

    void
    testDepositFloorBoundary()
    {
        testcase("Deposit floor boundary: 0.95 rounds to 0 (fails), 1.95 rounds to 1 (succeeds)");

        // With 1 asset and 1 share at scale=0, depositNAV = sharesTotal = 1.
        // floor(0.95 * 1 / 1) = 0  -> tecPRECISION_LOSS
        // floor(1.95 * 1 / 1) = 1  -> succeeds; actualAssets = 1 (depositor keeps 0.95)
        Vault vault{usd_, 0};

        BEAST_EXPECT(vault.deposit(STAmount{usd_, 1}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = 1,
                .assetsAvailable = 1,
                .sharesTotal = 1,
            });

        // 0.95 assets: shares = floor(0.95 * 1 / 1) = 0 -> tecPRECISION_LOSS
        auto const failResult = vault.deposit(STAmount{usd_, 95, -2});
        BEAST_EXPECT(!failResult);
        BEAST_EXPECT(failResult.error() == tecPRECISION_LOSS);

        // Vault state unchanged after failed deposit
        expectState(
            vault,
            {
                .assetsTotal = 1,
                .assetsAvailable = 1,
                .sharesTotal = 1,
            });

        // 1.95 assets: shares = floor(1.95 * 1 / 1) = 1; actualAssets = 1 * 1 / 1 = 1
        // The depositor keeps the 0.95 remainder (floor invariant)
        auto const [actualShares, actualAssets] = vault.deposit(STAmount{usd_, 195, -2}).value();
        BEAST_EXPECT(Number(actualShares) == 1);
        BEAST_EXPECT(Number(actualAssets) == 1);
        expectState(
            vault,
            {
                .assetsTotal = 2,
                .assetsAvailable = 2,
                .sharesTotal = 2,
            });
    }

    void
    testXRPFullCycle()
    {
        testcase("XRP full cycle: deposit, withdraw, redeem");

        // Complete lifecycle using XRP (drops, scale=0): deposit by amount, then
        // withdraw half by asset amount, then redeem the remainder by share count.
        // Verifies vault state is correct at each step and empties to zero at the end.
        Vault vault{xrpIssue()};

        auto const depositDrops = 10'000'000;
        auto const halfDrops = 5'000'000;

        // Deposit 10 XRP = 10M drops
        auto const [shares1, assets1] = vault.deposit(STAmount{xrpIssue(), depositDrops}).value();
        BEAST_EXPECT(Number(shares1) == depositDrops);
        expectState(
            vault,
            {
                .assetsTotal = depositDrops,
                .assetsAvailable = depositDrops,
                .sharesTotal = depositDrops,
            });

        // Withdraw 5M drops
        auto const [wShares, wAssets] = vault.withdraw(STAmount{xrpIssue(), halfDrops}).value();
        BEAST_EXPECT(Number(wShares) == halfDrops);
        BEAST_EXPECT(Number(wAssets) == halfDrops);
        expectState(
            vault,
            {
                .assetsTotal = depositDrops - halfDrops,
                .assetsAvailable = depositDrops - halfDrops,
                .sharesTotal = depositDrops - halfDrops,
            });

        // Redeem remaining 5M shares
        auto const rAssets = vault.redeem(STAmount{vault.shareAsset(), halfDrops}).value();
        BEAST_EXPECT(Number(rAssets) == halfDrops);
        expectState(vault, {.assetsTotal = 0, .assetsAvailable = 0, .sharesTotal = 0});
    }

    void
    testMPTFullCycle()
    {
        testcase("MPT full cycle: deposit, withdraw, redeem");

        // Complete lifecycle using MPT tokens (scale=0): deposit by amount, then
        // withdraw part by asset amount, then redeem the remainder by share count.
        // Verifies vault state is correct at each step and empties to zero at the end.
        MPTIssue const mptAsset{makeMptID(200, AccountID(0x5678))};
        Vault vault{mptAsset};

        auto const depositAmt = 1000;
        auto const withdrawAmt = 400;

        auto const [shares1, assets1] = vault.deposit(STAmount{mptAsset, depositAmt}).value();
        BEAST_EXPECT(Number(shares1) == depositAmt);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        // Withdraw 400 tokens
        auto const [wShares, wAssets] = vault.withdraw(STAmount{mptAsset, withdrawAmt}).value();
        BEAST_EXPECT(Number(wShares) == withdrawAmt);
        BEAST_EXPECT(Number(wAssets) == withdrawAmt);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt - withdrawAmt,
                .assetsAvailable = depositAmt - withdrawAmt,
                .sharesTotal = depositAmt - withdrawAmt,
            });

        // Redeem remaining 600 shares
        auto const remaining = depositAmt - withdrawAmt;
        auto const rAssets = vault.redeem(STAmount{vault.shareAsset(), remaining}).value();
        BEAST_EXPECT(Number(rAssets) == remaining);
        expectState(vault, {.assetsTotal = 0, .assetsAvailable = 0, .sharesTotal = 0});
    }

    void
    testTinyDepositIntoLargeVault()
    {
        testcase("Tiny deposit into large vault (IOU)");

        // Verifies share precision is maintained at an extreme asset-to-deposit ratio.
        // Vault seeded with 1e9 assets (scale=6 → sharesTotal = 1e15); then a tiny
        // deposit is made and the resulting shares are redeemed back without loss.
        Vault vault{usd_};  // scale=6

        auto const seedAmt = UINT64_C(1'000'000'000);
        auto const seedShares = Number(1, 15);
        auto const tinyDeposit = Number(1, -6);

        BEAST_EXPECT(vault.deposit(STAmount{usd_, seedAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = Number(seedAmt),
                .assetsAvailable = Number(seedAmt),
                .sharesTotal = seedShares,
            });

        // shares = floor(tinyDeposit * sharesTotal / depositNAV)
        Number const expectedShares = vault.depositShares(tinyDeposit);
        Number const expectedAssets = vault.depositAssets(expectedShares);

        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{usd_, tinyDeposit}).value();

        BEAST_EXPECT(actualShares == expectedShares);
        BEAST_EXPECT(actualAssets == expectedAssets);
        expectState(
            vault,
            {
                .assetsTotal = Number(seedAmt) + expectedAssets,
                .assetsAvailable = Number(seedAmt) + expectedAssets,
                .sharesTotal = seedShares + expectedShares,
            });

        // Redeem the deposited shares back
        auto const out = vault.redeem(STAmount{vault.shareAsset(), expectedShares}).value();
        BEAST_EXPECT(out == expectedAssets);
        expectState(
            vault,
            {
                .assetsTotal = Number(seedAmt),
                .assetsAvailable = Number(seedAmt),
                .sharesTotal = seedShares,
            });
    }

    void
    testTinyDepositIntoLargeVaultXRP()
    {
        testcase("Tiny deposit into large vault (XRP)");

        // Verifies share precision is maintained at an extreme asset-to-deposit ratio
        // with XRP (scale=0). Vault seeded with 1e14 drops; then 1 drop is deposited
        // and the resulting shares are redeemed back without loss.
        Vault vault{xrpIssue()};

        auto const seedDrops = UINT64_C(100'000'000'000'000);
        auto const seedNum = Number(1, 14);

        BEAST_EXPECT(vault.deposit(STAmount{xrpIssue(), seedDrops}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = seedNum,
                .assetsAvailable = seedNum,
                .sharesTotal = seedNum,
            });

        // shares = floor(tinyDeposit * sharesTotal / depositNAV)
        auto const tinyDeposit = 1;
        Number const expectedShares = vault.depositShares(Number(tinyDeposit));
        Number const expectedAssets = vault.depositAssets(expectedShares);

        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{xrpIssue(), tinyDeposit}).value();

        BEAST_EXPECT(actualShares == expectedShares);
        BEAST_EXPECT(actualAssets == expectedAssets);
        expectState(
            vault,
            {
                .assetsTotal = seedNum + expectedAssets,
                .assetsAvailable = seedNum + expectedAssets,
                .sharesTotal = seedNum + expectedShares,
            });

        // Redeem the deposited shares back
        auto const out = vault.redeem(STAmount{vault.shareAsset(), expectedShares}).value();
        BEAST_EXPECT(out == expectedAssets);
        expectState(
            vault,
            {
                .assetsTotal = seedNum,
                .assetsAvailable = seedNum,
                .sharesTotal = seedNum,
            });
    }

    void
    testLargeDepositIntoTinyVault()
    {
        testcase("Large deposit into tiny vault (IOU)");

        // Verifies proportional share allocation when a large deposit dwarfs the
        // existing vault. Vault seeded with 0.001 assets (scale=6 → 1000 shares);
        // then 1e9 assets are deposited and the original seed shares are redeemed.
        Vault vault{usd_};

        auto const seedAssets = Number(1, -3);
        auto const seedShares = 1000;
        auto const largeDeposit = Number(1, 9);

        BEAST_EXPECT(vault.deposit(STAmount{usd_, seedAssets}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = seedAssets,
                .assetsAvailable = seedAssets,
                .sharesTotal = seedShares,
            });

        // shares = floor(largeDeposit * sharesTotal / depositNAV)
        Number const expectedShares = vault.depositShares(largeDeposit);
        Number const expectedAssets = vault.depositAssets(expectedShares);

        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{usd_, largeDeposit}).value();

        BEAST_EXPECT(actualShares == expectedShares);
        BEAST_EXPECT(actualAssets == expectedAssets);
        expectState(
            vault,
            {
                .assetsTotal = seedAssets + expectedAssets,
                .assetsAvailable = seedAssets + expectedAssets,
                .sharesTotal = seedShares + expectedShares,
            });

        // Redeem original seed shares
        Number const expectedRedeem = vault.redeemAssets(Number(seedShares));
        auto const out = vault.redeem(STAmount{vault.shareAsset(), seedShares}).value();
        BEAST_EXPECT(out == expectedRedeem);
    }

    void
    testLargeDepositIntoTinyVaultXRP()
    {
        testcase("Large deposit into tiny vault (XRP)");

        // Verifies proportional share allocation with XRP at a 1:1e14 size ratio.
        // Vault seeded with 1 drop (scale=0 → 1 share); then 1e14 drops are deposited
        // and the original 1-drop seed share is redeemed at its exact proportional value.
        Vault vault{xrpIssue()};

        auto const seedAmt = 1;
        auto const largeDeposit = Number(1, 14);

        BEAST_EXPECT(vault.deposit(STAmount{xrpIssue(), seedAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });

        // Massive deposit: 1e14 drops (100M XRP)
        // shares = floor(1e14 * 1 / 1) = 1e14
        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{xrpIssue(), UINT64_C(100'000'000'000'000)}).value();

        BEAST_EXPECT(actualShares == largeDeposit);
        BEAST_EXPECT(actualAssets == largeDeposit);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt + largeDeposit,
                .assetsAvailable = seedAmt + largeDeposit,
                .sharesTotal = seedAmt + largeDeposit,
            });

        // Redeem original 1 share: assetsOut = 1 * (1e14+1) / (1e14+1) = 1
        auto const out = vault.redeem(STAmount{vault.shareAsset(), seedAmt}).value();
        BEAST_EXPECT(Number(out) == seedAmt);
        expectState(
            vault,
            {
                .assetsTotal = largeDeposit,
                .assetsAvailable = largeDeposit,
                .sharesTotal = largeDeposit,
            });
    }

    void
    testHighPrecisionWithInterest()
    {
        testcase("High precision deposit/redeem with unrealized interest");

        // Tests the full loan lifecycle at large scale:
        // issue loan -> tiny depositor joins -> loan repaid -> depositor redeems
        Vault vault{usd_, 0};  // scale=0

        auto const seedAmt = Number(1, 12);
        auto const principal = Number(1, 11);
        auto const interest = Number(5, 11);
        auto const tinyDeposit = 1;

        // Seed: 1e12 assets -> 1e12 shares
        BEAST_EXPECT(vault.deposit(STAmount{usd_, UINT64_C(1'000'000'000'000)}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });

        // Borrow 1e11 principal with 5e11 interest
        vault.borrow(principal, interest);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt + interest,
                .assetsAvailable = seedAmt - principal,
                .sharesTotal = seedAmt,
                .interestOutstanding = interest,
            });

        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{usd_, tinyDeposit}).value();
        BEAST_EXPECT(Number(actualShares) == tinyDeposit);
        BEAST_EXPECT(Number(actualAssets) == tinyDeposit);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt + interest + tinyDeposit,
                .assetsAvailable = seedAmt - principal + tinyDeposit,
                .sharesTotal = seedAmt + tinyDeposit,
                .interestOutstanding = interest,
            });

        // Loan repaid: interestUnrealized -> 0, assetsAvailable fully restored
        vault.repay(principal, interest);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt + interest + tinyDeposit,
                .assetsAvailable = seedAmt + interest + tinyDeposit,
                .sharesTotal = seedAmt + tinyDeposit,
            });

        Number const expectedOut = vault.redeemAssets(Number(tinyDeposit));
        STAmount const expectedAmt(usd_, expectedOut);
        auto const out = vault.redeem(STAmount{vault.shareAsset(), tinyDeposit}).value();
        BEAST_EXPECT(out == expectedAmt);

        expectState(
            vault,
            {
                .assetsTotal = seedAmt + interest + tinyDeposit - expectedOut,
                .assetsAvailable = seedAmt + interest + tinyDeposit - expectedOut,
                .sharesTotal = seedAmt,
            });
    }

    void
    testHighPrecisionWithLoss()
    {
        testcase("High precision redeem with unrealized loss");

        // Large vault with loss: tests that a tiny share redemption
        // correctly accounts for the discounted withdrawalNAV
        Vault vault{usd_, 0};

        auto const seedAmt = Number(1, 12);
        auto const borrowAmt = Number(2, 11);
        auto const redeemShares = 1;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, UINT64_C(1'000'000'000'000)}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });

        // Borrow 2e11 (no interest) so paper loss is backed by loan principal
        vault.borrow(borrowAmt, 0);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt - borrowAmt,
                .sharesTotal = seedAmt,
            });

        // 20% paper loss
        vault.addPaperLoss(borrowAmt);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt - borrowAmt,
                .sharesTotal = seedAmt,
                .lossUnrealized = borrowAmt,
            });

        Number const expectedOut = vault.redeemAssets(redeemShares);
        STAmount const expectedAmt(usd_, expectedOut);
        auto const out = vault.redeem(STAmount{vault.shareAsset(), redeemShares}).value();
        BEAST_EXPECT(out == expectedAmt);

        expectState(
            vault,
            {
                .assetsTotal = seedAmt - expectedOut,
                .assetsAvailable = seedAmt - borrowAmt - expectedOut,
                .sharesTotal = seedAmt - redeemShares,
                .lossUnrealized = borrowAmt,
            });
    }

    void
    testManyTinyDepositsLargeVault()
    {
        testcase("Many tiny deposits into large vault then full redeem");

        // One hundred sequential 1-asset deposits into a 1e9-asset vault. Verifies
        // that each tiny deposit receives exactly 1 share (floor holds) and that the
        // accumulated shares can be bulk-redeemed for the exact deposited amount.
        Vault vault{usd_, 0};  // scale=0

        auto const seedAmt = Number(1, 9);
        auto const numDeposits = 100;
        auto const tinyAmt = 1;

        // Seed with 1e9 assets
        BEAST_EXPECT(vault.deposit(STAmount{usd_, UINT64_C(1'000'000'000)}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });

        Number totalTinyShares{0};
        Number totalTinyAssets{0};

        // 100 tiny deposits of 1 asset each
        for (int i = 0; i < numDeposits; ++i)
        {
            auto const [s, a] = vault.deposit(STAmount{usd_, tinyAmt}).value();
            totalTinyShares += Number(s);
            totalTinyAssets += Number(a);
        }

        // Each deposit: shares = floor(1 * sharesTotal / assetsTotal)
        // First tiny: floor(1 * 1e9 / 1e9) = 1 share per deposit
        BEAST_EXPECT(totalTinyShares == numDeposits);
        BEAST_EXPECT(totalTinyAssets == numDeposits);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt + numDeposits,
                .assetsAvailable = seedAmt + numDeposits,
                .sharesTotal = seedAmt + numDeposits,
            });

        // Redeem all 100 tiny shares
        auto const out = vault.redeem(STAmount{vault.shareAsset(), Number(numDeposits)}).value();
        // assetsOut = 100 * (1e9 + 100) / (1e9 + 100) = 100
        BEAST_EXPECT(Number(out) == numDeposits);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });
    }

    void
    testExtremeLiquidityRatioIOU()
    {
        testcase("Extreme liquidity ratio - 1e15 to 1 (IOU)");

        // IOU vault at maximum practical size (1e15 assets, scale=0): deposit then
        // redeem 1 unit, verifying no rounding error at the extreme liquidity ratio.
        Vault vault{usd_, 0};

        auto const seedAmt = Number(1, 15);
        auto const tinyDeposit = 1;

        // 1e15 assets -> 1e15 shares
        BEAST_EXPECT(vault.deposit(STAmount{usd_, UINT64_C(1'000'000'000'000'000)}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });

        // Deposit 1 asset: shares = floor(1 * 1e15 / 1e15) = 1
        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{usd_, tinyDeposit}).value();
        BEAST_EXPECT(Number(actualShares) == tinyDeposit);
        BEAST_EXPECT(Number(actualAssets) == tinyDeposit);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt + tinyDeposit,
                .assetsAvailable = seedAmt + tinyDeposit,
                .sharesTotal = seedAmt + tinyDeposit,
            });

        // Redeem that 1 share
        auto const out = vault.redeem(STAmount{vault.shareAsset(), tinyDeposit}).value();
        BEAST_EXPECT(Number(out) == tinyDeposit);

        // Vault should be back to exactly 1e15
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });
    }

    void
    testExtremeLiquidityRatioMPT()
    {
        testcase("Extreme liquidity ratio - 1e15 to 1 (MPT)");

        // MPT vault at maximum practical size (1e15 tokens, scale=0): deposit then
        // redeem 1 token, verifying no rounding error at the extreme liquidity ratio.
        MPTIssue const mptAsset{makeMptID(300, AccountID(0x5678))};
        Vault vault{mptAsset};

        auto const seedAmt = Number(1, 15);
        auto const tinyDeposit = 1;

        BEAST_EXPECT(
            vault.deposit(STAmount{mptAsset, UINT64_C(1'000'000'000'000'000)}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });

        // Deposit 1 token: shares = floor(1 * 1e15 / 1e15) = 1
        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{mptAsset, tinyDeposit}).value();
        BEAST_EXPECT(Number(actualShares) == tinyDeposit);
        BEAST_EXPECT(Number(actualAssets) == tinyDeposit);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt + tinyDeposit,
                .assetsAvailable = seedAmt + tinyDeposit,
                .sharesTotal = seedAmt + tinyDeposit,
            });

        // Redeem that 1 share
        auto const out = vault.redeem(STAmount{vault.shareAsset(), tinyDeposit}).value();
        BEAST_EXPECT(Number(out) == tinyDeposit);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });
    }

    void
    testDefaultLoanHard()
    {
        testcase("Default loan (hard) - assetsTotal written down directly");

        // A hard default permanently reduces assetsTotal and interestUnrealized.
        // No lossUnrealized is created; the loss is immediately socialized.
        Vault vault{usd_, 0};

        auto const depositAmt = 1000;
        auto const principal = 400;
        auto const interest = 100;
        auto const redeemShares = 500;

        // Seed: 1000 assets, 1000 shares
        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        // Issue loan: principal=400, interest=100
        vault.borrow(principal, interest);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
            });

        // Hard default the full loan (isPaperLoss=false)
        // assetsTotal -= principal + interest; interestUnrealized -= interest
        vault.defaultLoan(principal, interest, false);
        expectState(
            vault,
            {
                // assetsTotal = 1000 + 100 - (400 + 100) = 600
                .assetsTotal = depositAmt - principal,
                // assetsAvailable = 600 (loan was already removed from available via borrow)
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
            });

        Number const expectedOut = vault.redeemAssets(redeemShares);
        auto const out = vault.redeem(STAmount{vault.shareAsset(), redeemShares}).value();
        BEAST_EXPECT(out == expectedOut);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt - principal - expectedOut,
                .assetsAvailable = depositAmt - principal - expectedOut,
                .sharesTotal = depositAmt - redeemShares,
            });
    }

    void
    testDefaultLoanPaperToReal()
    {
        testcase("Default loan (paper-to-real) - pre-announced loss confirmed as hard default");

        // A loan is pre-announced as paper loss via addPaperLoss, then confirmed
        // as a hard default via defaultLoan(hasPaperLoss=true).
        Vault vault{usd_, 0};

        auto const depositAmt = 1000;
        auto const principal = 400;
        auto const interest = 100;
        auto const redeemShares = 500;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        vault.borrow(principal, interest);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
            });

        // Pre-announce the full loan as paper loss
        vault.addPaperLoss(principal + interest);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
                .lossUnrealized = principal + interest,
            });

        // Hard default confirmed (hasPaperLoss=true):
        // assetsTotal -= (principal+interest); interestUnrealized -= interest; lossUnrealized -=
        // (principal+interest)
        vault.defaultLoan(principal, interest, true);
        expectState(
            vault,
            {
                // assetsTotal = 1000 + 100 - 500 = 600
                .assetsTotal = depositAmt - principal,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
            });

        Number const expectedOut = vault.redeemAssets(redeemShares);
        auto const out = vault.redeem(STAmount{vault.shareAsset(), redeemShares}).value();
        BEAST_EXPECT(out == expectedOut);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt - principal - expectedOut,
                .assetsAvailable = depositAmt - principal - expectedOut,
                .sharesTotal = depositAmt - redeemShares,
            });
    }

    void
    testClearPaperLoss()
    {
        testcase("Clear paper loss - withdrawalNAV recovers on loan recovery");

        // Scenario: loan marked as paper loss, then partially recovered.
        // clearPaperLoss reduces lossUnrealized, restoring withdrawalNAV.
        Vault vault{usd_, 0};

        auto const depositAmt = 1000;
        auto const borrowAmt = 200;
        auto const partialRecovery = 100;
        auto const redeemShares1 = 100;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        vault.borrow(borrowAmt, 0);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt - borrowAmt,
                .sharesTotal = depositAmt,
            });

        // Mark the full 200 as paper loss
        vault.addPaperLoss(borrowAmt);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt - borrowAmt,
                .sharesTotal = depositAmt,
                .lossUnrealized = borrowAmt,
            });

        // Recovery: 100 is repaid; clear that portion of paper loss
        vault.repay(partialRecovery, 0);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt - borrowAmt + partialRecovery,
                .sharesTotal = depositAmt,
                .lossUnrealized = borrowAmt,
            });

        vault.clearPaperLoss(partialRecovery);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt - borrowAmt + partialRecovery,
                .sharesTotal = depositAmt,
                .lossUnrealized = borrowAmt - partialRecovery,
            });

        Number const expectedOut1 = vault.redeemAssets(redeemShares1);
        auto const out = vault.redeem(STAmount{vault.shareAsset(), redeemShares1}).value();
        BEAST_EXPECT(out == expectedOut1);

        Number const outNum1 = Number(out);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt - outNum1,
                .assetsAvailable = depositAmt - borrowAmt + partialRecovery - outNum1,
                .sharesTotal = depositAmt - redeemShares1,
                .lossUnrealized = borrowAmt - partialRecovery,
            });

        // Full recovery: clear remaining 100 paper loss
        auto const remainingLoan = borrowAmt - partialRecovery;
        vault.repay(remainingLoan, 0);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt - outNum1,
                .assetsAvailable = depositAmt - outNum1,
                .sharesTotal = depositAmt - redeemShares1,
                .lossUnrealized = remainingLoan,
            });

        vault.clearPaperLoss(remainingLoan);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt - outNum1,
                .assetsAvailable = depositAmt - outNum1,
                .sharesTotal = depositAmt - redeemShares1,
            });

        // Redeem 100 shares: assetsOut = 100 * withdrawalNAV / sharesTotal
        auto const redeemShares2 = 100;
        Number const expectedOut2 = vault.redeemAssets(redeemShares2);
        STAmount const expectedAmt2(usd_, expectedOut2);
        auto const out2 = vault.redeem(STAmount{vault.shareAsset(), redeemShares2}).value();
        BEAST_EXPECT(out2 == expectedAmt2);

        Number const outNum2 = Number(out2);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt - outNum1 - outNum2,
                .assetsAvailable = depositAmt - outNum1 - outNum2,
                .sharesTotal = depositAmt - redeemShares1 - redeemShares2,
            });
    }

    void
    testWithdrawPrecisionLoss()
    {
        testcase("Withdraw precision loss - zero shares condition");

        // A withdrawal so small that the share calculation rounds to 0
        // represents tecPRECISION_LOSS in the real implementation.
        Vault vault{usd_, 0};

        auto const depositAmt = 1;
        STAmount const tinyWithdraw{usd_, 1, -4};  // 0.0001

        // 1 asset, 1 share -> withdrawalNAV = 1
        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        // Too small to produce any shares -> tecPRECISION_LOSS
        auto const result = vault.withdraw(tinyWithdraw);
        BEAST_EXPECT(!result);
        BEAST_EXPECT(result.error() == tecPRECISION_LOSS);

        // Vault state unchanged after failed withdraws
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });
    }

    void
    testRedeemZeroShares()
    {
        testcase("Redeem zero shares - guard rejects non-positive share amount");

        // redeem() rejects zero (or negative) share amounts with tecPRECISION_LOSS.
        Vault vault{usd_, 0};

        auto const depositAmt = 100;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        // Redeem zero shares
        auto const result = vault.redeem(STAmount{vault.shareAsset(), 0});
        BEAST_EXPECT(!result);
        BEAST_EXPECT(result.error() == tecPRECISION_LOSS);

        // Vault state must be unchanged
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });
    }

    void
    testRedeemInsufficientFunds()
    {
        testcase("Redeem/withdraw with outstanding loan - tecINSUFFICIENT_FUNDS");

        // When a loan is outstanding, assetsAvailable < assetsTotal.
        // A redemption that would require more than assetsAvailable must be rejected.

        // --- redeem() ---
        {
            auto const depositAmt = 1000;
            auto const borrowAmt = 800;

            Vault vault{usd_, 0};
            BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt,
                    .sharesTotal = depositAmt,
                });

            // Borrow 800: assetsAvailable = 200, assetsTotal = 1000
            vault.borrow(borrowAmt, 0);
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt - borrowAmt,
                    .sharesTotal = depositAmt,
                });

            // Redeem 500 shares: assetsOut = 500 > assetsAvailable (200)
            auto const result = vault.redeem(STAmount{vault.shareAsset(), 500});
            BEAST_EXPECT(!result);
            BEAST_EXPECT(result.error() == tecINSUFFICIENT_FUNDS);

            // Vault state unchanged
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt - borrowAmt,
                    .sharesTotal = depositAmt,
                });

            // Redeeming exactly assetsAvailable (200 shares -> 200 assets) succeeds
            auto const ok = vault.redeem(STAmount{vault.shareAsset(), depositAmt - borrowAmt});
            BEAST_EXPECT(ok.has_value());
            BEAST_EXPECT(Number(ok.value()) == depositAmt - borrowAmt);
            expectState(
                vault,
                {
                    .assetsTotal = borrowAmt,
                    .assetsAvailable = 0,
                    .sharesTotal = borrowAmt,
                });
        }

        // --- withdraw() ---
        {
            auto const depositAmt = 1000;
            auto const borrowAmt = 800;

            Vault vault{usd_, 0};
            BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt,
                    .sharesTotal = depositAmt,
                });

            vault.borrow(borrowAmt, 0);
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt - borrowAmt,
                    .sharesTotal = depositAmt,
                });

            // Withdraw 500 assets: assetsOut = 500 > assetsAvailable (200)
            auto const result = vault.withdraw(STAmount{usd_, 500});
            BEAST_EXPECT(!result);
            BEAST_EXPECT(result.error() == tecINSUFFICIENT_FUNDS);

            // Vault state unchanged
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt - borrowAmt,
                    .sharesTotal = depositAmt,
                });

            // Withdraw exactly assetsAvailable succeeds
            auto const ok = vault.withdraw(STAmount{usd_, depositAmt - borrowAmt});
            BEAST_EXPECT(ok.has_value());
            expectState(
                vault,
                {
                    .assetsTotal = borrowAmt,
                    .assetsAvailable = 0,
                    .sharesTotal = borrowAmt,
                });
        }
    }

    void
    testRedepositAmtAfterFullDrain()
    {
        testcase("Deposit after full vault drain (re-seeding)");

        // After all shares are redeemed the vault returns to the empty state.
        // The next deposit should use the initial seeding formula (shares = assets * 10^scale).
        Vault vault{usd_, 0};

        auto const firstDeposit = 100;
        auto const secondDeposit = 200;

        // First life: deposit 100, redeem all
        BEAST_EXPECT(vault.deposit(STAmount{usd_, firstDeposit}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = firstDeposit,
                .assetsAvailable = firstDeposit,
                .sharesTotal = firstDeposit,
            });

        BEAST_EXPECT(vault.redeem(STAmount{vault.shareAsset(), firstDeposit}).has_value());
        expectState(vault, {.assetsTotal = 0, .assetsAvailable = 0, .sharesTotal = 0});

        // Re-seed: should behave like initial deposit
        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{usd_, secondDeposit}).value();
        BEAST_EXPECT(Number(actualShares) == secondDeposit);
        BEAST_EXPECT(Number(actualAssets) == secondDeposit);
        expectState(
            vault,
            {
                .assetsTotal = secondDeposit,
                .assetsAvailable = secondDeposit,
                .sharesTotal = secondDeposit,
            });
    }

    void
    testAssetsAvailableBorrowRepay()
    {
        testcase("assetsAvailable tracked correctly through borrow/repay");

        // Multiple borrow/repay cycles with varying principal, interest, and extra
        // interest. Verifies assetsAvailable increases by principal + interest on repay,
        // and that extra interest accrues to both assetsTotal and assetsAvailable.
        Vault vault{usd_, 0};

        auto const depositAmt = 1000;
        auto const principal1 = 500;
        auto const interest1 = 50;
        auto const principal2 = 200;
        auto const extraInterest = 30;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        // Borrow 500 principal with 50 interest
        // assetsAvailable decreases by principal only (not by interest)
        vault.borrow(principal1, interest1);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest1,
                .assetsAvailable = depositAmt - principal1,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest1,
            });

        // Repay 500 principal + 50 interest: assetsAvailable fully restored
        vault.repay(principal1, interest1);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest1,
                .assetsAvailable = depositAmt + interest1,
                .sharesTotal = depositAmt,
            });

        // Borrow again then repay with extra interest
        vault.borrow(principal2, 0);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest1,
                .assetsAvailable = depositAmt + interest1 - principal2,
                .sharesTotal = depositAmt,
            });

        // repay 200 + 0 normal interest + 30 extra
        vault.repay(principal2, 0, extraInterest);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest1 + extraInterest,
                .assetsAvailable = depositAmt + interest1 + extraInterest,
                .sharesTotal = depositAmt,
            });
    }

    void
    testLossDistributionMultipleDepositors()
    {
        testcase("Loss distribution across multiple depositors");

        // User A deposits before loss is marked.
        // User B deposits after (at full depositNAV, unaffected by lossUnrealized).
        // On redemption both bear the loss proportionally to their share count.
        Vault vault{usd_, 0};

        auto const depositAmtA = 1000;
        auto const borrowAmt = 200;
        auto const depositB = 500;

        // User A: 1000 assets -> 1000 shares
        auto const [sharesA, assetsA] = vault.deposit(STAmount{usd_, depositAmtA}).value();
        BEAST_EXPECT(Number(sharesA) == depositAmtA);
        expectState(
            vault,
            {
                .assetsTotal = depositAmtA,
                .assetsAvailable = depositAmtA,
                .sharesTotal = depositAmtA,
            });

        // Issue loan: borrow 200 principal (no interest) to back the paper loss
        vault.borrow(borrowAmt, 0);
        expectState(
            vault,
            {
                .assetsTotal = depositAmtA,
                .assetsAvailable = depositAmtA - borrowAmt,
                .sharesTotal = depositAmtA,
            });

        // Mark 200 as paper loss
        vault.addPaperLoss(borrowAmt);
        expectState(
            vault,
            {
                .assetsTotal = depositAmtA,
                .assetsAvailable = depositAmtA - borrowAmt,
                .sharesTotal = depositAmtA,
                .lossUnrealized = borrowAmt,
            });

        // User B deposits 500: depositNAV=1000, shares=floor(500*1000/1000)=500
        auto const [sharesB, assetsB] = vault.deposit(STAmount{usd_, depositB}).value();
        BEAST_EXPECT(Number(sharesB) == depositB);
        BEAST_EXPECT(Number(assetsB) == depositB);
        expectState(
            vault,
            {
                .assetsTotal = depositAmtA + depositB,
                .assetsAvailable = depositAmtA - borrowAmt + depositB,
                .sharesTotal = depositAmtA + depositB,
                .lossUnrealized = borrowAmt,
            });

        // A redeems 1000 shares: assetsOut = 1000 * withdrawalNAV / sharesTotal
        Number const expectedA = vault.redeemAssets(Number(depositAmtA));
        STAmount const expectedAmtA(usd_, expectedA);
        auto const outA = vault.redeem(STAmount{vault.shareAsset(), depositAmtA}).value();
        BEAST_EXPECT(outA == expectedAmtA);

        Number const outANum = Number(outA);
        expectState(
            vault,
            {
                .assetsTotal = depositAmtA + depositB - outANum,
                .assetsAvailable = depositAmtA - borrowAmt + depositB - outANum,
                .sharesTotal = depositB,
                .lossUnrealized = borrowAmt,
            });

        // B redeems all remaining shares
        Number const expectedB = vault.redeemAssets(Number(depositB));
        STAmount const expectedAmtB(usd_, expectedB);
        auto const outB = vault.redeem(STAmount{vault.shareAsset(), depositB}).value();
        BEAST_EXPECT(outB == expectedAmtB);
    }

    void
    testNAVAsymmetryExplicit()
    {
        testcase("Explicit NAV asymmetry: deposit at depositNAV, redeem at withdrawalNAV");

        // This test directly verifies the spec's core design intent:
        // depositNAV = assetsTotal - interestUnrealized  (does NOT subtract lossUnrealized)
        // withdrawalNAV = assetsTotal - interestUnrealized - lossUnrealized
        Vault vault{usd_, 0};

        auto const depositAmt = 1000;
        auto const principal = 400;
        auto const interest = 100;
        auto const paperLoss = 150;
        auto const newDeposit = 100;
        auto const redeemShares = 100;

        // Seed: 1000 assets, 1000 shares
        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        // Create asymmetry: interestUnrealized=100, lossUnrealized=150
        vault.borrow(principal, interest);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
            });

        vault.addPaperLoss(paperLoss);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
                .lossUnrealized = paperLoss,
            });

        // New depositor: shares = floor(newDeposit * sharesTotal / depositNAV)
        Number const expectedNewShares = vault.depositShares(Number(newDeposit));
        Number const expectedNewAssets = vault.depositAssets(expectedNewShares);

        auto const [newShares, newAssets] = vault.deposit(STAmount{usd_, newDeposit}).value();
        BEAST_EXPECT(newShares == expectedNewShares);
        BEAST_EXPECT(newAssets == expectedNewAssets);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest + expectedNewAssets,
                .assetsAvailable = depositAmt - principal + expectedNewAssets,
                .sharesTotal = depositAmt + expectedNewShares,
                .interestOutstanding = interest,
                .lossUnrealized = paperLoss,
            });

        // Existing holder redeems shares using withdrawalNAV (lower than depositNAV)
        Number const expectedRedeemAssets = vault.redeemAssets(Number(redeemShares));
        STAmount const expectedRedeemAmt(usd_, expectedRedeemAssets);
        auto const redeemOut = vault.redeem(STAmount{vault.shareAsset(), redeemShares}).value();
        BEAST_EXPECT(redeemOut == expectedRedeemAmt);

        Number const totalNum = Number(
            STAmount{usd_, depositAmt + interest + expectedNewAssets - expectedRedeemAssets});
        Number const availNum = Number(
            STAmount{usd_, depositAmt - principal + expectedNewAssets - expectedRedeemAssets});
        expectState(
            vault,
            {
                .assetsTotal = totalNum,
                .assetsAvailable = availNum,
                .sharesTotal = depositAmt + expectedNewShares - redeemShares,
                .interestOutstanding = interest,
                .lossUnrealized = paperLoss,
            });

        // New depositor paid more per share than redeemer received --
        // this asymmetry discourages bank runs: early exiters bear losses.
        BEAST_EXPECT(redeemOut < newAssets);
    }

    void
    testNonDefaultScale()
    {
        testcase("Non-default IOU scale values (scale=2 and scale=18)");

        // Verifies the scale multiplier is applied correctly for both initial and
        // subsequent deposits across two non-default IOU scales.

        // scale=2: initial deposit gives assets * 10^2 shares
        {
            Vault vault{usd_, 2};

            auto const depositAmt = 50;
            auto const expectedShares = 5000;  // 50 * 10^2
            auto const depositAmt2 = 25;
            auto const expectedShares2 = 2500;  // 25 * 10^2
            auto const redeemShares = 5000;

            auto const [actualShares, actualAssets] =
                vault.deposit(STAmount{usd_, depositAmt}).value();
            BEAST_EXPECT(Number(actualShares) == expectedShares);
            BEAST_EXPECT(Number(actualAssets) == depositAmt);
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt,
                    .sharesTotal = expectedShares,
                });

            // Subsequent deposit: proportional
            auto const [shares2, assets2] = vault.deposit(STAmount{usd_, depositAmt2}).value();
            BEAST_EXPECT(Number(shares2) == expectedShares2);
            BEAST_EXPECT(Number(assets2) == depositAmt2);
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt + depositAmt2,
                    .assetsAvailable = depositAmt + depositAmt2,
                    .sharesTotal = expectedShares + expectedShares2,
                });

            // Redeem 5000 shares: assetsOut = 5000 * 75 / 7500 = 50
            auto const out = vault.redeem(STAmount{vault.shareAsset(), redeemShares}).value();
            BEAST_EXPECT(Number(out) == depositAmt);
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt2,
                    .assetsAvailable = depositAmt2,
                    .sharesTotal = expectedShares2,
                });
        }

        // scale=18: maximum, initial deposit gives assets * 10^18 shares
        {
            Vault vault{usd_, 18};

            auto const depositAmt = 1;
            auto const expectedShares = Number(1, 18);

            // Deposit 1 asset -> 10^18 shares (= 1e18)
            auto const [actualShares, actualAssets] =
                vault.deposit(STAmount{usd_, depositAmt}).value();
            BEAST_EXPECT(actualShares == expectedShares);
            BEAST_EXPECT(Number(actualAssets) == depositAmt);
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt,
                    .sharesTotal = expectedShares,
                });

            // Subsequent deposit of 1 asset: shares = floor(1 * 1e18 / 1) = 1e18
            auto const [shares2, assets2] = vault.deposit(STAmount{usd_, depositAmt}).value();
            BEAST_EXPECT(shares2 == expectedShares);
            BEAST_EXPECT(Number(assets2) == depositAmt);
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt * 2,
                    .assetsAvailable = depositAmt * 2,
                    .sharesTotal = expectedShares * 2,
                });

            // Redeem 1e18 shares: assetsOut = 1e18 * 2 / 2e18 = 1
            auto const out = vault.redeem(STAmount{vault.shareAsset(), expectedShares}).value();
            BEAST_EXPECT(Number(out) == depositAmt);
            expectState(
                vault,
                {
                    .assetsTotal = depositAmt,
                    .assetsAvailable = depositAmt,
                    .sharesTotal = expectedShares,
                });
        }
    }

    void
    testPaperLossThenActualDefault()
    {
        testcase("Pre-announced paper loss followed by actual default");

        // A loan is first flagged as a paper loss via addPaperLoss, then later
        // confirmed as a hard default via defaultLoan(hasPaperLoss=true).
        Vault vault{usd_, 0};

        auto const depositAmt = 1000;
        auto const principal = 300;
        auto const interest = 30;
        auto const intermediateRedeem = 100;

        // Seed: 1000 assets, 1000 shares
        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        // Issue loan: principal=300, interest=30
        vault.borrow(principal, interest);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
            });

        // Step 1: Pre-announce as paper loss (lossUnrealized = principal + interest = 330)
        vault.addPaperLoss(principal + interest);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest,
                .assetsAvailable = depositAmt - principal,
                .sharesTotal = depositAmt,
                .interestOutstanding = interest,
                .lossUnrealized = principal + interest,
            });

        // Intermediate redeem to confirm discounted NAV is applied
        Number const expectedBefore = vault.redeemAssets(intermediateRedeem);
        auto const outBefore =
            vault.redeem(STAmount{vault.shareAsset(), intermediateRedeem}).value();
        BEAST_EXPECT(outBefore == expectedBefore);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt + interest - expectedBefore,
                .assetsAvailable = depositAmt - principal - expectedBefore,
                .sharesTotal = depositAmt - intermediateRedeem,
                .interestOutstanding = interest,
                .lossUnrealized = principal + interest,
            });

        // Step 2: Loan confirmed as hard default — clears interest and loss
        // defaultLoan subtracts (principal+interest) from assetsTotal
        Number const expectedTotalAfterDefault =
            Number(vault.assetsTotal()) - (principal + interest);
        vault.defaultLoan(principal, interest, true);

        expectState(
            vault,
            {
                .assetsTotal = expectedTotalAfterDefault,
                .assetsAvailable = expectedTotalAfterDefault,
                .sharesTotal = depositAmt - intermediateRedeem,
            });

        // Redeem all remaining shares
        auto const remainingShares = depositAmt - intermediateRedeem;
        Number const expectedAfter = vault.redeemAssets(remainingShares);
        auto const outAfter =
            vault.redeem(STAmount{vault.shareAsset(), Number(remainingShares)}).value();
        BEAST_EXPECT(outAfter == expectedAfter);
        expectState(vault, {.assetsTotal = 0, .assetsAvailable = 0, .sharesTotal = 0});
    }

    void
    testDepositZeroSharesLargeVault()
    {
        testcase("Deposit zero shares - vault too large relative to deposit");

        // In a large IOU vault (scale=6), a deposit that is too small relative to the
        // vault's total assets produces floor(shares) = 0, triggering tecPRECISION_LOSS.

        auto const seedAmt = UINT64_C(1'000'000'000);
        auto const seedNum = Number(1, 9);
        auto const seedShares = Number(1, 15);

        // Case 1: 5e-7 -> rawShares = 5e-7 * 1e15 / 1e9 = 0.5 -> floor = 0 -> tecPRECISION_LOSS
        {
            Vault vault{usd_};  // scale=6
            BEAST_EXPECT(vault.deposit(STAmount{usd_, seedAmt}).has_value());
            expectState(
                vault,
                {
                    .assetsTotal = seedNum,
                    .assetsAvailable = seedNum,
                    .sharesTotal = seedShares,
                });

            auto const tinyResult = vault.deposit(STAmount{usd_, 5, -7});
            BEAST_EXPECT(!tinyResult);
            BEAST_EXPECT(tinyResult.error() == tecPRECISION_LOSS);

            // Vault state unchanged after failed deposit
            expectState(
                vault,
                {
                    .assetsTotal = seedNum,
                    .assetsAvailable = seedNum,
                    .sharesTotal = seedShares,
                });
        }

        // Case 2: boundary testing
        {
            Vault vault{usd_};  // scale=6
            BEAST_EXPECT(vault.deposit(STAmount{usd_, seedAmt}).has_value());
            expectState(
                vault,
                {
                    .assetsTotal = seedNum,
                    .assetsAvailable = seedNum,
                    .sharesTotal = seedShares,
                });

            // Just below threshold: 9e-7 -> rawShares = 0.9 -> floor = 0 -> tecPRECISION_LOSS
            STAmount const justBelow{usd_, 9, -7};
            auto const belowResult = vault.deposit(justBelow);
            BEAST_EXPECT(!belowResult);
            BEAST_EXPECT(belowResult.error() == tecPRECISION_LOSS);

            // Vault state unchanged after failed deposit
            expectState(
                vault,
                {
                    .assetsTotal = seedNum,
                    .assetsAvailable = seedNum,
                    .sharesTotal = seedShares,
                });

            // At threshold: 1e-6 -> exactly 1 share (deposit succeeds)
            STAmount const atThreshold{usd_, 1, -6};
            Number const expectedShares = vault.depositShares(Number(atThreshold));
            Number const expectedAssets = vault.depositAssets(expectedShares);

            auto const [sharesAt, assetsAt] = vault.deposit(atThreshold).value();
            BEAST_EXPECT(sharesAt == expectedShares);
            BEAST_EXPECT(assetsAt == expectedAssets);
            expectState(
                vault,
                {
                    .assetsTotal = seedNum + expectedAssets,
                    .assetsAvailable = seedNum + expectedAssets,
                    .sharesTotal = seedShares + expectedShares,
                });
        }
    }

    void
    testWithdrawAssetsOutLERequested()
    {
        testcase("Withdraw floor: assetsOut <= requested (spec-mandated invariant)");

        // When floor(requested * S / withdrawalNAV) = 3 but requested * S / withdrawalNAV = 3.75,
        // assetsOut = 3 * withdrawalNAV / S = 2.4 < 3.
        Vault vault{usd_, 0};

        auto const depositAmt = 10;
        auto const borrowAmt = 2;
        auto const paperLoss = 2;
        auto const withdrawRequested = 3;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        // Borrow 2 (no interest) so paper loss is backed by loan principal
        vault.borrow(borrowAmt, 0);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt - borrowAmt,
                .sharesTotal = depositAmt,
            });

        // lossUnrealized=2: withdrawalNAV = 10 - 0 - 2 = 8
        vault.addPaperLoss(paperLoss);
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt - borrowAmt,
                .sharesTotal = depositAmt,
                .lossUnrealized = paperLoss,
            });

        // shares = floor(requested * sharesTotal / withdrawalNAV)
        Number const expectedShares = vault.withdrawShares(Number(withdrawRequested));
        // assetsOut = shares * withdrawalNAV / sharesTotal
        Number const expectedAssetsOut = vault.withdrawAssets(expectedShares);

        auto const [actualShares, actualAssets] =
            vault.withdraw(STAmount{usd_, withdrawRequested}).value();
        BEAST_EXPECT(actualShares == expectedShares);

        STAmount const expectedAssetsAmt{usd_, expectedAssetsOut};
        BEAST_EXPECT(actualAssets == expectedAssetsAmt);

        // assetsOut strictly less than requested (floor discards fractional share)
        BEAST_EXPECT(Number(actualAssets) < withdrawRequested);

        expectState(
            vault,
            {
                .assetsTotal = depositAmt - expectedAssetsOut,
                .assetsAvailable = depositAmt - borrowAmt - expectedAssetsOut,
                .sharesTotal = depositAmt - expectedShares,
                .lossUnrealized = paperLoss,
            });
    }

    void
    testDepositActualAssetsLERequested()
    {
        testcase("Deposit floor: actualAssets <= requested (depositor keeps remainder)");

        // Floor on shares means actualAssets (back-calculated from floored shares) is
        // always <= the depositor's requested amount.
        Vault vault{usd_, 0};

        auto const seedAmt = 7;
        auto const extraInterest = 3;
        auto const depositRequested = 3;

        // Seed with 7 assets -> 7 shares, then add 3 extra via extra interest
        BEAST_EXPECT(vault.deposit(STAmount{usd_, seedAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });

        vault.borrow(1, 0);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt - 1,
                .sharesTotal = seedAmt,
            });

        vault.repay(1, 0, extraInterest);
        // assetsTotal=10, sharesTotal=7, depositNAV=10
        expectState(
            vault,
            {
                .assetsTotal = seedAmt + extraInterest,
                .assetsAvailable = seedAmt + extraInterest,
                .sharesTotal = seedAmt,
            });

        // shares = floor(depositRequested * sharesTotal / depositNAV)
        Number const expectedShares = vault.depositShares(Number(depositRequested));
        // actualAssets = shares * depositNAV / sharesTotal
        Number const expectedAssets = vault.depositAssets(expectedShares);

        auto const [actualShares, actualAssets] =
            vault.deposit(STAmount{usd_, depositRequested}).value();
        BEAST_EXPECT(actualShares == expectedShares);

        STAmount const expectedAmt{usd_, expectedAssets};
        BEAST_EXPECT(actualAssets == expectedAmt);
        // Floor means depositor keeps the remainder
        BEAST_EXPECT(Number(actualAssets) < depositRequested);

        Number const totalNum = Number(STAmount{usd_, seedAmt + extraInterest + expectedAssets});
        Number const availNum = Number(STAmount{usd_, seedAmt + extraInterest + expectedAssets});
        expectState(
            vault,
            {
                .assetsTotal = totalNum,
                .assetsAvailable = availNum,
                .sharesTotal = seedAmt + expectedShares,
            });
    }

    void
    testRedeemNonTerminatingFraction()
    {
        testcase("Redeem non-terminating fraction - STAmount precision truncation");

        // Vault: 1 asset, 3 shares (scale=0).
        // Redeeming 1 share gives assetsOut = 1/3 = 0.333... (non-terminating).
        // STAmount truncates to 16 significant digits on construction.

        // Build vault with assetsTotal=1, sharesTotal=3:
        // Deposit 3 assets -> 3 shares, hard-default 2 assets.
        Vault vault{usd_, 0};

        auto const seedAmt = 3;
        auto const defaultAmt = 2;
        auto const redeemShares = 1;

        BEAST_EXPECT(vault.deposit(STAmount{usd_, seedAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt,
                .sharesTotal = seedAmt,
            });

        // Hard default 2 assets (no interest): assetsTotal=1, sharesTotal=3
        vault.borrow(defaultAmt, 0);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt,
                .assetsAvailable = seedAmt - defaultAmt,
                .sharesTotal = seedAmt,
            });

        vault.defaultLoan(defaultAmt, 0, false);
        expectState(
            vault,
            {
                .assetsTotal = seedAmt - defaultAmt,
                .assetsAvailable = seedAmt - defaultAmt,
                .sharesTotal = seedAmt,
            });

        // 1/3 is non-terminating -- STAmount truncates to 16 digits
        Number const expectedOut = vault.redeemAssets(Number(redeemShares));
        // Compare as STAmount: redeem() returns STAmount which truncates
        // to 16 significant digits, so comparing directly to the
        // full-precision Number would fail for non-terminating fractions.
        STAmount const expectedAmt{usd_, expectedOut};
        auto const out = vault.redeem(STAmount{vault.shareAsset(), redeemShares}).value();
        BEAST_EXPECT(out == expectedAmt);

        // Truncation floors: sharesTotal copies of out must be <= withdrawalNAV
        Number const remainingAssets = seedAmt - defaultAmt;
        BEAST_EXPECT(out * seedAmt <= remainingAssets);
        // And close: drift from truncation is within 1e-15
        BEAST_EXPECT(remainingAssets - out * seedAmt < Number(1, -15));
        BEAST_EXPECT(Number(out) > 0);
    }

    void
    testSequentialVsBulkRedeem()
    {
        testcase("Sequential partial redeems vs single bulk redeem - STAmount drift");

        // Vault: 1 asset, 3 shares (scale=0), built same way as above.
        // Bulk: redeem(3) -> 1 asset exactly (3 * 1/3 = 1 in Number, exact).
        // Sequential: redeem(1) x 3 -> sum may differ due to STAmount truncation at each step.

        auto const seedAmt = 3;
        auto const defaultAmt = 2;
        auto const remainingAssets = seedAmt - defaultAmt;
        auto const redeemPerStep = 1;

        auto makeVault = [&]() {
            Vault v{usd_, 0};
            v.deposit(STAmount{usd_, seedAmt}).value();
            v.borrow(defaultAmt, 0);
            v.defaultLoan(defaultAmt, 0, false);
            return v;
        };

        // Bulk redeem
        {
            auto vault = makeVault();
            expectState(
                vault,
                {
                    .assetsTotal = remainingAssets,
                    .assetsAvailable = remainingAssets,
                    .sharesTotal = seedAmt,
                });

            auto const out = vault.redeem(STAmount{vault.shareAsset(), seedAmt}).value();
            BEAST_EXPECT(Number(out) == remainingAssets);
            expectState(vault, {.assetsTotal = 0, .assetsAvailable = 0, .sharesTotal = 0});
        }

        // Sequential redeem x 3
        {
            auto vault = makeVault();
            expectState(
                vault,
                {
                    .assetsTotal = remainingAssets,
                    .assetsAvailable = remainingAssets,
                    .sharesTotal = seedAmt,
                });

            Number total{0};
            for (int i = 0; i < seedAmt; ++i)
                total += Number(vault.redeem(STAmount{vault.shareAsset(), redeemPerStep}).value());

            // Sequential sum should be <= 1 (truncation can only lose, never gain)
            BEAST_EXPECT(total <= remainingAssets);
            // And not too far off: at most 3 ULP of 1/3 = 3e-16 drift
            BEAST_EXPECT(Number(remainingAssets) - total < Number(1, -15));
        }
    }

    void
    testWithdrawExactHalfBoundary()
    {
        testcase("Withdraw floor at exact 0.5 boundary - confirms floor, not round");

        // Vault: 10 assets, 10 shares, withdrawalNAV=10 (no loss/interest).
        // Withdraw 1.5: rawShares = 1.5 * 10 / 10 = 1.5 exactly.
        // floor(1.5) = 1  (not 2 -- positive confirmation of floor behaviour)
        // assetsOut = 1 * 10 / 10 = 1.0
        Vault vault{usd_, 0};

        auto const depositAmt = 10;
        auto const withdrawRequested = Number(15, -1);  // 1.5

        BEAST_EXPECT(vault.deposit(STAmount{usd_, depositAmt}).has_value());
        expectState(
            vault,
            {
                .assetsTotal = depositAmt,
                .assetsAvailable = depositAmt,
                .sharesTotal = depositAmt,
            });

        // shares = floor(requested * sharesTotal / withdrawalNAV)
        Number const expectedShares = vault.withdrawShares(withdrawRequested);
        // assetsOut = shares * withdrawalNAV / sharesTotal
        Number const expectedAssetsOut = vault.withdrawAssets(expectedShares);

        auto const [actualShares, actualAssets] =
            vault.withdraw(STAmount{usd_, withdrawRequested}).value();
        BEAST_EXPECT(actualShares == expectedShares);
        BEAST_EXPECT(actualAssets == expectedAssetsOut);

        // Floor discards the fractional share: assetsOut < requested
        BEAST_EXPECT(actualAssets < withdrawRequested);

        expectState(
            vault,
            {
                .assetsTotal = depositAmt - expectedAssetsOut,
                .assetsAvailable = depositAmt - expectedAssetsOut,
                .sharesTotal = depositAmt - expectedShares,
            });
    }

    void
    run() override
    {
        testInitialDepositIOU();
        testInitialDepositXRP();
        testInitialDepositMPT();
        testSubsequentDeposit();
        testRedeemBasic();
        testWithdrawBasic();
        testAsymmetricDepositWithInterest();
        testAsymmetricWithdrawWithLoss();
        testSpecExample();
        testDepositRoundingDown();
        testWithdrawRoundingFloor();
        testRedeemAll();
        testWithdrawAll();
        testLossOnlyVault();
        testMultipleDepositorsIOU();
        testPrecisionLoss();
        testDepositFloorBoundary();
        testXRPFullCycle();
        testMPTFullCycle();
        testTinyDepositIntoLargeVault();
        testTinyDepositIntoLargeVaultXRP();
        testLargeDepositIntoTinyVault();
        testLargeDepositIntoTinyVaultXRP();
        testHighPrecisionWithInterest();
        testHighPrecisionWithLoss();
        testManyTinyDepositsLargeVault();
        testExtremeLiquidityRatioIOU();
        testExtremeLiquidityRatioMPT();
        testDefaultLoanHard();
        testDefaultLoanPaperToReal();
        testClearPaperLoss();
        testWithdrawPrecisionLoss();
        testRedeemZeroShares();
        testRedeemInsufficientFunds();
        testRedepositAmtAfterFullDrain();
        testAssetsAvailableBorrowRepay();
        testLossDistributionMultipleDepositors();
        testNAVAsymmetryExplicit();
        testNonDefaultScale();
        testPaperLossThenActualDefault();
        testDepositZeroSharesLargeVault();
        testWithdrawAssetsOutLERequested();
        testDepositActualAssetsLERequested();
        testRedeemNonTerminatingFraction();
        testSequentialVsBulkRedeem();
        testWithdrawExactHalfBoundary();
    }
};

BEAST_DEFINE_TESTSUITE(VaultSharePricing, basics, xrpl);

}  // namespace xrpl
