#ifndef RIPPLE_IFEATURES_H
#define RIPPLE_IFEATURES_H

/** Feature table interface.

	The feature table stores the list of enabled and potential features.
	Individuals features are voted on by validators during the consensus
	process.
*/
class IFeatures
{
public:
	static IFeatures* New (uint32 majorityTime, int majorityFraction);

	virtual ~IFeatures () { }

	virtual void doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation) = 0;

    virtual void doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) = 0;
};

#endif
