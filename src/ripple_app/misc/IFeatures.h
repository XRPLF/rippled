//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_IFEATURES_H
#define RIPPLE_IFEATURES_H

namespace ripple {

class FeatureSet
{
    // the status of all features requested in a given window
public:
    std::uint32_t  mCloseTime;
    int     mTrustedValidations;                // number of trusted validations
    boost::unordered_map<uint256, int> mVotes;  // yes votes by feature

    FeatureSet (std::uint32_t ct, int tv) : mCloseTime (ct), mTrustedValidations (tv)
    {
        ;
    }
    void addVote (uint256 const& feature)
    {
        ++mVotes[feature];
    }
};

class FeatureState
{
public:
    bool        mVetoed;            // We don't want this feature enabled
    bool        mEnabled;
    bool        mSupported;
    bool        mDefault;           // Include in genesis ledger

    std::uint32_t      mFirstMajority;     // First time we saw a majority (close time)
    std::uint32_t      mLastMajority;      // Most recent time we saw a majority (close time)

    std::string mFriendlyName;

    FeatureState ()
        : mVetoed (false), mEnabled (false), mSupported (false), mDefault (false),
          mFirstMajority (0), mLastMajority (0)
    {
        ;
    }

    void setVeto ()
    {
        mVetoed = true;
    }
    void setDefault ()
    {
        mDefault = true;
    }
    bool isDefault ()
    {
        return mDefault;
    }
    bool isSupported ()
    {
        return mSupported;
    }
    bool isVetoed ()
    {
        return mVetoed;
    }
    bool isEnabled ()
    {
        return mEnabled;
    }
    const std::string& getFiendlyName ()
    {
        return mFriendlyName;
    }
    void setFriendlyName (const std::string& n)
    {
        mFriendlyName = n;
    }
};

/** Feature table interface.

    The feature table stores the list of enabled and potential features.
    Individuals features are voted on by validators during the consensus
    process.
*/
class IFeatures
{
public:
    static IFeatures* New (std::uint32_t majorityTime, int majorityFraction);

    virtual ~IFeatures () { }

    virtual void addInitialFeatures () = 0;

    virtual FeatureState* addKnownFeature (const char* featureID, const char* friendlyName, bool veto) = 0;
    virtual uint256 getFeature (const std::string& name) = 0;

    virtual bool vetoFeature (uint256 const& feature) = 0;
    virtual bool unVetoFeature (uint256 const& feature) = 0;

    virtual bool enableFeature (uint256 const& feature) = 0;
    virtual bool disableFeature (uint256 const& feature) = 0;

    virtual bool isFeatureEnabled (uint256 const& feature) = 0;
    virtual bool isFeatureSupported (uint256 const& feature) = 0;

    virtual void setEnabledFeatures (const std::vector<uint256>& features) = 0;
    virtual void setSupportedFeatures (const std::vector<uint256>& features) = 0;

    // VFALCO NOTE these can't possibly be used since featureList_t was/is private.
    /*
    featureList_t getVetoedFeatures() = 0;
    featureList_t getEnabledFeatures() = 0;
    featureList_t getFeaturesToEnable(std::uint32_t closeTime) = 0;    // gets features we would vote to enable
    featureList_t getDesiredFeatures() = 0;                     // features we support, do not veto, are not enabled
    */

    virtual void reportValidations (const FeatureSet&) = 0;

    virtual Json::Value getJson (int) = 0;
    virtual Json::Value getJson (uint256 const& ) = 0;

    virtual void doValidation (Ledger::ref lastClosedLedger, STObject& baseValidation) = 0;
    virtual void doVoting (Ledger::ref lastClosedLedger, SHAMap::ref initialPosition) = 0;

};

} // ripple

#endif
