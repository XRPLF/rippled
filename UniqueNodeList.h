#ifndef __UNIQUE_NODE_LIST__
#define __UNIQUE_NODE_LIST__
#include "newcoin.pb.h"

class UniqueNodeList
{
	// hanko to public key
	//std::map<uint160,uint512> mUNL;
public:
	//void load();
	//void save();

	void addNode(uint160& hanko,std::vector<unsigned char>& publicKey);
	void removeNode(uint160& hanko);

	// 0- we don't care, 1- we care and is valid, 2-invalid signature
	int checkValid(newcoin::Validation& valid);

	
};

#endif