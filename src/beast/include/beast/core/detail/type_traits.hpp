//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_TYPE_TRAITS_HPP
#define BEAST_DETAIL_TYPE_TRAITS_HPP

#include <tuple>
#include <type_traits>
#include <stdexcept>
#include <string>

namespace beast {
namespace detail {

template<class... Ts>
struct make_void
{
    using type = void;
};

template<class... Ts>
using void_t = typename make_void<Ts...>::type;

template<class... Ts>
inline
void
ignore_unused(Ts const& ...)
{
}

template<class... Ts>
inline
void
ignore_unused()
{}

template<class U>
std::size_t constexpr
max_sizeof()
{
    return sizeof(U);
}

template<class U0, class U1, class... Us>
std::size_t constexpr
max_sizeof()
{
    return
        max_sizeof<U0>() > max_sizeof<U1, Us...>() ?
        max_sizeof<U0>() : max_sizeof<U1, Us...>();
}

template<unsigned N, class T, class... Tn>
struct repeat_tuple_impl
{
    using type = typename repeat_tuple_impl<
        N - 1, T, T, Tn...>::type;
};

template<class T, class... Tn>
struct repeat_tuple_impl<0, T, Tn...>
{
    using type = std::tuple<T, Tn...>;
};

template<unsigned N, class T>
struct repeat_tuple
{
    using type =
        typename repeat_tuple_impl<N-1, T>::type;
};

template<class T>
struct repeat_tuple<0, T>
{
    using type = std::tuple<>;
};

template<class Exception>
Exception
make_exception(char const* reason, char const* file, int line)
{
	char const* n = file;
	for(auto p = file; *p; ++p)
		if(*p == '\\' || *p == '/')
			n = p + 1;
	return Exception{std::string(reason) + " (" +
		n + ":" + std::to_string(line) + ")"};
}

} // detail
} // beast

#endif
