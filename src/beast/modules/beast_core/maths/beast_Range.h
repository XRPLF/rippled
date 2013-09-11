//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#ifndef BEAST_RANGE_H_INCLUDED
#define BEAST_RANGE_H_INCLUDED


//==============================================================================
/** A general-purpose range object, that simply represents any linear range with
    a start and end point.

    The templated parameter is expected to be a primitive integer or floating point
    type, though class types could also be used if they behave in a number-like way.
*/
template <typename ValueType>
class Range
{
public:
    //==============================================================================
    /** Constructs an empty range. */
    Range() noexcept  : start(), end()
    {
    }

    /** Constructs a range with given start and end values. */
    Range (const ValueType startValue, const ValueType endValue) noexcept
        : start (startValue), end (bmax (startValue, endValue))
    {
    }

    /** Constructs a copy of another range. */
    Range (const Range& other) noexcept
        : start (other.start), end (other.end)
    {
    }

    /** Copies another range object. */
    Range& operator= (Range other) noexcept
    {
        start = other.start;
        end = other.end;
        return *this;
    }

    /** Returns the range that lies between two positions (in either order). */
    static Range between (const ValueType position1, const ValueType position2) noexcept
    {
        return position1 < position2 ? Range (position1, position2)
                                     : Range (position2, position1);
    }

    /** Returns a range with the specified start position and a length of zero. */
    static Range emptyRange (const ValueType start) noexcept
    {
        return Range (start, start);
    }

    //==============================================================================
    /** Returns the start of the range. */
    inline ValueType getStart() const noexcept          { return start; }

    /** Returns the length of the range. */
    inline ValueType getLength() const noexcept         { return end - start; }

    /** Returns the end of the range. */
    inline ValueType getEnd() const noexcept            { return end; }

    /** Returns true if the range has a length of zero. */
    inline bool isEmpty() const noexcept                { return start == end; }

    //==============================================================================
    /** Changes the start position of the range, leaving the end position unchanged.
        If the new start position is higher than the current end of the range, the end point
        will be pushed along to equal it, leaving an empty range at the new position.
    */
    void setStart (const ValueType newStart) noexcept
    {
        start = newStart;
        if (end < newStart)
            end = newStart;
    }

    /** Returns a range with the same end as this one, but a different start.
        If the new start position is higher than the current end of the range, the end point
        will be pushed along to equal it, returning an empty range at the new position.
    */
    Range withStart (const ValueType newStart) const noexcept
    {
        return Range (newStart, bmax (newStart, end));
    }

    /** Returns a range with the same length as this one, but moved to have the given start position. */
    Range movedToStartAt (const ValueType newStart) const noexcept
    {
        return Range (newStart, end + (newStart - start));
    }

    /** Changes the end position of the range, leaving the start unchanged.
        If the new end position is below the current start of the range, the start point
        will be pushed back to equal the new end point.
    */
    void setEnd (const ValueType newEnd) noexcept
    {
        end = newEnd;
        if (newEnd < start)
            start = newEnd;
    }

    /** Returns a range with the same start position as this one, but a different end.
        If the new end position is below the current start of the range, the start point
        will be pushed back to equal the new end point.
    */
    Range withEnd (const ValueType newEnd) const noexcept
    {
        return Range (bmin (start, newEnd), newEnd);
    }

    /** Returns a range with the same length as this one, but moved to have the given end position. */
    Range movedToEndAt (const ValueType newEnd) const noexcept
    {
        return Range (start + (newEnd - end), newEnd);
    }

    /** Changes the length of the range.
        Lengths less than zero are treated as zero.
    */
    void setLength (const ValueType newLength) noexcept
    {
        end = start + bmax (ValueType(), newLength);
    }

    /** Returns a range with the same start as this one, but a different length.
        Lengths less than zero are treated as zero.
    */
    Range withLength (const ValueType newLength) const noexcept
    {
        return Range (start, start + newLength);
    }

    //==============================================================================
    /** Adds an amount to the start and end of the range. */
    inline Range operator+= (const ValueType amountToAdd) noexcept
    {
        start += amountToAdd;
        end += amountToAdd;
        return *this;
    }

    /** Subtracts an amount from the start and end of the range. */
    inline Range operator-= (const ValueType amountToSubtract) noexcept
    {
        start -= amountToSubtract;
        end -= amountToSubtract;
        return *this;
    }

    /** Returns a range that is equal to this one with an amount added to its
        start and end.
    */
    Range operator+ (const ValueType amountToAdd) const noexcept
    {
        return Range (start + amountToAdd, end + amountToAdd);
    }

    /** Returns a range that is equal to this one with the specified amount
        subtracted from its start and end. */
    Range operator- (const ValueType amountToSubtract) const noexcept
    {
        return Range (start - amountToSubtract, end - amountToSubtract);
    }

    bool operator== (Range other) const noexcept     { return start == other.start && end == other.end; }
    bool operator!= (Range other) const noexcept     { return start != other.start || end != other.end; }

    //==============================================================================
    /** Returns true if the given position lies inside this range. */
    bool contains (const ValueType position) const noexcept
    {
        return start <= position && position < end;
    }

    /** Returns the nearest value to the one supplied, which lies within the range. */
    ValueType clipValue (const ValueType value) const noexcept
    {
        return blimit (start, end, value);
    }

    /** Returns true if the given range lies entirely inside this range. */
    bool contains (Range other) const noexcept
    {
        return start <= other.start && end >= other.end;
    }

    /** Returns true if the given range intersects this one. */
    bool intersects (Range other) const noexcept
    {
        return other.start < end && start < other.end;
    }

    /** Returns the range that is the intersection of the two ranges, or an empty range
        with an undefined start position if they don't overlap. */
    Range getIntersectionWith (Range other) const noexcept
    {
        return Range (bmax (start, other.start),
                      bmin (end, other.end));
    }

    /** Returns the smallest range that contains both this one and the other one. */
    Range getUnionWith (Range other) const noexcept
    {
        return Range (bmin (start, other.start),
                      bmax (end, other.end));
    }

    /** Returns a given range, after moving it forwards or backwards to fit it
        within this range.

        If the supplied range has a greater length than this one, the return value
        will be this range.

        Otherwise, if the supplied range is smaller than this one, the return value
        will be the new range, shifted forwards or backwards so that it doesn't extend
        beyond this one, but keeping its original length.
    */
    Range constrainRange (Range rangeToConstrain) const noexcept
    {
        const ValueType otherLen = rangeToConstrain.getLength();
        return getLength() <= otherLen
                ? *this
                : rangeToConstrain.movedToStartAt (blimit (start, end - otherLen, rangeToConstrain.getStart()));
    }

private:
    //==============================================================================
    ValueType start, end;
};


#endif   // BEAST_RANGE_H_INCLUDED
