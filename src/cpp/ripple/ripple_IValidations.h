#ifndef RIPPLE_IVALIDATIONS_H
#define RIPPLE_IVALIDATIONS_H

// VFALCO TODO rename and move these typedefs into the IValidations interface
typedef boost::unordered_map<uint160, SerializedValidation::pointer> ValidationSet;
typedef std::pair<int, uint160> currentValidationCount; // nodes validating and highest node ID validating

class IValidations
{
public:
    static IValidations* New ();

    virtual ~IValidations () { }

	virtual bool addValidation (SerializedValidation::ref, const std::string& source) = 0;
	
    virtual ValidationSet getValidations (uint256 const& ledger) = 0;

	virtual void getValidationCount (uint256 const& ledger, bool currentOnly, int& trusted, int& untrusted) = 0;
	virtual void getValidationTypes (uint256 const& ledger, int& full, int& partial) = 0;

	virtual int getTrustedValidationCount (uint256 const& ledger) = 0;

	virtual int getNodesAfter (uint256 const& ledger) = 0;
	virtual int getLoadRatio (bool overLoaded) = 0;

    // VFALCO TODO make a typedef for this ugly return value!
	virtual boost::unordered_map<uint256, currentValidationCount> getCurrentValidations (
		uint256 currentLedger, uint256 previousLedger) = 0;

	virtual std::list <SerializedValidation::pointer> getCurrentTrustedValidations () = 0;

	virtual void tune (int size, int age) = 0;
	
    virtual void flush () = 0;

    virtual void sweep() = 0;
};

#endif
