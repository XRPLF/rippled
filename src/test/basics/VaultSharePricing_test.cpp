#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

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
class Vault : public beast::unit_test::suite
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
        if (!BEAST_EXPECTS(
                principal > 0 && interest >= 0,
                "xrpl::Vault::borrow requires positive principal and non-negative interest"))
            return;
        if (!BEAST_EXPECTS(
                principal <= assetsAvailable_,
                "xrpl::Vault::borrow principal exceeds assets available"))
            return;

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
        if (!BEAST_EXPECTS(
                principal > 0 && interest >= 0 && (!extraInterest || *extraInterest >= 0),
                "xrpl::Vault::repay requires positive principal and non-negative interest"))
            return;
        if (!BEAST_EXPECTS(
                principal + interest + assetsAvailable_ <= assetsTotal_,
                "xrpl::Vault::repay exceeds total assets"))
            return;

        assetsAvailable_ += principal + interest;
        interestUnrealized_ -= interest;

        if (extraInterest)
        {
            assetsTotal_ += extraInterest.value();
            assetsAvailable_ += extraInterest.value();
        }
    }

    void
    addPaperLoss(Number const& principal, Number const& interest)
    {
        if (!BEAST_EXPECTS(
                principal > 0 && interest >= 0,
                "xrpl::Vault::addPaperLoss requires positive principal and non-negative interest"))
            return;
        // Spec invariant: lossUnrealized <= assetsTotal - assetsAvailable
        // (paper loss cannot exceed total outstanding loan exposure: principal + interest)
        if (!BEAST_EXPECTS(
                lossUnrealized_ + principal + interest <= assetsTotal_ - assetsAvailable_,
                "xrpl::Vault::addPaperLoss exceeds outstanding loan exposure"))
            return;

        lossUnrealized_ += principal + interest;
    }

    void
    clearPaperLoss(Number const& principal, Number const& interest)
    {
        if (!BEAST_EXPECTS(
                principal > 0 && interest >= 0,
                "xrpl::Vault::clearPaperLoss requires positive principal and non-negative "
                "interest"))
            return;
        if (!BEAST_EXPECTS(
                principal + interest <= lossUnrealized_,
                "xrpl::Vault::clearPaperLoss exceeds unrealized loss"))
            return;

        lossUnrealized_ -= principal + interest;
    }

    void
    defaultLoan(Number const& principal, Number const& interest, bool hasPaperLoss = false)
    {
        if (!BEAST_EXPECTS(
                principal > 0 && interest >= 0,
                "xrpl::Vault::default requires positive principal and non-negative interest"))
            return;
        if (!BEAST_EXPECTS(
                principal + interest <= assetsTotal_, "xrpl::Vault::default exceeds total assets"))
            return;
        if (!BEAST_EXPECTS(
                interest <= interestUnrealized_,
                "xrpl::Vault::default interest exceeds unrealized interest"))
            return;

        assetsTotal_ -= principal + interest;
        interestUnrealized_ -= interest;
        if (hasPaperLoss)
            clearPaperLoss(principal, interest);
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
};

class VaultSharePricing_test : public beast::unit_test::suite
{
public:
    void
    testInitialDepositIOU()
    {
        testcase("Initial deposit IOU (scale=6)");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd};  // scale defaults to 6

        auto const [shares, assets] = vault.deposit(STAmount{usd, 100}).value();

        // 100 * 10^6 = 100,000,000 shares
        BEAST_EXPECT(Number(shares) == Number(100'000'000));
        BEAST_EXPECT(Number(assets) == Number(100));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(100));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(100'000'000));
    }

    void
    testInitialDepositXRP()
    {
        testcase("Initial deposit XRP (scale=0)");

        Vault vault{xrpIssue()};

        // 10 XRP = 10,000,000 drops; scale=0 so shares = drops 1:1
        auto const [shares, assets] = vault.deposit(STAmount{xrpIssue(), 10'000'000}).value();

        BEAST_EXPECT(Number(shares) == Number(10'000'000));
        BEAST_EXPECT(Number(assets) == Number(10'000'000));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(10'000'000));
    }

    void
    testInitialDepositMPT()
    {
        testcase("Initial deposit MPT (scale=0)");

        MPTIssue const mptAsset{makeMptID(100, AccountID(0x5678))};
        Vault vault{mptAsset};

        auto const [shares, assets] = vault.deposit(STAmount{mptAsset, 500}).value();

        // scale=0, so 500 assets → 500 shares
        BEAST_EXPECT(Number(shares) == Number(500));
        BEAST_EXPECT(Number(assets) == Number(500));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(500));
    }

    void
    testSubsequentDeposit()
    {
        testcase("Subsequent deposit (proportional)");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd};

        // Initial: 100 assets → 100M shares
        vault.deposit(STAmount{usd, 100}).value();

        // Second deposit: 50 assets at same price → 50M shares
        auto const [shares, assets] = vault.deposit(STAmount{usd, 50}).value();

        BEAST_EXPECT(Number(shares) == Number(50'000'000));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(150));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(150));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(150'000'000));
    }

    void
    testRedeemBasic()
    {
        testcase("Redeem basic");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd};

        vault.deposit(STAmount{usd, 100}).value();

        // Redeem half the shares
        auto const assetsOut = vault.redeem(STAmount{vault.shareAsset(), 50'000'000}).value();

        BEAST_EXPECT(Number(assetsOut) == Number(50));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(50));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(50));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(50'000'000));
    }

    void
    testWithdrawBasic()
    {
        testcase("Withdraw basic");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd};

        vault.deposit(STAmount{usd, 100}).value();

        // Withdraw 50 assets
        auto const [shares, assets] = vault.withdraw(STAmount{usd, 50}).value();

        BEAST_EXPECT(Number(shares) == Number(50'000'000));
        BEAST_EXPECT(Number(assets) == Number(50));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(50));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(50));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(50'000'000));
    }

    void
    testAsymmetricDepositWithInterest()
    {
        testcase("Asymmetric pricing - deposit with unrealized interest");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};  // scale=0 for simpler math

        // Seed vault: 950 assets, 950 shares
        vault.deposit(STAmount{usd, 950}).value();
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(950));

        // Borrow 500 principal with 50 interest: assetsTotal becomes 1000, omega=50
        // depositNAV = assetsTotal - omega = 1000 - 50 = 950
        vault.borrow(Number(500), Number(50));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(1000));

        // Deposit 95 assets: shares = floor(95 * 950 / 950) = floor(95) = 95
        // actualAssets = 95 * 950 / 950 = 95
        auto const [shares, assets] = vault.deposit(STAmount{usd, 95}).value();

        BEAST_EXPECT(Number(shares) == Number(95));
        BEAST_EXPECT(Number(assets) == Number(95));
    }

    void
    testAsymmetricWithdrawWithLoss()
    {
        testcase("Asymmetric pricing - withdraw with unrealized loss");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        // Seed: 1000 assets, 1000 shares.
        // Borrow 500 principal with 50 interest: assetsTotal=1050, omega=50.
        // Mark 100 as paper loss: iota=100.
        vault.deposit(STAmount{usd, 1000}).value();
        vault.borrow(Number(500), Number(50));
        vault.addPaperLoss(Number(100), Number(0));

        // withdrawalNAV = assetsTotal - omega - iota = 1050 - 50 - 100 = 900
        // Redeem 100 shares: assets = 100 * 900 / 1000 = 90
        // assetsAvailable before redeem = 500 (1000 - 500 borrowed); 90 < 500 ✓
        auto const assetsOut = vault.redeem(STAmount{vault.shareAsset(), 100}).value();

        BEAST_EXPECT(Number(assetsOut) == Number(90));
        // assetsAvailable decreases by assetsOut: 500 - 90 = 410
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(410));
    }

    void
    testSpecExample()
    {
        testcase("Spec example: deposit at full NAV, redeem at loss-adjusted NAV");

        // Vault: assetsTotal=1000, omega=50 (loan outstanding), iota=100, sharesTotal=1000
        // Achieved by: seed 950, issue loan of 50, mark 100 as loss
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        vault.deposit(STAmount{usd, 950}).value();
        vault.borrow(Number(500), Number(50));  // assetsTotal=1000, omega=50, assetsAvailable=450
        vault.addPaperLoss(Number(100), Number(0));

        // depositNAV = assetsTotal - omega = 1000 - 50 = 950
        // sharesTotal = 950
        // Depositing 0.95 assets: shares = floor(0.95 * 950 / 950) = floor(0.95) = 0
        // Instead deposit 1 asset: shares = floor(1 * 950 / 950) = 1
        {
            auto const [shares, assets] = vault.deposit(STAmount{usd, 1}).value();
            BEAST_EXPECT(Number(shares) == Number(1));
            BEAST_EXPECT(Number(assets) == Number(1));
            // assetsAvailable = 450 + 1 = 451
            BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(451));
        }

        // After deposit: assetsTotal = 1001, sharesTotal = 951
        // withdrawalNAV = assetsTotal - omega - iota = 1001 - 50 - 100 = 851
        // Redeem 1 share: assetsOut = 1 * 851 / 951
        {
            Number const expected = Number(851) / Number(951);
            STAmount const expectedAmt(usd, expected);
            auto const redeemAssets = vault.redeem(STAmount{vault.shareAsset(), 1}).value();
            BEAST_EXPECT(redeemAssets == expectedAmt);
            // assetsAvailable = 451 - 851/951 (STAmount-truncated)
            STAmount const expectedAvailAmt{usd, Number(451) - Number(851) / Number(951)};
            BEAST_EXPECT(vault.assetsAvailable() == expectedAvailAmt);
        }
    }

    void
    testDepositRoundingDown()
    {
        testcase("Deposit rounding - shares floor");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};
        vault.deposit(STAmount{usd, 3}).value();
        // 3 assets, 3 shares
        // Borrow 1, then repay it with 4 extra interest: assetsTotal becomes 7, sharesTotal stays 3
        vault.borrow(Number(1), Number(0));
        vault.repay(Number(1), Number(0), Number(4));
        // depositNAV = 7 (omega=0), sharesTotal = 3
        // Deposit 3 assets: shares = floor(3 * 3 / 7) = floor(1.285...) = 1
        auto const [shares, assets] = vault.deposit(STAmount{usd, 3}).value();
        BEAST_EXPECT(Number(shares) == Number(1));
        // Recalculated assets: 1 * 7 / 3
        STAmount const expectedAssets{usd, Number(7) / Number(3)};
        BEAST_EXPECT(assets == expectedAssets);
    }

    void
    testWithdrawRoundingFloor()
    {
        testcase("Withdraw rounding - shares floor");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        vault.deposit(STAmount{usd, 10}).value();
        // Borrow 3 (no interest) so paper loss is backed by loan principal
        vault.borrow(Number(3), Number(0));
        // Mark 3 as paper loss: withdrawalNAV = 10 - 0 - 3 = 7
        vault.addPaperLoss(Number(3), Number(0));
        // Withdraw 1 asset: shares = floor(1 * 10 / 7) = floor(1.4285...) = 1
        // assetsOut = 1 * 7 / 10 = 0.7
        auto const [shares, assets] = vault.withdraw(STAmount{usd, 1}).value();
        BEAST_EXPECT(Number(shares) == Number(1));
        BEAST_EXPECT(Number(assets) == Number(7, -1));

        // rawShares = 1.1*10/7 ≈ 1.571 → floor = 1 (not 2)
        Vault vault2{usd, 0};
        vault2.deposit(STAmount{usd, 10}).value();
        vault2.borrow(Number(3), Number(0));
        vault2.addPaperLoss(Number(3), Number(0));
        auto const [shares2, assets2] = vault2.withdraw(STAmount{usd, 11, -1}).value();  // 1.1
        BEAST_EXPECT(Number(shares2) == Number(1));
        // assetsOut = 1*7/10 = 0.7
        BEAST_EXPECT(Number(assets2) == Number(7, -1));
    }

    void
    testRedeemAll()
    {
        testcase("Redeem all shares - vault empties");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd};

        vault.deposit(STAmount{usd, 100}).value();
        auto const assetsOut = vault.redeem(STAmount{vault.shareAsset(), 100'000'000}).value();

        BEAST_EXPECT(Number(assetsOut) == Number(100));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(0));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(0));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(0));
    }

    void
    testLossOnlyVault()
    {
        testcase("Vault with lossUnrealized only (no interestUnrealized)");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        vault.deposit(STAmount{usd, 1000}).value();
        // Borrow 200 (no interest) so paper loss is backed by loan principal
        vault.borrow(Number(200), Number(0));
        vault.addPaperLoss(Number(200), Number(0));

        // Deposit NAV = 1000 - 0 = 1000 (loss doesn't affect deposit)
        auto const [depShares, depAssets] = vault.deposit(STAmount{usd, 100}).value();
        BEAST_EXPECT(Number(depShares) == Number(100));

        // Withdrawal NAV = 1100 - 0 - 200 = 900 (total is now 1100)
        // Redeem 100 shares: assets = 100 * 900 / 1100 ≈ 81.818...
        auto const redeemAssets = vault.redeem(STAmount{vault.shareAsset(), 100}).value();
        // 100 * 900 / 1100 = 81.8181...
        // Compare as STAmount to account for IOU normalization
        Number const expected = (Number(100) * Number(900)) / Number(1100);
        STAmount const expectedAmt(usd, expected);
        BEAST_EXPECT(redeemAssets == expectedAmt);
    }

    void
    testMultipleDepositorsIOU()
    {
        testcase("Multiple depositors with yield (IOU)");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};  // scale=0 for clean integer math

        // User A deposits 100 → 100 shares
        auto const [sharesA, assetsA] = vault.deposit(STAmount{usd, 100}).value();
        BEAST_EXPECT(Number(sharesA) == Number(100));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(100));

        // Loan repaid with extra interest: assetsTotal becomes 110
        vault.borrow(Number(1), Number(0));
        vault.repay(Number(1), Number(0), Number(10));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(110));

        // User B deposits 100: depositNAV=110, shares=floor(100*100/110)=90
        // actualAssets = 90*110/100 = 99
        auto const [sharesB, assetsB] = vault.deposit(STAmount{usd, 100}).value();
        BEAST_EXPECT(Number(sharesB) == Number(90));
        BEAST_EXPECT(Number(assetsB) == Number(99));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(209));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(190));

        // A redeems 100 shares: assetsOut = 100 * 209 / 190 = 110
        auto const outA = vault.redeem(STAmount{vault.shareAsset(), 100}).value();
        BEAST_EXPECT(Number(outA) == Number(110));

        // B redeems 90 shares: assetsOut = 90 * 99 / 90 = 99
        auto const outB = vault.redeem(STAmount{vault.shareAsset(), 90}).value();
        BEAST_EXPECT(Number(outB) == Number(99));

        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(0));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(0));
    }

    void
    testPrecisionLoss()
    {
        testcase("Precision loss - zero shares condition");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        // Initial deposit: 1 asset → 1 share
        vault.deposit(STAmount{usd, 1}).value();
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(1));

        // Deposit 0.0001 assets: shares = floor(0.0001 * 1 / 1) = 0 → tecPRECISION_LOSS
        auto const tinyResult = vault.deposit(STAmount{usd, 1, -4});
        BEAST_EXPECT(!tinyResult);
        BEAST_EXPECT(tinyResult.error() == tecPRECISION_LOSS);
    }

    void
    testXRPFullCycle()
    {
        testcase("XRP full cycle: deposit, withdraw, redeem");

        Vault vault{xrpIssue()};

        // Deposit 10 XRP = 10M drops
        auto const [shares1, assets1] = vault.deposit(STAmount{xrpIssue(), 10'000'000}).value();
        BEAST_EXPECT(Number(shares1) == Number(10'000'000));

        // Withdraw 5M drops
        auto const [wShares, wAssets] = vault.withdraw(STAmount{xrpIssue(), 5'000'000}).value();
        BEAST_EXPECT(Number(wShares) == Number(5'000'000));
        BEAST_EXPECT(Number(wAssets) == Number(5'000'000));

        // Redeem remaining 5M shares
        auto const rAssets = vault.redeem(STAmount{vault.shareAsset(), 5'000'000}).value();
        BEAST_EXPECT(Number(rAssets) == Number(5'000'000));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(0));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(0));
    }

    void
    testMPTFullCycle()
    {
        testcase("MPT full cycle: deposit, withdraw, redeem");

        MPTIssue const mptAsset{makeMptID(200, AccountID(0x5678))};
        Vault vault{mptAsset};

        auto const [shares1, assets1] = vault.deposit(STAmount{mptAsset, 1000}).value();
        BEAST_EXPECT(Number(shares1) == Number(1000));

        auto const [wShares, wAssets] = vault.withdraw(STAmount{mptAsset, 400}).value();
        BEAST_EXPECT(Number(wShares) == Number(400));

        auto const rAssets = vault.redeem(STAmount{vault.shareAsset(), 600}).value();
        BEAST_EXPECT(Number(rAssets) == Number(600));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(0));
    }

    void
    testTinyDepositIntoLargeVault()
    {
        testcase("Tiny deposit into large vault (IOU)");

        // Large vault with 1 billion assets at scale=6
        // sharesTotal = 1e9 * 1e6 = 1e15
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd};  // scale=6

        vault.deposit(STAmount{usd, UINT64_C(1'000'000'000)}).value();
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(1, 15));

        // Tiny deposit: 0.000001 (1e-6) into a 1e9 vault
        // shares = floor(1e-6 * 1e15 / 1e9) = floor(1e0) = 1
        auto const [shares, assets] = vault.deposit(STAmount{usd, 1, -6}).value();

        BEAST_EXPECT(Number(shares) == Number(1));
        // Recalculated assets: 1 * 1e9 / 1e15 = 1e-6
        BEAST_EXPECT(Number(assets) == Number(1, -6));

        // Redeem the 1 share back
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 1}).value();
        BEAST_EXPECT(Number(out) == Number(1, -6));
    }

    void
    testTinyDepositIntoLargeVaultXRP()
    {
        testcase("Tiny deposit into large vault (XRP)");

        // 100 million XRP = 1e14 drops, scale=0 so shares = 1e14
        Vault vault{xrpIssue()};

        vault.deposit(STAmount{xrpIssue(), UINT64_C(100'000'000'000'000)}).value();
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(1, 14));

        // Deposit 1 drop into a 1e14-drop vault
        // shares = floor(1 * 1e14 / 1e14) = 1
        auto const [shares, assets] = vault.deposit(STAmount{xrpIssue(), 1}).value();

        BEAST_EXPECT(Number(shares) == Number(1));
        BEAST_EXPECT(Number(assets) == Number(1));

        // Redeem the 1 share
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 1}).value();
        BEAST_EXPECT(Number(out) == Number(1));
    }

    void
    testLargeDepositIntoTinyVault()
    {
        testcase("Large deposit into tiny vault (IOU)");

        // Tiny vault: 0.001 assets, scale=6 → shares = 0.001 * 1e6 = 1000
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd};

        vault.deposit(STAmount{usd, 1, -3}).value();
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(1000));

        // Massive deposit: 1 billion assets
        // shares = floor(1e9 * 1000 / 0.001) = floor(1e15) = 1e15
        auto const [shares, assets] = vault.deposit(STAmount{usd, UINT64_C(1'000'000'000)}).value();

        BEAST_EXPECT(Number(shares) == Number(1, 15));
        BEAST_EXPECT(Number(assets) == Number(1, 9));

        // Vault now: assetsTotal = 1e9 + 0.001, sharesTotal = 1e15 + 1000
        // Redeem original 1000 shares: assetsOut = 1000 * (1e9+0.001) / (1e15+1000)
        // ≈ 1e12 / 1e15 = 0.001 (the original depositor gets back roughly 0.001)
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 1000}).value();
        // Approximate bounds: should be within 10% of 0.001
        BEAST_EXPECT(Number(out) > Number(9, -4));   // > 0.0009
        BEAST_EXPECT(Number(out) < Number(11, -4));  // < 0.0011
    }

    void
    testLargeDepositIntoTinyVaultXRP()
    {
        testcase("Large deposit into tiny vault (XRP)");

        // Tiny vault: 1 drop, scale=0 → 1 share
        Vault vault{xrpIssue()};

        vault.deposit(STAmount{xrpIssue(), 1}).value();
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(1));

        // Massive deposit: 1e14 drops (100M XRP)
        // shares = floor(1e14 * 1 / 1) = 1e14
        auto const [shares, assets] =
            vault.deposit(STAmount{xrpIssue(), UINT64_C(100'000'000'000'000)}).value();

        BEAST_EXPECT(Number(shares) == Number(1, 14));
        BEAST_EXPECT(Number(assets) == Number(1, 14));

        // Redeem original 1 share: assetsOut = 1 * (1e14+1) / (1e14+1) = 1
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 1}).value();
        BEAST_EXPECT(Number(out) == Number(1));
    }

    void
    testHighPrecisionWithInterest()
    {
        testcase("High precision deposit/redeem with unrealized interest");

        // Tests the full loan lifecycle at large scale:
        // issue loan → tiny depositor joins → loan repaid → depositor redeems
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};  // scale=0

        // Seed: 1e12 assets → 1e12 shares
        vault.deposit(STAmount{usd, UINT64_C(1'000'000'000'000)}).value();

        // Borrow 1e11 principal with 5e11 interest: assetsTotal=1.5e12, omega=5e11
        // depositNAV = 1.5e12 - 5e11 = 1e12 (unchanged from seed)
        vault.borrow(Number(1, 11), Number(5, 11));

        // Tiny deposit: 1 asset
        // shares = floor(1 * 1e12 / 1e12) = 1
        auto const [shares, assets] = vault.deposit(STAmount{usd, 1}).value();
        BEAST_EXPECT(Number(shares) == Number(1));
        BEAST_EXPECT(Number(assets) == Number(1));

        // Loan repaid: omega → 0, assetsAvailable fully restored to assetsTotal = 1.5e12 + 1
        vault.repay(Number(1, 11), Number(5, 11));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(15, 11) + Number(1));

        // Redeem that 1 share
        // withdrawalNAV = (1.5e12 + 1) - 0 - 0 = 1.5e12 + 1
        // assetsOut = 1 * (1.5e12 + 1) / (1e12 + 1)
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 1}).value();
        Number const expectedNum =
            (Number(1) * (Number(15, 11) + Number(1))) / (Number(1, 12) + Number(1));
        STAmount const expectedAmt(usd, expectedNum);
        BEAST_EXPECT(out == expectedAmt);
        // assetsAvailable after redeem: (1.5e12+1) - assetsOut (STAmount-truncated)
        STAmount const expectedAvailAmt{usd, (Number(15, 11) + Number(1)) - expectedNum};
        BEAST_EXPECT(vault.assetsAvailable() == expectedAvailAmt);
    }

    void
    testHighPrecisionWithLoss()
    {
        testcase("High precision redeem with unrealized loss");

        // Large vault with loss: tests that a tiny share redemption
        // correctly accounts for the discounted withdrawalNAV
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        vault.deposit(STAmount{usd, UINT64_C(1'000'000'000'000)}).value();

        // Borrow 2e11 (no interest) so paper loss is backed by loan principal
        vault.borrow(Number(2, 11), Number(0));
        // 20% paper loss
        vault.addPaperLoss(Number(2, 11), Number(0));
        // withdrawalNAV = 1e12 - 0 - 2e11 = 8e11

        // Redeem 1 share out of 1e12
        // assetsOut = 1 * 8e11 / 1e12 = 0.8
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 1}).value();
        Number const expectedNum = Number(8, 11) / Number(1, 12);
        STAmount const expectedAmt(usd, expectedNum);
        BEAST_EXPECT(out == expectedAmt);
    }

    void
    testManyTinyDepositsLargeVault()
    {
        testcase("Many tiny deposits into large vault then full redeem");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};  // scale=0

        // Seed with 1e9 assets
        vault.deposit(STAmount{usd, UINT64_C(1'000'000'000)}).value();

        Number totalTinyShares{0};
        Number totalTinyAssets{0};

        // 100 tiny deposits of 1 asset each
        for (int i = 0; i < 100; ++i)
        {
            auto const [s, a] = vault.deposit(STAmount{usd, 1}).value();
            totalTinyShares += Number(s);
            totalTinyAssets += Number(a);
        }

        // Each deposit: shares = floor(1 * sharesTotal / assetsTotal)
        // First tiny: floor(1 * 1e9 / 1e9) = 1 share per deposit
        BEAST_EXPECT(totalTinyShares == Number(100));
        BEAST_EXPECT(totalTinyAssets == Number(100));

        // Redeem all 100 tiny shares
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 100}).value();
        // assetsOut = 100 * (1e9 + 100) / (1e9 + 100) = 100
        BEAST_EXPECT(Number(out) == Number(100));
    }

    void
    testExtremeLiquidityRatioIOU()
    {
        testcase("Extreme liquidity ratio - 1e15 to 1 (IOU)");

        // Vault with maximum practical IOU assets at scale=0
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        // 1e15 assets → 1e15 shares
        vault.deposit(STAmount{usd, UINT64_C(1'000'000'000'000'000)}).value();
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(1, 15));

        // Deposit 1 asset: shares = floor(1 * 1e15 / 1e15) = 1
        auto const [shares, assets] = vault.deposit(STAmount{usd, 1}).value();
        BEAST_EXPECT(Number(shares) == Number(1));
        BEAST_EXPECT(Number(assets) == Number(1));

        // Redeem that 1 share
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 1}).value();
        BEAST_EXPECT(Number(out) == Number(1));

        // Vault should be back to exactly 1e15
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(1, 15));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(1, 15));
    }

    void
    testExtremeLiquidityRatioMPT()
    {
        testcase("Extreme liquidity ratio - 1e15 to 1 (MPT)");

        MPTIssue const mptAsset{makeMptID(300, AccountID(0x5678))};
        Vault vault{mptAsset};

        vault.deposit(STAmount{mptAsset, UINT64_C(1'000'000'000'000'000)}).value();
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(1, 15));

        auto const [shares, assets] = vault.deposit(STAmount{mptAsset, 1}).value();
        BEAST_EXPECT(Number(shares) == Number(1));

        auto const out = vault.redeem(STAmount{vault.shareAsset(), 1}).value();
        BEAST_EXPECT(Number(out) == Number(1));
    }

    void
    testDefaultLoanHard()
    {
        testcase("Default loan (hard) - assetsTotal written down directly");

        // A hard default permanently reduces assetsTotal and interestUnrealized.
        // No lossUnrealized is created; the loss is immediately socialized.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        // Seed: 1000 assets, 1000 shares
        vault.deposit(STAmount{usd, 1000}).value();

        // Issue loan: principal=400, interest=100
        // assetsTotal = 1000 + 100 = 1100, omega = 100, assetsAvailable = 600
        vault.borrow(Number(400), Number(100));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(1100));
        BEAST_EXPECT(Number(vault.interestUnrealized()) == Number(100));

        // Hard default the full loan (isPaperLoss=false)
        // assetsTotal -= 400 + 100 = 500 → assetsTotal = 600
        // interestUnrealized -= 100 → omega = 0
        // lossUnrealized stays 0
        vault.defaultLoan(Number(400), Number(100), false);
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(600));
        BEAST_EXPECT(Number(vault.interestUnrealized()) == Number(0));
        BEAST_EXPECT(Number(vault.lossUnrealized()) == Number(0));

        // After hard default: assetsAvailable unchanged at 600 (borrow removed principal)
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(600));

        // withdrawalNAV = 600 - 0 - 0 = 600
        // Redeem 500 shares: assetsOut = 500 * 600 / 1000 = 300 < 600 (assetsAvailable) ✓
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 500}).value();
        BEAST_EXPECT(Number(out) == Number(300));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(300));
    }

    void
    testDefaultLoanPaperToReal()
    {
        testcase("Default loan (paper-to-real) - pre-announced loss confirmed as hard default");

        // A loan is pre-announced as paper loss via addPaperLoss, then confirmed
        // as a hard default via defaultLoan(hasPaperLoss=true).
        // defaultLoan decrements both assetsTotal and lossUnrealized simultaneously,
        // so the withdrawalNAV (assetsTotal - omega - iota) recovers after the default.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        vault.deposit(STAmount{usd, 1000}).value();
        vault.borrow(Number(400), Number(100));
        // assetsTotal=1100, omega=100, assetsAvailable=600

        // Pre-announce the full loan as paper loss
        // assetsTotal - assetsAvailable = 500, so lossUnrealized can reach 500
        vault.addPaperLoss(Number(400), Number(100));
        BEAST_EXPECT(Number(vault.lossUnrealized()) == Number(500));
        // withdrawalNAV = 1100 - 100 - 500 = 500

        // Hard default confirmed (hasPaperLoss=true):
        // assetsTotal -= 500 → 600; omega -= 100 → 0; lossUnrealized -= 500 → 0
        vault.defaultLoan(Number(400), Number(100), true);
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(600));
        BEAST_EXPECT(Number(vault.interestUnrealized()) == Number(0));
        BEAST_EXPECT(Number(vault.lossUnrealized()) == Number(0));

        // withdrawalNAV = 600 - 0 - 0 = 600
        // Redeem 500 shares: assetsOut = 500 * 600 / 1000 = 300
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 500}).value();
        BEAST_EXPECT(Number(out) == Number(300));
    }

    void
    testClearPaperLoss()
    {
        testcase("Clear paper loss - withdrawalNAV recovers on loan recovery");

        // Scenario: loan marked as paper loss, then partially recovered.
        // clearPaperLoss reduces iota, restoring withdrawalNAV.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        vault.deposit(STAmount{usd, 1000}).value();
        vault.borrow(Number(200), Number(0));
        // assetsTotal=1000, omega=0, assetsAvailable=800

        // Mark the full 200 as paper loss
        vault.addPaperLoss(Number(200), Number(0));
        BEAST_EXPECT(Number(vault.lossUnrealized()) == Number(200));
        // withdrawalNAV = 1000 - 0 - 200 = 800

        // Recovery: 100 is repaid; clear that portion of paper loss
        // (In the real system: repay restores assetsAvailable, clearPaperLoss reduces iota)
        vault.repay(Number(100), Number(0));
        vault.clearPaperLoss(Number(100), Number(0));
        BEAST_EXPECT(Number(vault.lossUnrealized()) == Number(100));
        // assetsTotal=1000, omega=0, iota=100
        // withdrawalNAV = 1000 - 0 - 100 = 900

        // Redeem 100 shares: assetsOut = 100 * 900 / 1000 = 90
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 100}).value();
        BEAST_EXPECT(Number(out) == Number(90));

        // Full recovery: clear remaining 100 paper loss
        vault.repay(Number(100), Number(0));
        vault.clearPaperLoss(Number(100), Number(0));
        BEAST_EXPECT(Number(vault.lossUnrealized()) == Number(0));
        // State: assetsTotal=910, omega=0, iota=0, sharesTotal=900
        // (assetsTotal after redeem=910; repay restored assetsAvailable but not assetsTotal)
        // withdrawalNAV = 910
        // Redeem 100 shares (of 900 remaining): assetsOut = 100 * 910 / 900
        Number const expected2 = (Number(100) * Number(910)) / Number(900);
        STAmount const expectedAmt2(usd, expected2);
        auto const out2 = vault.redeem(STAmount{vault.shareAsset(), 100}).value();
        BEAST_EXPECT(out2 == expectedAmt2);
    }

    void
    testWithdrawPrecisionLoss()
    {
        testcase("Withdraw precision loss - zero shares condition");

        // A withdrawal so small that the share calculation rounds to 0
        // represents tecPRECISION_LOSS in the real implementation.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        // 1 asset, 1 share → withdrawalNAV = 1
        vault.deposit(STAmount{usd, 1}).value();

        // Withdraw 0.0001 assets: shares = truncate(0.0001 * 1 / 1) = 0 → tecPRECISION_LOSS
        BEAST_EXPECT(!vault.withdraw(STAmount{usd, 1, -4}));
        BEAST_EXPECT(vault.withdraw(STAmount{usd, 1, -4}).error() == tecPRECISION_LOSS);
    }

    void
    testRedeemZeroShares()
    {
        testcase("Redeem zero shares - guard rejects non-positive share amount");

        // redeem() line 88 guards against zero (or negative) share amounts.
        // NOTE: The PoC returns tecPRECISION_LOSS for this guard; semantically the
        // spec may prefer a different error code (e.g. temBAD_AMOUNT), but this
        // test documents the current PoC behaviour and would expose any regression.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        vault.deposit(STAmount{usd, 100}).value();

        // Redeem zero shares
        auto const result = vault.redeem(STAmount{vault.shareAsset(), 0});
        BEAST_EXPECT(!result);
        BEAST_EXPECT(result.error() == tecPRECISION_LOSS);

        // Vault state must be unchanged
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(100));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(100));
    }

    void
    testRedeemInsufficientFunds()
    {
        testcase("Redeem/withdraw with outstanding loan - tecINSUFFICIENT_FUNDS");

        // When a loan is outstanding, assetsAvailable < assetsTotal.
        // A redemption that would require more than assetsAvailable must be rejected
        // with tecINSUFFICIENT_FUNDS rather than silently going negative.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};

        // --- redeem() ---
        {
            Vault vault{usd, 0};
            vault.deposit(STAmount{usd, 1000}).value();
            // Borrow 800: assetsAvailable = 200, assetsTotal = 1000
            vault.borrow(Number(800), Number(0));
            BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(200));

            // withdrawalNAV = 1000, sharesTotal = 1000
            // Redeem 500 shares: assetsOut = 500 * 1000 / 1000 = 500 > 200
            auto const result = vault.redeem(STAmount{vault.shareAsset(), 500});
            BEAST_EXPECT(!result);
            BEAST_EXPECT(result.error() == tecINSUFFICIENT_FUNDS);

            // Vault state unchanged
            BEAST_EXPECT(Number(vault.assetsTotal()) == Number(1000));
            BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(200));
            BEAST_EXPECT(Number(vault.sharesTotal()) == Number(1000));

            // Redeeming exactly assetsAvailable (200 shares → 200 assets) succeeds
            auto const ok = vault.redeem(STAmount{vault.shareAsset(), 200});
            BEAST_EXPECT(ok.has_value());
            BEAST_EXPECT(Number(ok.value()) == Number(200));
        }

        // --- withdraw() ---
        {
            Vault vault{usd, 0};
            vault.deposit(STAmount{usd, 1000}).value();
            vault.borrow(Number(800), Number(0));
            BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(200));

            // withdrawalNAV = 1000
            // Withdraw 500 assets: shares = floor(500*1000/1000) = 500; assetsOut = 500 > 200
            auto const result = vault.withdraw(STAmount{usd, 500});
            BEAST_EXPECT(!result);
            BEAST_EXPECT(result.error() == tecINSUFFICIENT_FUNDS);

            // Vault state unchanged
            BEAST_EXPECT(Number(vault.assetsTotal()) == Number(1000));
            BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(200));

            // Withdraw exactly assetsAvailable (200 assets → 200 shares) succeeds
            auto const ok = vault.withdraw(STAmount{usd, 200});
            BEAST_EXPECT(ok.has_value());
        }
    }

    void
    testReseedAfterFullDrain()
    {
        testcase("Deposit after full vault drain (re-seeding)");

        // After all shares are redeemed the vault returns to the empty state.
        // The next deposit should use the initial seeding formula (shares = assets * 10^scale).
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        // First life: deposit 100, redeem all
        vault.deposit(STAmount{usd, 100}).value();
        vault.redeem(STAmount{vault.shareAsset(), 100}).value();
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(0));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(0));

        // Re-seed: should behave like initial deposit
        auto const [shares, assets] = vault.deposit(STAmount{usd, 200}).value();
        BEAST_EXPECT(Number(shares) == Number(200));
        BEAST_EXPECT(Number(assets) == Number(200));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(200));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(200));
    }

    void
    testAssetsAvailableBorrowRepay()
    {
        testcase("assetsAvailable tracked correctly through borrow/repay");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        vault.deposit(STAmount{usd, 1000}).value();
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(1000));

        // Borrow 500 principal with 50 interest
        // assetsAvailable decreases by principal only (not by interest)
        vault.borrow(Number(500), Number(50));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(500));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(1050));

        // Repay 500 principal + 50 interest: assetsAvailable fully restored
        vault.repay(Number(500), Number(50));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(1050));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(1050));

        // Borrow again then repay with extra interest
        vault.borrow(Number(200), Number(0));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(850));

        // repay 200 + 0 normal interest + 30 extra
        vault.repay(Number(200), Number(0), Number(30));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(1080));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(1080));
    }

    void
    testLossDistributionMultipleDepositors()
    {
        testcase("Loss distribution across multiple depositors");

        // User A deposits before loss is marked.
        // User B deposits after (at full depositNAV, unaffected by iota).
        // On redemption both bear the loss proportionally to their share count.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        // User A: 1000 assets → 1000 shares
        auto const [sharesA, assetsA] = vault.deposit(STAmount{usd, 1000}).value();
        BEAST_EXPECT(Number(sharesA) == Number(1000));

        // Issue loan: borrow 200 principal (no interest) to back the paper loss
        // assetsTotal=1000, omega=0, assetsAvailable=800, depositNAV=1000
        vault.borrow(Number(200), Number(0));

        // Mark 200 as paper loss: iota=200
        // depositNAV = 1000, withdrawalNAV = 1000 - 0 - 200 = 800
        vault.addPaperLoss(Number(200), Number(0));

        // User B deposits 500: depositNAV=1000, shares=floor(500*1000/1000)=500
        // actualAssets = 500 * 1000 / 1000 = 500
        auto const [sharesB, assetsB] = vault.deposit(STAmount{usd, 500}).value();
        BEAST_EXPECT(Number(sharesB) == Number(500));
        BEAST_EXPECT(Number(assetsB) == Number(500));
        // assetsTotal=1500, sharesTotal=1500, assetsAvailable=800+500=1300

        // withdrawalNAV = 1500 - 0 - 200 = 1300
        // A redeems 1000 shares: assetsOut = 1000 * 1300 / 1500 ≈ 866.67
        // assetsAvailable=1300; 866.67 < 1300 ✓
        Number const expectedA = (Number(1000) * Number(1300)) / Number(1500);
        STAmount const expectedAmtA(usd, expectedA);
        auto const outA = vault.redeem(STAmount{vault.shareAsset(), 1000}).value();
        BEAST_EXPECT(outA == expectedAmtA);
        // assetsAvailable after A: 1300 - 866.67 ≈ 433.33
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(vault.assetsTotal()) - Number(200));

        // assetsTotal after A redeems: 1500 - 1000*1300/1500 = 1500 - 2600/3 = 1900/3
        // sharesTotal: 500
        // withdrawalNAV = 1900/3 - 0 - 200 = 1900/3 - 600/3 = 1300/3
        // B redeems 500 shares: assetsOut = 500 * (1300/3) / 500 = 1300/3
        // assetsAvailable = 1300/3; exactly sufficient ✓
        Number const expectedB = Number(1300) / Number(3);
        STAmount const expectedAmtB(usd, expectedB);
        auto const outB = vault.redeem(STAmount{vault.shareAsset(), 500}).value();
        BEAST_EXPECT(outB == expectedAmtB);
    }

    void
    testNAVAsymmetryExplicit()
    {
        testcase("Explicit NAV asymmetry: deposit at depositNAV, redeem at withdrawalNAV");

        // This test directly verifies the spec's core design intent:
        // depositNAV = assetsTotal - omega  (does NOT subtract iota)
        // withdrawalNAV = assetsTotal - omega - iota  (DOES subtract iota)
        // A new depositor pays a fair price; an existing holder exiting bears losses.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        // Seed: 1000 assets, 1000 shares
        vault.deposit(STAmount{usd, 1000}).value();

        // Create asymmetry: omega=100 (unrealized interest), iota=150 (paper loss)
        vault.borrow(Number(400), Number(100));  // assetsTotal=1100, omega=100, assetsAvailable=600
        vault.addPaperLoss(Number(150), Number(0));  // iota=150

        // depositNAV   = 1100 - 100       = 1000
        // withdrawalNAV = 1100 - 100 - 150 = 850

        // New depositor: 100 assets → shares = floor(100 * 1000 / 1000) = 100
        auto const [newShares, newAssets] = vault.deposit(STAmount{usd, 100}).value();
        BEAST_EXPECT(Number(newShares) == Number(100));
        BEAST_EXPECT(Number(newAssets) == Number(100));
        // assetsTotal=1200, sharesTotal=1100, assetsAvailable=700
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(700));

        // Existing holder redeems 100 shares using withdrawalNAV
        // withdrawalNAV = 1200 - 100 - 150 = 950
        // assetsOut = 100 * 950 / 1100 ≈ 86.36...
        Number const expected = (Number(100) * Number(950)) / Number(1100);
        STAmount const expectedAmt(usd, expected);
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 100}).value();
        BEAST_EXPECT(out == expectedAmt);
        // assetsAvailable = 700 - 100*950/1100 (STAmount-truncated)
        STAmount const expectedAvailAmt{
            usd, Number(700) - Number(100) * Number(950) / Number(1100)};
        BEAST_EXPECT(vault.assetsAvailable() == expectedAvailAmt);

        // Confirm: new depositor paid 100 per 100 shares, redeemer got ~86.36 per 100 shares
        // This asymmetry discourages bank runs: early exiters bear losses.
        BEAST_EXPECT(Number(out) < Number(newAssets));
    }

    void
    testNonDefaultScale()
    {
        testcase("Non-default IOU scale values (scale=2 and scale=18)");

        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};

        // scale=2: initial deposit gives assets * 10^2 shares
        {
            Vault vault{usd, 2};
            auto const [shares, assets] = vault.deposit(STAmount{usd, 50}).value();
            // 50 * 10^2 = 5000 shares
            BEAST_EXPECT(Number(shares) == Number(5000));
            BEAST_EXPECT(Number(assets) == Number(50));

            // Subsequent deposit: proportional
            auto const [shares2, assets2] = vault.deposit(STAmount{usd, 25}).value();
            BEAST_EXPECT(Number(shares2) == Number(2500));
            BEAST_EXPECT(Number(assets2) == Number(25));

            // Redeem 5000 shares: assetsOut = 5000 * 75 / 7500 = 50
            auto const out = vault.redeem(STAmount{vault.shareAsset(), 5000}).value();
            BEAST_EXPECT(Number(out) == Number(50));
        }

        // scale=18: maximum, initial deposit gives assets * 10^18 shares
        {
            Vault vault{usd, 18};
            // Deposit 1 asset → 10^18 shares (= 1e18)
            auto const [shares, assets] = vault.deposit(STAmount{usd, 1}).value();
            BEAST_EXPECT(Number(shares) == Number(1, 18));
            BEAST_EXPECT(Number(assets) == Number(1));

            // Subsequent deposit of 1 asset: shares = floor(1 * 1e18 / 1) = 1e18
            auto const [shares2, assets2] = vault.deposit(STAmount{usd, 1}).value();
            BEAST_EXPECT(Number(shares2) == Number(1, 18));
            BEAST_EXPECT(Number(assets2) == Number(1));

            // Redeem 1e18 shares: assetsOut = 1e18 * 2 / 2e18 = 1
            auto const out = vault.redeem(STAmount{vault.shareAsset(), Number(1, 18)}).value();
            BEAST_EXPECT(Number(out) == Number(1));
        }
    }

    void
    testPaperLossThenActualDefault()
    {
        testcase("Pre-announced paper loss followed by actual default");

        // A loan is first flagged as a paper loss via addPaperLoss, then later
        // confirmed as a hard default via defaultLoan(hasPaperLoss=true).
        // defaultLoan(hasPaperLoss=true) atomically:
        //   - writes the loss off assetsTotal (finalises the default)
        //   - decrements lossUnrealized (removes the paper loss entry)
        // This is equivalent to the two-step: clearPaperLoss + defaultLoan(false).
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        // Seed: 1000 assets, 1000 shares
        vault.deposit(STAmount{usd, 1000}).value();

        // Issue loan: principal=300, interest=30
        // assetsTotal=1030, omega=30, assetsAvailable=700
        vault.borrow(Number(300), Number(30));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(1030));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(700));

        // Step 1: Pre-announce as paper loss (iota = 330)
        // withdrawalNAV = 1030 - 30 - 330 = 670
        vault.addPaperLoss(Number(300), Number(30));
        BEAST_EXPECT(Number(vault.lossUnrealized()) == Number(330));

        // Intermediate redeem to confirm discounted NAV is applied
        // Redeem 100 shares: assetsOut = 100 * 670 / 1000 = 67
        // assetsAvailable = 700 - 67 = 633
        auto const outBefore = vault.redeem(STAmount{vault.shareAsset(), 100}).value();
        BEAST_EXPECT(Number(outBefore) == Number(67));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(633));
        // assetsTotal=963, sharesTotal=900

        // Step 2: Loan confirmed as hard default — defaultLoan(hasPaperLoss=true)
        // atomically writes off assetsTotal and clears lossUnrealized.
        // assetsAvailable unchanged (loan was already removed via borrow).
        vault.defaultLoan(Number(300), Number(30), true);
        // assetsTotal -= 330 → 633; omega -= 30 → 0; lossUnrealized -= 330 → 0
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(633));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(633));
        BEAST_EXPECT(Number(vault.interestUnrealized()) == Number(0));
        BEAST_EXPECT(Number(vault.lossUnrealized()) == Number(0));

        // withdrawalNAV = 633 - 0 - 0 = 633; sharesTotal = 900
        // Redeem 900 shares: assetsOut = 900 * 633 / 900 = 633
        auto const outAfter = vault.redeem(STAmount{vault.shareAsset(), 900}).value();
        BEAST_EXPECT(Number(outAfter) == Number(633));
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(0));
        BEAST_EXPECT(Number(vault.assetsAvailable()) == Number(0));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(0));
    }

    void
    testDepositZeroSharesLargeVault()
    {
        testcase("Deposit zero shares - vault too large relative to deposit");

        // In a large IOU vault (scale=6), a deposit that is too small relative to the
        // vault's total assets produces floor(shares) = 0, triggering tecPRECISION_LOSS.
        //
        // The assert in deposit() guards this. Here we verify the formula directly:
        //   shares = floor(deposit * sharesTotal / depositNAV)
        //
        // Case 1: Vault has 1e9 assets at scale=6 → sharesTotal = 1e15.
        //         depositNAV = assetsTotal = 1e9 (no loans).
        //         Deposit 5e-7 (0.0000005):
        //           rawShares = 5e-7 * 1e15 / 1e9 = 5e-7 * 1e6 = 0.5 → floor = 0
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        {
            Vault vault{usd};  // scale=6
            vault.deposit(STAmount{usd, UINT64_C(1'000'000'000)}).value();
            BEAST_EXPECT(Number(vault.sharesTotal()) == Number(1, 15));

            // 5e-7: rawShares = 5e-7 * 1e15 / 1e9 = 0.5 → floor = 0 → tecPRECISION_LOSS
            auto const tinyResult = vault.deposit(STAmount{usd, 5, -7});
            BEAST_EXPECT(!tinyResult);
            BEAST_EXPECT(tinyResult.error() == tecPRECISION_LOSS);
        }

        // Case 2: Same vault but deposit just above the threshold (1e-6 gives exactly 1 share).
        //         Deposit 9e-7: rawShares = 9e-7 * 1e15 / 1e9 = 0.9 → floor = 0 (still blocked)
        //         Deposit 1e-6: rawShares = 1e-6 * 1e15 / 1e9 = 1.0 → floor = 1 (allowed)
        {
            Vault vault{usd};  // scale=6
            vault.deposit(STAmount{usd, UINT64_C(1'000'000'000)}).value();

            // Just below threshold: 9e-7 → rawShares = 0.9 → floor = 0 → tecPRECISION_LOSS
            STAmount const justBelow{usd, 9, -7};
            auto const belowResult = vault.deposit(justBelow);
            BEAST_EXPECT(!belowResult);
            BEAST_EXPECT(belowResult.error() == tecPRECISION_LOSS);

            // At threshold: 1e-6 → exactly 1 share (deposit succeeds)
            auto const [sharesAt, assetsAt] = vault.deposit(STAmount{usd, 1, -6}).value();
            BEAST_EXPECT(Number(sharesAt) == Number(1));
        }
    }

    void
    testWithdrawAssetsOutLERequested()
    {
        testcase("Withdraw floor: assetsOut <= requested (spec-mandated invariant)");

        // When floor(requested * S / withdrawalNAV) = 3 but requested * S / withdrawalNAV = 3.75,
        // assetsOut = 3 * withdrawalNAV / S = 2.4 < 3.
        // This proves why floor is required: round would give shares=4, assetsOut=3.2 > requested.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        vault.deposit(STAmount{usd, 10}).value();
        // Borrow 2 (no interest) so paper loss is backed by loan principal
        vault.borrow(Number(2), Number(0));
        // iota=2: withdrawalNAV = 10 - 0 - 2 = 8
        vault.addPaperLoss(Number(2), Number(0));

        // Withdraw 3: rawShares = 3 * 10 / 8 = 3.75 → floor = 3
        // assetsOut = 3 * 8 / 10 = 2.4 < 3
        auto const [shares, assets] = vault.withdraw(STAmount{usd, 3}).value();
        BEAST_EXPECT(Number(shares) == Number(3));
        STAmount const expectedAssets{usd, Number(24, -1)};
        BEAST_EXPECT(assets == expectedAssets);
        // assetsOut strictly less than requested
        BEAST_EXPECT(Number(assets) < Number(3));
    }

    void
    testDepositActualAssetsLERequested()
    {
        testcase("Deposit floor: actualAssets <= requested (depositor keeps remainder)");

        // Floor on shares means actualAssets (back-calculated from floored shares) is
        // always <= the depositor's requested amount. The depositor keeps the remainder.
        // Vault: 10 assets, 7 shares (depositNAV=10).
        // Deposit 3: rawShares = 3*7/10 = 2.1 → floor = 2
        // actualAssets = 2 * 10 / 7 ≈ 2.857 < 3.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        // Seed with 7 assets → 7 shares, then add 3 extra via extra interest
        vault.deposit(STAmount{usd, 7}).value();
        vault.borrow(Number(1), Number(0));
        vault.repay(Number(1), Number(0), Number(3));
        // assetsTotal=10, sharesTotal=7, depositNAV=10

        // Deposit 3: rawShares = 3*7/10 = 2.1 → floor = 2
        // actualAssets = 2 * 10 / 7 ≈ 2.857 < 3
        auto const [shares, assets] = vault.deposit(STAmount{usd, 3}).value();
        BEAST_EXPECT(Number(shares) == Number(2));
        Number const expectedActual = (Number(2) * Number(10)) / Number(7);
        STAmount const expectedAmt{usd, expectedActual};
        BEAST_EXPECT(assets == expectedAmt);
        BEAST_EXPECT(Number(assets) < Number(3));
    }

    void
    testRedeemNonTerminatingFraction()
    {
        testcase("Redeem non-terminating fraction - STAmount precision truncation");

        // Vault: 1 asset, 3 shares (scale=0).
        // Redeeming 1 share gives assetsOut = 1/3 = 0.333... (non-terminating).
        // STAmount truncates to 16 significant digits on construction.
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};

        // Build vault with assetsTotal=1, sharesTotal=3:
        // Deposit 3 assets → 3 shares, hard-default 2 assets.
        Vault vault{usd, 0};
        vault.deposit(STAmount{usd, 3}).value();
        // Hard default 2 assets (no interest): assetsTotal=1, sharesTotal=3
        vault.borrow(Number(2), Number(0));
        vault.defaultLoan(Number(2), Number(0), false);
        BEAST_EXPECT(Number(vault.assetsTotal()) == Number(1));
        BEAST_EXPECT(Number(vault.sharesTotal()) == Number(3));

        // Redeem 1 share: assetsOut = 1 * 1 / 3 = 0.3333... (truncated by STAmount)
        auto const out = vault.redeem(STAmount{vault.shareAsset(), 1}).value();
        // Truncation floors: three copies of out must be <= 1 (no rounding up)
        BEAST_EXPECT(Number(out) * 3 <= Number(1));
        // And close: at most 1 ULP of (1/3) difference per copy → 3 copies within 3e-16 of 1
        BEAST_EXPECT(Number(1) - Number(out) * 3 < Number(1, -15));
        BEAST_EXPECT(Number(out) > Number(0));
    }

    void
    testSequentialVsBulkRedeem()
    {
        testcase("Sequential partial redeems vs single bulk redeem - STAmount drift");

        // Vault: 1 asset, 3 shares (scale=0), built same way as above.
        // Bulk: redeem(3) → 1 asset exactly (3 * 1/3 = 1 in Number, exact).
        // Sequential: redeem(1) × 3 → sum may differ due to STAmount truncation at each step.
        // The spec property: sequential_sum <= bulk (truncation always floors the output).
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};

        auto makeVault = [&]() {
            Vault v{usd, 0};
            v.deposit(STAmount{usd, 3}).value();
            v.borrow(Number(2), Number(0));
            v.defaultLoan(Number(2), Number(0), false);
            return v;
        };

        // Bulk redeem
        {
            auto vault = makeVault();
            auto const out = vault.redeem(STAmount{vault.shareAsset(), 3}).value();
            BEAST_EXPECT(Number(out) == Number(1));
        }

        // Sequential redeem × 3
        {
            auto vault = makeVault();
            Number total{0};
            for (int i = 0; i < 3; ++i)
                total += Number(vault.redeem(STAmount{vault.shareAsset(), 1}).value());

            // Sequential sum should be <= 1 (truncation can only lose, never gain)
            BEAST_EXPECT(total <= Number(1));
            // And not too far off: at most 3 ULP of 1/3 ≈ 3e-16 drift
            BEAST_EXPECT(Number(1) - total < Number(1, -15));
        }
    }

    void
    testWithdrawExactHalfBoundary()
    {
        testcase("Withdraw floor at exact 0.5 boundary - confirms floor, not round");

        // Vault: 10 assets, 10 shares, withdrawalNAV=10 (no loss/interest).
        // Withdraw 1.5: rawShares = 1.5 * 10 / 10 = 1.5 exactly.
        // floor(1.5) = 1  (not 2 — positive confirmation of floor behaviour)
        // assetsOut = 1 * 10 / 10 = 1.0
        Issue const usd{Currency(0x5553440000000000), AccountID(0x4985601)};
        Vault vault{usd, 0};

        vault.deposit(STAmount{usd, 10}).value();
        auto const [shares, assets] = vault.withdraw(STAmount{usd, 15, -1}).value();  // 1.5
        BEAST_EXPECT(Number(shares) == Number(1));
        BEAST_EXPECT(Number(assets) == Number(1));
        // assetsOut (1.0) < requested (1.5): floor discards the fractional share
        BEAST_EXPECT(Number(assets) < Number(15, -1));
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
        testLossOnlyVault();
        testMultipleDepositorsIOU();
        testPrecisionLoss();
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
        testReseedAfterFullDrain();
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
