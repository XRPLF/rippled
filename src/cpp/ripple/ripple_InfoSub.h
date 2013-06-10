#ifndef RIPPLE_INFOSUB_H
#define RIPPLE_INFOSUB_H

// Operations that clients may wish to perform against the network
// Master operational handler, server sequencer, network tracker

class PFRequest;

DEFINE_INSTANCE(InfoSub);

// VFALCO TODO Move InfoSub to a separate file
class InfoSub : public IS_INSTANCE(InfoSub)
{
public:
	typedef boost::shared_ptr<InfoSub>			pointer;

    // VFALCO TODO Standardize on the names of weak / strong pointer typedefs.
	typedef boost::weak_ptr<InfoSub>			wptr;

    typedef const boost::shared_ptr<InfoSub>&	ref;

public:
	InfoSub ();

	virtual ~InfoSub ();

	virtual	void send (const Json::Value& jvObj, bool broadcast) = 0;

    // VFALCO NOTE Why is this virtual?
    virtual void send (const Json::Value& jvObj, const std::string& sObj, bool broadcast);

	uint64 getSeq ();

	void onSendEmpty ();

	void insertSubAccountInfo (RippleAddress addr, uint32 uLedgerIndex);

	void clearPFRequest();

	void setPFRequest (const boost::shared_ptr<PFRequest>& req);

	boost::shared_ptr <PFRequest> const& getPFRequest ();

protected:
    // VFALCO TODO make accessor for this member
	boost::mutex								mLockInfo;

private:
	static uint64								sSeq;
	static boost::mutex							sSeqLock;

    boost::unordered_set<RippleAddress>			mSubAccountInfo;
	boost::unordered_set<RippleAddress>			mSubAccountTransaction;
	boost::shared_ptr <PFRequest>				mPFRequest;

	uint64										mSeq;
};

#endif
