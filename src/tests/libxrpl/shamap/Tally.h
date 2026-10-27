#pragma once

#include <xrpl/shamap/SHAMapAddNode.h>

namespace xrpl::tests {

/**
 * Whether a batch verdict carries exactly the given counts.
 *
 * @param san The verdict to check.
 * @param good How many nodes the batch should have hooked in.
 * @param bad How many it should have rejected.
 * @param duplicate How many it should have already held.
 * @return Whether the verdict matches.
 */
[[nodiscard]] inline bool
tallyIs(SHAMapAddNode const& san, int good, int bad, int duplicate)
{
    return san.getGood() == good && san.getBad() == bad && san.getDuplicate() == duplicate;
}

}  // namespace xrpl::tests
