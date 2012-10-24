#ifndef RANGESET__H
#define RANGESET__H

#include <list>

#include <boost/foreach.hpp>

class RangeSet
{

public:

	typedef std::pair<int, int>							Range;
	typedef std::list<Range>::iterator					iterator;
	typedef std::list<Range>::const_iterator			const_iterator;
	typedef std::list<Range>::reverse_iterator			reverse_iterator;
	typedef std::list<Range>::const_reverse_iterator	const_reverse_iterator;
	static const int RangeSetAbsent = -1;

protected:

	std::list<Range> mRanges;

public:

	RangeSet()					{ ; }

	bool hasValue(int) const;
	int getFirst() const;
	int getNext(int) const;
	int getLast() const;
	int getPrev(int) const;

	bool setValue(int);
	void setRange(int, int);
	bool clearValue(int);
	void clearRange(int, int);

	// iterator stuff
	iterator begin()						{ return mRanges.begin(); }
	iterator end()							{ return mRanges.end(); }
	const_iterator begin() const			{ return mRanges.begin(); }
	const_iterator end() const				{ return mRanges.end(); }
	reverse_iterator rbegin()				{ return mRanges.rbegin(); }
	reverse_iterator rend()					{ return mRanges.rend(); }
	const_reverse_iterator rbegin() const	{ return mRanges.rbegin(); }
	const_reverse_iterator rend() const		{ return mRanges.rend(); }
};

inline RangeSet::const_iterator	range_begin(const RangeSet& r)	{ return r.begin(); }
inline RangeSet::iterator		range_begin(RangeSet& r)		{ return r.begin(); }
inline RangeSet::const_iterator	range_end(const RangeSet& r)	{ return r.end(); }
inline RangeSet::iterator		range_end(RangeSet& r)			{ return r.end(); }

namespace boost
{
	template<> struct range_mutable_iterator<RangeSet>
	{
		typedef RangeSet::iterator type;
	};
	template<> struct range_const_iterator<RangeSet>
	{
		typedef RangeSet::const_iterator type;
	};
}

#endif
