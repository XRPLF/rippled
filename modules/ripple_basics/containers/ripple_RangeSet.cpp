//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (RangeSet)

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
        return RangeSetAbsent;

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
    return RangeSetAbsent;
}

uint32 RangeSet::getLast () const
{
    const_reverse_iterator it = mRanges.rbegin ();

    if (it == mRanges.rend ())
        return RangeSetAbsent;

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
    return RangeSetAbsent;
}

uint32 RangeSet::prevMissing (uint32 v) const
{
    // largest number not in the set that is less than the given number
    BOOST_FOREACH (const value_type & it, mRanges)
    {
        if (contains (it, v))
            return it.first - 1;

        if (it.first > v)
            return v + 1;
    }
    return RangeSetAbsent;
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
                    mRanges.erase (it);
                else
                {
                    ++ (it->first);
                }
            }
            else if (it->second == v)
                -- (it->second);
            else
            {
                uint32 oldEnd = it->second;
                it->second = v - 1;
                mRanges[v + 1] = oldEnd;
            }

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
            ret += boost::lexical_cast<std::string> ((it.first));
        else
            ret += boost::lexical_cast<std::string> (it.first) + "-"
                   + boost::lexical_cast<std::string> (it.second);
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
            return;

        if (it->second >= (nit->first - 1))
        {
            // ranges overlap
            it->second = nit->second;
            mRanges.erase (nit);
        }
        else
            it = nit;
    }
}

BOOST_AUTO_TEST_SUITE (RangeSet_suite)

BOOST_AUTO_TEST_CASE (RangeSet_test)
{
    WriteLog (lsTRACE, RangeSet) << "RangeSet test begins";

    RangeSet r1, r2;

    r1.setRange (1, 10);
    r1.clearValue (5);
    r1.setRange (11, 20);

    r2.setRange (1, 4);
    r2.setRange (6, 10);
    r2.setRange (10, 20);

    if (r1.hasValue (5))     BOOST_FAIL ("RangeSet fail");

    if (!r2.hasValue (9))    BOOST_FAIL ("RangeSet fail");

    // TODO: Traverse functions must be tested

    WriteLog (lsTRACE, RangeSet) << "RangeSet test complete";
}

BOOST_AUTO_TEST_SUITE_END ()

// vim:ts=4
