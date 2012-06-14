#include "NewcoinAddress.h"
#include "key.h"
#include "Config.h"
#include "BitcoinUtil.h"
#include "rfc1751.h"
#include "utils.h"

#include <algorithm>
#include <boost/format.hpp>
#include <boost/functional/hash.hpp>
#include <boost/test/unit_test.hpp>
#include <cassert>
#include <iostream>
#include <openssl/rand.h>

NewcoinAddress::NewcoinAddress()
{
    nVersion = VER_NONE;
}

bool NewcoinAddress::isValid() const
{
    bool	bValid	= false;

	if (!vchData.empty())
	{
		CKey	key;

		switch (nVersion) {
		case VER_NODE_PUBLIC:
			bValid	= key.SetPubKey(getNodePublic());
			break;

		case VER_ACCOUNT_PUBLIC:
			bValid	= key.SetPubKey(getAccountPublic());
			break;

		case VER_ACCOUNT_PRIVATE:
			bValid	= key.SetPrivateKeyU(getAccountPrivate());
			break;

		default:
			bValid	= true;
			break;
		}
	}

	return bValid;
}

void NewcoinAddress::clear()
{
    nVersion = VER_NONE;
    vchData.clear();
}

//
// NodePublic
//

NewcoinAddress NewcoinAddress::createNodePublic(const NewcoinAddress& naSeed)
{
	CKey			ckSeed(naSeed.getFamilySeed());
	NewcoinAddress	naNew;

	// YYY Should there be a GetPubKey() equiv that returns a uint256?
	naNew.setNodePublic(ckSeed.GetPubKey());

	return naNew;
}

NewcoinAddress NewcoinAddress::createNodePublic(const std::vector<unsigned char>& vPublic)
{
	NewcoinAddress	naNew;

	naNew.setNodePublic(vPublic);

	return naNew;
}

uint160 NewcoinAddress::getNodeID() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

    case VER_NODE_PUBLIC:
		// Note, we are encoding the left.
		return Hash160(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}
const std::vector<unsigned char>& NewcoinAddress::getNodePublic() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source");

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

bool NewcoinAddress::verifyNodePublic(const uint256& hash, const std::vector<unsigned char>& vchSig) const
{
	CKey	pubkey	= CKey();
	bool	bVerified;

	if (!pubkey.SetPubKey(getNodePublic()))
	{
		// Failed to set public key.
		bVerified	= false;
	}
	else
	{
		bVerified	= pubkey.Verify(hash, vchSig);
	}

	return bVerified;
}

bool NewcoinAddress::verifyNodePublic(const uint256& hash, const std::string& strSig) const
{
	std::vector<unsigned char> vchSig(strSig.begin(), strSig.end());

	return verifyNodePublic(hash, vchSig);
}

//
// NodePrivate
//

NewcoinAddress NewcoinAddress::createNodePrivate(const NewcoinAddress& naSeed)
{
	uint256			uPrivKey;
	NewcoinAddress	naNew;
	CKey			ckSeed(naSeed.getFamilySeed());

	ckSeed.GetPrivateKeyU(uPrivKey);

	naNew.setNodePrivate(uPrivKey);

	return naNew;
}

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

void NewcoinAddress::signNodePrivate(const uint256& hash, std::vector<unsigned char>& vchSig) const
{
	CKey	ckPrivKey;

	ckPrivKey.SetPrivateKeyU(getNodePrivate());

	if (!ckPrivKey.Sign(hash, vchSig))
		throw std::runtime_error("Signing failed.");
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
		// Note, we are encoding the left.
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

NewcoinAddress NewcoinAddress::createAccountPublic(const NewcoinAddress& naGenerator, int iSeq)
{
	CKey			ckPub(naGenerator, iSeq);
	NewcoinAddress	naNew;

	naNew.setAccountPublic(ckPub.GetPubKey());

	return naNew;
}

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

bool NewcoinAddress::accountPublicVerify(const uint256& uHash, const std::vector<unsigned char>& vucSig) const
{
	CKey		ckPublic;
	bool		bVerified;

	if (!ckPublic.SetPubKey(getAccountPublic()))
	{
		// Bad private key.
		std::cerr << "accountPublicVerify: Bad private key." << std::endl;
		bVerified	= false;
	}
	else
	{
		bVerified	= ckPublic.Verify(uHash, vucSig);
	}

	return bVerified;
}

NewcoinAddress NewcoinAddress::createAccountID(const uint160& uiAccountID)
{
	NewcoinAddress	na;

	na.setAccountID(uiAccountID);

	return na;
}

//
// AccountPrivate
//

NewcoinAddress NewcoinAddress::createAccountPrivate(const NewcoinAddress& naGenerator, const NewcoinAddress& naSeed, int iSeq)
{
	NewcoinAddress	naNew;

	naNew.setAccountPrivate(naGenerator, naSeed, iSeq);

	return naNew;
}

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

void NewcoinAddress::setAccountPrivate(const NewcoinAddress& naGenerator, const NewcoinAddress& naSeed, int seq)
{
	CKey	pubkey		= CKey(naSeed.getFamilySeed());
	CKey	ckPrivkey	= CKey(naGenerator, pubkey.GetSecretBN(), seq);
	uint256	uPrivKey;

	ckPrivkey.GetPrivateKeyU(uPrivKey);

	setAccountPrivate(uPrivKey);
}

bool NewcoinAddress::accountPrivateSign(const uint256& uHash, std::vector<unsigned char>& vucSig) const
{
	CKey		ckPrivate;
	bool		bResult;

	if (!ckPrivate.SetPrivateKeyU(getAccountPrivate()))
	{
		// Bad private key.
		std::cerr << "accountPrivateSign: Bad private key." << std::endl;
		bResult	= false;
	}
	else
	{
		bResult	= ckPrivate.Sign(uHash, vucSig);
		if (!bResult)
			std::cerr << "accountPrivateSign: Signing failed." << std::endl;
	}

	return bResult;
}

#if 0
bool NewcoinAddress::accountPrivateVerify(const uint256& uHash, const std::vector<unsigned char>& vucSig) const
{
	CKey		ckPrivate;
	bool		bVerified;

	if (!ckPrivate.SetPrivateKeyU(getAccountPrivate()))
	{
		// Bad private key.
		std::cerr << "accountPrivateVerify: Bad private key." << std::endl;
		bVerified	= false;
	}
	else
	{
		bVerified	= ckPrivate.Verify(uHash, vucSig);
	}

	return bVerified;
}
#endif

std::vector<unsigned char> NewcoinAddress::accountPrivateEncrypt(const NewcoinAddress& naPublicTo, const std::vector<unsigned char>& vucPlainText) const
{
	CKey						ckPrivate;
	CKey						ckPublic;
	std::vector<unsigned char>	vucCipherText;

	if (!ckPublic.SetPubKey(naPublicTo.getAccountPublic()))
	{
		// Bad public key.
		std::cerr << "accountPrivateEncrypt: Bad public key." << std::endl;
	}
	else if (!ckPrivate.SetPrivateKeyU(getAccountPrivate()))
	{
		// Bad private key.
		std::cerr << "accountPrivateEncrypt: Bad private key." << std::endl;
	}
	else
	{
		try {
			vucCipherText = ckPrivate.encryptECIES(ckPublic, vucPlainText);
		}
		catch (...)
		{
			nothing();
		}
	}

	return vucCipherText;
}

std::vector<unsigned char> NewcoinAddress::accountPrivateDecrypt(const NewcoinAddress& naPublicFrom, const std::vector<unsigned char>& vucCipherText) const
{
	CKey						ckPrivate;
	CKey						ckPublic;
	std::vector<unsigned char>	vucPlainText;

	if (!ckPublic.SetPubKey(naPublicFrom.getAccountPublic()))
	{
		// Bad public key.
		std::cerr << "accountPrivateDecrypt: Bad public key." << std::endl;
	}
	else if (!ckPrivate.SetPrivateKeyU(getAccountPrivate()))
	{
		// Bad private key.
		std::cerr << "accountPrivateDecrypt: Bad private key." << std::endl;
	}
	else
	{
		try {
			vucPlainText = ckPrivate.decryptECIES(ckPublic, vucCipherText);
		}
		catch (...)
		{
			nothing();
		}
	}

	return vucPlainText;
}

//
// Family Generators
//

BIGNUM* NewcoinAddress::getFamilyGeneratorBN() const
{ // returns the public generator
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
{ // returns the public generator
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

NewcoinAddress NewcoinAddress::createGeneratorPublic(const NewcoinAddress& naSeed)
{
	CKey			ckSeed(naSeed.getFamilySeed());
	NewcoinAddress	naNew;

	naNew.setFamilyGenerator(ckSeed.GetPubKey());

	return naNew;
}

//
// Family Seed
//

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

bool NewcoinAddress::setFamilySeedGeneric(const std::string& strText)
{
	NewcoinAddress	naTemp;
	bool			bResult	= true;

	if (strText.empty()
		|| naTemp.setAccountID(strText)
		|| naTemp.setAccountPublic(strText)
		|| naTemp.setAccountPrivate(strText))
	{
		bResult	= false;
	}
	else if (setFamilySeed(strText))
	{
		// std::cerr << "Recognized seed." << std::endl;
		nothing();
	}
	else if (1 == setFamilySeed1751(strText))
	{
		// std::cerr << "Recognized 1751 seed." << std::endl;
		nothing();
	}
	else
	{
		// std::cerr << "Creating seed from pass phrase." << std::endl;
		setFamilySeed(CKey::PassPhraseToKey(strText));
	}

	return bResult;
}

void NewcoinAddress::setFamilySeed(uint128 hash128) {
    SetData(VER_FAMILY_SEED, hash128.begin(), 16);
}

void NewcoinAddress::setFamilySeedRandom()
{
	// XXX Maybe we should call MakeNewKey
	uint128 key;

	RAND_bytes((unsigned char *) &key, sizeof(key));

	NewcoinAddress::setFamilySeed(key);
}

NewcoinAddress NewcoinAddress::createSeedRandom()
{
	NewcoinAddress	naNew;

	naNew.setFamilySeedRandom();

	return naNew;
}

NewcoinAddress NewcoinAddress::createSeedGeneric(const std::string& strText)
{
	NewcoinAddress	naNew;

	naNew.setFamilySeedGeneric(strText);

	return naNew;
}

BOOST_AUTO_TEST_SUITE(newcoin_address)

BOOST_AUTO_TEST_CASE( my_test )
{
	// Construct a seed.
	NewcoinAddress	naSeed;

	BOOST_CHECK(naSeed.setFamilySeedGeneric("masterpassphrase"));
	BOOST_CHECK_MESSAGE(naSeed.humanFamilySeed() == "snoPBiXtMeMyMHUVTgbuqAfg1SUTb", naSeed.humanFamilySeed());

	// Create node public/private key pair
	NewcoinAddress	naNodePublic	= NewcoinAddress::createNodePublic(naSeed);
	NewcoinAddress	naNodePrivate	= NewcoinAddress::createNodePrivate(naSeed);

	BOOST_CHECK_MESSAGE(naNodePublic.humanNodePublic() == "n94a1u4jAz288pZLtw6yFWVbr89YamrC6JBXPVUj5zmExe5fTVg9", naNodePublic.humanNodePublic());
	BOOST_CHECK_MESSAGE(naNodePrivate.humanNodePrivate() == "pnen77YEeUd4fFKG7rycBWcwKpTaeFRkW2WFostaATy1DSupwXe", naNodePrivate.humanNodePrivate());

	// Check node signing.
	std::vector<unsigned char> vucTextSrc = strCopy("Hello, nurse!");
	uint256	uHash	= Serializer::getSHA512Half(vucTextSrc);
	std::vector<unsigned char> vucTextSig;

	naNodePrivate.signNodePrivate(uHash, vucTextSig);
	BOOST_CHECK_MESSAGE(naNodePublic.verifyNodePublic(uHash, vucTextSig), "Verify failed.");

	// Construct a public generator from the seed.
	NewcoinAddress	naGenerator		= NewcoinAddress::createGeneratorPublic(naSeed);

	BOOST_CHECK_MESSAGE(naGenerator.humanFamilyGenerator() == "fhuJKihSDzV2SkjLn9qbwm5AaRmixDPfFsHDCP6yfDZWcxDFz4mt", naGenerator.humanFamilyGenerator());

	// Create account #0 public/private key pair.
	NewcoinAddress	naAccountPublic0	= NewcoinAddress::createAccountPublic(naGenerator, 0);
	NewcoinAddress	naAccountPrivate0	= NewcoinAddress::createAccountPrivate(naGenerator, naSeed, 0);

	BOOST_CHECK_MESSAGE(naAccountPublic0.humanAccountID() == "iHb9CJAWyB4ij91VRWn96DkukG4bwdtyTh", naAccountPublic0.humanAccountID());
	BOOST_CHECK_MESSAGE(naAccountPublic0.humanAccountPublic() == "aBQG8RQAzjs1eTKFEAQXi2gS4utcDrEC9wmr7pfUPTr27VCahwgw", naAccountPublic0.humanAccountPublic());
	BOOST_CHECK_MESSAGE(naAccountPrivate0.humanAccountPrivate() == "p9JfM6HHr64m6mvB6v5k7G2b1cXzGmYrCNJf6GHPKvFTWdeRVjh", naAccountPrivate0.humanAccountPrivate());

	// Create account #1 public/private key pair.
	NewcoinAddress	naAccountPublic1	= NewcoinAddress::createAccountPublic(naGenerator, 1);
	NewcoinAddress	naAccountPrivate1	= NewcoinAddress::createAccountPrivate(naGenerator, naSeed, 1);

	BOOST_CHECK_MESSAGE(naAccountPublic1.humanAccountID() == "i4bYF7SLUMD7QgSLLpgJx38WJSY12VrRjP", naAccountPublic1.humanAccountID());
	BOOST_CHECK_MESSAGE(naAccountPublic1.humanAccountPublic() == "aBPXpTfuLy1Bhk3HnGTTAqnovpKWQ23NpFMNkAF6F1Atg5vDyPiw", naAccountPublic1.humanAccountPublic());
	BOOST_CHECK_MESSAGE(naAccountPrivate1.humanAccountPrivate() == "p9JEm822LMizJrr1k7TvdphfENTp6G5ji253Xa5ikzUWVi8ogQt", naAccountPrivate1.humanAccountPrivate());

	// Check account signing.
	BOOST_CHECK_MESSAGE(naAccountPrivate0.accountPrivateSign(uHash, vucTextSig), "Signing failed.");
	BOOST_CHECK_MESSAGE(naAccountPublic0.accountPublicVerify(uHash, vucTextSig), "Verify failed.");
	BOOST_CHECK_MESSAGE(!naAccountPublic1.accountPublicVerify(uHash, vucTextSig), "Anti-verify failed.");

	BOOST_CHECK_MESSAGE(naAccountPrivate1.accountPrivateSign(uHash, vucTextSig), "Signing failed.");
	BOOST_CHECK_MESSAGE(naAccountPublic1.accountPublicVerify(uHash, vucTextSig), "Verify failed.");
	BOOST_CHECK_MESSAGE(!naAccountPublic0.accountPublicVerify(uHash, vucTextSig), "Anti-verify failed.");

	// Check account encryption.
	std::vector<unsigned char> vucTextCipher
		= naAccountPrivate0.accountPrivateEncrypt(naAccountPublic1, vucTextSrc);
	std::vector<unsigned char> vucTextRecovered
		= naAccountPrivate1.accountPrivateDecrypt(naAccountPublic0, vucTextCipher);

	BOOST_CHECK_MESSAGE(vucTextSrc == vucTextRecovered, "Encrypt-decrypt failed.");
}

BOOST_AUTO_TEST_SUITE_END()
// vim:ts=4
