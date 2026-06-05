#pragma once

#include <optional>
#include <type_traits>

namespace xrpl::protocol_autogen {

template <typename ValueType>
using Optional = std::conditional_t<
    std::is_reference_v<ValueType>,
    std::optional<std::reference_wrapper<std::remove_reference_t<ValueType>>>,
    std::optional<ValueType>>;

}
