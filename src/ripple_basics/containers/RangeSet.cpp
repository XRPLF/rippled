//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (RangeSet)

// VFALCO NOTE std::min and std::max not good enough?
//        NOTE Why isn't this written as a template?
//        TODO Replace this with std calls.
//
inline uint32 min (uint32 x, uint32 y)
{
    return (x < y) ? x : y;
}
inline uint32 max (uint32 x, uint32 y)
{
    return (x > y) ? x : y;
}

bool RangeSet::hasValue (uint32 v) const
{
    BOOST_FOREACH (const value_type & it, mRanges)
    {
        if (contains (it, v))
            return true;
    }
    return false;
}

uint32 RangeSet::getFirst () const
{
    const_iterator it = mRanges.begin ();

    if (it == mRanges.end ())
        return absent;

    return it->first;
}

uint32 RangeSet::getNext (uint32 v) const
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

uint32 RangeSet::getLast () const
{
    const_reverse_iterator it = mRanges.rbegin ();

    if (it == mRanges.rend ())
        return absent;

    return it->second;
}

uint32 RangeSet::getPrev (uint32 v) const
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
uint32 RangeSet::prevMissing (uint32 v) const
{
    uint32 result = absent;

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

void RangeSet::setValue (uint32 v)
{
    if (!hasValue (v))
    {
        mRanges[v] = v;
        
        simplify ();
    }
}

void RangeSet::setRange (uint32 minV, uint32 maxV)
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

void RangeSet::clearValue (uint32 v)
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
                    uint32 oldEnd = it->second;
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
                uint32 oldEnd = it->second;
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
            ret += lexicalCastThrow <std::string> ((it.first));
        else
            ret += lexicalCastThrow <std::string> (it.first) + "-"
                   + lexicalCastThrow <std::string> (it.second);
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
            it->second = nit->second;
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

class RangeSetTests : public UnitTest
{
public:
    RangeSetTests () : UnitTest ("RangeSet", "ripple")
    {
    }

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
        beginTestCase ("membership");

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
        beginTestCase ("prevMissing");

        RangeSet const set = createPredefinedSet ();

        for (int i = 0; i < 100; ++i)
        {
            int const oneBelowRange = (10*(i/10))-1;

            int const expectedPrevMissing =
                ((i % 10) > 6) ? (i-1) : oneBelowRange;

            expect (set.prevMissing (i) == expectedPrevMissing);
        }
    }

    void runTest ()
    {
        testMembership ();

        testPrevMissing ();

        // TODO: Traverse functions must be tested
    }
};

static RangeSetTests rangeSetTests;

