#ifndef __UNIQUE_NODE_LIST__
#define __UNIQUE_NODE_LIST__

#include "../json/value.h"

#include "uint256.h"
#include "NewcoinAddress.h"

class UniqueNodeList
{
	// hanko to public key
	//std::map<uint160, uint256> mUNL;
public:
	//void load();
	//void save();

	void addNode(NewcoinAddress nodePublic, std::string strComment);
	void removeNode(NewcoinAddress hanko);

	// 0- we don't care, 1- we care and is valid, 2-invalid signature
//	int checkValid(newcoin::Validation& valid);

	Json::Value getUnlJson();
};

#endif
