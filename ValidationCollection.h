#ifndef __VALIDATION_COLLECTION__
#define __VALIDATION_COLLECTION__

#include "newcoin.pb.h"
#include "uint256.h"
#include "types.h"
#include "Ledger.h"
#include <list>

class ValidationCollection
{

	// from ledger hash to the validation
	//std::map<uint256, std::vector<newcoin::Validation> > mValidations;
	//std::map<uint256, std::vector<newcoin::Validation> > mIgnoredValidations;

	// this maps ledgerIndex to an array of groups. Each group is a list of validations. 
	// a validation can be in multiple groups since compatibility isn't transitive
	// 
	class Group
	{
	public:
		std::vector<newcoin::Validation> mValidations;
		Ledger::pointer mSuperLedger;

		bool addIfCompatible(Ledger::pointer ledger,newcoin::Validation& valid);
	};

	std::map<uint32, std::vector< Group > > mIndexGroups; // all the groups at each index
	//std::map<uint32, std::vector< newcoin::Validation > > mIndexValidations; // all the validations at each index

	bool hasValidation(uint256& ledgerHash,uint160& hanko,uint32 seqnum);
	void addToGroup(newcoin::Validation& valid);
	void addToDB(newcoin::Validation& valid,bool weCare);
public:
	ValidationCollection();

	void save();
	void load();

	void addValidation(newcoin::Validation& valid);

	std::vector<newcoin::Validation>* getValidations(uint32 ledgerIndex);


	// It can miss some compatible ledgers of course if you don't know them
	
	// fills out retLedger if there is a consensusLedger you can check
	// fills out retHash if there isn't a consensusLedger to check. We need to fetch this ledger
	// returns false if there isn't a consensus yet or we are in the consensus
	bool getConsensusLedger(uint32 ledgerIndex, uint256& ourHash, Ledger::pointer& retLedger, uint256& retHash);

	int getSeqNum(uint32 ledgerIndex);
};

#endif