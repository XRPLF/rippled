#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <vector>

namespace xrpl {

/**
 * @brief Invariants: Permissioned Domains must have some rules and
 * AcceptedCredentials must have length between 1 and 10 inclusive.
 *
 * Since only permissions constitute rules, an empty credentials list
 * means that there are no rules and the invariant is violated.
 *
 * Credentials must be sorted and no duplicates allowed
 *
 */
class ValidPermissionedDomain
{
    struct SleStatus
    {
        std::size_t credentialsSize_{0};
        bool isSorted_ = false;
        bool isUnique_ = false;
        bool isDelete_ = false;
    };
    std::vector<SleStatus> sleStatus_;

public:
    void
    visitEntry(bool, std::shared_ptr<SLE const> const&, std::shared_ptr<SLE const> const&);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
