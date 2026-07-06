#pragma once

#include <boost/beast/core/stream_traits.hpp>

namespace xrpl {

template <class T>
decltype(auto)
getLowestLayer(T& t) noexcept
{
    return boost::beast::get_lowest_layer(t);
}

}  // namespace xrpl
