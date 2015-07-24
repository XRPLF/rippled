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

#ifndef RIPPLE_BASICS_RANGESET_H_INCLUDED
#define RIPPLE_BASICS_RANGESET_H_INCLUDED

#include <cstdint>
#include <map>
#include <string>

namespace ripple {

/** A sparse set of integers. */
// VFALCO TODO Replace with juce::SparseSet
class RangeSet
{
public:
    static const std::uint32_t absent = static_cast <std::uint32_t> (-1);

public:
    RangeSet () { }

    bool hasValue (std::uint32_t) const;

    std::uint32_t getFirst () const;
    std::uint32_t getNext (std::uint32_t) const;
    std::uint32_t getLast () const;
    std::uint32_t getPrev (std::uint32_t) const;

    // largest number not in the set that is less than the given number
    std::uint32_t prevMissing (std::uint32_t) const;

    // Add an item to the set
    void setValue (std::uint32_t);

    // Add the closed interval to the set
    void setRange (std::uint32_t, std::uint32_t);

    void clearValue (std::uint32_t);

    std::string toString () const;

    /** Returns the sum of the Lebesgue measures of all sub-ranges. */
    std::size_t
    lebesgue_sum() const;

    /** Check invariants of the data.

        This is for diagnostics, and does nothing in release builds.
    */
    void checkInternalConsistency () const noexcept;

private:
    void simplify ();

private:
    using Map = std::map <std::uint32_t, std::uint32_t>;

    using const_iterator         = Map::const_iterator;
    using const_reverse_iterator = Map::const_reverse_iterator;
    using value_type             = Map::value_type;
    using iterator               = Map::iterator;

    static bool contains (value_type const& it, std::uint32_t v)
    {
        return (it.first <= v) && (it.second >= v);
    }

    // First is lowest value in range, last is highest value in range
    Map mRanges;
};

} // ripple

#endif
