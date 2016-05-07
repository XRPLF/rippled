//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_TEST_FAIL_COUNTER_HPP
#define BEAST_TEST_FAIL_COUNTER_HPP

#include <beast/core/error.hpp>

namespace beast {
namespace test {

/** A countdown to simulated failure.

    On the Nth operation, the class will fail with the specified
    error code, or the default error code of invalid_argument.
*/
class fail_counter
{
    std::size_t n_;
    error_code ec_;

public:
    fail_counter(fail_counter&&) = default;

    /** Construct a counter.

        @param The 0-based index of the operation to fail on or after.
    */
    explicit
    fail_counter(std::size_t n = 0)
        : n_(n)
        , ec_(boost::system::errc::make_error_code(
            boost::system::errc::errc_t::invalid_argument))
    {
    }

    /// Throw an exception on the Nth failure
    void
    fail()
    {
        if(n_ > 0)
            --n_;
        if(! n_)
            throw system_error{ec_};
    }

    /// Set an error code on the Nth failure
    bool
    fail(error_code& ec)
    {
        if(n_ > 0)
            --n_;
        if(! n_)
        {
            ec = ec_;
            return true;
        }
        return false;
    }
};

} // test
} // beast

#endif
