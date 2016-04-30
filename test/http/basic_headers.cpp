//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/basic_headers.hpp>

#include <beast/unit_test/suite.hpp>

namespace beast {
namespace http {

class basic_headers_test : public beast::unit_test::suite
{
public:
    template<class Allocator>
    using bha = basic_headers<Allocator>;

    using bh = basic_headers<std::allocator<char>>;

    template<class Allocator>
    static
    void
    fill(std::size_t n, basic_headers<Allocator>& h)
    {
        for(std::size_t i = 1; i<= n; ++i)
            h.insert(std::to_string(i), i);
    }

    template<class U, class V>
    static
    void
    self_assign(U& u, V&& v)
    {
        u = std::forward<V>(v);
    }

    void testHeaders()
    {
        bh h1;
        expect(h1.empty());
        fill(1, h1);
        expect(h1.size() == 1);
        bh h2;
        h2 = h1;
        expect(h2.size() == 1);
        h2.insert("2", "2");
        expect(std::distance(h2.begin(), h2.end()) == 2);
        h1 = std::move(h2);
        expect(h1.size() == 2);
        expect(h2.size() == 0);
        bh h3(std::move(h1));
        expect(h3.size() == 2);
        expect(h1.size() == 0);
        self_assign(h3, std::move(h3));
        expect(h3.size() == 2);
        expect(h2.erase("Not-Present") == 0);
    }

    void testRFC2616()
    {
        bh h;
        h.insert("a", "x");
        h.insert("a", "y");
        expect(h["a"] == "x,y");
    }

    void run() override
    {
        testHeaders();
        testRFC2616();
    }
};

BEAST_DEFINE_TESTSUITE(basic_headers,http,beast);

} // http
} // beast
