
#include <fstream>
#include <iostream>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include "../json/reader.h"
#include "../json/writer.h"

#include "RPCServer.h"
#include "RequestParser.h"
#include "HttpReply.h"
#include "HttpsClient.h"
#include "Application.h"
#include "RPC.h"
#include "Wallet.h"
#include "Conversion.h"
#include "LocalTransaction.h"
#include "NewcoinAddress.h"
#include "AccountState.h"
#include "utils.h"

#define VALIDATORS_FETCH_SECONDS	30
#define VALIDATORS_FILE_PATH		"/" VALIDATORS_FILE_NAME
#define VALIDATORS_FILE_BYTES_MAX	(50 << 10)

/*
Just read from wire until the entire request is in.
*/

RPCServer::RPCServer(boost::asio::io_service& io_service)
	: mSocket(io_service)
{
}

void RPCServer::connected()
{
	//BOOST_LOG_TRIVIAL(info) << "RPC request";
	std::cout << "RPC request" << std::endl;

	mSocket.async_read_some(boost::asio::buffer(mReadBuffer),
		boost::bind(&RPCServer::handle_read, shared_from_this(),
		boost::asio::placeholders::error,
		boost::asio::placeholders::bytes_transferred));
}

void RPCServer::handle_read(const boost::system::error_code& e,
	std::size_t bytes_transferred)
{
	if (!e)
	{
		boost::tribool result;
		result = mRequestParser.parse(
			mIncomingRequest, mReadBuffer.data(), mReadBuffer.data() + bytes_transferred);

		if (result)
		{
			mReplyStr=handleRequest(mIncomingRequest.mBody);
			sendReply();
		}
		else if (!result)
		{ // bad request
			std::cout << "bad request" << std::endl;
		}
		else
		{  // not done keep reading
			mSocket.async_read_some(boost::asio::buffer(mReadBuffer),
				boost::bind(&RPCServer::handle_read, shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred));
		}
	}
	else if (e != boost::asio::error::operation_aborted)
	{

	}
}

std::string RPCServer::handleRequest(const std::string& requestStr)
{
	std::cout << "handleRequest " << requestStr << std::endl;
	Json::Value id;

	// Parse request
	Json::Value valRequest;
	Json::Reader reader;
	if (!reader.parse(requestStr, valRequest) || valRequest.isNull() || !valRequest.isObject())
		return(HTTPReply(400, ""));

	// Parse id now so errors from here on will have the id
	id = valRequest["id"];

	// Parse method
	Json::Value valMethod = valRequest["method"];
	if (valMethod.isNull())
		return(HTTPReply(400, ""));
	if (!valMethod.isString())
		return(HTTPReply(400, ""));
	std::string strMethod = valMethod.asString();

	// Parse params
	Json::Value valParams = valRequest["params"];
	if (valParams.isNull())
		valParams = Json::Value(Json::arrayValue);
	else if (!valParams.isArray())
		return(HTTPReply(400, ""));

#ifdef DEBUG
	Json::StyledStreamWriter w;
	w.write(std::cerr, valParams);
#endif

	Json::Value result(doCommand(strMethod, valParams));

#ifdef DEBUG
	w.write(std::cerr, result);
#endif

	std::string strReply = JSONRPCReply(result, Json::Value(), id);
	return( HTTPReply(200, strReply) );
}

int RPCServer::getParamCount(const Json::Value& params)
{ // If non-array, only counts strings
	if (params.isNull()) return 0;
	if (params.isArray()) return params.size();
	if (!params.isConvertibleTo(Json::stringValue))
		return 0;
	return 1;
}

bool RPCServer::extractString(std::string& param, const Json::Value& params, int index)
{
	if (params.isNull()) return false;

	if (index!=0)
	{
		if (!params.isArray() || !params.isValidIndex(index))
			return false;
		Json::Value p(params.get(index, Json::nullValue));
		if (p.isNull() || !p.isConvertibleTo(Json::stringValue))
			return false;
		param = p.asString();
		return true;
	}

	if (params.isArray())
	{
		if ( (!params.isValidIndex(0)) || (!params[0u].isConvertibleTo(Json::stringValue)) )
			return false;
		param = params[0u].asString();
		return true;
	}

	if (!params.isConvertibleTo(Json::stringValue))
		return false;
	param = params.asString();
	return true;
}

#if 0
NewcoinAddress RPCServer::parseFamily(const std::string& fParam)
{
	NewcoinAddress	family;

	if (family.setFamilyGenerator(fParam))
	{
		family = theApp->getWallet().findFamilyPK(family);
	}

	return family;
}
#endif

// account_info <account>|<nickname>|<account_public_key>
// account_info <seed>|<pass_phrase>|<key> [<index>]
Json::Value RPCServer::doAccountInfo(Json::Value &params)
{
	if (params.size() < 1 || params.size() > 2)
	{
		return "invalid params";
	}
	else if (!theApp->getOPs().available()) {
		return "network not available";
	}
	else
	{
		std::string		strIdent	= params[0u].asString();
		bool			bIndex		= 2 == params.size();
		int				iIndex		= bIndex ? boost::lexical_cast<int>(params[1u].asString()) : 0;

		NewcoinAddress	naAccount;
		NewcoinAddress	naSeed;

		if (!bIndex && (naAccount.setAccountPublic(strIdent) || naAccount.setAccountID(strIdent)))
		{
			// Got the account.
			nothing();
		}
		// Must be a seed.
		else if (!naSeed.setFamilySeedGeneric(strIdent))
		{
			return "disallowed seed";
		}
		else
		{
			// We allow the use of the seeds to access #0.
			// This is poor practice and merely for debuging convenience.
			NewcoinAddress	naGenerator;
			NewcoinAddress	naRegular0Public;
			NewcoinAddress	naRegular0Private;

			naGenerator.setFamilyGenerator(naSeed);

			naRegular0Public.setAccountPublic(naGenerator, 0);
			naRegular0Private.setAccountPrivate(naGenerator, naSeed, 0);

			uint160							uGeneratorID	= naRegular0Public.getAccountID();

			Ledger::pointer					ledger			= theApp->getMasterLedger().getCurrentLedger();
			LedgerStateParms				qry				= lepNONE;
			SerializedLedgerEntry::pointer	sleGen			= ledger->getGenerator(qry, naRegular0Public.getAccountID());
			if (!sleGen)
			{
				// Didn't find a generator map, assume it is a master generator.
				nothing();
			}
			else
			{
				// Found master public key.
				std::vector<unsigned char>	vucCipher				= sleGen->getIFieldVL(sfGenerator);
				std::vector<unsigned char>	vucMasterGenerator		= naRegular0Private.accountPrivateDecrypt(naRegular0Public, vucCipher);
				if (vucMasterGenerator.empty())
				{
					return "internal error: password failed to decrypt master public generator";
				}

				naGenerator.setFamilyGenerator(vucMasterGenerator);
			}

			bIndex	= true;

			naAccount.setAccountPublic(naGenerator, iIndex);
		}

		// Get info on account.
		Json::Value ret(Json::objectValue);

		AccountState::pointer as=theApp->getMasterLedger().getCurrentLedger()->getAccountState(naAccount);
		if (as)
		{
			as->addJson(ret);
		}
		else
		{
			ret["account"]	= naAccount.humanAccountID();
			ret["status"]	= "NotFound";
			ret["bIndex"]	= bIndex;
			if (bIndex)
				ret["index"]	= iIndex;
		}

		return ret;
	}
}

#if 0
Json::Value RPCServer::doLock(Json::Value &params)
{   // lock <family>
	// lock
	std::string fParam;

	if (extractString(fParam, params, 0))
	{ // local <family>
		NewcoinAddress family = parseFamily(fParam);
		if (!family.isValid()) return JSONRPCError(500, "Family not found");

		theApp->getWallet().lock(family);
	}
	else
	{
		theApp->getWallet().lock();
	}

	return "locked";
}

Json::Value RPCServer::doUnlock(Json::Value &params)
{   // unlock sXXXX
    // unlock "<pass phrase>"

	std::string param;
	NewcoinAddress familyGenerator;

	if (!extractString(param, params, 0) || familyGenerator.setFamilyGenerator(param))
		return JSONRPCError(500, "Private key required");

	NewcoinAddress family;
	NewcoinAddress familySeed;

	if (familySeed.setFamilySeed(param))
		// sXXX
		family=theApp->getWallet().addFamily(familySeed, false);
	else
		// pass phrase
		family=theApp->getWallet().addFamily(param, false);

	if (!family.isValid())
		return JSONRPCError(500, "Bad family");

	Json::Value ret(theApp->getWallet().getFamilyJson(family));
	if (ret.isNull()) return JSONRPCError(500, "Invalid family");

	return ret;
}
#endif

Json::Value RPCServer::doConnect(Json::Value& params)
{
	// connect <ip> [port]
	std::string strIp;
	int			iPort	= -1;

	if (!params.isArray() || !params.size() || params.size() > 2)
		return JSONRPCError(500, "Invalid parameters");

	// XXX Might allow domain for manual connections.
	if (!extractString(strIp, params, 0))
		return JSONRPCError(500, "Host IP required");

	if (params.size() == 2)
	{
		std::string strPort;

		// YYY Should make an extract int.
		if (!extractString(strPort, params, 1))
			return JSONRPCError(500, "Bad port");

		iPort	= boost::lexical_cast<int>(strPort);
	}

	if (!theApp->getConnectionPool().connectTo(strIp, iPort))
		return "connected";

	return "connecting";
}

Json::Value RPCServer::doPeers(Json::Value& params)
{
	// peers
	return theApp->getConnectionPool().getPeersJson();
}


Json::Value RPCServer::doSend(Json::Value& params)
{   // Implement simple sending without gathering
	// sendto <destination> <amount>
	// sendto <destination> <amount> <tag>
	if (!params.isArray() || (params.size()<2))
		return JSONRPCError(500, "Invalid parameters");

	int paramCount=getParamCount(params);
	if ((paramCount<2)||(paramCount>3))
		return JSONRPCError(500, "Invalid parameters");

	std::string sDest, sAmount;
	if (!extractString(sDest, params, 0) || !extractString(sAmount, params, 1))
		return JSONRPCError(500, "Invalid parameters");

	NewcoinAddress destAccount;

	destAccount.setAccountID(sDest) || destAccount.setAccountPublic(sDest);
	if (!destAccount.isValid())
		return JSONRPCError(500, "Unable to parse destination account");

	uint64 iAmount;
	try
	{
		iAmount=boost::lexical_cast<uint64>(sAmount);
		if (iAmount<=0) return JSONRPCError(500, "Invalid amount");
	}
	catch (...)
	{
		return JSONRPCError(500, "Invalid amount");
	}

	uint32 iTag(0);
	try
	{
		if (paramCount>2)
		{
			std::string sTag;
			extractString(sTag, params, 2);
			iTag=boost::lexical_cast<uint32>(sTag);
		}
	}
	catch (...)
	{
		return JSONRPCError(500, "Invalid tag");
	}

#ifdef DEBUG
	std::cerr << "SendTo(" << destAccount.humanAccountID() << ") amount=" << iAmount <<
		", tag=" << iTag << std::endl;
#endif

	LocalTransaction::pointer lt(new LocalTransaction(destAccount, iAmount, iTag));
	if (!lt->makeTransaction())
		return JSONRPCError(500, "Insufficient funds in unlocked accounts");
	lt->performTransaction();
	return lt->getTransaction()->getJson(true);
}

Json::Value RPCServer::doTx(Json::Value& params)
{
	// tx
	// tx <txID>
	// tx <family> <seq>
	// tx <account>

	std::string param1, param2;
	if (!extractString(param1, params, 0))
	{ // all local transactions
#if 1
		return "not implemented";
#else
		Json::Value ret(Json::objectValue);
		theApp->getWallet().addLocalTransactions(ret);
		return ret;
#endif
	}

	if (Transaction::isHexTxID(param1))
	{ // transaction by ID
		Json::Value ret;
		uint256 txid(param1);
		if (theApp->getWallet().getTxJson(txid, ret))
			return ret;

		Transaction::pointer txn=theApp->getMasterTransaction().fetch(txid, true);
		if (!txn) return JSONRPCError(500, "Transaction not found");
		return txn->getJson(true);
	}

	if (extractString(param2, params, 1))
	{ // family seq
		return "not implemented";
	}
	else
	{
		// account
	}

	return "not implemented";
}

Json::Value RPCServer::doLedger(Json::Value& params)
{
	// ledger
	// ledger <seq>
	// ledger <account>

	int paramCount = getParamCount(params);

	if (paramCount == 0)
	{
		Json::Value ret(Json::objectValue), current(Json::objectValue), closed(Json::objectValue);
		theApp->getMasterLedger().getCurrentLedger()->addJson(current);
		theApp->getMasterLedger().getClosedLedger()->addJson(closed);
		ret["open"] = current;
		ret["closed"] = closed;
		return ret;
	}

	return "not implemented";
}

// unl_add <domain><node_public> [<comment>]
Json::Value RPCServer::doUnlAdd(Json::Value& params)
{
	if (params.size() == 1 || params.size() == 2)
	{
		std::string	strNode		= params[0u].asString();
		std::string strComment	= (params.size() == 2) ? "" : params[1u].asString();

		NewcoinAddress	nodePublic;

		if (nodePublic.setNodePublic(strNode))
		{
			theApp->getUNL().nodeAddPublic(nodePublic, strComment);

			return "adding node by public key";
		}
		else
		{
			theApp->getUNL().nodeAddDomain(strNode, UniqueNodeList::vsManual, strComment);

			return "adding node by domain";
		}
	}
	return "invalid params";
}

// validation_create
// validation_create <pass_phrase>
// validation_create <seed>
// validation_create <seed_key>
//
// NOTE: It is poor security to specify secret information on the command line.  This information might be saved in the command
// shell history file (e.g. .bash_history) and it may be leaked via the process status command (i.e. ps).
Json::Value RPCServer::doValidatorCreate(Json::Value& params) {
	NewcoinAddress	familySeed;
	NewcoinAddress	familyGenerator;
	NewcoinAddress	nodePublicKey;
	NewcoinAddress	nodePrivateKey;

	if (params.size() > 1)
	{
		return "invalid params";
	}
	else if (params.empty())
	{
		std::cerr << "Creating random validation seed." << std::endl;

		familySeed.setFamilySeedRandom();					// Get a random seed.
	}
	else if (!familySeed.setFamilySeedGeneric(params[0u].asString()))
	{
		return "disallowed seed";
	}

	// Derive generator from seed.
	familyGenerator.setFamilyGenerator(familySeed);

	// The node public and private is 0th of the sequence.
	nodePublicKey.setNodePublic(CKey(familyGenerator, 0).GetPubKey());
	nodePrivateKey.setNodePrivate(CKey(familyGenerator, familySeed.getFamilyPrivateKey(), 0).GetSecret());

	// Paranoia
	assert(1 == familySeed.setFamilySeed1751(familySeed.humanFamilySeed1751()));

	Json::Value obj(Json::objectValue);

	obj["validation_public_key"]	= nodePublicKey.humanNodePublic();
	obj["validation_seed"]			= familySeed.humanFamilySeed();
	obj["validation_key"]			= familySeed.humanFamilySeed1751();

	return obj;
}

Json::Value RPCServer::doWalletAccounts(Json::Value& params)
{
	return "not implemented";
}

Json::Value RPCServer::doWalletAdd(Json::Value& params)
{
	return "not implemented";
}

// wallet_claim <master_seed> <regular_seed> [<source_tag>] [<account_annotation>]
//
// To provide an example to client writers, we do everything we expect a client to do here.
Json::Value RPCServer::doWalletClaim(Json::Value& params)
{
	NewcoinAddress	naMasterSeed;
	NewcoinAddress	naRegularSeed;

	if (params.size() < 2 || params.size() > 4)
	{
		return "invalid params";
	}
	else if (!naMasterSeed.setFamilySeedGeneric(params[0u].asString()))
	{
		// Should also not allow account id's as seeds.
		return "master seed expected";
	}
	else if (!naRegularSeed.setFamilySeedGeneric(params[1u].asString()))
	{
		// Should also not allow account id's as seeds.
		return "regular seed expected";
	}
	else
	{
		// Trying to build:
		//   peer_wallet_claim <account_id> <authorized_key> <encrypted_master_public_generator> <generator_pubkey> <generator_signature>
		//		<source_tag> [<annotation>]
		//
		//
		// Which has no confidential information.

		// XXX Need better parsing.
		uint32		uSourceTag		= (params.size() == 2) ? 0 : boost::lexical_cast<uint32>(params[2u].asString());
		// XXX Annotation is ignored.
		std::string strAnnotation	= (params.size() == 3) ? "" : params[3u].asString();

		NewcoinAddress	naMasterGenerator;
		NewcoinAddress	naRegularGenerator;
		NewcoinAddress	naRegular0Public;
		NewcoinAddress	naRegular0Private;

		NewcoinAddress	naAccountPublic;
		NewcoinAddress	naAccountPrivate;

		naMasterGenerator.setFamilyGenerator(naMasterSeed);
		naAccountPublic.setAccountPublic(naMasterGenerator, 0);
		naAccountPrivate.setAccountPrivate(naMasterGenerator, naMasterSeed, 0);

		naRegularGenerator.setFamilyGenerator(naRegularSeed);

		naRegular0Public.setAccountPublic(naRegularGenerator, 0);
		naRegular0Private.setAccountPrivate(naRegularGenerator, naRegularSeed, 0);

		// hash of regular account #reserved public key.
		uint160						uGeneratorID		= naRegular0Public.getAccountID();
		std::vector<unsigned char>	vucGeneratorCipher	= naRegular0Private.accountPrivateEncrypt(naRegular0Public, naMasterGenerator.getFamilyGenerator());
		std::vector<unsigned char>	vucGeneratorSig;

		// XXX Check result.
		naRegular0Private.accountPrivateSign(Serializer::getSHA512Half(vucGeneratorCipher), vucGeneratorSig);

		Transaction::pointer	trns	= Transaction::sharedClaim(
			naAccountPublic, naAccountPrivate,
			naAccountPublic,
			uSourceTag,
			vucGeneratorCipher,
			naRegular0Public.getAccountPublic(),
			vucGeneratorSig);

		(void) theApp->getOPs().processTransaction(trns);

		Json::Value obj(Json::objectValue);

		// We "echo" the seeds so they can be checked.
		obj["master_seed"]		= naMasterSeed.humanFamilySeed();
		obj["master_key"]		= naMasterSeed.humanFamilySeed1751();
		obj["regular_seed"]		= naRegularSeed.humanFamilySeed();
		obj["regular_key"]		= naRegularSeed.humanFamilySeed1751();

		obj["account_id"]		= naAccountPublic.humanAccountID();
		obj["generator_id"]		= strHex(uGeneratorID);
		obj["generator"]		= strHex(vucGeneratorCipher);
		obj["annotation"]		= strAnnotation;

		obj["transaction"]		= trns->getSTransaction()->getJson(0);
		obj["status"]			= trns->getStatus();

		return obj;
	}
}

// wallet_create regular_seed paying_account account_id [initial_funds]
// We don't allow creating an account_id by default here because we want to make sure the person has a chance to write down the
// master seed of the account to be created.
// YYY Need annotation and source tag
Json::Value RPCServer::doWalletCreate(Json::Value& params)
{
	NewcoinAddress	naSourceID;
	NewcoinAddress	naCreateID;
	NewcoinAddress	naRegularSeed;

	if (params.size() < 3 || params.size() > 4)
	{
		return "invalid params";
	}
	else if (!naSourceID.setAccountID(params[1u].asString()))
	{
		return "source account id needed";
	}
	else if (!naCreateID.setAccountID(params[2u].asString()))
	{
		return "create account id needed";
	}
	else if (!naRegularSeed.setFamilySeedGeneric(params[0u].asString()))
	{
		return "disallowed seed";
	}
	else if (!theApp->getOPs().available()) {
		// We require access to the paying account's sequence number and key information.
		return "network not available";
	}
	else if (theApp->getMasterLedger().getCurrentLedger()->getAccountState(naCreateID))
	{
		return "account already exists";
	}
	else
	{
		// Trying to build:
		//   peer_wallet_create <paying_account> <paying_signature> <account_id> [<initial_funds>] [<annotation>]
		//   peer_payment
		//
		// Which has no confidential information.

		Ledger::pointer					ledger			= theApp->getMasterLedger().getCurrentLedger();
		LedgerStateParms				qry				= lepNONE;
	    SerializedLedgerEntry::pointer	sleSrc			= ledger->getAccountRoot(qry, naSourceID);

		if (!sleSrc)
		{
			return "source account does not exist";
		}

		STAmount						saSrcBalance	= sleSrc->getIValueFieldAmount(sfBalance);
		STAmount						saInitialFunds	= (params.size() < 4) ? 0 : boost::lexical_cast<uint64>(params[3u].asString());

		if (saSrcBalance < theConfig.FEE_CREATE + saInitialFunds)
		{
			return "insufficent funds";
		}
		else if (!sleSrc->getIFieldPresent(sfAuthorizedKey))
		{
			return "source account has not been claimed";
		}

		NewcoinAddress	naRegularGenerator;
		NewcoinAddress	naRegular0Public;
		NewcoinAddress	naRegular0Private;

		naRegularGenerator.setFamilyGenerator(naRegularSeed);
		naRegular0Public.setAccountPublic(naRegularGenerator, 0);
		naRegular0Private.setAccountPrivate(naRegularGenerator, naRegularSeed, 0);

										qry				= lepNONE;
		SerializedLedgerEntry::pointer	sleGen			= ledger->getGenerator(qry, naRegular0Public.getAccountID());

		if (!sleGen)
		{
			// No account has been claimed or has had it password set for seed.
			return "wrong password";
		}

		std::vector<unsigned char>	vucCipher			= sleGen->getIFieldVL(sfGenerator);
		std::vector<unsigned char>	vucMasterGenerator	= naRegular0Private.accountPrivateDecrypt(naRegular0Public, vucCipher);
		if (vucMasterGenerator.empty())
		{
			return "internal error: password failed to decrypt master public generator";
		}

		NewcoinAddress	naMasterGenerator;

		naMasterGenerator.setFamilyGenerator(vucMasterGenerator);

		//
		// Find the index of the account from the master generator, so we can generator the public and private keys.
		//
		NewcoinAddress	naMasterAccountPublic;
		uint			iIndex = -1;	// Compensate for initial increment.

		// XXX Stop after Config.account_probe_max
		do {
			++iIndex;
			naMasterAccountPublic.setAccountPublic(naMasterGenerator, iIndex);
		} while (naSourceID.getAccountID() != naMasterAccountPublic.getAccountID());

		NewcoinAddress	naRegularAccountPublic;
		NewcoinAddress	naRegularAccountPrivate;

		naRegularAccountPublic.setAccountPublic(naRegularGenerator, iIndex);
		naRegularAccountPrivate.setAccountPrivate(naRegularGenerator, naRegularSeed, iIndex);

		if (sleSrc->getIFieldH160(sfAuthorizedKey) != naRegularAccountPublic.getAccountID())
		{
			std::cerr << "iIndex: " << iIndex << std::endl;
			std::cerr << "sfAuthorizedKey: " << strHex(sleSrc->getIFieldH160(sfAuthorizedKey)) << std::endl;
			std::cerr << "naRegularAccountPublic: " << strHex(naRegularAccountPublic.getAccountID()) << std::endl;

			return "wrong password (changed)";
		}

		Transaction::pointer	trans	= Transaction::sharedCreate(
			naRegularAccountPublic, naRegularAccountPrivate,
			naSourceID,
			sleSrc->getIFieldU32(sfSequence),
			theConfig.FEE_CREATE,
			0,											// YYY No source tag
			naCreateID,
			saInitialFunds);							// Initial funds in XNC.

		(void) theApp->getOPs().processTransaction(trans);

		Json::Value obj(Json::objectValue);

		obj["transaction"]		= trans->getSTransaction()->getJson(0);
		obj["status"]			= trans->getStatus();

		return obj;
	}
}

// wallet_propose
Json::Value RPCServer::doWalletPropose(Json::Value& params)
{
	if (params.size())
	{
		return "invalid params";
	}
	else
	{
		NewcoinAddress	naSeed;
		NewcoinAddress	naGenerator;
		NewcoinAddress	naAccount;

		naSeed.setFamilySeedRandom();
		naGenerator.setFamilyGenerator(naSeed);
		naAccount.setAccountPublic(naGenerator, 0);

		Json::Value obj(Json::objectValue);

		obj["master_seed"]		= naSeed.humanFamilySeed();
		obj["master_key"]		= naSeed.humanFamilySeed1751();
		obj["account_id"]		= naAccount.humanAccountID();

		return obj;
	}
}

// wallet_seed [<seed>|<passphrase>|<passkey>]
Json::Value RPCServer::doWalletSeed(Json::Value& params)
{
	NewcoinAddress	naSeed;

	if (params.size() > 1)
	{
		return "invalid params";
	}
	else if (params.size()
		&& !naSeed.setFamilySeedGeneric(params[0u].asString()))
	{
		return "disallowed seed";
	}
	else
	{
		NewcoinAddress	naGenerator;
		NewcoinAddress	naAccount;

		if (!params.size())
		{
			naSeed.setFamilySeedRandom();
		}
		naGenerator.setFamilyGenerator(naSeed);
		naAccount.setAccountPublic(naGenerator, 0);

		Json::Value obj(Json::objectValue);

		obj["seed"]		= naSeed.humanFamilySeed();
		obj["key"]		= naSeed.humanFamilySeed1751();

		return obj;
	}
}

Json::Value RPCServer::doWalletVerify(Json::Value& params)
{
	return "not implemented";
}

void RPCServer::validatorsResponse(const boost::system::error_code& err, std::string strResponse)
{
	std::cerr << "Fetch '" VALIDATORS_FILE_NAME "' complete." << std::endl;

	if (!err)
	{
		theApp->getUNL().nodeDefault(strResponse);
	}
	else
	{
		std::cerr << "Error: " << err.message() << std::endl;
	}
}

// Populate the UNL from a validators.txt file.
Json::Value RPCServer::doUnlDefault(Json::Value& params) {
	if (!params.size() || (1==params.size() && !params[0u].compare("network")))
	{
		bool			bNetwork	= 1 == params.size();
		std::string		strValidators;

		if (!bNetwork)
		{
			std::ifstream	ifsDefault(VALIDATORS_FILE_NAME, std::ios::in);

			if (!ifsDefault)
			{
				std::cerr << "Failed to open '" VALIDATORS_FILE_NAME "'." << std::endl;

				bNetwork	= true;
			}
			else
			{
				strValidators.assign((std::istreambuf_iterator<char>(ifsDefault)),
					std::istreambuf_iterator<char>());

				if (ifsDefault.bad())
				{
					std::cerr << "Failed to read '" VALIDATORS_FILE_NAME "'." << std::endl;

					bNetwork	= true;
				}
			}
		}

		if (bNetwork)
		{
			HttpsClient::httpsGet(
				theApp->getIOService(),
				VALIDATORS_SITE,
				443,
				VALIDATORS_FILE_PATH,
				VALIDATORS_FILE_BYTES_MAX,
				boost::posix_time::seconds(VALIDATORS_FETCH_SECONDS),
				boost::bind(&RPCServer::validatorsResponse, this, _1, _2));

			return "fetching " VALIDATORS_FILE_NAME;
		}
		else
		{
			theApp->getUNL().nodeDefault(strValidators);

			return "processing " VALIDATORS_FILE_NAME;
		}
	}
	else return "invalid params";
}

// unl_delete <public_key>
Json::Value RPCServer::doUnlDelete(Json::Value& params) {
	if (1 == params.size())
	{
		std::string	strNodePublic = params[0u].asString();

		NewcoinAddress	naNodePublic;

		if (naNodePublic.setNodePublic(strNodePublic))
		{
			theApp->getUNL().nodeRemove(naNodePublic);

			return "removing node";
		}
		else
		{
			return "invalid public key";
		}
	}
	else return "invalid params";
}

Json::Value RPCServer::doUnlList(Json::Value& params) {
	return theApp->getUNL().getUnlJson();
}

// unl_reset
Json::Value RPCServer::doUnlReset(Json::Value& params) {
	if (!params.size())
	{
		theApp->getUNL().nodeReset();

		return "removing nodes";
	}
	else return "invalid params";
}

// unl_score
Json::Value RPCServer::doUnlScore(Json::Value& params) {
	if (!params.size())
	{
		theApp->getUNL().nodeScore();

		return "scoring requested";
	}
	else return "invalid params";
}

Json::Value RPCServer::doStop(Json::Value& params) {
	if (!params.size())
	{
		theApp->stop();

		return SYSTEM_NAME " server stopping";
	}
	else return "invalid params";
}

Json::Value RPCServer::doCommand(const std::string& command, Json::Value& params)
{
	std::cerr << "RPC:" << command << std::endl;

	if (command == "account_info")		return doAccountInfo(params);
	if (command == "connect")			return doConnect(params);
	if (command == "peers")				return doPeers(params);

	if (command == "send")				return doSend(params);
	if (command == "stop")				return doStop(params);

	if (command == "unl_add")			return doUnlAdd(params);
	if (command == "unl_default")		return doUnlDefault(params);
	if (command == "unl_delete")		return doUnlDelete(params);
	if (command == "unl_list")			return doUnlList(params);
	if (command == "unl_reset")			return doUnlReset(params);
	if (command == "unl_score")			return doUnlScore(params);

	if (command == "validation_create")	return doValidatorCreate(params);

	if (command == "wallet_accounts")	return doWalletAccounts(params);
	if (command == "wallet_add")		return doWalletAdd(params);
	if (command == "wallet_claim")		return doWalletClaim(params);
	if (command == "wallet_create")		return doWalletCreate(params);
	if (command == "wallet_propose")	return doWalletPropose(params);
	if (command == "wallet_seed")		return doWalletSeed(params);
	if (command == "wallet_verify")		return doWalletVerify(params);

	//
	// Obsolete or need rewrite:
	//

	if (command=="tx") return doTx(params);
	if (command=="ledger") return doLedger(params);

	return "unknown command";
}

void RPCServer::sendReply()
{
	boost::asio::async_write(mSocket, boost::asio::buffer(mReplyStr),
			boost::bind(&RPCServer::handle_write, shared_from_this(),
			boost::asio::placeholders::error));
}

void RPCServer::handle_write(const boost::system::error_code& /*error*/)
{
}

// vim:ts=4
