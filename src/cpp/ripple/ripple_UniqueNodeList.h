//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_UNIQUENODELIST_H_INCLUDED
#define RIPPLE_UNIQUENODELIST_H_INCLUDED

class UniqueNodeList
{
public:
    enum ValidatorSource
    {
        vsConfig    = 'C',  // rippled.cfg
        vsInbound   = 'I',
        vsManual    = 'M',
        vsReferral  = 'R',
        vsTold      = 'T',
        vsValidator = 'V',  // validators.txt
        vsWeb       = 'W',
    };

    // VFALCO TODO rename this to use the right coding style
    typedef long score;

public:
    // VFALCO TODO make this not use boost::asio...
    static UniqueNodeList* New ();

    virtual ~UniqueNodeList () { }

    // VFALCO TODO Roll this into the constructor so there is one less state.
    virtual void start () = 0;

    // VFALCO TODO rename all these, the "node" prefix is redundant (lol)
    virtual void nodeAddPublic (const RippleAddress& naNodePublic, ValidatorSource vsWhy, const std::string& strComment) = 0;
    virtual void nodeAddDomain (std::string strDomain, ValidatorSource vsWhy, const std::string& strComment = "") = 0;
    virtual void nodeRemovePublic (const RippleAddress& naNodePublic) = 0;
    virtual void nodeRemoveDomain (std::string strDomain) = 0;
    virtual void nodeReset () = 0;

    virtual void nodeScore () = 0;

    virtual bool nodeInUNL (const RippleAddress& naNodePublic) = 0;
    virtual bool nodeInCluster (const RippleAddress& naNodePublic) = 0;
    virtual bool nodeInCluster (const RippleAddress& naNodePublic, std::string& name) = 0;
    virtual bool nodeUpdate (const RippleAddress& naNodePublic, ClusterNodeStatus const& cnsStatus) = 0;
    virtual std::map<RippleAddress, ClusterNodeStatus> getClusterStatus () = 0;
    virtual uint32 getClusterFee () = 0;
    virtual void addClusterStatus (Json::Value&) = 0;

    virtual void nodeBootstrap () = 0;
    virtual bool nodeLoad (boost::filesystem::path pConfig) = 0;
    virtual void nodeNetwork () = 0;

    virtual Json::Value getUnlJson () = 0;

    virtual int iSourceScore (ValidatorSource vsWhy) = 0;
};

#endif

// vim:ts=4
