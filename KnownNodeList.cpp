#include "KnownNodeList.h"
#include "util/pugixml.hpp"

using namespace pugi;

KnownNode::KnownNode(const char* ip,int port,int lastSeen,int lastTried) 
	: mIP(ip), mPort(port), mLastSeen(lastSeen), mLastTried(lastTried)
{

}

KnownNodeList::KnownNodeList()
{
	mTriedIndex=0;
}

void KnownNodeList::load()
{
	xml_document doc;
	xml_parse_result result = doc.load_file("nodes.xml");
	xml_node nodes=doc.child("nodes");
	for(xml_node child = nodes.first_child(); child; child = child.next_sibling())
	{
		mNodes.push_back(KnownNode(child.attribute("ip").value(),child.attribute("port").as_int(),child.attribute("last").as_int(),0));
	}
}

KnownNode* KnownNodeList::getNextNode()
{
	if(mTriedIndex>=mNodes.size())
	{
		return(NULL);
	}else
	{
		mTriedIndex++;
		return(&(mNodes[mTriedIndex-1]));
	}
}