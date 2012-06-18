#define WIN32_LEAN_AND_MEAN 

#include <string>

#include "openssl/ec.h"

#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>

#include "Wallet.h"
#include "Ledger.h"
#include "NewcoinAddress.h"
#include "Application.h"
#include "utils.h"

Wallet::Wallet() : mLedger(0) {
}

void Wallet::start()
{
	// We need our node identity before we begin networking.
	// - Allows others to identify if they have connected multiple times.
	// - Determines our CAS routing and responsibilities.
	// - This is not our validation identity.
	if (!nodeIdentityLoad()) {
		nodeIdentityCreate();
		if (!nodeIdentityLoad())
			throw std::runtime_error("unable to retrieve new node identity.");
	}

	std::cerr << "NodeIdentity: " << mNodePublicKey.humanNodePublic() << std::endl;

	theApp->getUNL().start();
}

// Retrieve network identity.
bool Wallet::nodeIdentityLoad()
{
	Database*	db=theApp->getWalletDB()->getDB();
	ScopedLock	sl(theApp->getWalletDB()->getDBLock());
	bool		bSuccess	= false;

	if (db->executeSQL("SELECT * FROM NodeIdentity;") && db->startIterRows())
	{
		std::string strPublicKey, strPrivateKey;

		db->getStr("PublicKey", strPublicKey);
		db->getStr("PrivateKey", strPrivateKey);

		mNodePublicKey.setNodePublic(strPublicKey);
		mNodePrivateKey.setNodePrivate(strPrivateKey);

		mDh512	= DH_der_load(db->getStrBinary("Dh512"));
		mDh1024	= DH_der_load(db->getStrBinary("Dh1024"));

		db->endIterRows();
		bSuccess	= true;
	}

	return bSuccess;
}

// Create and store a network identity.
bool Wallet::nodeIdentityCreate() {
	std::cerr << "NodeIdentity: Creating." << std::endl;

	//
	// Generate the public and private key
	//
	NewcoinAddress	naSeed			= NewcoinAddress::createSeedRandom();
	NewcoinAddress	naNodePublic	= NewcoinAddress::createNodePublic(naSeed);
	NewcoinAddress	naNodePrivate	= NewcoinAddress::createNodePrivate(naSeed);

	// Make new key.

	std::string		strDh512		= DH_der_gen(512);
#if 1
	std::string		strDh1024		= strDh512;				// For testing and most cases 512 is fine.
#else
	std::string		strDh1024		= DH_der_gen(1024);
#endif

	//
	// Store the node information
	//
	Database* db	= theApp->getWalletDB()->getDB();

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	db->executeSQL(str(boost::format("INSERT INTO NodeIdentity (PublicKey,PrivateKey,Dh512,Dh1024) VALUES ('%s','%s',%s,%s);")
		% naNodePublic.humanNodePublic()
		% naNodePrivate.humanNodePrivate()
		% sqlEscape(strDh512)
		% sqlEscape(strDh1024)));
	// XXX Check error result.

	std::cerr << "NodeIdentity: Created." << std::endl;

	return true;
}

bool Wallet::dataDelete(const std::string& strKey)
{
	Database* db	= theApp->getWalletDB()->getDB();

	ScopedLock sl(theApp->getWalletDB()->getDBLock());

	return db->executeSQL(str(boost::format("DELETE FROM RPCData WHERE Key=%s;")
		% db->escape(strKey)));
}

bool Wallet::dataFetch(const std::string& strKey, std::string& strValue)
{
	Database* db	= theApp->getWalletDB()->getDB();

	ScopedLock sl(theApp->getWalletDB()->getDBLock());

	bool		bSuccess	= false;

	if (db->executeSQL(str(boost::format("SELECT Value FROM RPCData WHERE Key=%s;")
		% db->escape(strKey))) && db->startIterRows())
	{
		std::string strPublicKey, strPrivateKey;

		std::vector<unsigned char> vucData	= db->getBinary("Value");
		strValue.assign(vucData.begin(), vucData.end());

		db->endIterRows();

		bSuccess	= true;
	}

	return bSuccess;
}

bool Wallet::dataStore(const std::string& strKey, const std::string& strValue)
{
	Database* db	= theApp->getWalletDB()->getDB();

	ScopedLock sl(theApp->getWalletDB()->getDBLock());

	bool		bSuccess	= false;

	return (db->executeSQL(str(boost::format("REPLACE INTO RPCData (Key, Value) VALUES (%s,%s);")
		% db->escape(strKey)
		% db->escape(strValue)
		)));

	return bSuccess;
}

// vim:ts=4
