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

#include <ripple/basics/log/Log.h>
#include <ripple/basics/containers/RangeSet.h>
#include <beast/unit_test/suite.h>
#include <beast/module/core/text/LexicalCast.h>
#include <boost/foreach.hpp>
#include <cstdint>

namespace ripple {

SETUP_LOG (RangeSet)

// VFALCO NOTE std::min and std::max not good enough?
//        NOTE Why isn't this written as a template?
//        TODO Replace this with std calls.
//
inline std::uint32_t min (std::uint32_t x, std::uint32_t y)
{
    return (x < y) ? x : y;
}
inline std::uint32_t max (std::uint32_t x, std::uint32_t y)
{
    return (x > y) ? x : y;
}

bool RangeSet::hasValue (std::uint32_t v) const
{
    BOOST_FOREACH (const value_type & it, mRanges)
    {
        if (contains (it, v))
            return true;
    }
    return false;
}

std::uint32_t RangeSet::getFirst () const
{
    const_iterator it = mRanges.begin ();

    if (it == mRanges.end ())
        return absent;

    return it->first;
}

std::uint32_t RangeSet::getNext (std::uint32_t v) const
{
    BOOST_FOREACH (const value_type & it, mRanges)
    {
        if (it.first > v)
            return it.first;

        if (contains (it, v + 1))
            return v + 1;
    }
    return absent;
}

std::uint32_t RangeSet::getLast () const
{
    const_reverse_iterator it = mRanges.rbegin ();

    if (it == mRanges.rend ())
        return absent;

    return it->second;
}

std::uint32_t RangeSet::getPrev (std::uint32_t v) const
{
    BOOST_REVERSE_FOREACH (const value_type & it, mRanges)
    {
        if (it.second < v)
            return it.second;

        if (contains (it, v + 1))
            return v - 1;
    }
    return absent;
}

// Return the largest number not in the set that is less than the given number
//
std::uint32_t RangeSet::prevMissing (std::uint32_t v) const
{
    std::uint32_t result = absent;

    if (v != 0)
    {
        checkInternalConsistency ();

        // Handle the case where the loop reaches the terminating condition
        //
        result = v - 1;

        for (const_reverse_iterator cur = mRanges.rbegin (); cur != mRanges.rend (); ++cur)
        {
            // See if v-1 is in the range
            if (contains (*cur, result))
            {
                result = cur->first - 1;
                break;
            }
        }
    }

    bassert (result == absent || !hasValue (result));

    return result;
}

void RangeSet::setValue (std::uint32_t v)
{
    if (!hasValue (v))
    {
        mRanges[v] = v;
        
        simplify ();
    }
}

void RangeSet::setRange (std::uint32_t minV, std::uint32_t maxV)
{
    while (hasValue (minV))
    {
        ++minV;

        if (minV >= maxV)
            return;
    }

    mRanges[minV] = maxV;

    simplify ();
}

void RangeSet::clearValue (std::uint32_t v)
{
    for (iterator it = mRanges.begin (); it != mRanges.end (); ++it)
    {
        if (contains (*it, v))
        {
            if (it->first == v)
            {
                if (it->second == v)
                {
                    mRanges.erase (it);
                }
                else
                {
                    std::uint32_t oldEnd = it->second;
                    mRanges.erase(it);
                    mRanges[v + 1] = oldEnd;
                }
            }
            else if (it->second == v)
            {
                -- (it->second);
            }
            else
            {
                std::uint32_t oldEnd = it->second;
                it->second = v - 1;
                mRanges[v + 1] = oldEnd;
            }

            checkInternalConsistency();
            return;
        }
    }
}

std::string RangeSet::toString () const
{
    std::string ret;
    BOOST_FOREACH (value_type const & it, mRanges)
    {
        if (!ret.empty ())
            ret += ",";

        if (it.first == it.second)
            ret += beast::lexicalCastThrow <std::string> ((it.first));
        else
            ret += beast::lexicalCastThrow <std::string> (it.first) + "-"
                   + beast::lexicalCastThrow <std::string> (it.second);
    }

    if (ret.empty ())
        return "empty";

    return ret;
}

void RangeSet::simplify ()
{
    iterator it = mRanges.begin ();

    while (1)
    {
        iterator nit = it;

        if (++nit == mRanges.end ())
        {
            checkInternalConsistency();
            return;
        }

        if (it->second >= (nit->first - 1))
        {
            // ranges overlap
            it->second = std::max(it->second, nit->second);
            mRanges.erase (nit);
        }
        else
        {
            it = nit;
        }
    }
}

void RangeSet::checkInternalConsistency () const noexcept
{
#if BEAST_DEBUG
    if (mRanges.size () > 1)
    {
        const_iterator const last = std::prev (mRanges.end ());

        for (const_iterator cur = mRanges.begin (); cur != last; ++cur)
        {
            const_iterator const next = std::next (cur);

            bassert (cur->first <= cur->second);

            bassert (next->first <= next->second);

            bassert (cur->second + 1 < next->first);
        }
    }
    else if (mRanges.size () == 1)
    {
        const_iterator const iter = mRanges.begin ();

        bassert (iter->first <= iter->second);
    }

#endif
}

//------------------------------------------------------------------------------

class RangeSet_test : public beast::unit_test::suite
{
public:
    RangeSet createPredefinedSet ()
    {
        RangeSet set;

        // Set will include:
        // [ 0, 5]
        // [10,15]
        // [20,25]
        // etc...

        for (int i = 0; i < 10; ++i)
            set.setRange (10 * i, 10 * i + 5);

        return set;
    }

    void testMembership ()
    {
        testcase ("membership");

        RangeSet r1, r2;

        r1.setRange (1, 10);
        r1.clearValue (5);
        r1.setRange (11, 20);

        r2.setRange (1, 4);
        r2.setRange (6, 10);
        r2.setRange (10, 20);

        expect (!r1.hasValue (5));

        expect (r2.hasValue (9));
    }

    void testPrevMissing ()
    {
        testcase ("prevMissing");

        RangeSet const set = createPredefinedSet ();

        for (int i = 0; i < 100; ++i)
        {
            int const oneBelowRange = (10*(i/10))-1;

            int const expectedPrevMissing =
                ((i % 10) > 6) ? (i-1) : oneBelowRange;

            expect (set.prevMissing (i) == expectedPrevMissing);
        }
    }

    void run ()
    {
        testMembership ();

        testPrevMissing ();

        // TODO: Traverse functions must be tested
    }
};

BEAST_DEFINE_TESTSUITE(RangeSet,ripple_basics,ripple);

} // ripple

