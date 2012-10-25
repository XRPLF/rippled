#ifndef RANGESET__H
#define RANGESET__H

#include <list>
#include <string>

#include <boost/foreach.hpp>
#include <boost/icl/interval_set.hpp>

class RangeSet
{

public:

	typedef boost::icl::interval_set<int>		iRangeSet;
	typedef iRangeSet::iterator					iterator;
	typedef iRangeSet::const_iterator			const_iterator;
	typedef iRangeSet::reverse_iterator			reverse_iterator;
	typedef iRangeSet::const_reverse_iterator	const_reverse_iterator;
	static const int RangeSetAbsent = -1;

protected:

	iRangeSet mRanges;

public:

	RangeSet()					{ ; }

	bool hasValue(int) const;
	int getFirst() const;
	int getNext(int) const;
	int getLast() const;
	int getPrev(int) const;

	void setValue(int);
	void setRange(int, int);
	void clearValue(int);
	void clearRange(int, int);

	void clear()							{ mRanges.clear(); }

	// iterator stuff
	iterator begin()						{ return mRanges.begin(); }
	iterator end()							{ return mRanges.end(); }
	const_iterator begin() const			{ return mRanges.begin(); }
	const_iterator end() const				{ return mRanges.end(); }
	reverse_iterator rbegin()				{ return mRanges.rbegin(); }
	reverse_iterator rend()					{ return mRanges.rend(); }
	const_reverse_iterator rbegin() const	{ return mRanges.rbegin(); }
	const_reverse_iterator rend() const		{ return mRanges.rend(); }

	static int lower(const_iterator& it)				{ return it->lower(); }
	static int upper(const_iterator& it)				{ return it->upper() - 1; }
	static int lower(const_reverse_iterator& it)		{ return it->lower(); }
	static int upper(const_reverse_iterator& it)		{ return it->upper() - 1; }


	bool operator!=(const RangeSet& r) const	{ return mRanges != r.mRanges; }
	bool operator==(const RangeSet& r) const	{ return mRanges == r.mRanges; }

	std::string toString() const;
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

// vim:ts=4
