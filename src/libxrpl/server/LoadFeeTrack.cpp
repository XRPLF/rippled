#include <xrpl/server/LoadFeeTrack.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace xrpl {

bool
LoadFeeTrack::raiseLocalFee()
{
    std::scoped_lock const sl(lock_);

    if (++raiseCount_ < 2)
        return false;

    std::uint32_t const origFee = localTxnLoadFee_;

    // make sure this fee takes effect
    localTxnLoadFee_ = std::max(localTxnLoadFee_, remoteTxnLoadFee_);

    // Increase slowly
    localTxnLoadFee_ += (localTxnLoadFee_ / kLftFeeIncFraction);

    localTxnLoadFee_ = std::min(localTxnLoadFee_, kLftFeeMax);

    if (origFee == localTxnLoadFee_)
        return false;

    JLOG(j_.debug()) << "Local load fee raised from " << origFee << " to " << localTxnLoadFee_;
    return true;
}

bool
LoadFeeTrack::lowerLocalFee()
{
    std::scoped_lock const sl(lock_);
    std::uint32_t const origFee = localTxnLoadFee_;
    raiseCount_ = 0;

    // Reduce slowly
    localTxnLoadFee_ -= (localTxnLoadFee_ / kLftFeeDecFraction);

    localTxnLoadFee_ = std::max(localTxnLoadFee_, kLftNormalFee);

    if (origFee == localTxnLoadFee_)
        return false;

    JLOG(j_.debug()) << "Local load fee lowered from " << origFee << " to " << localTxnLoadFee_;
    return true;
}

//------------------------------------------------------------------------------

// Scale using load as well as base rate
XRPAmount
scaleFeeLoad(XRPAmount fee, LoadFeeTrack const& feeTrack, Fees const& fees, bool bUnlimited)
{
    if (fee == 0)
        return fee;

    // Collect the fee rates
    auto [feeFactor, uRemFee] = feeTrack.getScalingFactors();

    // Let privileged users pay the normal fee until
    //   the local load exceeds four times the remote.
    if (bUnlimited && (feeFactor > uRemFee) && (feeFactor < (4 * uRemFee)))
        feeFactor = uRemFee;

    // Compute:
    // fee = fee * feeFactor / (lftNormalFee);
    // without overflow, and as accurately as possible

    auto const result = mulDiv(fee, feeFactor, safeCast<std::uint64_t>(feeTrack.getLoadBase()));
    if (!result)
        Throw<std::overflow_error>("scaleFeeLoad");
    return *result;
}

}  // namespace xrpl
