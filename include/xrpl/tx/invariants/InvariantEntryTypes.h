#pragma once

#include <xrpl/protocol/LedgerFormats.h>

namespace xrpl {

struct VisitAllLedgerEntryTypes
{
    static constexpr bool visitsAll = true;
    static constexpr bool visitsNone = false;
    static constexpr bool empty = false;

    template <LedgerEntryType>
    static consteval bool
    contains()
    {
        return true;
    }
};

struct VisitNoLedgerEntryTypes
{
    static constexpr bool visitsAll = false;
    static constexpr bool visitsNone = true;
    static constexpr bool empty = true;

    template <LedgerEntryType>
    static consteval bool
    contains()
    {
        return false;
    }
};

template <LedgerEntryType... Types>
struct VisitLedgerEntryTypes
{
    static constexpr bool visitsAll = false;
    static constexpr bool visitsNone = false;
    static constexpr bool empty = sizeof...(Types) == 0;

    template <LedgerEntryType Type>
    static consteval bool
    contains()
    {
        return ((Type == Types) || ...);
    }
};

}  // namespace xrpl
