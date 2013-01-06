#define WIN32_LEAN_AND_MEAN 

#include <string>

#include "openssl/ec.h"

#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>

#include "Wallet.h"
#include "Ledger.h"
#include "RippleAddress.h"
#include "Application.h"
#include "utils.h"

Wallet::Wallet() : mDh512(NULL), mDh1024(NULL), mLedger(0)
{ ; }

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
	RippleAddress	naSeed			= RippleAddress::createSeedRandom();
	RippleAddress	naNodePublic	= RippleAddress::createNodePublic(naSeed);
	RippleAddress	naNodePrivate	= RippleAddress::createNodePrivate(naSeed);

	// Make new key.

#ifdef CREATE_NEW_DH_PARAMS
	std::string		strDh512		= DH_der_gen(512);
#else
	static const unsigned char dh512Param[] = {
		0x30, 0x46, 0x02, 0x41, 0x00, 0x98, 0x15, 0xd2, 0xd0, 0x08, 0x32, 0xda,
		0xaa, 0xac, 0xc4, 0x71, 0xa3, 0x1b, 0x11, 0xf0, 0x6c, 0x62, 0xb2, 0x35,
		0x8a, 0x10, 0x92, 0xc6, 0x0a, 0xa3, 0x84, 0x7e, 0xaf, 0x17, 0x29, 0x0b,
		0x70, 0xef, 0x07, 0x4f, 0xfc, 0x9d, 0x6d, 0x87, 0x99, 0x19, 0x09, 0x5b,
		0x6e, 0xdb, 0x57, 0x72, 0x4a, 0x7e, 0xcd, 0xaf, 0xbd, 0x3a, 0x97, 0x55,
		0x51, 0x77, 0x5a, 0x34, 0x7c, 0xe8, 0xc5, 0x71, 0x63, 0x02, 0x01, 0x02
	};
	std::string		strDh512(reinterpret_cast<const char *>(dh512Param), sizeof(dh512Param));
#endif


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
	Database* db	= theApp->getRpcDB()->getDB();

	ScopedLock sl(theApp->getRpcDB()->getDBLock());

	return db->executeSQL(str(boost::format("DELETE FROM RPCData WHERE Key=%s;")
		% sqlEscape(strKey)));
}

bool Wallet::dataFetch(const std::string& strKey, std::string& strValue)
{
	Database* db	= theApp->getRpcDB()->getDB();

	ScopedLock sl(theApp->getRpcDB()->getDBLock());

	bool		bSuccess	= false;

	if (db->executeSQL(str(boost::format("SELECT Value FROM RPCData WHERE Key=%s;")
		% sqlEscape(strKey))) && db->startIterRows())
	{
		std::vector<unsigned char> vucData	= db->getBinary("Value");
		strValue.assign(vucData.begin(), vucData.end());

		db->endIterRows();

		bSuccess	= true;
	}

	return bSuccess;
}

bool Wallet::dataStore(const std::string& strKey, const std::string& strValue)
{
	Database* db	= theApp->getRpcDB()->getDB();

	ScopedLock sl(theApp->getRpcDB()->getDBLock());

	bool		bSuccess	= false;

	return (db->executeSQL(str(boost::format("REPLACE INTO RPCData (Key, Value) VALUES (%s,%s);")
		% sqlEscape(strKey)
		% sqlEscape(strValue)
		)));

	return bSuccess;
}

// vim:ts=4
