#ifndef RIPPLE_DISPUTEDTX_H
#define RIPPLE_DISPUTEDTX_H

/** A transaction discovered to be in dispute during conensus.

    During consensus, a @ref DisputedTx is created when a transaction
    is discovered to be disputed. The object persists only as long as
    the dispute.

    Undisputed transactions have no corresponding @ref DisputedTx object.
*/
class DisputedTx
{
public:
	typedef boost::shared_ptr <DisputedTx> pointer;

	DisputedTx (uint256 const& txID,
                Blob const& tx,
                bool ourVote) :
		mTransactionID(txID), mYays(0), mNays(0), mOurVote(ourVote), transaction(tx) { ; }

	uint256 const& getTransactionID() const				{ return mTransactionID; }
	
    bool getOurVote() const								{ return mOurVote; }
	
    // VFALCO TODO make this const
    Serializer& peekTransaction()						{ return transaction; }

    void setOurVote(bool o)								{ mOurVote = o; }

    // VFALCO NOTE its not really a peer, its the 160 bit hash of the validator's public key
    //
	void setVote(uint160 const& peer, bool votesYes);
	void unVote (uint160 const& peer);

	bool updateVote(int percentTime, bool proposing);
	Json::Value getJson();

private:
	uint256 mTransactionID;
	int mYays;
    int mNays;
	bool mOurVote;
	Serializer transaction;
	boost::unordered_map <uint160, bool> mVotes;
};

// VFALCO TODO Rename and put these in a tidy place
typedef std::map<uint256, DisputedTx::pointer>::value_type u256_lct_pair;
typedef std::map<uint160, LedgerProposal::pointer>::value_type u160_prop_pair;
#define LEDGER_TOTAL_PASSES 8
#define LEDGER_RETRY_PASSES 5

#endif
