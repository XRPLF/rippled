

// VFALCO: TODO, remove this when it's safe to do so.
#ifdef __APPLICATION__
#error Including Application.h is disallowed!
#endif

SETUP_LOG (RippleAddress)

RippleAddress::RippleAddress() : mIsValid(false)
{
    nVersion = VER_NONE;
}

void RippleAddress::clear()
{
    nVersion = VER_NONE;
    vchData.clear();
}

bool RippleAddress::isSet() const
{
	return nVersion != VER_NONE;
}

std::string RippleAddress::humanAddressType() const
{
	switch (nVersion)
	{
	    case VER_NONE:				return "VER_NONE";
	    case VER_NODE_PUBLIC:		return "VER_NODE_PUBLIC";
	    case VER_NODE_PRIVATE:		return "VER_NODE_PRIVATE";
	    case VER_ACCOUNT_ID:		return "VER_ACCOUNT_ID";
	    case VER_ACCOUNT_PUBLIC:	return "VER_ACCOUNT_PUBLIC";
	    case VER_ACCOUNT_PRIVATE:	return "VER_ACCOUNT_PRIVATE";
	    case VER_FAMILY_GENERATOR:	return "VER_FAMILY_GENERATOR";
	    case VER_FAMILY_SEED:		return "VER_FAMILY_SEED";
	}

	return "unknown";
}

//
// NodePublic
//

RippleAddress RippleAddress::createNodePublic(const RippleAddress& naSeed)
{
	CKey			ckSeed(naSeed.getSeed());
	RippleAddress	naNew;

	// YYY Should there be a GetPubKey() equiv that returns a uint256?
	naNew.setNodePublic(ckSeed.GetPubKey());

	return naNew;
}

RippleAddress RippleAddress::createNodePublic(const std::vector<unsigned char>& vPublic)
{
	RippleAddress	naNew;

	naNew.setNodePublic(vPublic);

	return naNew;
}

RippleAddress RippleAddress::createNodePublic(const std::string& strPublic)
{
	RippleAddress	naNew;

	naNew.setNodePublic(strPublic);

	return naNew;
}

uint160 RippleAddress::getNodeID() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - getNodeID");

    case VER_NODE_PUBLIC:
		// Note, we are encoding the left.
		return Hash160(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}
const std::vector<unsigned char>& RippleAddress::getNodePublic() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - getNodePublic");

    case VER_NODE_PUBLIC:
		return vchData;

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string RippleAddress::humanNodePublic() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - humanNodePublic");

    case VER_NODE_PUBLIC:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool RippleAddress::setNodePublic(const std::string& strPublic)
{
	mIsValid		= SetString(strPublic.c_str(), VER_NODE_PUBLIC);

	return mIsValid;
}

void RippleAddress::setNodePublic(const std::vector<unsigned char>& vPublic)
{
	mIsValid		= true;

    SetData(VER_NODE_PUBLIC, vPublic);
}

bool RippleAddress::verifyNodePublic(const uint256& hash, const std::vector<unsigned char>& vchSig) const
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

bool RippleAddress::verifyNodePublic(const uint256& hash, const std::string& strSig) const
{
	std::vector<unsigned char> vchSig(strSig.begin(), strSig.end());

	return verifyNodePublic(hash, vchSig);
}

//
// NodePrivate
//

RippleAddress RippleAddress::createNodePrivate(const RippleAddress& naSeed)
{
	uint256			uPrivKey;
	RippleAddress	naNew;
	CKey			ckSeed(naSeed.getSeed());

	ckSeed.GetPrivateKeyU(uPrivKey);

	naNew.setNodePrivate(uPrivKey);

	return naNew;
}

const std::vector<unsigned char>& RippleAddress::getNodePrivateData() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - getNodePrivateData");

    case VER_NODE_PRIVATE:
		return vchData;

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

uint256 RippleAddress::getNodePrivate() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source = getNodePrivate");

    case VER_NODE_PRIVATE:
		return uint256(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string RippleAddress::humanNodePrivate() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - humanNodePrivate");

    case VER_NODE_PRIVATE:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool RippleAddress::setNodePrivate(const std::string& strPrivate)
{
	mIsValid		= SetString(strPrivate.c_str(), VER_NODE_PRIVATE);

	return mIsValid;
}

void RippleAddress::setNodePrivate(const std::vector<unsigned char>& vPrivate)
{
	mIsValid		= true;

    SetData(VER_NODE_PRIVATE, vPrivate);
}

void RippleAddress::setNodePrivate(uint256 hash256)
{
	mIsValid		= true;

    SetData(VER_NODE_PRIVATE, hash256.begin(), 32);
}

void RippleAddress::signNodePrivate(const uint256& hash, std::vector<unsigned char>& vchSig) const
{
	CKey	ckPrivKey;

	ckPrivKey.SetPrivateKeyU(getNodePrivate());

	if (!ckPrivKey.Sign(hash, vchSig))
		throw std::runtime_error("Signing failed.");
}

//
// AccountID
//

uint160 RippleAddress::getAccountID() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - getAccountID");

    case VER_ACCOUNT_ID:
		return uint160(vchData);

    case VER_ACCOUNT_PUBLIC:
		// Note, we are encoding the left.
		return Hash160(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

static boost::mutex rncLock;
static boost::unordered_map< std::vector<unsigned char>, std::string > rncMap;

std::string RippleAddress::humanAccountID() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - humanAccountID");

    case VER_ACCOUNT_ID:
    {
	    boost::mutex::scoped_lock sl(rncLock);
	    boost::unordered_map< std::vector<unsigned char>, std::string >::iterator it = rncMap.find(vchData);
	    if (it != rncMap.end())
	        return it->second;
		if (rncMap.size() > 10000)
			rncMap.clear();
	    return rncMap[vchData] = ToString();
	}

    case VER_ACCOUNT_PUBLIC:
	{
	    RippleAddress	accountID;

	    (void) accountID.setAccountID(getAccountID());

	    return accountID.ToString();
	}

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool RippleAddress::setAccountID(const std::string& strAccountID, const char* pAlphabet)
{
	if (strAccountID.empty())
	{
		setAccountID(uint160());

		mIsValid	= true;
	}
	else
	{
		mIsValid	= SetString(strAccountID.c_str(), VER_ACCOUNT_ID, pAlphabet);
	}

	return mIsValid;
}

void RippleAddress::setAccountID(const uint160& hash160)
{
	mIsValid		= true;

    SetData(VER_ACCOUNT_ID, hash160.begin(), 20);
}

//
// AccountPublic
//

RippleAddress RippleAddress::createAccountPublic(const RippleAddress& naGenerator, int iSeq)
{
	CKey			ckPub(naGenerator, iSeq);
	RippleAddress	naNew;

	naNew.setAccountPublic(ckPub.GetPubKey());

	return naNew;
}

const std::vector<unsigned char>& RippleAddress::getAccountPublic() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - getAccountPublic");

    case VER_ACCOUNT_ID:
		throw std::runtime_error("public not available from account id");
		break;

    case VER_ACCOUNT_PUBLIC:
		return vchData;

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string RippleAddress::humanAccountPublic() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - humanAccountPublic");

    case VER_ACCOUNT_ID:
		throw std::runtime_error("public not available from account id");

    case VER_ACCOUNT_PUBLIC:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool RippleAddress::setAccountPublic(const std::string& strPublic)
{
    mIsValid		= SetString(strPublic.c_str(), VER_ACCOUNT_PUBLIC);

	return mIsValid;
}

void RippleAddress::setAccountPublic(const std::vector<unsigned char>& vPublic)
{
	mIsValid		= true;

    SetData(VER_ACCOUNT_PUBLIC, vPublic);
}

void RippleAddress::setAccountPublic(const RippleAddress& generator, int seq)
{
	CKey	pubkey	= CKey(generator, seq);

	setAccountPublic(pubkey.GetPubKey());
}

bool RippleAddress::accountPublicVerify(const uint256& uHash, const std::vector<unsigned char>& vucSig) const
{
	CKey		ckPublic;
	bool		bVerified;

	if (!ckPublic.SetPubKey(getAccountPublic()))
	{
		// Bad private key.
		WriteLog (lsWARNING, RippleAddress) << "accountPublicVerify: Bad private key.";
		bVerified	= false;
	}
	else
	{
		bVerified	= ckPublic.Verify(uHash, vucSig);
	}

	return bVerified;
}

RippleAddress RippleAddress::createAccountID(const uint160& uiAccountID)
{
	RippleAddress	na;

	na.setAccountID(uiAccountID);

	return na;
}

//
// AccountPrivate
//

RippleAddress RippleAddress::createAccountPrivate(const RippleAddress& naGenerator, const RippleAddress& naSeed, int iSeq)
{
	RippleAddress	naNew;

	naNew.setAccountPrivate(naGenerator, naSeed, iSeq);

	return naNew;
}

uint256 RippleAddress::getAccountPrivate() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - getAccountPrivate");

    case VER_ACCOUNT_PRIVATE:
		return uint256(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string RippleAddress::humanAccountPrivate() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - humanAccountPrivate");

    case VER_ACCOUNT_PRIVATE:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool RippleAddress::setAccountPrivate(const std::string& strPrivate)
{
	mIsValid		= SetString(strPrivate.c_str(), VER_ACCOUNT_PRIVATE);

	return mIsValid;
}

void RippleAddress::setAccountPrivate(const std::vector<unsigned char>& vPrivate)
{
	mIsValid		= true;

    SetData(VER_ACCOUNT_PRIVATE, vPrivate);
}

void RippleAddress::setAccountPrivate(uint256 hash256)
{
	mIsValid		= true;

    SetData(VER_ACCOUNT_PRIVATE, hash256.begin(), 32);
}

void RippleAddress::setAccountPrivate(const RippleAddress& naGenerator, const RippleAddress& naSeed, int seq)
{
	CKey	ckPubkey	= CKey(naSeed.getSeed());
	CKey	ckPrivkey	= CKey(naGenerator, ckPubkey.GetSecretBN(), seq);
	uint256	uPrivKey;

	ckPrivkey.GetPrivateKeyU(uPrivKey);

	setAccountPrivate(uPrivKey);
}

bool RippleAddress::accountPrivateSign(const uint256& uHash, std::vector<unsigned char>& vucSig) const
{
	CKey		ckPrivate;
	bool		bResult;

	if (!ckPrivate.SetPrivateKeyU(getAccountPrivate()))
	{
		// Bad private key.
		WriteLog (lsWARNING, RippleAddress) << "accountPrivateSign: Bad private key.";
		bResult	= false;
	}
	else
	{
		bResult	= ckPrivate.Sign(uHash, vucSig);
		CondLog (!bResult, lsWARNING, RippleAddress) << "accountPrivateSign: Signing failed.";
	}

	return bResult;
}

#if 0
bool RippleAddress::accountPrivateVerify(const uint256& uHash, const std::vector<unsigned char>& vucSig) const
{
	CKey		ckPrivate;
	bool		bVerified;

	if (!ckPrivate.SetPrivateKeyU(getAccountPrivate()))
	{
		// Bad private key.
		WriteLog (lsWARNING, RippleAddress) << "accountPrivateVerify: Bad private key.";
		bVerified	= false;
	}
	else
	{
		bVerified	= ckPrivate.Verify(uHash, vucSig);
	}

	return bVerified;
}
#endif

std::vector<unsigned char> RippleAddress::accountPrivateEncrypt(const RippleAddress& naPublicTo, const std::vector<unsigned char>& vucPlainText) const
{
	CKey						ckPrivate;
	CKey						ckPublic;
	std::vector<unsigned char>	vucCipherText;

	if (!ckPublic.SetPubKey(naPublicTo.getAccountPublic()))
	{
		// Bad public key.
		WriteLog (lsWARNING, RippleAddress) << "accountPrivateEncrypt: Bad public key.";
	}
	else if (!ckPrivate.SetPrivateKeyU(getAccountPrivate()))
	{
		// Bad private key.
		WriteLog (lsWARNING, RippleAddress) << "accountPrivateEncrypt: Bad private key.";
	}
	else
	{
		try
		{
			vucCipherText = ckPrivate.encryptECIES(ckPublic, vucPlainText);
		}
		catch (...)
		{
			nothing();
		}
	}

	return vucCipherText;
}

std::vector<unsigned char> RippleAddress::accountPrivateDecrypt(const RippleAddress& naPublicFrom, const std::vector<unsigned char>& vucCipherText) const
{
	CKey						ckPrivate;
	CKey						ckPublic;
	std::vector<unsigned char>	vucPlainText;

	if (!ckPublic.SetPubKey(naPublicFrom.getAccountPublic()))
	{
		// Bad public key.
		WriteLog (lsWARNING, RippleAddress) << "accountPrivateDecrypt: Bad public key.";
	}
	else if (!ckPrivate.SetPrivateKeyU(getAccountPrivate()))
	{
		// Bad private key.
		WriteLog (lsWARNING, RippleAddress) << "accountPrivateDecrypt: Bad private key.";
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
// Generators
//

BIGNUM* RippleAddress::getGeneratorBN() const
{ // returns the public generator
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - getGeneratorBN");

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

const std::vector<unsigned char>& RippleAddress::getGenerator() const
{ // returns the public generator
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - getGenerator");

    case VER_FAMILY_GENERATOR:
		// Do nothing.
		return vchData;

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string RippleAddress::humanGenerator() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - humanGenerator");

    case VER_FAMILY_GENERATOR:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

bool RippleAddress::setGenerator(const std::string& strGenerator)
{
	mIsValid		= SetString(strGenerator.c_str(), VER_FAMILY_GENERATOR);

	return mIsValid;
}

void RippleAddress::setGenerator(const std::vector<unsigned char>& vPublic)
{
	mIsValid		= true;

    SetData(VER_FAMILY_GENERATOR, vPublic);
}

RippleAddress RippleAddress::createGeneratorPublic(const RippleAddress& naSeed)
{
	CKey			ckSeed(naSeed.getSeed());
	RippleAddress	naNew;

	naNew.setGenerator(ckSeed.GetPubKey());

	return naNew;
}

//
// Seed
//

uint128 RippleAddress::getSeed() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - getSeed");

    case VER_FAMILY_SEED:
		return uint128(vchData);

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string RippleAddress::humanSeed1751() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - humanSeed1751");

    case VER_FAMILY_SEED:
		{
			std::string strHuman;
			std::string strLittle;
			std::string strBig;
			uint128 uSeed	= getSeed();

			strLittle.assign(uSeed.begin(), uSeed.end());

			strBig.assign(strLittle.rbegin(), strLittle.rend());

			RFC1751::getEnglishFromKey (strHuman, strBig);

			return strHuman;
		}

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

std::string RippleAddress::humanSeed() const
{
    switch (nVersion) {
    case VER_NONE:
		throw std::runtime_error("unset source - humanSeed");

    case VER_FAMILY_SEED:
		return ToString();

    default:
		throw std::runtime_error(str(boost::format("bad source: %d") % int(nVersion)));
    }
}

int RippleAddress::setSeed1751(const std::string& strHuman1751)
{
	std::string strKey;
	int			iResult	= RFC1751::getKeyFromEnglish (strKey, strHuman1751);

	if (1 == iResult)
	{
		std::vector<unsigned char>	vchLittle(strKey.rbegin(), strKey.rend());
		uint128		uSeed(vchLittle);

		setSeed(uSeed);
	}

	return iResult;
}

bool RippleAddress::setSeed(const std::string& strSeed)
{
	mIsValid		= SetString(strSeed.c_str(), VER_FAMILY_SEED);

	return mIsValid;
}

bool RippleAddress::setSeedGeneric(const std::string& strText)
{
	RippleAddress	naTemp;
	bool			bResult	= true;
	uint128			uSeed;

	if (strText.empty()
		|| naTemp.setAccountID(strText)
		|| naTemp.setAccountPublic(strText)
		|| naTemp.setAccountPrivate(strText)
		|| naTemp.setNodePublic(strText)
		|| naTemp.setNodePrivate(strText))
	{
		bResult	= false;
	}
	else if (strText.length() == 32 && uSeed.SetHex(strText, true))
	{
		setSeed(uSeed);
	}
	else if (setSeed(strText))
	{
		// std::cerr << "Recognized seed." << std::endl;
		nothing();
	}
	else if (1 == setSeed1751(strText))
	{
		// std::cerr << "Recognized 1751 seed." << std::endl;
		nothing();
	}
	else
	{
		// std::cerr << "Creating seed from pass phrase." << std::endl;
		setSeed(CKey::PassPhraseToKey(strText));
	}

	return bResult;
}

void RippleAddress::setSeed(uint128 hash128) {
	mIsValid		= true;

    SetData(VER_FAMILY_SEED, hash128.begin(), 16);
}

void RippleAddress::setSeedRandom()
{
	// XXX Maybe we should call MakeNewKey
	uint128 key;

	RandomNumbers::getInstance ().fillBytes (key.begin(), key.size());

	RippleAddress::setSeed(key);
}

RippleAddress RippleAddress::createSeedRandom()
{
	RippleAddress	naNew;

	naNew.setSeedRandom();

	return naNew;
}

RippleAddress RippleAddress::createSeedGeneric(const std::string& strText)
{
	RippleAddress	naNew;

	naNew.setSeedGeneric(strText);

	return naNew;
}

BOOST_AUTO_TEST_SUITE(ripple_address)

BOOST_AUTO_TEST_CASE( check_crypto )
{
	// Construct a seed.
	RippleAddress	naSeed;

	BOOST_CHECK(naSeed.setSeedGeneric("masterpassphrase"));
	BOOST_CHECK_MESSAGE(naSeed.humanSeed() == "snoPBrXtMeMyMHUVTgbuqAfg1SUTb", naSeed.humanSeed());

	// Create node public/private key pair
	RippleAddress	naNodePublic	= RippleAddress::createNodePublic(naSeed);
	RippleAddress	naNodePrivate	= RippleAddress::createNodePrivate(naSeed);

	BOOST_CHECK_MESSAGE(naNodePublic.humanNodePublic() == "n94a1u4jAz288pZLtw6yFWVbi89YamiC6JBXPVUj5zmExe5fTVg9", naNodePublic.humanNodePublic());
	BOOST_CHECK_MESSAGE(naNodePrivate.humanNodePrivate() == "pnen77YEeUd4fFKG7iycBWcwKpTaeFRkW2WFostaATy1DSupwXe", naNodePrivate.humanNodePrivate());

	// Check node signing.
	std::vector<unsigned char> vucTextSrc = strCopy("Hello, nurse!");
	uint256	uHash	= Serializer::getSHA512Half(vucTextSrc);
	std::vector<unsigned char> vucTextSig;

	naNodePrivate.signNodePrivate(uHash, vucTextSig);
	BOOST_CHECK_MESSAGE(naNodePublic.verifyNodePublic(uHash, vucTextSig), "Verify failed.");

	// Construct a public generator from the seed.
	RippleAddress	naGenerator		= RippleAddress::createGeneratorPublic(naSeed);

	BOOST_CHECK_MESSAGE(naGenerator.humanGenerator() == "fhuJKrhSDzV2SkjLn9qbwm5AaRmrxDPfFsHDCP6yfDZWcxDFz4mt", naGenerator.humanGenerator());

	// Create account #0 public/private key pair.
	RippleAddress	naAccountPublic0	= RippleAddress::createAccountPublic(naGenerator, 0);
	RippleAddress	naAccountPrivate0	= RippleAddress::createAccountPrivate(naGenerator, naSeed, 0);

	BOOST_CHECK_MESSAGE(naAccountPublic0.humanAccountID() == "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh", naAccountPublic0.humanAccountID());
	BOOST_CHECK_MESSAGE(naAccountPublic0.humanAccountPublic() == "aBQG8RQAzjs1eTKFEAQXr2gS4utcDiEC9wmi7pfUPTi27VCahwgw", naAccountPublic0.humanAccountPublic());
	BOOST_CHECK_MESSAGE(naAccountPrivate0.humanAccountPrivate() == "p9JfM6HHi64m6mvB6v5k7G2b1cXzGmYiCNJf6GHPKvFTWdeRVjh", naAccountPrivate0.humanAccountPrivate());

	// Create account #1 public/private key pair.
	RippleAddress	naAccountPublic1	= RippleAddress::createAccountPublic(naGenerator, 1);
	RippleAddress	naAccountPrivate1	= RippleAddress::createAccountPrivate(naGenerator, naSeed, 1);

	BOOST_CHECK_MESSAGE(naAccountPublic1.humanAccountID() == "r4bYF7SLUMD7QgSLLpgJx38WJSY12ViRjP", naAccountPublic1.humanAccountID());
	BOOST_CHECK_MESSAGE(naAccountPublic1.humanAccountPublic() == "aBPXpTfuLy1Bhk3HnGTTAqnovpKWQ23NpFMNkAF6F1Atg5vDyPrw", naAccountPublic1.humanAccountPublic());
	BOOST_CHECK_MESSAGE(naAccountPrivate1.humanAccountPrivate() == "p9JEm822LMrzJii1k7TvdphfENTp6G5jr253Xa5rkzUWVr8ogQt", naAccountPrivate1.humanAccountPrivate());

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
