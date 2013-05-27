
SETUP_LOG (RangeSet)

inline uint32 min(uint32 x, uint32 y)	{ return (x < y) ? x : y; }
inline uint32 max(uint32 x, uint32 y)	{ return (x > y) ? x : y; }

bool RangeSet::hasValue(uint32 v) const
{
	return mRanges.find(v) != mRanges.end();
}

uint32 RangeSet::getFirst() const
{
	const_iterator it = begin();
	if (it == end())
		return RangeSetAbsent;
	return lower(it);
}

uint32 RangeSet::getNext(uint32 v) const
{
	for (const_iterator it = begin(); it != end(); ++it)
	{
		if (upper(it) > v)
			return max(v + 1, lower(it));
	}
	return RangeSetAbsent;
}

uint32 RangeSet::getLast() const
{
	const_reverse_iterator it = rbegin();
	if (it == rend())
		return RangeSetAbsent;
	return upper(it);
}

uint32 RangeSet::getPrev(uint32 v) const
{
	for (const_reverse_iterator it = rbegin(); it != rend(); ++it)
	{
		if (lower(it) < v)
			return min(v - 1, upper(it));
	}
	return RangeSetAbsent;
}

uint32 RangeSet::prevMissing(uint32 v) const
{ // largest number not in the set that is less than the given number
	WriteLog (lsTRACE, RangeSet) << "prevMissing(" << v << ") " << toString();
	for (const_reverse_iterator it = rbegin(); it != rend(); ++it)
	{
		if ((upper(it) + 1) < v)
			return upper(it) + 1;
		if (lower(it) == 0)
			return RangeSetAbsent;
		if ((lower(it) - 1) < v)
			return lower(it) - 1;
	}
	if (v > 0)
		return v - 1;
	return RangeSetAbsent;
}

void RangeSet::setValue(uint32 v)
{
	setRange(v, v);
}

void RangeSet::setRange(uint32 minV, uint32 maxV)
{
	mRanges.add(boost::icl::discrete_interval<uint32>(minV, maxV + 1));
}

void RangeSet::clearValue(uint32 v)
{
	clearRange(v, v);
}

void RangeSet::clearRange(uint32 minV, uint32 maxV)
{
	mRanges.erase(boost::icl::discrete_interval<uint32>(minV, maxV + 1));
}

std::string RangeSet::toString() const
{
	std::string ret;
	for (const_iterator it = begin(); it != end(); ++it)
	{
		if (!ret.empty())
			ret += ",";
		if (lower(it) == upper(it))
			ret += boost::lexical_cast<std::string>(lower(it));
		else
			ret += boost::lexical_cast<std::string>(lower(it)) + "-"
				+ boost::lexical_cast<std::string>(upper(it));
	}
	if (ret.empty())
		return "empty";
	return ret;
}

BOOST_AUTO_TEST_SUITE(RangeSet_suite)

BOOST_AUTO_TEST_CASE(RangeSet_test)
{
	WriteLog (lsTRACE, RangeSet) << "RangeSet test begins";

	RangeSet r1, r2;

	r1.setRange(1,10);
	r1.clearValue(5);
	r1.setRange(11, 20);

	r2.setRange(1, 4);
	r2.setRange(6, 10);
	r2.setRange(10, 20);
	if (r1.hasValue(5))		BOOST_FAIL("RangeSet fail");
	if (!r2.hasValue(9))	BOOST_FAIL("RangeSet fail");

	// TODO: Traverse functions must be tested

	WriteLog (lsTRACE, RangeSet) << "RangeSet test complete";
}

BOOST_AUTO_TEST_SUITE_END()

// vim:ts=4
