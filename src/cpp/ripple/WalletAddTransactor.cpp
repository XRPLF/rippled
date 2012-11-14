#include "WalletAddTransactor.h"

TER WalletAddTransactor::doApply()
{
	std::cerr << "WalletAdd>" << std::endl;

	const std::vector<unsigned char>	vucPubKey		= mTxn.getFieldVL(sfPublicKey);
	const std::vector<unsigned char>	vucSignature	= mTxn.getFieldVL(sfSignature);
	const uint160						uAuthKeyID		= mTxn.getFieldAccount160(sfAuthorizedKey);
	const RippleAddress				naMasterPubKey	= RippleAddress::createAccountPublic(vucPubKey);
	const uint160						uDstAccountID	= naMasterPubKey.getAccountID();

	// FIXME: This should be moved to the transaction's signature check logic and cached
	if (!naMasterPubKey.accountPublicVerify(Serializer::getSHA512Half(uAuthKeyID.begin(), uAuthKeyID.size()), vucSignature))
	{
		std::cerr << "WalletAdd: unauthorized: bad signature " << std::endl;

		return tefBAD_ADD_AUTH;
	}

	SLE::pointer		sleDst	= mEngine->entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	if (sleDst)
	{
		std::cerr << "WalletAdd: account already created" << std::endl;

		return tefCREATED;
	}

	STAmount			saAmount		= mTxn.getFieldAmount(sfAmount);
	STAmount			saSrcBalance	= mTxnAccount->getFieldAmount(sfBalance);

	if (saSrcBalance < saAmount)
	{
		std::cerr
			<< boost::str(boost::format("WalletAdd: Delay transaction: insufficient balance: balance=%s amount=%s")
			% saSrcBalance.getText()
			% saAmount.getText())
			<< std::endl;

		return terUNFUNDED;
	}

	// Deduct initial balance from source account.
	mTxnAccount->setFieldAmount(sfBalance, saSrcBalance-saAmount);

	// Create the account.
	sleDst	= mEngine->entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	sleDst->setFieldAccount(sfAccount, uDstAccountID);
	sleDst->setFieldU32(sfSequence, 1);
	sleDst->setFieldAmount(sfBalance, saAmount);
	sleDst->setFieldAccount(sfAuthorizedKey, uAuthKeyID);

	std::cerr << "WalletAdd<" << std::endl;

	return tesSUCCESS;
}