
#include "Ledger.h"

// For an entry put in the 64 bit index or quality.
uint256 Ledger::getQualityIndex(const uint256& uBase, const uint64 uNodeDir)
{
	// Indexes are stored in big endian format: they print as hex as stored.
	// Most significant bytes are first.  Least significant bytes repesent adjcent entries.
	// We place uNodeDir in the 8 right most bytes to be adjcent.
	// Want uNodeDir in big endian format so ++ goes to the next entry for indexes.
	uint256	uNode(uBase);

	((uint64*) uNode.end())[-1]	= htobe64(uNodeDir);

	return uNode;
}

uint64 Ledger::getQuality(const uint256& uBase)
{
	return be64toh(((uint64*) uBase.end())[-1]);
}

uint256 Ledger::getQualityNext(const uint256& uBase)
{
	static	uint256	uNext("10000000000000000");

	uint256	uResult	= uBase;

	uResult += uNext;

	return uResult;
}

uint256 Ledger::getAccountRootIndex(const uint160& uAccountID)
{
	Serializer	s(22);

	s.add16(spaceAccount);	//  2
	s.add160(uAccountID);	// 20

	return s.getSHA512Half();
}

uint256 Ledger::getBookBase(const uint160& uCurrencyIn, const uint160& uAccountIn,
	const uint160& uCurrencyOut, const uint160& uAccountOut)
{
	bool		bInNative	= uCurrencyIn.isZero();
	bool		bOutNative	= uCurrencyOut.isZero();

	assert(!bInNative || !bOutNative);									// Stamps to stamps not allowed.
	assert(bInNative == uAccountIn.isZero());							// Make sure issuer is specified as needed.
	assert(bOutNative == uAccountOut.isZero());						// Make sure issuer is specified as needed.
	assert(uCurrencyIn != uCurrencyOut || uAccountIn != uAccountOut);	// Currencies or accounts must differ.

	Serializer	s(82);

	s.add16(spaceBookDir);		//  2
	s.add160(uCurrencyIn);		// 20
	s.add160(uCurrencyOut);		// 20
	s.add160(uAccountIn);		// 20
	s.add160(uAccountOut);		// 20

	return getQualityIndex(s.getSHA512Half());	// Return with quality 0.
}

uint256 Ledger::getDirNodeIndex(const uint256& uDirRoot, const uint64 uNodeIndex)
{
	if (uNodeIndex)
	{
		Serializer	s(42);

		s.add16(spaceDirNode);		//  2
		s.add256(uDirRoot);			// 32
		s.add64(uNodeIndex);		//  8

		return s.getSHA512Half();
	}
	else
	{
		return uDirRoot;
	}
}

uint256 Ledger::getGeneratorIndex(const uint160& uGeneratorID)
{
	Serializer	s(22);

	s.add16(spaceGenerator);	//  2
	s.add160(uGeneratorID);		// 20

	return s.getSHA512Half();
}

// What is important:
// --> uNickname: is a Sha256
// <-- SHA512/2: for consistency and speed in generating indexes.
uint256 Ledger::getNicknameIndex(const uint256& uNickname)
{
	Serializer	s(34);

	s.add16(spaceNickname);		//  2
	s.add256(uNickname);		// 32

	return s.getSHA512Half();
}

uint256 Ledger::getOfferIndex(const uint160& uAccountID, uint32 uSequence)
{
	Serializer	s(26);

	s.add16(spaceOffer);		//  2
	s.add160(uAccountID);		// 20
	s.add32(uSequence);			//  4

	return s.getSHA512Half();
}

uint256 Ledger::getOwnerDirIndex(const uint160& uAccountID)
{
	Serializer	s(22);

	s.add16(spaceOwnerDir);		//  2
	s.add160(uAccountID);		// 20

	return s.getSHA512Half();
}

uint256 Ledger::getRippleDirIndex(const uint160& uAccountID)
{
	Serializer	s(22);

	s.add16(spaceRippleDir);	//  2
	s.add160(uAccountID);		// 20

	return s.getSHA512Half();
}

uint256 Ledger::getRippleStateIndex(const NewcoinAddress& naA, const NewcoinAddress& naB, const uint160& uCurrency)
{
	uint160		uAID	= naA.getAccountID();
	uint160		uBID	= naB.getAccountID();
	bool		bAltB	= uAID < uBID;
	Serializer	s(62);

	s.add16(spaceRipple);			//  2
	s.add160(bAltB ? uAID : uBID);	// 20
	s.add160(bAltB ? uBID : uAID);  // 20
	s.add160(uCurrency);			// 20

	return s.getSHA512Half();
}

// vim:ts=4
