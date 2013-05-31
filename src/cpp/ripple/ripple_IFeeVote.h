#ifndef RIPPLE_IFEEVOTE_H
#define RIPPLE_IFEEVOTE_H

/** Manager to process fee votes.
*/
class IFeeVote
{
public:
	/** Create a new fee vote manager.

		@param targetBaseFee
		@param targetReserveBase
		@param targetReserveIncrement
	*/
	static IFeeVote* New (uint64 targetBaseFee,
						  uint32 targetReserveBase,
						  uint32 targetReserveIncrement);

	virtual ~IFeeVote () { }

	/** Add local fee preference to validation.

		@param lastClosedLedger
		@param baseValidation
	*/
	virtual void doValidation (Ledger::ref lastClosedLedger,
							   STObject& baseValidation) = 0;

	/** Cast our local vote on the fee.

		@param lastClosedLedger
		@param initialPosition
	*/
	virtual void doVoting (Ledger::ref lastClosedLedger,
						   SHAMap::ref initialPosition) = 0;
};

#endif
