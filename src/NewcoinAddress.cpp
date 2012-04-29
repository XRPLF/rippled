#include "NewcoinAddress.h"
#include "key.h"
#include "Config.h"
#include "BitcoinUtil.h"
#include "rfc1751.h"

#include "openssl/rand.h"

#include <cassert>
#include <algorithm>
#include <iostream>
#include <boost/format.hpp>
#include <boost/functional/hash.hpp>

NewcoinAddress::NewcoinAddress()
{
    nVersion = VER_NONE;
}

bool NewcoinAddress::isValid() const
{
    return !vchData.empty();
}

void NewcoinAddress::clear()
{
    nVersion = VER_NONE;
    vchData.clear();
}

#if 0
//
// Hanko - OBSOLETE
//

uint160 NewcoinAddress::getHanko() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_HANKO:
		return uint160(vchData);

    case VER_NODE_PUBLIC:
		// Note, we are encoding the left or right.
		return Hash160(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string NewcoinAddress::humanHanko() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_HANKO:
		return ToString();

    case VER_NODE_PUBLIC:
	{
	    NewcoinAddress	hanko;

	    (void) hanko.setHanko(getHanko());

	    return hanko.ToString();
	}

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool NewcoinAddress::setHanko(const std::string& strHanko)
{
    return SetString(strHanko.c_str(), VER_HANKO);
}

void NewcoinAddress::setHanko(const uint160& hash160)
{
    SetData(VER_HANKO, hash160.begin(), 20);
}

void NewcoinAddress::setHanko(const NewcoinAddress& nodePublic) {
	setHanko(nodePublic.getHanko());
}
#endif

//
// NodePublic
//

const std::vector<unsigned char>& NewcoinAddress::getNodePublic() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_HANKO:
		throw std::runtime_error("public not available from hanko");

    case VER_NODE_PUBLIC:
		return vchData;

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string NewcoinAddress::humanNodePublic() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_HANKO:
		throw std::runtime_error("public not available from hanko");

    case VER_NODE_PUBLIC:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool NewcoinAddress::setNodePublic(const std::string& strPublic)
{
    return SetString(strPublic.c_str(), VER_NODE_PUBLIC);
}

void NewcoinAddress::setNodePublic(const std::vector<unsigned char>& vPublic)
{
    SetData(VER_NODE_PUBLIC, vPublic);
}

//
// NodePrivate
//

const std::vector<unsigned char>& NewcoinAddress::getNodePrivateData() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_NODE_PRIVATE:
		return vchData;

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

uint256 NewcoinAddress::getNodePrivate() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_NODE_PRIVATE:
		return uint256(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string NewcoinAddress::humanNodePrivate() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_NODE_PRIVATE:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool NewcoinAddress::setNodePrivate(const std::string& strPrivate)
{
    return SetString(strPrivate.c_str(), VER_NODE_PRIVATE);
}

void NewcoinAddress::setNodePrivate(const std::vector<unsigned char>& vPrivate) {
    SetData(VER_NODE_PRIVATE, vPrivate);
}

void NewcoinAddress::setNodePrivate(uint256 hash256)
{
    SetData(VER_NODE_PRIVATE, hash256.begin(), 32);
}

//
// AccountID
//

uint160 NewcoinAddress::getAccountID() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_ACCOUNT_ID:
		return uint160(vchData);

    case VER_ACCOUNT_PUBLIC:
		// Note, we are encoding the left or right.
		return Hash160(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string NewcoinAddress::humanAccountID() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_ACCOUNT_ID:
		return ToString();

    case VER_ACCOUNT_PUBLIC:
	{
	    NewcoinAddress	accountID;

	    (void) accountID.setAccountID(getAccountID());

	    return accountID.ToString();
	}

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool NewcoinAddress::setAccountID(const std::string& strAccountID)
{
    return SetString(strAccountID.c_str(), VER_ACCOUNT_ID);
}

void NewcoinAddress::setAccountID(const uint160& hash160)
{
    SetData(VER_ACCOUNT_ID, hash160.begin(), 20);
}

//
// AccountPublic
//

const std::vector<unsigned char>& NewcoinAddress::getAccountPublic() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_ACCOUNT_ID:
		throw std::runtime_error("public not available from account id");
		break;

    case VER_ACCOUNT_PUBLIC:
		return vchData;

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string NewcoinAddress::humanAccountPublic() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_ACCOUNT_ID:
		throw std::runtime_error("public not available from account id");

    case VER_ACCOUNT_PUBLIC:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool NewcoinAddress::setAccountPublic(const std::string& strPublic)
{
    return SetString(strPublic.c_str(), VER_ACCOUNT_PUBLIC);
}

void NewcoinAddress::setAccountPublic(const std::vector<unsigned char>& vPublic)
{
    SetData(VER_ACCOUNT_PUBLIC, vPublic);
}

void NewcoinAddress::setAccountPublic(const NewcoinAddress& generator, int seq)
{
	CKey	pubkey	= CKey(generator, seq);

	setAccountPublic(pubkey.GetPubKey());
}

//
// AccountPrivate
//

uint256 NewcoinAddress::getAccountPrivate() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_ACCOUNT_PRIVATE:
		return uint256(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string NewcoinAddress::humanAccountPrivate() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_ACCOUNT_PRIVATE:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool NewcoinAddress::setAccountPrivate(const std::string& strPrivate)
{
    return SetString(strPrivate.c_str(), VER_ACCOUNT_PRIVATE);
}

void NewcoinAddress::setAccountPrivate(const std::vector<unsigned char>& vPrivate)
{
    SetData(VER_ACCOUNT_PRIVATE, vPrivate);
}

void NewcoinAddress::setAccountPrivate(uint256 hash256)
{
    SetData(VER_ACCOUNT_PRIVATE, hash256.begin(), 32);
}

//
// Family Generators
//

BIGNUM* NewcoinAddress::getFamilyGeneratorBN() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_FAMILY_GENERATOR:
		// Do nothing.
		break;

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }

    BIGNUM*	ret	= BN_bin2bn(&vchData[0], vchData.size(), NULL);
	assert(ret);

    return ret;
}

const std::vector<unsigned char>& NewcoinAddress::getFamilyGenerator() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_FAMILY_GENERATOR:
		// Do nothing.
		return vchData;

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string NewcoinAddress::humanFamilyGenerator() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_FAMILY_GENERATOR:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool NewcoinAddress::setFamilyGenerator(const std::string& strGenerator)
{
    return SetString(strGenerator.c_str(), VER_FAMILY_GENERATOR);
}

void NewcoinAddress::setFamilyGenerator(const std::vector<unsigned char>& vPublic)
{
    SetData(VER_FAMILY_GENERATOR, vPublic);
}

void NewcoinAddress::setFamilyGenerator(const NewcoinAddress& seed)
{
	seed.seedInfo(this, 0);
}

//
// Family Seed
//

// --> dstGenerator: Set the public generator from our seed.
void NewcoinAddress::seedInfo(NewcoinAddress* dstGenerator, BIGNUM** dstPrivateKey) const
{
	CKey	pubkey	= CKey(getFamilySeed());

    if (dstGenerator) {
		dstGenerator->setFamilyGenerator(pubkey.GetPubKey());
	}

	if (dstPrivateKey)
		*dstPrivateKey	= pubkey.GetSecretBN();
}

uint128 NewcoinAddress::getFamilySeed() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_FAMILY_SEED:
		return uint128(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

BIGNUM*	NewcoinAddress::getFamilyPrivateKey() const
{
	BIGNUM*	ret;

	seedInfo(0, &ret);

	return ret;
}

std::string NewcoinAddress::humanFamilySeed1751() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_FAMILY_SEED:
		{
			std::string strHuman;
			std::string strLittle;
			std::string strBig;
			uint128 uSeed	= getFamilySeed();

			strLittle.assign(uSeed.begin(), uSeed.end());

			strBig.assign(strLittle.rbegin(), strLittle.rend());

			key2eng(strHuman, strBig);

			return strHuman;
		}

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string NewcoinAddress::humanFamilySeed() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_FAMILY_SEED:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

int NewcoinAddress::setFamilySeed1751(const std::string& strHuman1751)
{
	std::string strKey;
	int			iResult	= eng2key(strKey, strHuman1751);

	if (1 == iResult)
	{
		std::vector<unsigned char>	vchLittle(strKey.rbegin(), strKey.rend());
		uint128		uSeed(vchLittle);

		setFamilySeed(uSeed);
	}

	return iResult;
}

bool NewcoinAddress::setFamilySeed(const std::string& strSeed)
{
    return SetString(strSeed.c_str(), VER_FAMILY_SEED);
}

void NewcoinAddress::setFamilySeed(uint128 hash128) {
    SetData(VER_FAMILY_SEED, hash128.begin(), 16);
}

void NewcoinAddress::setFamilySeedRandom()
{
	uint128 key;

	RAND_bytes((unsigned char *) &key, sizeof(key));

	NewcoinAddress::setFamilySeed(key);
}
// vim:ts=4
