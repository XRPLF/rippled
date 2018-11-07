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

#ifndef RIPPLE_PROTOCOL_STBLOB_H_INCLUDED
#define RIPPLE_PROTOCOL_STBLOB_H_INCLUDED

#include <ripple/basics/Buffer.h>
#include <ripple/basics/Slice.h>
#include <ripple/protocol/STBase.h>
#include <cstring>
#include <memory>

namespace ripple {

// variable length byte string
class STBlob
    : public STBase
{
public:
    using value_type = Slice;

    STBlob () = default;
    STBlob (STBlob const& rhs)
        :STBase(rhs)
        , value_ (rhs.data (), rhs.size ())
    {
    }

    /** Construct with size and initializer.
        Init will be called as:
            void(void* data, std::size_t size)
    */
    template <class Init>
    STBlob (SField const& f, std::size_t size,
            Init&& init)
        : STBase(f), value_ (size)
    {
        init(value_.data(), value_.size());
    }

    STBlob (SField const& f,
            void const* data, std::size_t size)
        : STBase(f), value_ (data, size)
    {
    }

    STBlob (SField const& f, Buffer&& b)
       : STBase(f), value_(std::move (b))
    {
    }

    STBlob (SField const& n)
        : STBase (n)
    {
    }

    STBlob (SerialIter&, SField const& name = sfGeneric);

    STBase*
    copy (std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move (std::size_t n, void* buf) override
    {
        return emplace(n, buf, std::move(*this));
    }

    std::size_t
    size() const
    {
        return value_.size();
    }

    std::uint8_t const*
    data() const
    {
        return reinterpret_cast<
            std::uint8_t const*>(value_.data());
    }

    SerializedTypeID
    getSType () const override
    {
        return STI_VL;
    }

    std::string
    getText () const override;

    void
    add (Serializer& s) const override
    {
        assert (fName->isBinary ());
        assert ((fName->fieldType == STI_VL) ||
            (fName->fieldType == STI_ACCOUNT));
        s.addVL (value_.data (), value_.size ());
    }

    Buffer const&
    peekValue () const
    {
        return value_;
    }

    STBlob&
    operator= (Slice const& slice)
    {
        value_ = Buffer(slice.data(), slice.size());
        return *this;
    }

    value_type
    value() const noexcept
    {
        return value_;
    }

    STBlob&
    operator= (Buffer&& buffer)
    {
        value_ = std::move(buffer);
        return *this;
    }

    Buffer&
    peekValue ()
    {
        return value_;
    }

    void
    setValue (Buffer&& b)
    {
        value_ = std::move (b);
    }

    void
    setValue (void const* data, std::size_t size)
    {
        if (value_.alloc (size))
            std::memcpy(value_.data(), data, size);
    }

    bool
    isEquivalent (const STBase& t) const override;

    bool
    isDefault () const override
    {
        return value_.empty ();
    }

private:
    Buffer value_;
};

} // ripple

#endif
