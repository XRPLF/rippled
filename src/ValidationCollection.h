#ifndef __VALIDATION_COLLECTION__
#define __VALIDATION_COLLECTION__

#include <vector>

#include <boost/unordered_map.hpp>
#include <boost/thread/mutex.hpp>

#include "uint256.h"
#include "types.h"
#include "SerializedValidation.h"

typedef boost::unordered_map<uint160, SerializedValidation::pointer> ValidationSet;

class ValidationCollection
{
protected:

	boost::mutex mValidationLock;
	boost::unordered_map<uint256, ValidationSet> 					mValidations;
	boost::unordered_map<uint160, SerializedValidation::pointer> 	mCurrentValidations;
	std::vector<SerializedValidation::pointer> 						mStaleValidations;
	std::list<uint256>												mDeadLedgers;

	bool mWriting;

	void doWrite();
	void condWrite();

public:
	ValidationCollection() : mWriting(false) { ; }

	bool addValidation(const SerializedValidation::pointer&);
	ValidationSet getValidations(const uint256& ledger);
	void getValidationCount(const uint256& ledger, bool currentOnly, int& trusted, int& untrusted);

	int getTrustedValidationCount(const uint256& ledger);
	int getCurrentValidationCount(uint32 afterTime);

	boost::unordered_map<uint256, int> getCurrentValidations();

	void addDeadLedger(const uint256&);
	bool isDeadLedger(const uint256&);
	std::list<uint256> getDeadLedgers() { return mDeadLedgers; }

	void flush();
};

#endif
