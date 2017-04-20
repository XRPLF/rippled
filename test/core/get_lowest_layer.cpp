//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/detail/get_lowest_layer.hpp>

#include <beast/unit_test/suite.hpp>
#include <type_traits>

namespace beast {
namespace detail {

class get_lowest_layer_test
    : public beast::unit_test::suite
{
public:
    struct F1
    {
    };

    struct F2
    {
    };

    template<class F>
    struct F3
    {
        using next_layer_type =
            typename std::remove_reference<F>::type;

        using lowest_layer_type = typename
            get_lowest_layer<next_layer_type>::type;
    };

    template<class F>
    struct F4
    {
        using next_layer_type =
            typename std::remove_reference<F>::type;

        using lowest_layer_type = typename
            get_lowest_layer<next_layer_type>::type;
    };

    void
    run()
    {
        static_assert(! has_lowest_layer<F1>::value, "");
        static_assert(! has_lowest_layer<F2>::value, "");
        static_assert(has_lowest_layer<F3<F1>>::value, "");
        static_assert(has_lowest_layer<F4<F3<F2>>>::value, "");

        static_assert(std::is_same<
            get_lowest_layer<F1>::type, F1>::value, "");

        static_assert(std::is_same<
            get_lowest_layer<F2>::type, F2>::value, "");

        static_assert(std::is_same<
            get_lowest_layer<F3<F1>>::type, F1>::value, "");

        static_assert(std::is_same<
            get_lowest_layer<F3<F2>>::type, F2>::value, "");

        static_assert(std::is_same<
            get_lowest_layer<F4<F1>>::type, F1>::value, "");

        static_assert(std::is_same<
            get_lowest_layer<F4<F2>>::type, F2>::value, "");

        static_assert(std::is_same<
            get_lowest_layer<F4<F3<F1>>>::type, F1>::value, "");

        static_assert(std::is_same<
            get_lowest_layer<F4<F3<F2>>>::type, F2>::value, "");

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(get_lowest_layer,core,beast);

} // detail
} // beast
