#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <vector>

namespace xrpl {

class LoanSet : public Transactor
{
private:
    static std::uint32_t
    getStartDate(ReadView const& view, STTx const& tx);
    static bool
    isTwoStepFlowEnabled(Rules const& rules);
    /* Returns true if the transaction is using the two-step flow. */
    static bool
    isTwoStepFlow(STTx const& tx);
    /* Returns true if the transaction is using the one-step flow. */
    static bool
    isOneStepFlow(STTx const& tx);

    /* Returns the counterparty account: the explicit Counterparty field if
     * present, otherwise the LoanBroker owner. */
    static AccountID
    getCounterparty(STTx const& tx, AccountID const& brokerOwner);
    /* Returns the borrower account. In the two-step flow this is the named
     * Borrower; in the immediate flow it is whichever of the signer /
     * counterparty is not the LoanBroker owner. */
    static AccountID
    getBorrower(STTx const& tx, AccountID const& brokerOwner, AccountID const& signingAccount);

    /* Holds the values validated and computed by doApply() that the flow
     * functions need to create the loan and update the ledger. The ledger
     * entries themselves are fetched (and their existence verified) by the flow
     * functions that mutate them; the derived borrower / counterparty account
     * IDs and the pure computed scalars are carried here so they are resolved
     * once, in doApply(), rather than in each flow function. */
    struct LoanPlan
    {
        uint256 brokerID;
        AccountID borrower;
        AccountID counterparty;
        Number principalRequested;
        Number originationFee;
        Number interestDue;
        LoanProperties properties;
        std::uint32_t paymentInterval;
        std::uint32_t paymentTotal;
    };

    /* Build the Loan ledger entry from the plan, setting the pending flag when
     * requested. Does not insert the entry into the view. */
    std::shared_ptr<SLE>
    buildLoan(LoanPlan const& plan, SLE::ref brokerSle, bool pending);

    /* Create a pending loan for the two-step flow: charge the broker owner the
     * owner reserve, create the loan flagged pending, reserve the principal in
     * the vault, and link the loan into the broker directory only. */
    TER
    applyPendingLoan(LoanPlan const& plan);

    /* Create an active loan for the immediate flow: charge the borrower the
     * owner reserve, disburse the funds, create the loan, update the vault, and
     * link the loan into both the broker and borrower directories. */
    TER
    applyImmediateLoan(LoanPlan const& plan);

public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit LoanSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static NotTEC
    checkSign(PreclaimContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static std::vector<OptionaledField<STNumber>> const&
    getValueFields();

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    void
    visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

public:
    static constexpr std::uint32_t kMinPaymentTotal = 1;
    static constexpr std::uint32_t kDefaultPaymentTotal = 1;
    static_assert(kDefaultPaymentTotal >= kMinPaymentTotal);

    static constexpr std::uint32_t kMinPaymentInterval = 60;
    static constexpr std::uint32_t kDefaultPaymentInterval = 60;
    static_assert(kDefaultPaymentInterval >= kMinPaymentInterval);

    static constexpr std::uint32_t kDefaultGracePeriod = 60;
    static_assert(kDefaultGracePeriod >= kMinPaymentInterval);
};

//------------------------------------------------------------------------------

}  // namespace xrpl
