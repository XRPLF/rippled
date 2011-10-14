#ifndef __UNIQUE_NODE_LIST__
#define __UNIQUE_NODE_LIST__
#include "newcoin.pb.h"

class UniqueNodeList
{
public:
	void load();
	void save();

	bool findHanko(const std::string& hanko);

	
};

#endif