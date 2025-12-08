#ifndef XRPL_BASICS_COMPARATORS_H_INCLUDED
#define XRPL_BASICS_COMPARATORS_H_INCLUDED

#include <functional>

namespace ripple {

#ifdef _MSC_VER

/*
 * MSVC 2019 version 16.9.0 added [[nodiscard]] to the std comparison
 * operator() functions. boost::bimap checks that the comparator is a
 * BinaryFunction, in part by calling the function and ignoring the value.
 * These two things don't play well together. These wrapper classes simply
 * strip [[nodiscard]] from operator() for use in boost::bimap.
 *
 * See also:
 *  https://www.boost.org/doc/libs/1_75_0/libs/bimap/doc/html/boost_bimap/the_tutorial/controlling_collection_types.html
 */

template <class T = void>
struct less
{
    using result_type = bool;

    constexpr bool
    operator()(T const& left, T const& right) const
    {
        return std::less<T>()(left, right);
    }
};

template <class T = void>
struct equal_to
{
    using result_type = bool;

    constexpr bool
    operator()(T const& left, T const& right) const
    {
        return std::equal_to<T>()(left, right);
    }
};

#else

template <class T = void>
using less = std::less<T>;

template <class T = void>
using equal_to = std::equal_to<T>;

#endif

}  // namespace ripple

#endif
