#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace xrpl {

/**
 * A fixed-size array indexed by a scoped enumeration.
 *
 * This is a thin wrapper around std::array whose subscript operator accepts
 * the enumeration and nothing else: an integer is not accepted, and neither
 * is a different enumeration.
 *
 * @code
 * enum class Color : std::uint8_t { Red, Green, Blue };
 *
 * constexpr EnumArray<char const*, 3, Color> kColorNames{{{"red", "green", "blue"}}};
 *
 * auto const name = kColorNames[Color::Green];
 * @endcode
 *
 * This is an aggregate, and is initialized the way std::array is.
 *
 * The element for an enumerator is the one at the position given by the
 * enumerator's value, so they must range over [0, 1, ... N - 1], but we
 * cannot check or enforce this here; an enumerator with a value of N or
 * more indexes past the end of the array.
 *
 * @tparam T The element type.
 * @tparam N The number of elements.
 * @tparam Enum The scoped enumeration to index by.
 */
template <typename T, std::size_t N, typename Enum>
    requires std::is_scoped_enum_v<Enum>
struct EnumArray
{
    /**
     * The elements, in enumerator order.
     *
     * This is public only so that the class is an aggregate. Use it directly
     * when a position is needed instead of an enumerator.
     */
    std::array<T, N> values;

    /**
     * Access the element for an enumerator.
     *
     * @param self The array; constness and value category are forwarded to the result.
     * @param e The enumerator.
     * @return A reference to the corresponding element.
     */
    template <typename Self>
    [[nodiscard]] constexpr auto&&
    operator[](this Self&& self, Enum e) noexcept
    {
        auto const i = static_cast<std::size_t>(std::to_underlying(e));

        if !consteval
        {
            XRPL_ASSERT(i < N, "xrpl::EnumArray::operator[] : index in range");
        }

        return std::forward_like<Self>(self.values[i]);
    }

    /**
     * @return An iterator to the first element.
     */
    [[nodiscard]] constexpr auto
    begin(this auto&& self) noexcept
    {
        return self.values.begin();
    }

    /**
     * @return An iterator one past the last element.
     */
    [[nodiscard]] constexpr auto
    end(this auto&& self) noexcept
    {
        return self.values.end();
    }

    /**
     * @return The number of elements.
     */
    [[nodiscard]] constexpr std::size_t
    size() const noexcept
    {
        return N;
    }
};

}  // namespace xrpl
