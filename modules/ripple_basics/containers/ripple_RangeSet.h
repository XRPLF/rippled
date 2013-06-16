//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RANGESET_H
#define RIPPLE_RANGESET_H

/** A sparse set of integers.
*/
// VFALCO TODO Replace with juce::SparseSet
class RangeSet
{
public:
    static const uint32 RangeSetAbsent = static_cast<uint32> (-1);

protected:
    std::map<uint32, uint32>    mRanges;    // First is lowest value in range, last is highest value in range

    typedef std::map<uint32, uint32>::const_iterator            const_iterator;
    typedef std::map<uint32, uint32>::const_reverse_iterator    const_reverse_iterator;
    typedef std::map<uint32, uint32>::value_type                value_type;
    typedef std::map<uint32, uint32>::iterator                  iterator;

    static bool contains (value_type const& it, uint32 v)
    {
        return (it.first <= v) && (it.second >= v);
    }

    void simplify ();

public:
    RangeSet () { }

    bool hasValue (uint32) const;
    uint32 getFirst () const;
    uint32 getNext (uint32) const;
    uint32 getLast () const;
    uint32 getPrev (uint32) const;

    uint32 prevMissing (uint32) const;      // largest number not in the set that is less than the given number

    void setValue (uint32);
    void setRange (uint32, uint32);
    void clearValue (uint32);

    std::string toString () const;
};

// VFALCO TODO these parameters should not be const references.
template<typename T, typename U> T range_check_cast (const U& value, const T& minimum, const T& maximum)
{
    if ((value < minimum) || (value > maximum))
        throw std::runtime_error ("Value out of range");

    return static_cast<T> (value);
}


#endif

// vim:ts=4
