#include "UniqueNodeList.h"
#include "Application.h"


void UniqueNodeList::addNode(uint160& hanko,uint1024& publicKey)
{
	"INSERT INTO UNL values ("
	theApp->getDB()->executeSQL(sql);
}

void UniqueNodeList::removeNode(uint160& hanko)
{
	"DELETE FROM UNL where hanko=" 
	theApp->getDB()->executeSQL(sql);

}

int UniqueNodeList::checkValid(newcoin::Validation& valid)
{
	"SELECT pubkey from UNL where hanko="
	if( theApp->getDB()->executeSQL(sql) )
	{
		if(theApp->getDB()->getNextRow())
		{
			theApp->getDB()->getBytes();

		}else return(0); // not on our list
	}
	return(1);
}

