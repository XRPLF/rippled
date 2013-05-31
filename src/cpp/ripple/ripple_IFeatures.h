#ifndef RIPPLE_IFEATURES_H
#define RIPPLE_IFEATURES_H

/** Feature table interface.

	The feature table stores the list of enabled and potential features.
	Individuals features are voted on by validators during the consensus
	process.
*/
class IFeatureTable
{
public:
	static IFeatureTable* New (uint32 majorityTime, int majorityFraction);

	virtual ~IFeatureTable () { }

	virtual void doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation) = 0;
	virtual void doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) = 0;
};

#endif
