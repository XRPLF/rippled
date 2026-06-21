#pragma once

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <cstring>

namespace xrpl::test::formal_verification {

// Deterministic synthetic ids/assets for fixture entries.
template <class Id>
inline Id
fillId(uint8_t b)
{
    Id x;
    std::memset(x.data(), b, x.size());
    return x;
}

inline Asset
iouAsset()
{
    return Asset{Issue{fillId<Currency>(0x01), fillId<AccountID>(0x02)}};
}

inline Asset
mptAsset()
{
    return Asset{MPTIssue{makeMptID(2, fillId<AccountID>(0x94))}};
}

}  // namespace xrpl::test::formal_verification
