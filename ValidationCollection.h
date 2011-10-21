#ifndef __VALIDATION_COLLECTION__
#define __VALIDATION_COLLECTION__

#include "newcoin.pb.h"
#include "uint256.h"
#include "types.h"

class ValidationCollection
{

	// from ledger hash to the validation
	std::map<uint256, std::vector<newcoin::Validation> > mValidations;
	std::map<uint256, std::vector<newcoin::Validation> > mIgnoredValidations;

	// this maps ledgerIndex to an array of groups. Each group is a list of validations. 
	// a validation can be in multiple groups since compatibility isn't transitive
	// 
	std::map<uint32, std::vector< std::vector<newcoin::Validation> > > mGroupValidations;

	bool hasValidation(uint256& ledgerHash,uint160& hanko,uint32 seqnum);
	void addToGroup(newcoin::Validation& valid);
public:
	ValidationCollection();

	void addValidation(newcoin::Validation& valid);

	std::vector<newcoin::Validation>* getValidations(uint32 ledgerIndex);


	// It can miss some compatible ledgers of course if you don't know them
	// gets a list of all the compatible ledgers that were voted for the most
	// returns false if there isn't a consensus yet
	bool getConsensusLedgers(uint32 ledgerIndex, std::list<uint256>& retHashs);

	int getSeqNum(uint32 ledgerIndex);
};

#endif