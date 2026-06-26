#include <helpers/TxTest.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/CanonicalTXSet.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol_autogen/ledger_entries/AccountRoot.h>
#include <xrpl/protocol_autogen/ledger_entries/RippleState.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>
#include <xrpl/protocol_autogen/transactions/Payment.h>
#include <xrpl/tx/apply.h>

#include <helpers/Account.h>
#include <helpers/IOU.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xrpl::test {

//------------------------------------------------------------------------------
// Feature helpers
//------------------------------------------------------------------------------

FeatureBitset
allFeatures()
{
    static FeatureBitset const kFeatures = [] {
        auto const& sa = allAmendments();
        std::vector<uint256> feats;
        feats.reserve(sa.size());
        for ([[maybe_unused]] auto const& [name, _] : sa)
        {
            if (auto const f = getRegisteredFeature(name); f.has_value())
                feats.push_back(*f);
        }
        return FeatureBitset(feats);
    }();
    return kFeatures;
}

//------------------------------------------------------------------------------
// TxTest
//------------------------------------------------------------------------------

TxTest::TxTest(std::optional<FeatureBitset> features)
{
    // Convert FeatureBitset to unordered_set for Rules constructor
    auto const featureBits = features.value_or(allFeatures());
    foreachFeature(featureBits, [&](uint256 const& f) { featureSet_.insert(f); });

    // Create rules with the specified features
    rules_.emplace(featureSet_);

    // Default fees for testing
    Fees const fees{XRPAmount{10}, XRPAmount{10000000}, XRPAmount{2000000}};

    // Create a genesis ledger as the base
    closedLedger_ = std::make_shared<Ledger>(
        kCreateGenesis,
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        *rules_,
        fees,
        std::vector<uint256>{featureSet_.begin(), featureSet_.end()},
        registry_.getNodeFamily());

    // Initialize time from the genesis ledger
    now_ = closedLedger_->header().closeTime;

    // Create an open view on top of the genesis ledger
    openLedger_ =
        std::make_shared<OpenView>(kOpenLedger, closedLedger_.get(), *rules_, closedLedger_);
}

bool
TxTest::isEnabled(uint256 const& feature) const
{
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return rules_->enabled(feature);
}

Rules const&
TxTest::getRules() const
{
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return *rules_;
}

[[nodiscard]] TxResult
TxTest::submit(std::shared_ptr<STTx const> stx)
{
    auto result = apply(registry_, *openLedger_, *stx, TapNone, registry_.getJournal("apply"));

    // Track successfully applied transactions for canonical reordering on close
    // We make a copy since the TransactionBase doesn't own the STTx
    if (result.applied)
        pendingTxs_.push_back(stx);

    return TxResult{
        .ter = result.ter,
        .applied = result.applied,
        .metadata = std::move(result).metadata,
        .tx = std::move(stx)};
}

void
TxTest::createAccount(Account const& account, XRPAmount xrp, uint32_t accountFlags)
{
    auto const paymentTer =
        submit(transactions::PaymentBuilder{Account::kMaster, account, xrp}, Account::kMaster).ter;

    if (paymentTer != tesSUCCESS)
    {
        throw std::runtime_error("TxTest::createAccount: failed to create account");
    }

    close();

    if (accountFlags != 0)
    {
        auto const accountSetTer =
            submit(transactions::AccountSetBuilder{account}.setSetFlag(accountFlags), account).ter;
        if (accountSetTer != tesSUCCESS)
        {
            throw std::runtime_error("TxTest::createAccount: failed to set account flags");
        }
        close();
    }
}

ledger_entries::AccountRoot
TxTest::getAccountRoot(AccountID const& id) const
{
    auto const sle = getOpenLedger().read(keylet::account(id));
    if (!sle)
        Throw<std::runtime_error>("TxTest::getAccountRoot: account not found");
    return ledger_entries::AccountRoot{std::const_pointer_cast<SLE const>(sle)};
}

OpenView&
TxTest::getOpenLedger()
{
    return *openLedger_;
}

OpenView const&
TxTest::getOpenLedger() const
{
    return *openLedger_;
}

ReadView const&
TxTest::getClosedLedger() const
{
    return *closedLedger_;
}

void
TxTest::close()
{
    // Build a new closed ledger from the previous closed ledger,
    // similar to how buildLedgerImpl works:
    // 1. Create a new Ledger from the previous closed ledger
    // 2. Re-apply transactions in canonical order
    // 3. Mark it as accepted/immutable

    auto const& prevLedger = *closedLedger_;

    auto const ledgerCloseTime = now_ + prevLedger.header().closeTimeResolution;

    now_ = ledgerCloseTime;

    auto newLedger = std::make_shared<Ledger>(prevLedger, ledgerCloseTime);

    CanonicalTXSet txSet(prevLedger.header().hash);
    for (auto const& tx : pendingTxs_)
        txSet.insert(tx);

    {
        OpenView accum(&*newLedger);
        for (auto const& [key, tx] : txSet)
        {
            auto result = apply(registry_, accum, *tx, TapNone, registry_.getJournal("apply"));
            if (!result.applied)
            {
                throw std::runtime_error("TxTest::close: failed to apply transaction");
            }
        }
        accum.apply(*newLedger);
    }

    newLedger->setAccepted(ledgerCloseTime, newLedger->header().closeTimeResolution, true);

    closedLedger_ = newLedger;

    pendingTxs_.clear();

    openLedger_ =
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        std::make_shared<OpenView>(kOpenLedger, closedLedger_.get(), *rules_, closedLedger_);
}

void
TxTest::advanceTime(NetClock::duration duration)
{
    now_ += duration;
}

NetClock::time_point
TxTest::getCloseTime() const
{
    return now_;
}

STAmount
TxTest::getBalance(AccountID const& account, IOU const& iou) const
{
    auto const sle = openLedger_->read(keylet::line(account, iou.issue()));
    if (!sle)
        return STAmount{iou.issue(), 0};

    auto const rippleState = ledger_entries::RippleState{sle};

    auto balance = rippleState.getBalance();
    if (iou.issue().account == account)
    {
        throw std::logic_error("TxTest::getBalance: account is issuer");
    }

    balance.get<Issue>().account = iou.issue().account;
    if (account > iou.issue().account)
        balance.negate();
    return balance;
}

}  // namespace xrpl::test
