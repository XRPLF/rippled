//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_WSPROTO_DETAIL_MASKGEN_H_INCLUDED
#define BEAST_WSPROTO_DETAIL_MASKGEN_H_INCLUDED

#include <array>
#include <cstdint>
#include <random>

namespace beast {
namespace wsproto {
namespace detail {

// Pseudo-random source of mask keys
//
template<class = void>
class maskgen_t
{
private:
    std::size_t n_;
    std::mt19937 g_;

public:
    using result_type = typename std::mt19937::result_type;

    maskgen_t(maskgen_t const&) = delete;
    maskgen_t& operator=(maskgen_t const&) = delete;

    maskgen_t();
 
    result_type
    operator()() noexcept;

    void
    rekey();
};

template<class _>
maskgen_t<_>::maskgen_t()
{
    rekey();
}
 
template<class _>
auto
maskgen_t<_>::operator()() noexcept ->
    result_type
{
    if(++n_ == 65536)
        rekey();
    for(;;)
    {
        if(auto key = g_())
            return key;
    }
}

template<class _>
void
maskgen_t<_>::rekey()
{
    n_ = 0;
    std::random_device rng;
    std::array<std::uint32_t, 32> e;
    for(auto& i : e)
        i = rng();
    std::seed_seq ss(e.begin(), e.end());
    g_.seed(ss);
}

using maskgen = maskgen_t<>;

//------------------------------------------------------------------------------

// Apply mask key and copy a buffer in one step
//
template<class MutableBuffers, class ConstBuffers>
std::size_t
mask_and_copy(MutableBuffers const& mbs,
    ConstBuffers const& cbs, std::uint32_t key)
{
    // VFALCO This is the unoptimized version.
    //        On Intel platforms we can process
    //        an unaligned size_t at a time instead.
    //
    using namespace boost::asio;
    auto const size = buffer_size(mbs);
    assert(buffer_size(mbs) == buffer_size(cbs));
    if(size == 0)
        return 0;
    auto mit = mbs.begin();
    auto cit = cbs.begin();
    auto mb = *mit;
    auto cb = *cit;
    auto const maskcpy =
        [](std::uint32_t& key,
            void* dest, void const* src, std::size_t n)
        {
            auto pm = reinterpret_cast<
                std::uint8_t const*>(&key);
            auto pd = reinterpret_cast<
                std::uint8_t*>(dest);
            auto ps = reinterpret_cast<
                std::uint8_t const*>(src);
            while(n >= 4)
            {
                *pd++ = *ps++ ^ pm[0];
                *pd++ = *ps++ ^ pm[1];
                *pd++ = *ps++ ^ pm[2];
                *pd++ = *ps++ ^ pm[3];
                n -= 4;
                
            }
            switch(n)
            {
            case 3: pd[2] = ps[2] ^ pm[2];
            case 2: pd[1] = ps[1] ^ pm[1];
            case 1: pd[0] = ps[0] ^ pm[0];
                n *= 8;
                key = (key << n) | (key >> (32 - n));
            default:
                break;
            }
        };
    for(;;)
    {
        auto const n = std::min(
            buffer_size(mb), buffer_size(cb));
        maskcpy(key, buffer_cast<std::uint8_t*>(mb),
            buffer_cast<std::uint8_t const*>(cb), n);
        mb = mb + n;
        cb = cb + n;
        if(buffer_size(mb) == 0)
        {
            if(++mit == mbs.end())
                break;
            mb = *mit;
        }
        if(buffer_size(cb) == 0)
        {
            ++cit;
            assert(cit != cbs.end());
            cb = *cit;
        }
    }
    return size;
}

} // detail
} // wsproto
} // beast

#endif
