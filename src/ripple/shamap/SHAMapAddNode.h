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

#ifndef RIPPLE_SHAMAP_SHAMAPADDNODE_H_INCLUDED
#define RIPPLE_SHAMAP_SHAMAPADDNODE_H_INCLUDED

#include <cstdint>
#include <string>

namespace ripple {

// results of adding nodes
class SHAMapAddNode
{
private:
    std::uint32_t good_;
    std::uint32_t bad_;
    std::uint32_t duplicate_;

    constexpr SHAMapAddNode(int good, int bad, int duplicate)
        : good_(good), bad_(bad), duplicate_(duplicate)
    {
    }

public:
    constexpr SHAMapAddNode() : SHAMapAddNode(0, 0, 0)
    {
    }

    void
    incInvalid();
    void
    incUseful();
    void
    incDuplicate();

    [[nodiscard]] std::uint32_t
    getGood() const;

    [[nodiscard]] bool
    isGood() const;

    [[nodiscard]] bool
    isInvalid() const;

    [[nodiscard]] bool
    isUseful() const;

    [[nodiscard]] std::string
    get() const;

    SHAMapAddNode&
    operator+=(SHAMapAddNode const& n);

    [[nodiscard]] static SHAMapAddNode
    duplicate();

    [[nodiscard]] static SHAMapAddNode
    useful();

    [[nodiscard]] static SHAMapAddNode
    invalid();
};

inline void
SHAMapAddNode::incInvalid()
{
    ++bad_;
}

inline void
SHAMapAddNode::incUseful()
{
    ++good_;
}

inline void
SHAMapAddNode::incDuplicate()
{
    ++duplicate_;
}

inline std::uint32_t
SHAMapAddNode::getGood() const
{
    return good_;
}

inline bool
SHAMapAddNode::isInvalid() const
{
    return bad_ != 0;
}

inline bool
SHAMapAddNode::isUseful() const
{
    return good_ != 0;
}

inline SHAMapAddNode&
SHAMapAddNode::operator+=(SHAMapAddNode const& n)
{
    good_ += n.good_;
    bad_ += n.bad_;
    duplicate_ += n.duplicate_;

    return *this;
}

inline bool
SHAMapAddNode::isGood() const
{
    return (good_ + duplicate_) > bad_;
}

inline SHAMapAddNode
SHAMapAddNode::duplicate()
{
    return {0, 0, 1};
}

inline SHAMapAddNode
SHAMapAddNode::useful()
{
    return {1, 0, 0};
}

inline SHAMapAddNode
SHAMapAddNode::invalid()
{
    return {0, 1, 0};
}

inline std::string
SHAMapAddNode::get() const
{
    return "{ good: " + std::to_string(good_) +
        ", bad: " + std::to_string(bad_) +
        ", dup: " + std::to_string(duplicate_) + " }";
}

}  // namespace ripple

#endif
