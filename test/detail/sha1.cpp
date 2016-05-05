//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/detail/sha1.hpp>
#include <beast/detail/unit_test/suite.hpp>
#include <boost/algorithm/hex.hpp>
#include <array>

namespace beast {
namespace detail {

class sha1_test : public beast::detail::unit_test::suite
{
public:
    void
    check(std::string const& message, std::string const& answer)
    {
        using digest_type =
            std::array<std::uint8_t, sha1_context::digest_size>;
        digest_type digest;
        if(! expect(boost::algorithm::unhex(
                answer, digest.begin()) == digest.end()))
            return;
        sha1_context ctx;
        digest_type result;
        init(ctx);
        update(ctx, message.data(), message.size());
        finish(ctx, result.data());
        expect(result == digest);
    }

    void
    run()
    {
        // http://www.di-mgt.com.au/sha_testvectors.html
        //
        check("abc",
            "a9993e36" "4706816a" "ba3e2571" "7850c26c" "9cd0d89d");
        check("",
            "da39a3ee" "5e6b4b0d" "3255bfef" "95601890" "afd80709");
        check("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
            "84983e44" "1c3bd26e" "baae4aa1" "f95129e5" "e54670f1");
        check("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
            "a49b2446" "a02c645b" "f419f995" "b6709125" "3a04a259");
    }
};

BEAST_DEFINE_TESTSUITE(sha1,core,beast);

} // test
} // beast

