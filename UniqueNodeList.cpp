#include "UniqueNodeList.h"
#include "Application.h"
#include "Convertion.h"

using namespace std;

void UniqueNodeList::addNode(uint160& hanko, vector<unsigned char>& publicKey)
{
	Database* db=theApp->getDB();
	string sql="INSERT INTO UNL (Hanko,PubKey) values (";
	string hashStr;
	db->escape(hanko.begin(),hanko.GetSerializeSize(),hashStr);
	sql.append(hashStr);
	sql.append(",");
	db->escape(&(publicKey[0]),publicKey.size(),hashStr);
	sql.append(hashStr);
	sql.append(")");

	db->executeSQL(sql.c_str());
}

void UniqueNodeList::removeNode(uint160& hanko)
{
	Database* db=theApp->getDB();
	string sql="DELETE FROM UNL where hanko=";
	string hashStr;
	db->escape(hanko.begin(),hanko.GetSerializeSize(),hashStr);
	sql.append(hashStr);
	db->executeSQL(sql.c_str());
}

// 0- we don't care, 1- we care and is valid, 2-invalid signature
int UniqueNodeList::checkValid(newcoin::Validation& valid)
{
	Database* db=theApp->getDB();
	string sql="SELECT pubkey from UNL where hanko=";
	string hashStr;
	db->escape((unsigned char*) &(valid.hanko()[0]),valid.hanko().size(),hashStr);
	sql.append(hashStr);

	if( db->executeSQL(sql.c_str()) )
	{
		if(db->startIterRows() && db->getNextRow())
		{
			//theApp->getDB()->getBytes();

			// TODO: check that the public key makes the correct signature of the validation
			db->endIterRows();
			return(1);
		}else 
		{
			db->endIterRows();
			
		}
	}
	return(0); // not on our list
}


void UniqueNodeList::dumpUNL(std::string& retStr)
{
	Database* db=theApp->getDB();
	string sql="SELECT * FROM UNL";
	if( db->executeSQL(sql.c_str()) )
	{
		db->startIterRows();
		while(db->getNextRow())
		{
			uint160 hanko;
			int size=db->getBinary("Hanko",hanko.begin(),hanko.GetSerializeSize());
			string tstr;
			u160ToHuman(hanko,tstr);

			retStr.append(tstr);
			retStr.append("\n");
		}
		db->endIterRows();
	}
}

