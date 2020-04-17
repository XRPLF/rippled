//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#ifndef RIPPLE_CONDITIONS_PREIMAGE_SHA256_H
#define RIPPLE_CONDITIONS_PREIMAGE_SHA256_H

#include <ripple/basics/Buffer.h>
#include <ripple/basics/Slice.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/conditions/impl/error.h>
#include <ripple/protocol/digest.h>
#include <memory>

namespace ripple {
namespace cryptoconditions {

class PreimageSha256 final : public Fulfillment
{
public:
    /** The maximum allowed length of a preimage.

        The specification does not specify a minimum supported
        length, nor does it require all conditions to support
        the same minimum length.

        While future versions of this code will never lower
        this limit, they may opt to raise it.
    */
    static constexpr std::size_t maxPreimageLength = 128;

    /** Parse the payload for a PreimageSha256 condition

        @param s A slice containing the DER encoded payload
        @param ec indicates success or failure of the operation
        @return the preimage, if successful; empty pointer otherwise.
    */
    static std::unique_ptr<Fulfillment>
    deserialize(Slice s, std::error_code& ec)
    {
        // Per the RFC, a preimage fulfulliment is defined as
        // follows:
        //
        // PreimageFulfillment ::= SEQUENCE {
        //     preimage             OCTET STRING
        // }

        using namespace der;

        auto p = parsePreamble(s, ec);
        if (ec)
            return nullptr;

        if (!isPrimitive(p) || !isContextSpecific(p))
        {
            ec = error::incorrect_encoding;
            return {};
        }

        if (p.tag != 0)
        {
            ec = error::unexpected_tag;
            return {};
        }

        if (s.size() != p.length)
        {
            ec = error::trailing_garbage;
            return {};
        }

        if (s.size() > maxPreimageLength)
        {
            ec = error::preimage_too_long;
            return {};
        }

        auto b = parseOctetString(s, p.length, ec);
        if (ec)
            return {};

        return std::make_unique<PreimageSha256>(std::move(b));
    }

private:
    Buffer payload_;

public:
    PreimageSha256(Buffer&& b) noexcept : payload_(std::move(b))
    {
    }

    PreimageSha256(Slice s) noexcept : payload_(s)
    {
    }

    Type
    type() const override
    {
        return Type::preimageSha256;
    }

    Buffer
    fingerprint() const override
    {
        sha256_hasher h;
        h(payload_.data(), payload_.size());
        auto const d = static_cast<sha256_hasher::result_type>(h);
        return {d.data(), d.size()};
    }

    std::uint32_t
    cost() const override
    {
        return static_cast<std::uint32_t>(payload_.size());
    }

    Condition
    condition() const override
    {
        return {type(), cost(), fingerprint()};
    }

    bool validate(Slice) const override
    {
        // Perhaps counterintuitively, the message isn't
        // relevant.
        return true;
    }
};

}  // namespace cryptoconditions
}  // namespace ripple

#endif
