#ifndef RANGESET__H
#define RANGESET__H

#include <list>
#include <string>

#include <boost/foreach.hpp>
#include <boost/icl/interval_set.hpp>

#include "types.h"

class RangeSet
{

public:

	typedef boost::icl::interval_set<uint32>	iRangeSet;
	typedef iRangeSet::iterator					iterator;
	typedef iRangeSet::const_iterator			const_iterator;
	typedef iRangeSet::reverse_iterator			reverse_iterator;
	typedef iRangeSet::const_reverse_iterator	const_reverse_iterator;
	static const uint32 RangeSetAbsent = static_cast<uint32>(-1);

protected:

	iRangeSet mRanges;

public:

	RangeSet()					{ ; }

	bool hasValue(uint32) const;
	uint32 getFirst() const;
	uint32 getNext(uint32) const;
	uint32 getLast() const;
	uint32 getPrev(uint32) const;

	uint32 prevMissing(uint32) const;		// largest number not in the set that is less than the given number

	void setValue(uint32);
	void setRange(uint32, uint32);
	void clearValue(uint32);
	void clearRange(uint32, uint32);


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

	static uint32 lower(const_iterator& it)				{ return it->lower(); }
	static uint32 upper(const_iterator& it)				{ return it->upper() - 1; }
	static uint32 lower(const_reverse_iterator& it)		{ return it->lower(); }
	static uint32 upper(const_reverse_iterator& it)		{ return it->upper() - 1; }


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
