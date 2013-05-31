
class FeatureTable;

//------------------------------------------------------------------------------

class FeeVote : public IFeeVote
{
public:
	FeeVote (uint64 targetBaseFee, uint32 targetReserveBase, uint32 targetReserveIncrement)
		: mTargetBaseFee (targetBaseFee)
		, mTargetReserveBase (targetReserveBase)
		, mTargetReserveIncrement (targetReserveIncrement)
	{
	}

	//--------------------------------------------------------------------------

	void doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation)
	{
		if (lastClosedLedger->getBaseFee() != mTargetBaseFee)
		{
			WriteLog (lsINFO, FeatureTable) << "Voting for base fee of " << mTargetBaseFee;
			baseValidation.setFieldU64(sfBaseFee, mTargetBaseFee);
		}

		if (lastClosedLedger->getReserve(0) != mTargetReserveBase)
		{
			WriteLog (lsINFO, FeatureTable) << "Voting for base resrve of " << mTargetReserveBase;
			baseValidation.setFieldU32(sfReserveBase, mTargetReserveBase);
		}

		if (lastClosedLedger->getReserveInc() != mTargetReserveIncrement)
		{
			WriteLog (lsINFO, FeatureTable) << "Voting for reserve increment of " << mTargetReserveIncrement;
			baseValidation.setFieldU32(sfReserveIncrement, mTargetReserveIncrement);
		}
	}

	//--------------------------------------------------------------------------

	void doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition)
	{
		// LCL must be flag ledger
		assert((lastClosedLedger->getLedgerSeq() % 256) == 0);

		VotableInteger<uint64> baseFeeVote(lastClosedLedger->getBaseFee(), mTargetBaseFee);
		VotableInteger<uint32> baseReserveVote(lastClosedLedger->getReserve(0), mTargetReserveBase);
		VotableInteger<uint32> incReserveVote(lastClosedLedger->getReserveInc(), mTargetReserveIncrement);

		// get validations for ledger before flag
		ValidationSet set = theApp->getValidations().getValidations(lastClosedLedger->getParentHash());
		BOOST_FOREACH(ValidationSet::value_type& value, set)
		{
			SerializedValidation& val = *value.second;
			if (val.isTrusted())
			{
				if (val.isFieldPresent(sfBaseFee))
				{
					baseFeeVote.addVote(val.getFieldU64(sfBaseFee));
				}
				else
				{
					baseFeeVote.noVote();
				}

				if (val.isFieldPresent(sfReserveBase))
				{
					baseReserveVote.addVote(val.getFieldU32(sfReserveBase));
				}
				else
				{
					baseReserveVote.noVote();
				}

				if (val.isFieldPresent(sfReserveIncrement))
				{
					incReserveVote.addVote(val.getFieldU32(sfReserveIncrement));
				}
				else
				{
					incReserveVote.noVote();
				}
			}
		}

		// choose our positions
		uint64 baseFee = baseFeeVote.getVotes();
		uint32 baseReserve = baseReserveVote.getVotes();
		uint32 incReserve = incReserveVote.getVotes();

		// add transactions to our position
		if ((baseFee != lastClosedLedger->getBaseFee()) ||
			(baseReserve != lastClosedLedger->getReserve(0)) ||
			(incReserve != lastClosedLedger->getReserveInc()))
		{
			WriteLog (lsWARNING, FeatureTable) << "We are voting for a fee change: " << baseFee << "/" << baseReserve << "/" << incReserve;
			SerializedTransaction trans(ttFEE);
			trans.setFieldAccount(sfAccount, uint160());
			trans.setFieldU64(sfBaseFee, baseFee);
			trans.setFieldU32(sfReferenceFeeUnits, 10);
			trans.setFieldU32(sfReserveBase, baseReserve);
			trans.setFieldU32(sfReserveIncrement, incReserve);
			uint256 txID = trans.getTransactionID();
			WriteLog (lsWARNING, FeatureTable) << "Vote: " << txID;

			Serializer s;
			trans.add(s, true);

			SHAMapItem::pointer tItem = boost::make_shared<SHAMapItem>(txID, s.peekData());
			if (!initialPosition->addGiveItem(tItem, true, false))
			{
				WriteLog (lsWARNING, FeatureTable) << "Ledger already had fee change";
			}
		}
	}

private:
	uint64 mTargetBaseFee;
	uint32 mTargetReserveBase;
	uint32 mTargetReserveIncrement;
};

//------------------------------------------------------------------------------

IFeeVote* IFeeVote::New (uint64 targetBaseFee,
						 uint32 targetReserveBase,
						 uint32 targetReserveIncrement)
{
	return new FeeVote (targetBaseFee, targetReserveBase, targetReserveIncrement);
}

// vim:ts=4
