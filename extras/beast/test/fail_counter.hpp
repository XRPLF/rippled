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

enum error
{
    fail_error = 1
};

namespace detail {

class fail_error_category : public boost::system::error_category
{
public:
    const char*
    name() const noexcept override
    {
        return "test";
    }

    std::string
    message(int ev) const override
    {
        switch(static_cast<error>(ev))
        {
        default:
        case error::fail_error:
            return "test error";
        }
    }

    boost::system::error_condition
    default_error_condition(int ev) const noexcept override
    {
        return boost::system::error_condition{ev, *this};
    }

    bool
    equivalent(int ev,
        boost::system::error_condition const& condition
            ) const noexcept override
    {
        return condition.value() == ev &&
            &condition.category() == this;
    }

    bool
    equivalent(error_code const& error, int ev) const noexcept override
    {
        return error.value() == ev &&
            &error.category() == this;
    }
};

inline
boost::system::error_category const&
get_error_category()
{
    static fail_error_category const cat{};
    return cat;
}

} // detail

inline
error_code
make_error_code(error ev)
{
    return error_code{static_cast<int>(ev),
        detail::get_error_category()};
}

/** A countdown to simulated failure.

    On the Nth operation, the class will fail with the specified
    error code, or the default error code of @ref fail_error.
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
    fail_counter(std::size_t n,
            error_code ev = make_error_code(fail_error))
        : n_(n)
        , ec_(ev)
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

namespace boost {
namespace system {
template<>
struct is_error_code_enum<beast::test::error>
{
    static bool const value = true;
};
} // system
} // boost

#endif
