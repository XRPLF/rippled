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

#ifndef BEAST_INTERVAL_H_INCLUDED
#define BEAST_INTERVAL_H_INCLUDED

/** A half-open interval.

    This represents the half-open interval [begin, end) over the scalar
    type of template parameter `Ty`. It may also be considered as the
    specification of a subset of a 1-dimensional Euclidean space.

    @tparam Ty A scalar numerical type.
*/
template <class Ty>
class Interval
{
public:
    typedef Ty value_type;

    /** The empty interval.
    */
    static const Interval none;

    /** Create an uninitialized interval.
    */
    Interval ()
    {
    }

    /** Create an interval with the specified values.
    */
    Interval (Ty begin, Ty end)
        : m_begin (begin)
        , m_end (end)
    {
    }

    /** Create an interval from another interval.
    */
    Interval (Interval const& other)
        : m_begin (other.m_begin)
        , m_end (other.m_end)
    {
    }

    /** Assign from another interval.

        @param other The interval to assign from.

        @return A reference to this interval.
    */
    Interval& operator= (const Interval& other)
    {
        m_begin = other.m_begin;
        m_end = other.m_end;
        return *this;
    }

    /** Compare an interval for equality.

        Empty intervals are always equal to other empty intervals.

        @param rhs The other interval to compare.

        @return `true` if this interval is equal to the specified interval.
    */
    bool operator== (Interval const& rhs) const
    {
        return (empty () && rhs.empty ()) ||
               (m_begin == rhs.m_begin && m_end == rhs.m_end);
    }

    /** Compare an interval for inequality.

        @param rhs The other interval to compare.

        @return `true` if this interval is not equal to the specified interval.
    */
    bool operator!= (Interval const& rhs) const
    {
        return !this->operator== (rhs);
    }

    /** Get the starting value of the interval.

        @return The starting point of the interval.
    */
    Ty begin () const
    {
        return m_begin;
    }

    /** Get the ending value of the interval.

        @return The ending point of the interval.
    */
    Ty end () const
    {
        return m_end;
    }

    /** Get the Lebesque measure.

        @return The Lebesque measure.
    */
    Ty length () const
    {
        return empty () ? Ty () : (end () - begin ());
    }

    //Ty count () const { return length (); } // sugar
    //Ty distance () const { return length (); } // sugar

    /** Determine if the interval is empty.

        @return `true` if the interval is empty.
    */
    bool empty () const
    {
        return m_begin >= m_end;
    }

    /** Determine if the interval is non-empty.

        @return `true` if the interval is not empty.
    */
    bool notEmpty () const
    {
        return m_begin < m_end;
    }

    /** Set the starting point of the interval.

        @param v The starting point.
    */
    void setBegin (Ty v)
    {
        m_begin = v;
    }

    /** Set the ending point of the interval.

        @param v The ending point.
    */
    void setEnd (Ty v)
    {
        m_end = v;
    }

    /** Set the ending point relative to the starting point.

        @param v The length of the resulting interval.
    */
    void setLength (Ty v)
    {
        m_end = m_begin + v;
    }

    /** Determine if a value is contained in the interval.

        @param v The value to check.

        @return `true` if this interval contains `v`.
    */
    bool contains (Ty v) const
    {
        return notEmpty () && v >= m_begin && v < m_end;
    }

    /** Determine if this interval intersects another interval.

        @param other The other interval.

        @return `true` if the intervals intersect.
    */
    template <class To>
    bool intersects (Interval <To> const& other) const
    {
        return notEmpty () && other.notEmpty () &&
               end () > other.begin () && begin () < other.end ();
    }

    /** Determine if this interval adjoins another interval.

        An interval is adjoint to another interval if and only if the union of the
        intervals is a single non-empty half-open subset.

        @param other The other interval.

        @return `true` if the intervals are adjoint.
    */
    template <class To>
    bool adjoins (Interval <To> const& other) const
    {
        return (empty () != other.empty ()) ||
               (notEmpty () && end () >= other.begin ()
                && begin () <= other.end ());
    }

    /** Determine if this interval is disjoint from another interval.

        @param other The other interval.

        @return `true` if the intervals are disjoint.
    */
    bool disjoint (Interval const& other) const
    {
        return !intersects (other);
    }

    /** Determine if this interval is a superset of another interval.

        An interval A is a superset of interval B if B is empty or if A fully
        contains B.

        @param other The other interval.

        @return `true` if this is a superset of `other`.
    */
    template <class To>
    bool superset_of (Interval <To> const& other) const
    {
        return other.empty () ||
               (notEmpty () && begin () <= other.begin ()
                && end () >= other.end ());
    }

    /** Determine if this interval is a proper superset of another interval.

        An interval A is a proper superset of interval B if A is a superset of
        B and A is not equal to B.

        @param other The other interval.

        @return `true` if this interval is a proper superset of `other`.
    */
    template <class To>
    bool proper_superset_of (Interval <To> const& other) const
    {
        return this->superset_of (other) && this->operator != (other);
    }

    /** Determine if this interval is a subset of another interval.

        @param other The other interval.

        @return `true` if this interval is a subset of `other`.
    */
    template <class To>
    bool subset_of (Interval <To> const& other) const
    {
        return other.superset_of (*this);
    }

    /** Determine if this interval is a proper subset of another interval.

        @param other The other interval.

        @return `true` if this interval is a proper subset of `other`.
    */
    template <class To>
    bool proper_subset_of (Interval <To> const& other) const
    {
        return other.proper_superset_of (*this);
    }

    /** Return the intersection of this interval with another interval.

        @param other The other interval.

        @return The intersection of the intervals.
    */
    template <class To>
    Interval intersection (Interval <To> const& other) const
    {
        return Interval (std::max (begin (), other.begin ()),
                         std::min (end (), other.end ()));
    }

    /** Determine the smallest interval that contains both intervals.

        @param other The other interval.

        @return The simple union of the intervals.
    */
    template <class To>
    Interval simple_union (Interval <To> const& other) const
    {
        return Interval (
                   std::min (other.normalized ().begin (), normalized ().begin ()),
                   std::max (other.normalized ().end (), normalized ().end ()));
    }

    /** Calculate the single-interval union.

        The result is empty if the union cannot be represented as a
        single half-open interval.

        @param other The other interval.

        @return The simple union of the intervals.
    */
    template <class To>
    Interval single_union (Interval <To> const& other) const
    {
        if (empty ())
            return other;

        else if (other.empty ())
            return *this;

        else if (end () < other.begin () || begin () > other.end ())
            return none;

        else
            return Interval (std::min (begin (), other.begin ()),
                             std::max (end (), other.end ()));
    }

    /** Determine if the interval is correctly ordered.

        @return `true` if the interval is correctly ordered.
    */
    bool normal () const
    {
        return end () >= begin ();
    }

    /** Return a normalized interval.

        @return The normalized interval.
    */
    Interval normalized () const
    {
        if (normal ())
            return *this;
        else
            return Interval (end (), begin ());
    }

    /** Clamp a value to the interval.

        @param v The value to clamp.

        @return The clamped result.
    */
    template <typename Tv>
    Ty clamp (Tv v) const
    {
        // These conditionals are carefully ordered so
        // that if m_begin == m_end, value is assigned m_begin.
        if (v > end ())
            v = end () - (std::numeric_limits <Tv>::is_integer ? 1 :
                          std::numeric_limits <Tv>::epsilon ());

        if (v < begin ())
            v = begin ();

        return v;
    }

private:
    Ty m_begin;
    Ty m_end;
};

template <typename Ty>
const Interval<Ty> Interval<Ty>::none = Interval<Ty> (Ty (), Ty ());

#endif
