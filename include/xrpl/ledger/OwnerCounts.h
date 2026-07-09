#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STInteger.h>  // IWYU pragma: keep
#include <xrpl/protocol/STLedgerEntry.h>

#include <cstdint>
#include <limits>

namespace xrpl {

struct OwnerCounts
{
    std::uint32_t owner = 0;
    std::uint32_t sponsored = 0;
    std::uint32_t sponsoring = 0;

    OwnerCounts() = default;
    OwnerCounts(SLE::const_ref sle)
        : owner(sle->getFieldU32(sfOwnerCount))
        , sponsored(sle->at(~sfSponsoredOwnerCount).value_or(0))
        , sponsoring(sle->at(~sfSponsoringOwnerCount).value_or(0))
    {
        XRPL_ASSERT(
            owner >= sponsored,
            "xrpl::OwnerCounts : OwnerCount must be greater than or equal to "
            "SponsoredOwnerCount");
        XRPL_ASSERT(sle->getType() == ltACCOUNT_ROOT, "xrpl::OwnerCounts : sle is AccountRoot");
    }

    [[nodiscard]] std::uint32_t
    count() const
    {
        std::int64_t const x = static_cast<std::int64_t>(owner) - sponsored + sponsoring;
        if (x < 0)
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::OwnerCounts::count : count less than zero");
            return 0;
            // LCOV_EXCL_STOP
        }

        if (x > std::numeric_limits<std::uint32_t>::max())
            return std::numeric_limits<std::uint32_t>::max();
        return static_cast<std::uint32_t>(x);
    }

    auto
    operator<=>(OwnerCounts const& o) const
    {
        if (auto cmp = count() <=> o.count(); cmp != 0)
            return cmp;
        if (auto cmp = owner <=> o.owner; cmp != 0)
            return cmp;
        if (auto cmp = sponsored <=> o.sponsored; cmp != 0)
            return cmp;
        return sponsoring <=> o.sponsoring;
    }

    bool
    operator==(OwnerCounts const& o) const
    {
        return this == &o ||
            (owner == o.owner && sponsored == o.sponsored && sponsoring == o.sponsoring);
    }

    [[nodiscard]] bool
    valid() const
    {
        int64_t const x = static_cast<int64_t>(owner) - sponsored + sponsoring;
        if (x < 0 || x > std::numeric_limits<std::uint32_t>::max())
            return false;
        return owner >= sponsored;
    }
};

}  // namespace xrpl
