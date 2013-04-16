#include "ChangeTransactor.h"

TER ChangeTransactor::doApply()
{
	if (mTxn.getTxnType() == ttFEATURE)
		return applyFeature();

	if (mTxn.getTxnType() == ttFEE)
		return applyFee();

	return temUNKNOWN;
}

TER ChangeTransactor::checkSig()
{
	if (mTxn.getFieldAccount160(sfAccount).isNonZero())
		return temBAD_SRC_ACCOUNT;

	if (!mTxn.getSigningPubKey().empty() || !mTxn.getSignature().empty())
		return temBAD_SIGNATURE;

	return tesSUCCESS;
}

TER ChangeTransactor::checkSeq()
{
	if (mTxn.getSequence() != 0)
		return temBAD_SEQUENCE;

	return tesSUCCESS;
}

TER ChangeTransactor::payFee()
{
	if (mTxn.getTransactionFee() != STAmount())
		return temBAD_FEE;

	return tesSUCCESS;
}

TER ChangeTransactor::applyFeature()
{
	uint256 feature = mTxn.getFieldH256(sfFeature);

	SLE::pointer featureObject = mEngine->entryCache(ltFEATURES, Ledger::getLedgerFeatureIndex());
	if (!featureObject)
		featureObject = mEngine->entryCreate(ltFEATURES, Ledger::getLedgerFeatureIndex());

	STVector256 features = featureObject->getFieldV256(sfFeatures);
	if (features.hasValue(feature))
		return tefALREADY;
	
	features.addValue(feature);
	featureObject->setFieldV256(sfFeatures, features);
	mEngine->entryModify(featureObject);

	return tesSUCCESS;
}

TER ChangeTransactor::applyFee()
{

	SLE::pointer feeObject = mEngine->entryCache(ltFEE_SETTINGS, Ledger::getLedgerFeeIndex());
	if (!feeObject)
		feeObject = mEngine->entryCreate(ltFEE_SETTINGS, Ledger::getLedgerFeeIndex());

	feeObject->setFieldU64(sfBaseFee, mTxn.getFieldU64(sfBaseFee));
	feeObject->setFieldU32(sfReferenceFeeUnits, mTxn.getFieldU32(sfReferenceFeeUnits));
	feeObject->setFieldU32(sfReserveBase, mTxn.getFieldU32(sfReserveBase));
	feeObject->setFieldU32(sfReserveIncrement, mTxn.getFieldU32(sfReserveIncrement));

	mEngine->entryModify(feeObject);
	return tesSUCCESS;
}
