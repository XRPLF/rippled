//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/zlib/deflate_stream.hpp>

#include "ztest.hpp"
#include <beast/unit_test/suite.hpp>

namespace beast {
namespace zlib {

class deflate_stream_test : public beast::unit_test::suite
{
public:
    using self = deflate_stream_test;
    typedef void(self::*pmf_t)(
        int level, int windowBits, int strategy,
            std::string const&);

    static
    Strategy
    toStrategy(int strategy)
    {
        switch(strategy)
        {
        default:
        case 0: return Strategy::normal;
        case 1: return Strategy::filtered;
        case 2: return Strategy::huffman;
        case 3: return Strategy::rle;
        case 4: return Strategy::fixed;
        }
    }

    void
    doDeflate1_zlib(
        int level, int windowBits, int strategy,
            std::string const& check)
    {
        int result;
        std::string out;
        ::z_stream zs;
        std::memset(&zs, 0, sizeof(zs));
        result = deflateInit2(&zs,
            level,
            Z_DEFLATED,
            -windowBits,
            8,
            strategy);
        if(! BEAST_EXPECT(result == Z_OK))
            goto err;
        out.resize(deflateBound(&zs,
            static_cast<uLong>(check.size())));
        zs.next_in = (Bytef*)check.data();
        zs.avail_in = static_cast<uInt>(check.size());
        zs.next_out = (Bytef*)out.data();
        zs.avail_out = static_cast<uInt>(out.size());
        {
            bool progress = true;
            for(;;)
            {
                result = deflate(&zs, Z_FULL_FLUSH);
                if( result == Z_BUF_ERROR ||
                    result == Z_STREAM_END) // per zlib FAQ
                    goto fin;
                if(! BEAST_EXPECT(progress))
                    goto err;
                progress = false;
            }
        }

    fin:
        out.resize(zs.total_out);
        {
            z_inflator zi;
            auto const s = zi(out);
            BEAST_EXPECT(s == check);
        }

    err:
        deflateEnd(&zs);
    }

    void
    doDeflate1_beast(
        int level, int windowBits, int strategy,
            std::string const& check)
    {
        std::string out;
        z_params zs;
        deflate_stream ds;
        ds.reset(
            level,
            windowBits,
            8,
            toStrategy(strategy));
        out.resize(ds.upper_bound(
            static_cast<uLong>(check.size())));
        zs.next_in = (Bytef*)check.data();
        zs.avail_in = static_cast<uInt>(check.size());
        zs.next_out = (Bytef*)out.data();
        zs.avail_out = static_cast<uInt>(out.size());
        {
            bool progress = true;
            for(;;)
            {
                error_code ec;
                ds.write(zs, Flush::full, ec);
                if( ec == error::need_buffers ||
                    ec == error::end_of_stream) // per zlib FAQ
                    goto fin;
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    goto err;
                if(! BEAST_EXPECT(progress))
                    goto err;
                progress = false;
            }
        }

    fin:
        out.resize(zs.total_out);
        {
            z_inflator zi;
            auto const s = zi(out);
            BEAST_EXPECT(s == check);
        }

    err:
        ;
    }

    //--------------------------------------------------------------------------

    void
    doDeflate2_zlib(
        int level, int windowBits, int strategy,
            std::string const& check)
    {
        for(std::size_t i = 1; i < check.size(); ++i)
        {
            for(std::size_t j = 1;; ++j)
            {
                int result;
                ::z_stream zs;
                std::memset(&zs, 0, sizeof(zs));
                result = deflateInit2(&zs,
                    level,
                    Z_DEFLATED,
                    -windowBits,
                    8,
                    strategy);
                if(! BEAST_EXPECT(result == Z_OK))
                    continue;
                std::string out;
                out.resize(deflateBound(&zs,
                    static_cast<uLong>(check.size())));
                if(j >= out.size())
                {
                    deflateEnd(&zs);
                    break;
                }
                zs.next_in = (Bytef*)check.data();
                zs.avail_in = static_cast<uInt>(i);
                zs.next_out = (Bytef*)out.data();
                zs.avail_out = static_cast<uInt>(j);
                bool bi = false;
                bool bo = false;
                for(;;)
                {
                    int flush = bi ? Z_FULL_FLUSH : Z_NO_FLUSH;
                    result = deflate(&zs, flush);
                    if( result == Z_BUF_ERROR ||
                        result == Z_STREAM_END) // per zlib FAQ
                        goto fin;
                    if(! BEAST_EXPECT(result == Z_OK))
                        goto err;
                    if(zs.avail_in == 0 && ! bi)
                    {
                        bi = true;
                        zs.avail_in =
                            static_cast<uInt>(check.size() - i);
                    }
                    if(zs.avail_out == 0 && ! bo)
                    {
                        bo = true;
                        zs.avail_out =
                            static_cast<uInt>(out.size() - j);
                    }
                }

            fin:
                out.resize(zs.total_out);
                {
                    z_inflator zi;
                    auto const s = zi(out);
                    BEAST_EXPECT(s == check);
                }

            err:
                deflateEnd(&zs);
            }
        }
    }

    void
    doDeflate2_beast(
        int level, int windowBits, int strategy,
            std::string const& check)
    {
        for(std::size_t i = 1; i < check.size(); ++i)
        {
            for(std::size_t j = 1;; ++j)
            {
                z_params zs;
                deflate_stream ds;
                ds.reset(
                    level,
                    windowBits,
                    8,
                    toStrategy(strategy));
                std::string out;
                out.resize(ds.upper_bound(
                    static_cast<uLong>(check.size())));
                if(j >= out.size())
                    break;
                zs.next_in = (Bytef*)check.data();
                zs.avail_in = static_cast<uInt>(i);
                zs.next_out = (Bytef*)out.data();
                zs.avail_out = static_cast<uInt>(j);
                bool bi = false;
                bool bo = false;
                for(;;)
                {
                    error_code ec;
                    ds.write(zs,
                        bi ? Flush::full : Flush::none, ec);
                    if( ec == error::need_buffers ||
                        ec == error::end_of_stream) // per zlib FAQ
                        goto fin;
                    if(! BEAST_EXPECTS(! ec, ec.message()))
                        goto err;
                    if(zs.avail_in == 0 && ! bi)
                    {
                        bi = true;
                        zs.avail_in =
                            static_cast<uInt>(check.size() - i);
                    }
                    if(zs.avail_out == 0 && ! bo)
                    {
                        bo = true;
                        zs.avail_out =
                            static_cast<uInt>(out.size() - j);
                    }
                }

            fin:
                out.resize(zs.total_out);
                {
                    z_inflator zi;
                    auto const s = zi(out);
                    BEAST_EXPECT(s == check);
                }

            err:
                ;
            }
        }
    }

    //--------------------------------------------------------------------------

    void
    doMatrix(std::string const& label,
        std::string const& check, pmf_t pmf)
    {
        using namespace std::chrono;
        using clock_type = steady_clock;
        auto const when = clock_type::now();
        for(int level = 0; level <= 9; ++level)
        {
            for(int windowBits = 8; windowBits <= 9; ++windowBits)
            {
                for(int strategy = 0; strategy <= 4; ++strategy)
                {
                    (this->*pmf)(
                        level, windowBits, strategy, check);
                }
            }
        }
        auto const elapsed = clock_type::now() - when;
        log <<
            label << ": " <<
            duration_cast<
                milliseconds>(elapsed).count() << "ms\n";
        log.flush();
    }

    void
    testDeflate()
    {
        doMatrix("1.beast ", "Hello, world!", &self::doDeflate1_beast);
        doMatrix("1.zlib  ", "Hello, world!", &self::doDeflate1_zlib);
        doMatrix("2.beast ", "Hello, world!", &self::doDeflate2_beast);
        doMatrix("2.zlib  ", "Hello, world!", &self::doDeflate2_zlib);
        {
            auto const s = corpus1(56);
            doMatrix("3.beast ", s, &self::doDeflate2_beast);
            doMatrix("3.zlib  ", s, &self::doDeflate2_zlib);
        }
        {
            auto const s = corpus1(512 * 1024);
            doMatrix("4.beast ", s, &self::doDeflate1_beast);
            doMatrix("4.zlib  ", s, &self::doDeflate1_zlib);
        }
    }

    void
    run() override
    {
        log <<
            "sizeof(deflate_stream) == " <<
            sizeof(deflate_stream) << std::endl;

        testDeflate();
    }
};

BEAST_DEFINE_TESTSUITE(deflate_stream,core,beast);

} // zlib
} // beast
