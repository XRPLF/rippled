// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <cstddef>
#include <ostream>
#include <string>

namespace beast::unit_test {

/** Utility for producing nicely composed output of amounts with units. */
class Amount
{
private:
    std::size_t n_;
    std::string const& what_;

public:
    Amount(Amount const&) = default;
    Amount&
    operator=(Amount const&) = delete;

    template <class = void>
    Amount(std::size_t n, std::string const& what);

    friend std::ostream&
    operator<<(std::ostream& s, Amount const& t);
};

template <class>
Amount::Amount(std::size_t n, std::string const& what) : n_(n), what_(what)
{
}

inline std::ostream&
operator<<(std::ostream& s, Amount const& t)
{
    s << t.n_ << " " << t.what_ << ((t.n_ != 1) ? "s" : "");
    return s;
}

}  // namespace beast::unit_test
