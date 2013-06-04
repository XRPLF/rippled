#ifndef RIPPLE_JOB_H
#define RIPPLE_JOB_H

// Note that this queue should only be used for CPU-bound jobs
// It is primarily intended for signature checking
enum JobType
{
    // must be in priority order, low to high
	jtINVALID		= -1,
	jtPACK			= 1,	// Make a fetch pack for a peer
	jtPUBOLDLEDGER	= 2,	// An old ledger has been accepted
	jtVALIDATION_ut	= 3,	// A validation from an untrusted source
	jtPROOFWORK		= 4,	// A proof of work demand from another server
	jtPROPOSAL_ut	= 5,	// A proposal from an untrusted source
	jtLEDGER_DATA	= 6,	// Received data for a ledger we're acquiring
	jtUPDATE_PF		= 7,	// Update pathfinding requests
	jtCLIENT		= 8,	// A websocket command from the client
	jtTRANSACTION	= 9,	// A transaction received from the network
	jtPUBLEDGER		= 10,	// Publish a fully-accepted ledger
	jtWAL			= 11,	// Write-ahead logging
	jtVALIDATION_t	= 12,	// A validation from a trusted source
	jtWRITE			= 13,	// Write out hashed objects
	jtTRANSACTION_l	= 14,	// A local transaction
	jtPROPOSAL_t	= 15,	// A proposal from a trusted source
	jtADMIN			= 16,	// An administrative operation
	jtDEATH			= 17,	// job of death, used internally

// special types not dispatched by the job pool
	jtPEER			= 24,
	jtDISK			= 25,
	jtACCEPTLEDGER	= 26,
	jtTXN_PROC		= 27,
	jtOB_SETUP		= 28,
	jtPATH_FIND		= 29,
	jtHO_READ		= 30,
	jtHO_WRITE		= 31,
}; // CAUTION: If you add new types, add them to JobType.cpp too

// VFALCO: TODO, move this into the enum so it calculates itself?
#define NUM_JOB_TYPES 48 // why 48 and not 32?

class Job
{
public:

    // VFALCO: TODO, find out why these extra constructors are needed
	Job();

	Job (JobType type, uint64 index);

    // VFALCO: TODO, try to remove the dependency on LoadMonitor.
	Job (JobType type,
         std::string const& name,
         uint64 index,
         LoadMonitor& lm,
         FUNCTION_TYPE <void (Job&)> const& job);

	JobType getType() const;
	
    void doJob ();

    void rename (const std::string& n);

    // These comparison operators make the jobs sort in priority order in the job set
	bool operator< (const Job& j) const;
	bool operator> (const Job& j) const;
	bool operator<= (const Job& j) const;
	bool operator>= (const Job& j) const;

	static const char* toString (JobType);

private:
	JobType						mType;
	uint64						mJobIndex;
	FUNCTION_TYPE <void (Job&)>	mJob;
    // VFALCO: TODO, why is this called mLoadMonitor if the type is LoadEvent pointer?
	LoadEvent::pointer			mLoadMonitor;
	std::string					mName;
};

#endif
