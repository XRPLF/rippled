#ifndef __KNOWNNODELIST__
#define __KNOWNNODELIST__

#include "vector"

class KnownNode
{
public:
	std::string mIP;
	int mPort;
	int mLastSeen;
	int mLastTried;

	KnownNode(const char* ip,int port,int lastSeen,int lastTried);
};

class KnownNodeList
{
	unsigned int mTriedIndex;
	std::vector <KnownNode> mNodes;
public:
	KnownNodeList();

	void load();
	void addNode();
	KnownNode* getNextNode();

};

#endif