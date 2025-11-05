#ifndef XRPL_SERVER_LOWESTLAYER_H_INCLUDED
#define XRPL_SERVER_LOWESTLAYER_H_INCLUDED

#if BOOST_VERSION >= 107000
#include <boost/beast/core/stream_traits.hpp>
#else
#include <boost/beast/core/type_traits.hpp>
#endif

namespace ripple {

// Before boost 1.70, get_lowest_layer required an explicit templat parameter
template <class T>
decltype(auto)
get_lowest_layer(T& t) noexcept
{
#if BOOST_VERSION >= 107000
    return boost::beast::get_lowest_layer(t);
#else
    return t.lowest_layer();
#endif
}

}  // namespace ripple

#endif
