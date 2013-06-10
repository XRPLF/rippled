#ifndef RIPPLE_IPROOFOFWORKFACTORY_H
#define RIPPLE_IPROOFOFWORKFACTORY_H

enum POWResult
{
	powOK		= 0,
	powREUSED	= 1, // already submitted
	powBADNONCE	= 2, // you didn't solve it
	powEXPIRED	= 3, // time is up
	powCORRUPT	= 4,
	powTOOEASY	= 5, // the difficulty increased too much while you solved it
};

// VFALCO TODO move this to the class as a static member and rename it
bool powResultInfo (POWResult powCode, std::string& strToken, std::string& strHuman);

class IProofOfWorkFactory
{
public:
	typedef boost::bimap< boost::bimaps::multiset_of<time_t>, boost::bimaps::unordered_set_of<uint256> > powMap_t;
	typedef powMap_t::value_type	powMap_vt;

public:
    static IProofOfWorkFactory* New ();

    virtual ~IProofOfWorkFactory () { }

    // VFALCO TODO which members can be const?

	virtual ProofOfWork getProof () = 0;
	
    virtual POWResult checkProof (const std::string& token, uint256 const& solution) = 0;
	
    virtual uint64 getDifficulty() = 0;

    virtual void setDifficulty (int i) = 0;

	virtual void loadHigh () = 0;
	
    virtual void loadLow () = 0;
	
    virtual void sweep () = 0;

	virtual uint256 const& getSecret () const = 0;

	virtual void setSecret (uint256 const& secret) = 0;

public:
	static int getPowEntry (uint256 const& target, int iterations);
};

#endif

// vim:ts=4
