#pragma once

#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>
#include <initializer_list>
#include <span>

namespace xrpl::test {

// Owns a locator's wire bytes so a test can hand a `FieldLocator` view to a host function.
// In production the bytes are guest memory; here they have to live somewhere, and the
// view borrows them - so an `OwnedLocator` must outlive every locator taken from it. As a
// temporary in the call expression, it does.
class OwnedLocator
{
    Bytes bytes_;

public:
    OwnedLocator(std::initializer_list<std::int32_t> steps)
    {
        bytes_.reserve(steps.size() * sizeof(std::int32_t));
        for (auto const step : steps)
        {
            auto const bits = static_cast<std::uint32_t>(step);
            for (auto i = 0U; i < sizeof(std::int32_t); ++i)
            {
                bytes_.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF));
            }
        }
    }

    // NOLINTNEXTLINE(google-explicit-constructor) - the point is that a call site reads
    // as if it were passing a locator.
    operator FieldLocator() const
    {
        return FieldLocator{std::span<std::uint8_t const>{bytes_}};
    }
};

// `locator({sfMemos.getCode(), 0, sfMemoData.getCode()})` - the steps as a contract writes
// them, laid out little-endian the way a guest would.
inline OwnedLocator
locator(std::initializer_list<std::int32_t> steps)
{
    return OwnedLocator{steps};
}

}  // namespace xrpl::test
