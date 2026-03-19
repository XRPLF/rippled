#include <xrpl/tx/invariants/LoanInvariant.h>
//
#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TxFormats.h>

namespace xrpl {

void
ValidLoanBroker::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (after)
    {
        if (after->getType() == ltLOAN_BROKER)
        {
            auto& broker = brokers_[after->key()];
            broker.brokerBefore = before;
            broker.brokerAfter = after;
        }
        else if (after->getType() == ltACCOUNT_ROOT && after->isFieldPresent(sfLoanBrokerID))
        {
            auto const& loanBrokerID = after->at(sfLoanBrokerID);
            // create an entry if one doesn't already exist
            brokers_.emplace(loanBrokerID, BrokerInfo{});
        }
        else if (after->getType() == ltRIPPLE_STATE)
        {
            lines_.emplace_back(after);
        }
        else if (after->getType() == ltMPTOKEN)
        {
            mpts_.emplace_back(after);
        }
    }
}

bool
ValidLoanBroker::goodZeroDirectory(
    ReadView const& view,
    SLE::const_ref dir,
    beast::Journal const& j) const
{
    auto const next = dir->at(~sfIndexNext);
    auto const prev = dir->at(~sfIndexPrevious);
    if ((prev && *prev) || (next && *next))
    {
        JLOG(j.fatal()) << "Invariant failed: Loan Broker with zero "
                           "OwnerCount has multiple directory pages";
        return false;
    }
    auto indexes = dir->getFieldV256(sfIndexes);
    if (indexes.size() > 1)
    {
        JLOG(j.fatal()) << "Invariant failed: Loan Broker with zero "
                           "OwnerCount has multiple indexes in the Directory root";
        return false;
    }
    if (indexes.size() == 1)
    {
        auto const index = indexes.value().front();
        auto const sle = view.read(keylet::unchecked(index));
        if (!sle)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker directory corrupt";
            return false;
        }
        if (sle->getType() != ltRIPPLE_STATE && sle->getType() != ltMPTOKEN)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker with zero "
                               "OwnerCount has an unexpected entry in the directory";
            return false;
        }
    }

    return true;
}

bool
ValidLoanBroker::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // Loan Brokers will not exist on ledger if the Lending Protocol amendment
    // is not enabled, so there's no need to check it.

    for (auto const& line : lines_)
    {
        for (auto const& field : {&sfLowLimit, &sfHighLimit})
        {
            auto const account = view.read(keylet::account(line->at(*field).getIssuer()));
            // This Invariant doesn't know about the rules for Trust Lines, so
            // if the account is missing, don't treat it as an error. This
            // loop is only concerned with finding Broker pseudo-accounts
            if (account && account->isFieldPresent(sfLoanBrokerID))
            {
                auto const& loanBrokerID = account->at(sfLoanBrokerID);
                // create an entry if one doesn't already exist
                brokers_.emplace(loanBrokerID, BrokerInfo{});
            }
        }
    }
    for (auto const& mpt : mpts_)
    {
        auto const account = view.read(keylet::account(mpt->at(sfAccount)));
        // This Invariant doesn't know about the rules for MPTokens, so
        // if the account is missing, don't treat is as an error. This
        // loop is only concerned with finding Broker pseudo-accounts
        if (account && account->isFieldPresent(sfLoanBrokerID))
        {
            auto const& loanBrokerID = account->at(sfLoanBrokerID);
            // create an entry if one doesn't already exist
            brokers_.emplace(loanBrokerID, BrokerInfo{});
        }
    }

    for (auto const& [brokerID, broker] : brokers_)
    {
        auto const& after =
            broker.brokerAfter ? broker.brokerAfter : view.read(keylet::loanbroker(brokerID));

        if (!after)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker missing";
            return false;
        }

        auto const& before = broker.brokerBefore;

        // https://github.com/Tapanito/XRPL-Standards/blob/xls-66-lending-protocol/XLS-0066d-lending-protocol/README.md#3123-invariants
        // If `LoanBroker.OwnerCount = 0` the `DirectoryNode` will have at most
        // one node (the root), which will only hold entries for `RippleState`
        // or `MPToken` objects.
        if (after->at(sfOwnerCount) == 0)
        {
            auto const dir = view.read(keylet::ownerDir(after->at(sfAccount)));
            if (dir)
            {
                if (!goodZeroDirectory(view, dir, j))
                {
                    return false;
                }
            }
        }
        if (before && before->at(sfLoanSequence) > after->at(sfLoanSequence))
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker sequence number "
                               "decreased";
            return false;
        }
        if (after->at(sfDebtTotal) < 0)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker debt total is negative";
            return false;
        }
        if (after->at(sfCoverAvailable) < 0)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker cover available is negative";
            return false;
        }
        auto const vault = view.read(keylet::vault(after->at(sfVaultID)));
        if (!vault)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker vault ID is invalid";
            return false;
        }
        auto const& vaultAsset = vault->at(sfAsset);
        if (after->at(sfCoverAvailable) < accountHolds(
                                              view,
                                              after->at(sfAccount),
                                              vaultAsset,
                                              FreezeHandling::fhIGNORE_FREEZE,
                                              AuthHandling::ahIGNORE_AUTH,
                                              j))
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Broker cover available "
                               "is less than pseudo-account asset balance";
            return false;
        }
    }
    return true;
}

//------------------------------------------------------------------------------

void
ValidLoan::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltLOAN)
    {
        loans_.emplace_back(before, after);
    }
}

bool
ValidLoan::finalize(
    STTx const& tx,
    TER const,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // Loans will not exist on ledger if the Lending Protocol amendment
    // is not enabled, so there's no need to check it.

    for (auto const& [before, after] : loans_)
    {
        // https://github.com/Tapanito/XRPL-Standards/blob/xls-66-lending-protocol/XLS-0066d-lending-protocol/README.md#3223-invariants
        // If `Loan.PaymentRemaining = 0` then the loan MUST be fully paid off
        if (after->at(sfPaymentRemaining) == 0 &&
            (after->at(sfTotalValueOutstanding) != beast::zero ||
             after->at(sfPrincipalOutstanding) != beast::zero ||
             after->at(sfManagementFeeOutstanding) != beast::zero))
        {
            JLOG(j.fatal()) << "Invariant failed: Loan with zero payments "
                               "remaining has not been paid off";
            return false;
        }
        // If `Loan.PaymentRemaining != 0` then the loan MUST NOT be fully paid
        // off
        if (after->at(sfPaymentRemaining) != 0 &&
            after->at(sfTotalValueOutstanding) == beast::zero &&
            after->at(sfPrincipalOutstanding) == beast::zero &&
            after->at(sfManagementFeeOutstanding) == beast::zero)
        {
            JLOG(j.fatal()) << "Invariant failed: Loan with zero payments "
                               "remaining has not been paid off";
            return false;
        }
        if (before && (before->isFlag(lsfLoanOverpayment) != after->isFlag(lsfLoanOverpayment)))
        {
            JLOG(j.fatal()) << "Invariant failed: Loan Overpayment flag changed";
            return false;
        }
        // Must not be negative - STNumber
        for (auto const field :
             {&sfLoanServiceFee,
              &sfLatePaymentFee,
              &sfClosePaymentFee,
              &sfPrincipalOutstanding,
              &sfTotalValueOutstanding,
              &sfManagementFeeOutstanding})
        {
            if (after->at(*field) < 0)
            {
                JLOG(j.fatal()) << "Invariant failed: " << field->getName() << " is negative ";
                return false;
            }
        }
        // Must be positive - STNumber
        for (auto const field : {
                 &sfPeriodicPayment,
             })
        {
            if (after->at(*field) <= 0)
            {
                JLOG(j.fatal()) << "Invariant failed: " << field->getName()
                                << " is zero or negative ";
                return false;
            }
        }
    }
    return true;
}

}  // namespace xrpl
