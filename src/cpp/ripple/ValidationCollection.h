#ifndef __VALIDATION_COLLECTION__
#define __VALIDATION_COLLECTION__

#include <vector>

#include <boost/unordered_map.hpp>
#include <boost/thread/mutex.hpp>

#include "uint256.h"
#include "types.h"
#include "SerializedValidation.h"
#include "TaggedCache.h"

typedef boost::unordered_map<uint160, SerializedValidation::pointer> ValidationSet;

typedef std::pair<int, uint160> currentValidationCount; // nodes validating and highest node ID validating

class ValidationCollection
{
protected:

	boost::mutex mValidationLock;
	TaggedCache<uint256, ValidationSet>								mValidations;
	boost::unordered_map<uint160, SerializedValidation::pointer> 	mCurrentValidations;
	std::vector<SerializedValidation::pointer> 						mStaleValidations;

	bool mWriting;

	void doWrite();
	void condWrite();

	boost::shared_ptr<ValidationSet> findCreateSet(const uint256& ledgerHash);
	boost::shared_ptr<ValidationSet> findSet(const uint256& ledgerHash);

public:
	ValidationCollection() : mValidations("Validations", 128, 600), mWriting(false) { ; }

	bool addValidation(const SerializedValidation::pointer&);
	ValidationSet getValidations(const uint256& ledger);
	void getValidationCount(const uint256& ledger, bool currentOnly, int& trusted, int& untrusted);
	void getValidationTypes(const uint256& ledger, int& full, int& partial);

	int getTrustedValidationCount(const uint256& ledger);

	int getNodesAfter(const uint256& ledger);
	int getLoadRatio(bool overLoaded);

	boost::unordered_map<uint256, currentValidationCount> getCurrentValidations(uint256 currentLedger);
	std::list<SerializedValidation::pointer> getCurrentTrustedValidations();

	void flush();
	void sweep() { mValidations.sweep(); }
};

#endif
