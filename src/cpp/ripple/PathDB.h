#ifndef PATHDB__H
#define PATHBD__H

#include <set>

#include "uint256.h"
#include "TaggedCache.h"

typedef std::pair<uint160, uint160> currencyIssuer_t;
typedef std::pair<const uint160&, const uint160&> currencyIssuer_ct;

class PathDBEntry
{
public:
	typedef boost::shared_ptr<PathDBEntry>	pointer;
	typedef const pointer&					ref;

	static const unsigned int sIsExchange	= 0x00001;
	static const unsigned int sIsOffer 	= 0x00002;
	static const unsigned int sIsDirty		= 0x10000;

protected:
	currencyIssuer_t	mIn;
	currencyIssuer_t	mOut;
	uint32				mLastSeq;
	int					mUseCount;
	unsigned			mFlags;
	std::size_t			mHash;

public:

	void				updateSeq(uint32);

	const uint160&		getCurrencyIn() const		{ return mIn.first; }
	const uint160&		getIssuerIn() const			{ return mIn.second; }
	const uint160&		getCurrencyOut() const		{ return mOut.first; }
	const uint160&		getIssuerOut() const		{ return mOut.second; }

	bool				isExchange() const;
	bool				isOffer() const;
	bool				isDirty() const;
};

class PathDB
{
protected:
	boost::recursive_mutex						mLock;
	TaggedCache<currencyIssuer_t, PathDBEntry>	mFromCache;
	TaggedCache<currencyIssuer_t, PathDBEntry>	mToCache;
//	std::set<PathDBEntry::pointer>				mDirtyPaths;

public:

	PathDB();

	std::vector<PathDBEntry::pointer>	getPathsFrom(const uint160& currency, const uint160& issuer,
		int maxBestPaths = 10, int maxRandPaths = 10);

	std::vector<PathDBEntry::pointer>	getPathsTo(const uint160& currency, const uint160& issuer,
		int maxBestPaths = 10, int maxRandPaths = 10);

	void usedLine(const uint160& currency, const uint160& accountIn, const uint160& accountOut);
	void usedExchange(const uint160& currencyFrom, const uint160& issuerFrom,
		const uint160& currencyTo, const uint160& issuerTo);
};

extern std::size_t hash_value(const currencyIssuer_ct& ci)
{
	std::size_t r = hash_value(ci.second);
	return ci.first.hash_combine(r);
}

static inline std::size_t hash_value(const currencyIssuer_t& ci)
{
	std::size_t r = hash_value(ci.second);
	return ci.first.hash_combine(r);
}


#endif
