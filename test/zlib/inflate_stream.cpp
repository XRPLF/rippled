//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/zlib/inflate_stream.hpp>

#include "ztest.hpp"
#include <beast/unit_test/suite.hpp>
#include <chrono>
#include <random>

namespace beast {
namespace zlib {

class inflate_stream_test : public beast::unit_test::suite
{
public:
    //--------------------------------------------------------------------------

    enum Split
    {
        once,
        half,
        full
    };

    class Beast
    {
        Split in_;
        Split check_;
        Flush flush_;

    public:
        Beast(Split in, Split check, Flush flush = Flush::sync)
            : in_(in)
            , check_(check)
            , flush_(flush)
        {
        }

        void
        operator()(
            int window,
            std::string const& in,
            std::string const& check,
            unit_test::suite& suite) const
        {
            auto const f =
            [&](std::size_t i, std::size_t j)
            {
                std::string out(check.size(), 0);
                z_params zs;
                zs.next_in = in.data();
                zs.next_out = &out[0];
                zs.avail_in = i;
                zs.avail_out = j;
                inflate_stream is;
                is.reset(window);
                bool bi = ! (i < in.size());
                bool bo = ! (j < check.size());
                for(;;)
                {
                    error_code ec;
                    is.write(zs, flush_, ec);
                    if( ec == error::need_buffers ||
                        ec == error::end_of_stream)
                    {
                        out.resize(zs.total_out);
                        suite.expect(out == check, __FILE__, __LINE__);
                        break;
                    }
                    if(ec)
                    {
                        suite.fail(ec.message(), __FILE__, __LINE__);
                        break;
                    }
                    if(zs.avail_in == 0 && ! bi)
                    {
                        bi = true;
                        zs.avail_in = in.size() - i;
                    }
                    if(zs.avail_out == 0 && ! bo)
                    {
                        bo = true;
                        zs.avail_out = check.size() - j;
                    }
                }
            };

            std::size_t i0, i1;
            std::size_t j0, j1;

            switch(in_)
            {
            default:
            case once: i0 = in.size();     i1 = i0;         break;
            case half: i0 = in.size() / 2; i1 = i0;         break;
            case full: i0 = 1;             i1 = in.size();  break;
            }

            switch(check_)
            {
            default:
            case once: j0 = check.size();     j1 = j0;              break;
            case half: j0 = check.size() / 2; j1 = j0;              break;
            case full: j0 = 1;                j1 = check.size();    break;
            }

            for(std::size_t i = i0; i <= i1; ++i)
                for(std::size_t j = j0; j <= j1; ++j)
                    f(i, j);
        }
    };

    class ZLib
    {
        Split in_;
        Split check_;
        int flush_;

    public:
        ZLib(Split in, Split check, int flush = Z_SYNC_FLUSH)
            : in_(in)
            , check_(check)
            , flush_(flush)
        {
        }

        void
        operator()(
            int window,
            std::string const& in,
            std::string const& check,
            unit_test::suite& suite) const
        {
            auto const f =
            [&](std::size_t i, std::size_t j)
            {
                int result;
                std::string out(check.size(), 0);
                ::z_stream zs;
                memset(&zs, 0, sizeof(zs));
                result = inflateInit2(&zs, -window);
                if(result != Z_OK)
                {
                    suite.fail("! Z_OK", __FILE__, __LINE__);
                    return;
                }
                zs.next_in = (Bytef*)in.data();
                zs.next_out = (Bytef*)out.data();
                zs.avail_in = static_cast<uInt>(i);
                zs.avail_out = static_cast<uInt>(j);
                bool bi = ! (i < in.size());
                bool bo = ! (j < check.size());
                for(;;)
                {
                    result = inflate(&zs, flush_);
                    if( result == Z_BUF_ERROR ||
                        result == Z_STREAM_END) // per zlib FAQ
                    {
                        out.resize(zs.total_out);
                        suite.expect(out == check, __FILE__, __LINE__);
                        break;
                    }
                    if(result != Z_OK)
                    {
                        suite.fail("! Z_OK", __FILE__, __LINE__);
                        break;
                    }
                    if(zs.avail_in == 0 && ! bi)
                    {
                        bi = true;
                        zs.avail_in = static_cast<uInt>(in.size() - i);
                    }
                    if(zs.avail_out == 0 && ! bo)
                    {
                        bo = true;
                        zs.avail_out = static_cast<uInt>(check.size() - j);
                    }
                }
                inflateEnd(&zs);
            };

            std::size_t i0, i1;
            std::size_t j0, j1;

            switch(in_)
            {
            default:
            case once: i0 = in.size();     i1 = i0;         break;
            case half: i0 = in.size() / 2; i1 = i0;         break;
            case full: i0 = 1;             i1 = in.size();  break;
            }

            switch(check_)
            {
            default:
            case once: j0 = check.size();     j1 = j0;              break;
            case half: j0 = check.size() / 2; j1 = j0;              break;
            case full: j0 = 1;                j1 = check.size();    break;
            }

            for(std::size_t i = i0; i <= i1; ++i)
                for(std::size_t j = j0; j <= j1; ++j)
                    f(i, j);
        }
    };

    class Matrix
    {
        unit_test::suite& suite_;

        int level_[2];
        int window_[2];
        int strategy_[2];

    public:
        explicit
        Matrix(unit_test::suite& suite)
            : suite_(suite)
        {
            level_[0] = 0;
            level_[1] = 9;
            window_[0] = 8;
            window_[1] = 15;
            strategy_[0] = 0;
            strategy_[1] = 4;
        }

        void
        level(int from, int to)
        {
            level_[0] = from;
            level_[1] = to;
        }

        void
        level(int what)
        {
            level(what, what);
        }

        void
        window(int from, int to)
        {
            window_[0] = from;
            window_[1] = to;
        }

        void
        window(int what)
        {
            window(what, what);
        }

        void
        strategy(int from, int to)
        {
            strategy_[0] = from;
            strategy_[1] = to;
        }

        void
        strategy(int what)
        {
            strategy(what, what);
        }

        template<class F>
        void
        operator()(
            std::string label,
            F const& f,
            std::string const& check) const
        {
            using namespace std::chrono;
            using clock_type = steady_clock;
            auto const when = clock_type::now();

            for(auto level = level_[0];
                level <= level_[1]; ++level)
            {
                for(auto window = window_[0];
                    window <= window_[1]; ++window)
                {
                    for(auto strategy = strategy_[0];
                        strategy <= strategy_[1]; ++strategy)
                    {
                        z_deflator zd;
                        zd.level(level);
                        zd.windowBits(window);
                        zd.strategy(strategy);
                        auto const in = zd(check);
                        f(window, in, check, suite_);
                    }
                }
            }
            auto const elapsed = clock_type::now() - when;
            suite_.log <<
                label << ": " <<
                duration_cast<
                    milliseconds>(elapsed).count() << "ms\n";
            suite_.log.flush();
        }
    };

    void
    testInflate()
    {
        {
            Matrix m{*this};
            std::string check =
                "{\n   \"AutobahnPython/0.6.0\": {\n"
                "      \"1.1.1\": {\n"
                "         \"behavior\": \"OK\",\n"
                "         \"behaviorClose\": \"OK\",\n"
                "         \"duration\": 2,\n"
                "         \"remoteCloseCode\": 1000,\n"
                "         \"reportfile\": \"autobahnpython_0_6_0_case_1_1_1.json\"\n"
                ;
            m("1. beast", Beast{half, half}, check);
            m("1. zlib ", ZLib {half, half}, check);
        }
        {
            Matrix m{*this};
            auto const check = corpus1(50000);
            m("2. beast", Beast{half, half}, check);
            m("2. zlib ", ZLib {half, half}, check);
        }
        {
            Matrix m{*this};
            auto const check = corpus2(50000);
            m("3. beast", Beast{half, half}, check);
            m("3. zlib ", ZLib {half, half}, check);
        }
        {
            Matrix m{*this};
            auto const check = corpus1(10000);
            m.level(6);
            m.window(9);
            m.strategy(Z_DEFAULT_STRATEGY);
            m("4. beast", Beast{once, full}, check);
            m("4. zlib ", ZLib {once, full}, check);
        }
        {
            Matrix m{*this};
            auto const check = corpus2(10000);
            m.level(6);
            m.window(9);
            m.strategy(Z_DEFAULT_STRATEGY);
            m("5. beast", Beast{once, full}, check);
            m("5. zlib ", ZLib {once, full}, check);
        }
        {
            Matrix m{*this};
            m.level(6);
            m.window(9);
            auto const check = corpus1(200);
            m("6. beast", Beast{full, full}, check);
            m("6. zlib ", ZLib {full, full}, check);
        }
        {
            Matrix m{*this};
            m.level(6);
            m.window(9);
            auto const check = corpus2(500);
            m("7. beast", Beast{full, full}, check);
            m("7. zlib ", ZLib {full, full}, check);
        }
        {
            Matrix m{*this};
            auto const check = corpus2(10000);
            m.level(6);
            m.window(9);
            m.strategy(Z_DEFAULT_STRATEGY);
            m("8. beast", Beast{full, once, Flush::block}, check);
            m("8. zlib ", ZLib {full, once, Z_BLOCK}, check);
        }

        // VFALCO Fails, but I'm unsure of what the correct
        //        behavior of Z_TREES/Flush::trees is.
#if 0
        {
            Matrix m{*this};
            auto const check = corpus2(10000);
            m.level(6);
            m.window(9);
            m.strategy(Z_DEFAULT_STRATEGY);
            m("9. beast", Beast{full, once, Flush::trees}, check);
            m("9. zlib ", ZLib {full, once, Z_TREES}, check);
        }
#endif
    }

    void
    run() override
    {
        log <<
            "sizeof(inflate_stream) == " <<
            sizeof(inflate_stream) << std::endl;
        testInflate();
    }
};

BEAST_DEFINE_TESTSUITE(inflate_stream,core,beast);

} // zlib
} // beast
