#include "NewcoinAddress.h"
#include "key.h"
#include "Config.h"
#include "BitcoinUtil.h"
#include "openssl/ec.h"

#include <cassert>

NewcoinAddress::NewcoinAddress()
{
    version = VER_NONE;
}

bool NewcoinAddress::IsValid()
{
    return !vchData.empty();
}

//
// Hanko
//

uint160 NewcoinAddress::getHanko() const
{
    switch (version) {
    case VER_NONE:
	std::runtime_error("unset source");
	break;

    case VER_HANKO:
	return uint160(vchData);

    case VER_NODE_PUBLIC:
	// Note, we are encoding the left or right.
	return Hash160(vchData);

    default:
	std::runtime_error("bad source");
    }

    return 0;
}

std::string NewcoinAddress::humanHanko() const
{
    switch (version) {
    case VER_NONE:
	std::runtime_error("unset source");
	break;

    case VER_HANKO:
	return ToString();

    case VER_NODE_PUBLIC:
	{
	    NewcoinAddress	hanko;

	    (void) hanko.setHanko(getHanko());

	    return hanko.ToString();
	}

    default:
	std::runtime_error("bad source");
    }

    return 0;
}

bool NewcoinAddress::setHanko(const std::string& strHanko)
{
    return SetString(strHanko.c_str(), VER_HANKO);
}

void NewcoinAddress::setHanko(const uint160& hash160)
{
    SetData(VER_HANKO, hash160.begin(), 20);
}

//
// NodePublic
//

const std::vector<unsigned char>& NewcoinAddress::getNodePublic() const
{
    switch (version) {
    case VER_NONE:
	std::runtime_error("unset source");
	break;

    case VER_HANKO:
	std::runtime_error("public not available from hanko");
	break;

    case VER_NODE_PUBLIC:
	// Do nothing.
	break;

    default:
	std::runtime_error("bad source");
    }

    return vchData;
}

std::string NewcoinAddress::humanNodePublic() const
{
    switch (version) {
    case VER_NONE:
	std::runtime_error("unset source");
	break;

    case VER_HANKO:
	std::runtime_error("public not available from hanko");
	break;

    case VER_NODE_PUBLIC:
	return ToString();

    default:
	std::runtime_error("bad source");
    }

    return 0;
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

uint256 NewcoinAddress::getNodePrivate() const
{
    switch (version) {
    case VER_NONE:
	std::runtime_error("unset source");
	break;

    case VER_NODE_PRIVATE:
	// Nothing
	break;

    default:
	std::runtime_error("bad source");
    }

    return uint256(vchData);
}

std::string NewcoinAddress::humanNodePrivate() const
{
    switch (version) {
    case VER_NONE:
		std::runtime_error("unset source");
		break;

    case VER_NODE_PRIVATE:
		return ToString();

    default:
		std::runtime_error("bad source");
    }

    return 0;
}

bool NewcoinAddress::setNodePrivate(const std::string& strPrivate)
{
    return SetString(strPrivate.c_str(), VER_NODE_PRIVATE);
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
    switch (version) {
    case VER_NONE:
		std::runtime_error("unset source");
		break;

    case VER_ACCOUNT_ID:
		return uint160(vchData);

    case VER_ACCOUNT_PUBLIC:
		// Note, we are encoding the left or right.
		return Hash160(vchData);

    default:
		std::runtime_error("bad source");
    }

    return 0;
}

std::string NewcoinAddress::humanAccountID() const
{
    switch (version) {
    case VER_NONE:
		std::runtime_error("unset source");
		break;

    case VER_ACCOUNT_ID:
		return ToString();

    case VER_ACCOUNT_PUBLIC:
	{
	    NewcoinAddress	accountID;

	    (void) accountID.setHanko(getAccountID());

	    return accountID.ToString();
	}

    default:
	std::runtime_error("bad source");
    }

    return 0;
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
    switch (version) {
    case VER_NONE:
		std::runtime_error("unset source");
		break;

    case VER_ACCOUNT_ID:
		std::runtime_error("public not available from account id");
		break;

    case VER_ACCOUNT_PUBLIC:
		// Do nothing.
		break;

    default:
	std::runtime_error("bad source");
    }

    return vchData;
}

std::string NewcoinAddress::humanAccountPublic() const
{
    switch (version) {
    case VER_NONE:
		std::runtime_error("unset source");
		break;

    case VER_ACCOUNT_ID:
		std::runtime_error("public not available from account id");
		break;

    case VER_ACCOUNT_PUBLIC:
		return ToString();

    default:
		std::runtime_error("bad source");
    }

    return 0;
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
    switch (version) {
    case VER_NONE:
	std::runtime_error("unset source");
	break;

    case VER_ACCOUNT_PRIVATE:
	// Do nothing.
	break;

    default:
	std::runtime_error("bad source");
    }

    return uint256(vchData);
}

std::string NewcoinAddress::humanAccountPrivate() const
{
    switch (version) {
    case VER_NONE:
		std::runtime_error("unset source");
		break;

    case VER_ACCOUNT_PRIVATE:
		return ToString();

    default:
		std::runtime_error("bad source");
    }

    return 0;
}

bool NewcoinAddress::setAccountPrivate(const std::string& strPrivate)
{
    return SetString(strPrivate.c_str(), VER_ACCOUNT_PRIVATE);
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
    switch (version) {
    case VER_NONE:
		std::runtime_error("unset source");
		break;

    case VER_FAMILY_GENERATOR:
		// Do nothing.
		break;

    default:
		std::runtime_error("bad source");
    }

    // Convert to big-endian
    unsigned char be[vchData.size()];

    std::reverse_copy(vchData.begin(), vchData.end(), &be[0]);

    return BN_bin2bn(be, vchData.size(), NULL);
}

const std::vector<unsigned char>& NewcoinAddress::getFamilyGenerator() const
{
    switch (version) {
    case VER_NONE:
		std::runtime_error("unset source");
		break;

    case VER_FAMILY_GENERATOR:
		// Do nothing.
		break;

    default:
		std::runtime_error("bad source");
    }

    return vchData;
}

std::string NewcoinAddress::humanFamilyGenerator() const
{
    switch (version) {
    case VER_NONE:
	std::runtime_error("unset source");
	break;

    case VER_FAMILY_GENERATOR:
	return ToString();

    default:
	std::runtime_error("bad source");
    }

    return 0;
}

// YYY Would be nice if you could pass a family seed and derive the generator.
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

void NewcoinAddress::seedInfo(NewcoinAddress* dstGenerator, BIGNUM** dstPrivateKey) const
{
	// Generate root key
	EC_KEY *base=CKey::GenerateRootDeterministicKey(getFamilySeed());

	// Extract family name
	std::vector<unsigned char> rootPubKey(33, 0);
	unsigned char *begin=&rootPubKey[0];
	i2o_ECPublicKey(base, &begin);
	while(rootPubKey.size()<33) rootPubKey.push_back((unsigned char)0);

    if (dstGenerator)
		dstGenerator->setFamilyGenerator(rootPubKey);

	if (dstPrivateKey)
		*dstPrivateKey	= BN_dup(EC_KEY_get0_private_key(base));

	EC_KEY_free(base);
}

uint128 NewcoinAddress::getFamilySeed() const
{
    switch (version) {
    case VER_NONE:
		std::runtime_error("unset source");
		break;

    case VER_FAMILY_SEED:
		// Do nothing.
		break;

    default:
		std::runtime_error("bad source");
    }

    return uint128(vchData);
}

BIGNUM*	NewcoinAddress::getFamilyPrivateKey() const
{
	BIGNUM*	ret;

	seedInfo(0, &ret);

	return ret;
}

std::string NewcoinAddress::humanFamilySeed() const
{
    switch (version) {
    case VER_NONE:
		std::runtime_error("unset source");
		break;

    case VER_FAMILY_SEED:
		return ToString();

    default:
		std::runtime_error("bad source");
    }

    return 0;
}

bool NewcoinAddress::setFamilySeed(const std::string& strSeed)
{
    return SetString(strSeed.c_str(), VER_FAMILY_SEED);
}

void NewcoinAddress::setFamilySeed(uint128 hash128) {
    SetData(VER_FAMILY_SEED, hash128.begin(), 16);
}
// vim:ts=4
