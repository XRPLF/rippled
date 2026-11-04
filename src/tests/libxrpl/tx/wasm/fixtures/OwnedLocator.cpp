#include <tx/wasm/fixtures/OwnedLocator.h>

#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>
#include <initializer_list>
#include <span>

namespace xrpl::test {

OwnedLocator::OwnedLocator(std::initializer_list<std::int32_t> steps)
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

OwnedLocator::
operator FieldLocator() const
{
    return FieldLocator{std::span<std::uint8_t const>{bytes_}};
}

OwnedLocator
locator(std::initializer_list<std::int32_t> steps)
{
    return OwnedLocator{steps};
}

}  // namespace xrpl::test
