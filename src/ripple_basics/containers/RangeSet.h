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

/** A sparse set of integers.
*/
// VFALCO TODO Replace with juce::SparseSet
class RangeSet
{
public:
    static const beast::uint32 absent = static_cast <beast::uint32> (-1);

public:
    RangeSet () { }

    bool hasValue (beast::uint32) const;

    beast::uint32 getFirst () const;
    beast::uint32 getNext (beast::uint32) const;
    beast::uint32 getLast () const;
    beast::uint32 getPrev (beast::uint32) const;

    // largest number not in the set that is less than the given number
    beast::uint32 prevMissing (beast::uint32) const;

    // Add an item to the set
    void setValue (beast::uint32);

    // Add the closed interval to the set
    void setRange (beast::uint32, beast::uint32);

    void clearValue (beast::uint32);

    std::string toString () const;

    /** Check invariants of the data.

        This is for diagnostics, and does nothing in release builds.
    */
    void checkInternalConsistency () const noexcept;

private:
    void simplify ();

private:
    typedef std::map <beast::uint32, beast::uint32> Map;

    typedef Map::const_iterator            const_iterator;
    typedef Map::const_reverse_iterator    const_reverse_iterator;
    typedef Map::value_type                value_type;
    typedef Map::iterator                  iterator;

    static bool contains (value_type const& it, beast::uint32 v)
    {
        return (it.first <= v) && (it.second >= v);
    }

    // First is lowest value in range, last is highest value in range
    Map mRanges;
};

#endif
