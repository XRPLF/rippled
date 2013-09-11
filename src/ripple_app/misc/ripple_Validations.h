//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_VALIDATIONS_H_INCLUDED
#define RIPPLE_VALIDATIONS_H_INCLUDED

// VFALCO TODO rename and move these typedefs into the Validations interface
typedef boost::unordered_map<uint160, SerializedValidation::pointer> ValidationSet;
typedef std::pair<int, uint160> currentValidationCount; // nodes validating and highest node ID validating

class Validations : LeakChecked <Validations>
{
public:
    static Validations* New ();

    virtual ~Validations () { }

    virtual bool addValidation (SerializedValidation::ref, const std::string& source) = 0;

    virtual ValidationSet getValidations (uint256 const& ledger) = 0;

    virtual void getValidationCount (uint256 const& ledger, bool currentOnly, int& trusted, int& untrusted) = 0;
    virtual void getValidationTypes (uint256 const& ledger, int& full, int& partial) = 0;

    virtual int getTrustedValidationCount (uint256 const& ledger) = 0;

    virtual int getFeeAverage(uint256 const& ledger, uint64 ref, uint64& fee) = 0;

    virtual int getNodesAfter (uint256 const& ledger) = 0;
    virtual int getLoadRatio (bool overLoaded) = 0;

    // VFALCO TODO make a typedef for this ugly return value!
    virtual boost::unordered_map<uint256, currentValidationCount> getCurrentValidations (
        uint256 currentLedger, uint256 previousLedger) = 0;

    virtual std::list <SerializedValidation::pointer> getCurrentTrustedValidations () = 0;

    virtual void tune (int size, int age) = 0;

    virtual void flush () = 0;

    virtual void sweep () = 0;
};

#endif
