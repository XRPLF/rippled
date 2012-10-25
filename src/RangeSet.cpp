
#include "RangeSet.h"

#include <boost/test/unit_test.hpp>
#include <boost/lexical_cast.hpp>

#include "Log.h"

SETUP_LOG();

inline int min(int x, int y)	{ return (x < y) ? x : y; }
inline int max(int x, int y)	{ return (x > y) ? x : y; }

bool RangeSet::hasValue(int v) const
{
	return mRanges.find(v) != mRanges.end();
}

int RangeSet::getFirst() const
{
	const_iterator it = begin();
	if (it == end())
		return RangeSetAbsent;
	return lower(it);
}

int RangeSet::getNext(int v) const
{
	for (const_iterator it = begin(); it != end(); ++it)
	{
		if (upper(it) > v)
			return max(v + 1, lower(it));
	}
	return RangeSetAbsent;
}

int RangeSet::getLast() const
{
	const_reverse_iterator it = rbegin();
	if (it == rend())
		return RangeSetAbsent;
	return upper(it);
}

int RangeSet::getPrev(int v) const
{
	for (const_reverse_iterator it = rbegin(); it != rend(); ++it)
	{
		if (lower(it) < v)
			return min(v - 1, upper(it));
	}
	return RangeSetAbsent;
}

void RangeSet::setValue(int v)
{
	setRange(v, v);
}

void RangeSet::setRange(int minV, int maxV)
{
	mRanges.add(boost::icl::discrete_interval<int>(minV, maxV + 1));
}

void RangeSet::clearValue(int v)
{
	clearRange(v, v);
}

void RangeSet::clearRange(int minV, int maxV)
{
	mRanges.erase(boost::icl::discrete_interval<int>(minV, maxV + 1));
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
	cLog(lsTRACE) << "RangeSet test begins";

	RangeSet r1, r2;

	if (r1 != r2)		BOOST_FAIL("RangeSet fail");

	r1.setValue(1);
	if (r1 == r2)		BOOST_FAIL("RangeSet fail");
	r2.setRange(1, 1);
	if (r1 != r2)		BOOST_FAIL("RangeSet fail");

	r1.clear();
	r1.setRange(1,10);
	r1.clearValue(5);
	r1.setRange(11, 20);

	r2.clear();
	r2.setRange(1, 4);
	r2.setRange(6, 10);
	r2.setRange(10, 20);
	if (r1 != r2)			BOOST_FAIL("RangeSet fail");
	if (r1.hasValue(5))		BOOST_FAIL("RangeSet fail");
	if (!r2.hasValue(9))	BOOST_FAIL("RangeSet fail");

	// TODO: Traverse functions must be tested

	cLog(lsTRACE) << "RangeSet test complete";
}

BOOST_AUTO_TEST_SUITE_END()

// vim:ts=4
