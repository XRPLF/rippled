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

#ifndef RIPPLE_RANGESET_H_INCLUDED
#define RIPPLE_RANGESET_H_INCLUDED

#include <ripple/basics/types/BasicTypes.h>
#include <beast/utility/noexcept.h>
#include <cstdint>
#include <map>
#include <string>

namespace ripple {

/** A sparse set of integers. */
// VFALCO TODO Replace with juce::SparseSet
class RangeSet
{
public:
    static const uint32 absent = static_cast <uint32> (-1);

public:
    RangeSet () { }

    bool hasValue (uint32) const;

    uint32 getFirst () const;
    uint32 getNext (uint32) const;
    uint32 getLast () const;
    uint32 getPrev (uint32) const;

    // largest number not in the set that is less than the given number
    uint32 prevMissing (uint32) const;

    // Add an item to the set
    void setValue (uint32);

    // Add the closed interval to the set
    void setRange (uint32, uint32);

    void clearValue (uint32);

    std::string toString () const;

    /** Check invariants of the data.

        This is for diagnostics, and does nothing in release builds.
    */
    void checkInternalConsistency () const noexcept;

private:
    void simplify ();

private:
    typedef std::map <uint32, uint32> Map;

    typedef Map::const_iterator            const_iterator;
    typedef Map::const_reverse_iterator    const_reverse_iterator;
    typedef Map::value_type                value_type;
    typedef Map::iterator                  iterator;

    static bool contains (value_type const& it, uint32 v)
    {
        return (it.first <= v) && (it.second >= v);
    }

    // First is lowest value in range, last is highest value in range
    Map mRanges;
};

} // ripple

#endif
