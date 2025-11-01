#ifndef XRPL_APP_MISC_FEEVOTE_H_INCLUDED
#define XRPL_APP_MISC_FEEVOTE_H_INCLUDED

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/shamap/SHAMap.h>

namespace ripple {

/** Manager to process fee votes. */
class FeeVote
{
public:
    virtual ~FeeVote() = default;

    /** Add local fee preference to validation.

        @param lastClosedLedger
        @param baseValidation
    */
    virtual void
    doValidation(
        Fees const& lastFees,
        Rules const& rules,
        STValidation& val) = 0;

    /** Cast our local vote on the fee.

        @param lastClosedLedger
        @param initialPosition
    */
    virtual void
    doVoting(
        std::shared_ptr<ReadView const> const& lastClosedLedger,
        std::vector<std::shared_ptr<STValidation>> const& parentValidations,
        std::shared_ptr<SHAMap> const& initialPosition) = 0;
};

struct FeeSetup;
/** Create an instance of the FeeVote logic.
    @param setup The fee schedule to vote for.
    @param journal Where to log.
*/
std::unique_ptr<FeeVote>
make_FeeVote(FeeSetup const& setup, beast::Journal journal);

}  // namespace ripple

#endif
