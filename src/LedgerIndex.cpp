
#include "Ledger.h"

uint256 Ledger::getAccountRootIndex(const uint160& uAccountID)
{
	Serializer	s;

	s.add16(spaceAccount);
	s.add160(uAccountID);

	return s.getSHA512Half();
}

// What is important:
// --> uNickname: is a Sha256
// <-- SHA512/2: for consistency and speed in generating indexes.
uint256 Ledger::getNicknameIndex(const uint256& uNickname)
{
	Serializer	s;

	s.add16(spaceNickname);
	s.add256(uNickname);

	return s.getSHA512Half();
}

uint256 Ledger::getGeneratorIndex(const uint160& uGeneratorID)
{
	Serializer	s;

	s.add16(spaceGenerator);
	s.add160(uGeneratorID);

	return s.getSHA512Half();
}

uint256 Ledger::getRippleStateIndex(const NewcoinAddress& naA, const NewcoinAddress& naB, const uint160& uCurrency)
{
	uint160		uAID	= naA.getAccountID();
	uint160		uBID	= naB.getAccountID();
	bool		bAltB	= uAID < uBID;
	Serializer	s;

	s.add16(spaceRipple);
	s.add160(bAltB ? uAID : uBID);
	s.add160(bAltB ? uBID : uAID);
	s.add160(uCurrency);

	return s.getSHA512Half();
}

uint256 Ledger::getRippleDirIndex(const uint160& uAccountID)
{
	Serializer	s;

	s.add16(spaceRippleDir);
	s.add160(uAccountID);

	return s.getSHA512Half();
}

uint256 Ledger::getBookBase(const uint160& uCurrencyIn, const uint160& uAccountIn,
	const uint160& uCurrencyOut, const uint160& uAccountOut)
{
	bool		bInNative	= uCurrencyIn.isZero();
	bool		bOutNative	= uCurrencyOut.isZero();

	assert(!bInNative || !bOutNative);									// Stamps to stamps not allowed.
	assert(bInNative == !uAccountIn.isZero());							// Make sure issuer is specified as needed.
	assert(bOutNative == !uAccountOut.isZero());						// Make sure issuer is specified as needed.
	assert(uCurrencyIn != uCurrencyOut || uAccountIn != uAccountOut);	// Currencies or accounts must differ.

	Serializer	s(82);

	s.add16(spaceBookDir);		//  2
	s.add160(uCurrencyIn);		// 20
	s.add160(uCurrencyOut);		// 20
	s.add160(uAccountIn);		// 20
	s.add160(uAccountOut);		// 20

	return getDirIndex(s.getSHA512Half());	// Return with index 0.
}

uint256 Ledger::getOfferIndex(const uint160& uAccountID, uint32 uSequence)
{
	Serializer	s;

	s.add16(spaceOffer);
	s.add160(uAccountID);
	s.add32(uSequence);

	return s.getSHA512Half();
}

uint256 Ledger::getOfferDirIndex(const uint160& uAccountID)
{
	Serializer	s;

	s.add16(spaceOfferDir);
	s.add160(uAccountID);

	return s.getSHA512Half();
}

// vim:ts=4
