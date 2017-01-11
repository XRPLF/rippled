//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_ZTEST_HPP
#define BEAST_ZTEST_HPP

#include "zlib-1.2.8/zlib.h"
#include <cstdint>
#include <random>
#include <string>

class z_deflator
{
    int level_      = Z_DEFAULT_COMPRESSION;
    int windowBits_ = 15;
    int memLevel_   = 4;
    int strategy_   = Z_DEFAULT_STRATEGY;

public:
    // -1    = default
    //  0    = none
    //  1..9 = faster<-->better
    void
    level(int n)
    {
        level_ = n;
    }

    void
    windowBits(int n)
    {
        windowBits_ = n;
    }

    void
    memLevel(int n)
    {
        memLevel_ = n;
    }

    void
    strategy(int n)
    {
        strategy_ = n;
    }

    std::string
    operator()(std::string const& in)
    {
        int result;
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        result = deflateInit2(
            &zs,
            level_,
            Z_DEFLATED,
            -windowBits_,
            memLevel_,
            strategy_
        );
        if(result != Z_OK)
            throw std::logic_error("deflateInit2 failed");
        std::string out;
        out.resize(deflateBound(&zs,
            static_cast<uLong>(in.size())));
        zs.next_in = (Bytef*)in.data();
        zs.avail_in = static_cast<uInt>(in.size());
        zs.next_out = (Bytef*)&out[0];
        zs.avail_out = static_cast<uInt>(out.size());
        result = deflate(&zs, Z_FULL_FLUSH);
        if(result != Z_OK)
            throw std::logic_error("deflate failed");
        out.resize(zs.total_out);
        deflateEnd(&zs);
        return out;
    }
};

class z_inflator
{
public:
    std::string
    operator()(std::string const& in)
    {
        int result;
        std::string out;
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        result = inflateInit2(&zs, -15);
        try
        {
            zs.next_in = (Bytef*)in.data();
            zs.avail_in = static_cast<uInt>(in.size());
            for(;;)
            {
                out.resize(zs.total_out + 1024);
                zs.next_out = (Bytef*)&out[zs.total_out];
                zs.avail_out = static_cast<uInt>(
                    out.size() - zs.total_out);
                result = inflate(&zs, Z_SYNC_FLUSH);
                if( result == Z_NEED_DICT ||
                    result == Z_DATA_ERROR ||
                    result == Z_MEM_ERROR)
                {
                    throw std::logic_error("inflate failed");
                }
                if(zs.avail_out > 0)
                    break;
                if(result == Z_STREAM_END)
                    break;
            }
            out.resize(zs.total_out);
            inflateEnd(&zs);
        }
        catch(...)
        {
            inflateEnd(&zs);
            throw;
        }
        return out;
    }
};

// Lots of repeats, limited char range
inline
std::string
corpus1(std::size_t n)
{
    static std::string const alphabet{
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    };
    std::string s;
    s.reserve(n + 5);
    std::mt19937 g;
    std::uniform_int_distribution<std::size_t> d0{
        0, alphabet.size() - 1};
    std::uniform_int_distribution<std::size_t> d1{
        1, 5};
    while(s.size() < n)
    {
        auto const rep = d1(g);
        auto const ch = alphabet[d0(g)];
        s.insert(s.end(), rep, ch);
    }
    s.resize(n);
    return s;
}

// Random data
inline
std::string
corpus2(std::size_t n)
{
    std::string s;
    s.reserve(n);
    std::mt19937 g;
    std::uniform_int_distribution<std::uint32_t> d0{0, 255};
    while(n--)
        s.push_back(static_cast<char>(d0(g)));
    return s;
}

#endif
