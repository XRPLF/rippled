#ifndef __DETERMINISTICKEYS__
#define __DETERMINISTICKEYS__

#include <list>

#include "key.h"

class DetKeySet
{
protected:
	uint256 mBase;

public:
	DetKeySet(const uint256& b) : mBase(b) { ; }
	DetKeySet(const std::string& phrase);

	~DetKeySet()
	{
		memset(&mBase, 0, sizeof(mBase));
	}
	
	void reBase(const uint256& newBase) { mBase=newBase; }
	uint256 getKeySetName();

	static bool getRandom(uint256&);
	static void getPhrase(const std::string& phrase, uint256& base);

	CKey::pointer getPubKey(uint32 n);
	CKey::pointer getPrivKey(uint32 n);
	uint160 getAccountID(uint32 n);
	
	void getPubKeys(uint32 first, uint32 count, std::list<CKey::pointer>& keys);
	void getPrivKeys(uint32 first, uint32 count, std::list<CKey::pointer>& keys);

	static void unitTest();
};

#endif
