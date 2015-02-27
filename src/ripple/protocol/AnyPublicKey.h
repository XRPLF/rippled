//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_ANYPUBLICKEY_H_INCLUDED
#define RIPPLE_PROTOCOL_ANYPUBLICKEY_H_INCLUDED

#include <ripple/basics/Buffer.h>
#include <ripple/crypto/KeyType.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/STExchange.h>
#include <ripple/protocol/STObject.h>
#include <beast/hash/hash_append.h>
#include <beast/utility/noexcept.h>
#include <boost/utility/base_from_member.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace ripple {

/** Variant container for all public keys. */
class AnyPublicKeySlice
    : public Slice
{
public:
#ifdef _MSC_VER
    AnyPublicKeySlice (
            void const* data, std::size_t size)
        : Slice (data, size)
    {
    }
#else
    using Slice::Slice;
#endif

    AnyPublicKeySlice() = delete;

    AnyPublicKeySlice (
        AnyPublicKeySlice const&) = default;

    AnyPublicKeySlice& operator= (
        AnyPublicKeySlice const&) = default;

    /** Returns the type of key stored. */
    KeyType
    type() const noexcept;

    /** Verify a signature using this public key. */
    bool
    verify (void const* msg, std::size_t msg_size,
        void const* sig, std::size_t sig_size) const;
};

template <>
struct STExchange<STBlob, AnyPublicKeySlice>
{
    using value_type = AnyPublicKeySlice;

    static
    void
    get (boost::optional<value_type>& t,
        STBlob const& u)
    {
        t = boost::in_place(u.data(), u.size());
    }

    static
    std::unique_ptr<STBlob>
    set (SField const& f, AnyPublicKeySlice const& t)
    {
        return std::make_unique<STBlob>(
            f, t.data(), t.size());
    }
};

//------------------------------------------------------------------------------

/** Variant container for all public keys, with ownership. */
class AnyPublicKey
    : private boost::base_from_member<Buffer>
    , public AnyPublicKeySlice
{
private:
    using buffer_type = boost::base_from_member<Buffer>;

public:
    AnyPublicKey() = delete;
    AnyPublicKey (AnyPublicKey const&) = delete;
    AnyPublicKey& operator= (AnyPublicKey const&) = delete;

#ifdef _MSC_VER
    AnyPublicKey (AnyPublicKey&& other)
        : buffer_type(std::move(other.buffer_type::member))
        , AnyPublicKeySlice (buffer_type::member.data(),
            buffer_type::member.size())
    {
    }

    AnyPublicKey& operator= (AnyPublicKey&& other)
    {
        buffer_type::member =
            std::move(other.buffer_type::member);
        return *this;
    }
#else
    AnyPublicKey (AnyPublicKey&&) = default;
    AnyPublicKey& operator= (AnyPublicKey&&) = default;
#endif

    AnyPublicKey (void const* data_, std::size_t size_)
        : buffer_type (data_, size_)
        , AnyPublicKeySlice (
            member.data(), member.size())
    {
    }

    /** Returns ownership of the underlying Buffer.
        After calling this function, only the destructor
        or the move assignment operator may be called.
    */
    Buffer
    releaseBuffer() noexcept
    {
        return std::move(buffer_type::member);
    }
};

template <>
struct STExchange<STBlob, AnyPublicKey>
{
    using value_type = AnyPublicKey;

    static
    void
    get (boost::optional<value_type>& t,
        STBlob const& u)
    {
        t = boost::in_place(u.data(), u.size());
    }

    static
    std::unique_ptr<STBlob>
    set (SField const& f, AnyPublicKey const& t)
    {
        return std::make_unique<STBlob>(
            f, t.data(), t.size());
    }

    static
    std::unique_ptr<STBlob>
    set (SField const& f, AnyPublicKey&& t)
    {
        return std::make_unique<STBlob>(
            f, t.releaseBuffer());
    }
};

} // ripple

#endif
