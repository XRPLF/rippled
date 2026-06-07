#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <vector>

namespace xrpl {

/**
 * @brief Invariants: Loans are internally consistent
 *
 * 1. If `Loan.PaymentRemaining = 0` then `Loan.PrincipalOutstanding = 0`
 *
 */
class ValidLoan
{
    // Pair is <before, after>. After is used for most of the checks, except
    // those that check changed values.
    std::vector<std::pair<SLE::const_pointer, SLE::const_pointer>> loans_;

public:
    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
