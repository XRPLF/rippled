
#include "RangeSet.h"

inline int min(int x, int y)	{ return (x < y) ? x : y; }
inline int max(int x, int y)	{ return (x > y) ? x : y; }

bool RangeSet::hasValue(int v) const
{
	for (const_iterator it = begin(); it != end(); ++it)
	{
		if ((v >= it->first) && (v <= it->second))
			return true;
	}
	return false;
}

int RangeSet::getFirst() const
{
	const_iterator it = begin();
	if (it == end())
		return RangeSetAbsent;
	return it->first;
}

int RangeSet::getNext(int v) const
{
	for (const_iterator it = begin(); it != end(); ++it)
	{
		if (it->second > v)
			return min(v + 1, it->first);
	}
	return RangeSetAbsent;
}

int RangeSet::getLast() const
{
	const_reverse_iterator it = rbegin();
	if (it == rend())
		return RangeSetAbsent;
	return it->second;
}

int RangeSet::getPrev(int v) const
{
	for (const_reverse_iterator it = rbegin(); it != rend(); ++it)
	{
		if (it->first < v)
			return max(v - 1, it->second);
	}
	return RangeSetAbsent;
}

bool RangeSet::setValue(int v)
{
	for (iterator it = begin(); it != end(); ++it)
	{
		if (it->first >= v)
		{ // entry goes before or in this entry
			if (it->second <= v)
				return false;
			if (it->first > (v - 1))
			{
				mRanges.insert(it, std::make_pair(v, v));
				return true;
			}
			else if (it->first == (v - 1))
			{
				it->first = v;
				// WRITEME: check for consolidation
			}
			// WRITEME
		}
	}
	mRanges.push_back(std::make_pair(v, v));
	return true;
}

void RangeSet::setRange(int minV, int maxV)
{
}

bool RangeSet::clearValue(int v)
{
	for (iterator it = begin(); it != end(); ++it)
	{
		if (it->first >= v)
		{ // we are at or past the value we need to clear

			if (it->second > v)
				return false;

			if ((it->first == v) && (it->second == v))
				mRanges.erase(it);
			else if (it->first == v)
				++it->first;
			else if (it->second == v)
				--it->second;
			else
			{ // this pokes a hole
				int first = it->first;
				it->first = v + 1;
				mRanges.insert(it, std::make_pair(first, v - 1));
			}
			return true;
		}
	}
	return false;
}

void RangeSet::clearRange(int minV, int maxV)
{
}
