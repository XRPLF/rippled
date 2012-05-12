
#include <iostream>

//#include <boost/log/trivial.hpp>

#include "../database/SqliteDatabase.h"

#include "Application.h"
#include "Config.h"
#include "PeerDoor.h"
#include "RPCDoor.h"
#include "BitcoinUtil.h"
#include "key.h"
#include "utils.h"

Application* theApp=NULL;

/*
What needs to happen:
	Listen for connections
	Try to maintain the right number of connections
	Process messages from peers
	Process messages from RPC
	Periodically publish a new ledger
	Save the various pieces of data 

*/

DatabaseCon::DatabaseCon(const std::string& name, const char *initStrings[], int initCount)
{
	std::string path=strprintf("%s%s", theConfig.DATA_DIR.c_str(), name.c_str());
	mDatabase=new SqliteDatabase(path.c_str());
	mDatabase->connect();
	for(int i = 0; i < initCount; ++i)
		mDatabase->executeSQL(initStrings[i], true);
}

DatabaseCon::~DatabaseCon()
{
	mDatabase->disconnect();
	delete mDatabase;
}

Application::Application() : mNetOps(mIOService), mUNL(mIOService),
	mTxnDB(NULL), mLedgerDB(NULL), mWalletDB(NULL), mHashNodeDB(NULL), mNetNodeDB(NULL),
	mConnectionPool(mIOService), mPeerDoor(NULL), mRPCDoor(NULL)
{
	nothing();
}

extern const char *TxnDBInit[], *LedgerDBInit[], *WalletDBInit[], *HashNodeDBInit[], *NetNodeDBInit[];
extern int TxnDBCount, LedgerDBCount, WalletDBCount, HashNodeDBCount, NetNodeDBCount;

void Application::stop()
{
	mIOService.stop();

	std::cerr << "Stopped: " << mIOService.stopped() << std::endl;
}

void Application::run()
{
	assert(mTxnDB==NULL);

	//
	// Construct databases.
	//
	mTxnDB=new DatabaseCon("transaction.db", TxnDBInit, TxnDBCount);
	mLedgerDB=new DatabaseCon("ledger.db", LedgerDBInit, LedgerDBCount);
	mWalletDB=new DatabaseCon("wallet.db", WalletDBInit, WalletDBCount);
	mHashNodeDB=new DatabaseCon("hashnode.db", HashNodeDBInit, HashNodeDBCount);
	mNetNodeDB=new DatabaseCon("netnode.db", NetNodeDBInit, NetNodeDBCount);

	//
	// Begin validation and ip maintenance.
	// - Wallet maintains local information: including identity and network connection persistency information.
	//
	mWallet.start();

	//
	// Allow peer connections.
	//
	if(!theConfig.PEER_IP.empty() && theConfig.PEER_PORT)
	{
		mPeerDoor=new PeerDoor(mIOService);
	}
	else
	{
		std::cerr << "Peer interface: disabled" << std::endl;
	}

	//
	// Allow RPC connections.
	//
	if(!theConfig.RPC_IP.empty() && theConfig.RPC_PORT)
	{
		mRPCDoor=new RPCDoor(mIOService);
	}
	else
	{
		std::cerr << "RPC interface: disabled" << std::endl;
	}

	//
	// Begin connecting to network.
	//
	mConnectionPool.start();

	// New stuff.
	NewcoinAddress	rootSeedMaster;
	NewcoinAddress	rootGeneratorMaster;
	NewcoinAddress	rootAddress;

	rootSeedMaster.setFamilySeed(CKey::PassPhraseToKey("Master passphrase."));
	rootGeneratorMaster.setFamilyGenerator(rootSeedMaster);

	rootAddress.setAccountPublic(rootGeneratorMaster, 0);

	std::cerr << "Master seed: " << rootSeedMaster.humanFamilySeed() << std::endl;
	std::cerr << "Master generator: " << rootGeneratorMaster.humanFamilyGenerator() << std::endl;
	std::cerr << "Root address: " << rootAddress.humanAccountPublic() << std::endl;
#if 0
	NewcoinAddress	rootSeedRegular;
	NewcoinAddress	rootGeneratorRegular;
	NewcoinAddress	reservedPublicRegular;
	NewcoinAddress	reservedPrivateRegular;

	rootSeedRegular.setFamilySeed(CKey::PassPhraseToKey("Regular passphrase."));
	rootGeneratorRegular.setFamilyGenerator(rootSeedRegular);

	reservedPublicRegular.setAccountPublic(rootGeneratorRegular, -1);
	reservedPrivateRegular.setAccountPrivate(rootGeneratorRegular, rootSeedRegular, -1);

	// hash of regular account #reserved public key.
	uint160						uiGeneratorID		= reservedPublicRegular.getAccountID();

	// std::cerr << "uiGeneratorID: " << uiGeneratorID << std::endl;

	// Encrypt with regular account #reserved private key.
	std::vector<unsigned char>	vucGeneratorCipher	= reservedPrivateRegular.accountPrivateEncrypt(reservedPublicRegular, rootGeneratorMaster.getFamilyGenerator());

	std::cerr << "Plain: " << strHex(rootGeneratorMaster.getFamilyGenerator()) << std::endl;

	std::cerr << "Cipher: " << strHex(vucGeneratorCipher) << std::endl;

	std::vector<unsigned char>	vucGeneratorText	= reservedPrivateRegular.accountPrivateDecrypt(reservedPublicRegular, vucGeneratorCipher);

	std::cerr << "Plain: " << strHex(vucGeneratorText) << std::endl;
	std::cerr << "Regular seed: " << rootSeedRegular.humanFamilySeed() << std::endl;
	std::cerr << "Regular generator: " << rootGeneratorRegular.humanFamilyGenerator() << std::endl;
	std::cerr << "Reserved public regular: " << reservedPublicRegular.humanAccountPublic() << std::endl;
	std::cerr << "Reserved private regular: " << reservedPrivateRegular.humanAccountPrivate() << std::endl;
#endif

	// Temporary root account will be ["This is my payphrase."]:0
	NewcoinAddress rootFamilySeed;		// Hold the 128 password.
	NewcoinAddress rootFamilyGenerator;	// Hold the generator.
	// NewcoinAddress rootAddress;

	rootFamilySeed.setFamilySeed(CKey::PassPhraseToKey("This is my payphrase."));
	rootFamilyGenerator.setFamilyGenerator(rootFamilySeed);
	rootAddress.setAccountPublic(rootFamilyGenerator, 0);
	std::cerr << "Root account: " << rootAddress.humanAccountID() << std::endl;

	Ledger::pointer firstLedger = boost::make_shared<Ledger>(rootAddress, 100000000);
	assert(!!firstLedger->getAccountState(rootAddress));
	firstLedger->updateHash();
	firstLedger->setClosed();
	firstLedger->setAccepted();
	mMasterLedger.pushLedger(firstLedger);

	Ledger::pointer secondLedger = boost::make_shared<Ledger>(firstLedger);
	mMasterLedger.pushLedger(secondLedger);
	assert(!!secondLedger->getAccountState(rootAddress));
	mMasterLedger.setSynced();
	// temporary

	mWallet.load();
//	mWallet.syncToLedger(true, &(*secondLedger));
	mNetOps.setStateTimer(5);

	// temporary
	mIOService.run(); // This blocks

	//BOOST_LOG_TRIVIAL(info) << "Done.";
	std::cout << "Done." << std::endl;
}

Application::~Application()
{
	delete mTxnDB;
	delete mLedgerDB;
	delete mWalletDB;
	delete mHashNodeDB;
	delete mNetNodeDB;
}
// vim:ts=4
