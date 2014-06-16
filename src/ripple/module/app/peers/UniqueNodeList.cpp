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

#include <ripple/basics/utility/IniFile.h>
#include <ripple/basics/utility/Time.h>
#include <beast/module/core/thread/DeadlineTimer.h>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

namespace ripple {

// XXX Dynamically limit fetching by distance.
// XXX Want a limit of 2000 validators.

// Guarantees minimum throughput of 1 node per second.
#define NODE_FETCH_JOBS         10
#define NODE_FETCH_SECONDS      10
#define NODE_FILE_BYTES_MAX     (50<<10)    // 50k
#define NODE_FILE_NAME          SYSTEM_NAME ".txt"
#define NODE_FILE_PATH          "/" NODE_FILE_NAME

// Wait for validation information to be stable before scoring.
// #define SCORE_DELAY_SECONDS      20
#define SCORE_DELAY_SECONDS     5

// Don't bother propagating past this number of rounds.
#define SCORE_ROUNDS            10

// VFALCO TODO Replace macros with language constructs
#define VALIDATORS_FETCH_SECONDS    30
#define VALIDATORS_FILE_BYTES_MAX   (50 << 10)

// Gather string constants.
#define SECTION_CURRENCIES      "currencies"
#define SECTION_DOMAIN          "domain"
#define SECTION_IPS             "ips"
#define SECTION_IPS_URL         "ips_url"
#define SECTION_PUBLIC_KEY      "validation_public_key"
#define SECTION_VALIDATORS      "validators"
#define SECTION_VALIDATORS_URL  "validators_url"

// Limit pollution of database.
// YYY Move to config file.
#define REFERRAL_VALIDATORS_MAX 50
#define REFERRAL_IPS_MAX        50

SETUP_LOG (UniqueNodeList)

// VFALCO TODO move all function definitions inlined into the class.
class UniqueNodeListImp
    : public UniqueNodeList
    , public beast::DeadlineTimer::Listener
{
private:
    // VFALCO TODO Rename these structs? Are they objects with static storage?
    //             This looks like C and not C++...
    //
    typedef struct
    {
        std::string                 strDomain;
        RippleAddress               naPublicKey;
        ValidatorSource             vsSource;
        boost::posix_time::ptime    tpNext;
        boost::posix_time::ptime    tpScan;
        boost::posix_time::ptime    tpFetch;
        uint256                     iSha256;
        std::string                 strComment;
    } seedDomain;

    typedef struct
    {
        RippleAddress               naPublicKey;
        ValidatorSource             vsSource;
        boost::posix_time::ptime    tpNext;
        boost::posix_time::ptime    tpScan;
        boost::posix_time::ptime    tpFetch;
        uint256                     iSha256;
        std::string                 strComment;
    } seedNode;

    // Used to distribute scores.
    typedef struct
    {
        int                 iScore;
        int                 iRoundScore;
        int                 iRoundSeed;
        int                 iSeen;
        std::string         strValidator;   // The public key.
        std::vector<int>    viReferrals;
    } scoreNode;

    typedef ripple::unordered_map<std::string, int> strIndex;
    typedef std::pair<std::string, int> IPAndPortNumber;
    typedef ripple::unordered_map<std::pair< std::string, int>, score>   epScore;

public:
    explicit UniqueNodeListImp (Stoppable& parent)
        : UniqueNodeList (parent)
        , m_scoreTimer (this)
        , mFetchActive (0)
        , m_fetchTimer (this)
    {
    }

    //--------------------------------------------------------------------------

    void onStop ()
    {
        m_fetchTimer.cancel ();
        m_scoreTimer.cancel ();

        stopped ();
    }

    //--------------------------------------------------------------------------

    void doScore ()
    {
        mtpScoreNext    = boost::posix_time::ptime (boost::posix_time::not_a_date_time); // Timer not set.
        mtpScoreStart   = boost::posix_time::second_clock::universal_time ();           // Scoring.

        WriteLog (lsTRACE, UniqueNodeList) << "Scoring: Start";

        scoreCompute ();

        WriteLog (lsTRACE, UniqueNodeList) << "Scoring: End";

        // Save update time.
        mtpScoreUpdated = mtpScoreStart;
        miscSave ();

        mtpScoreStart   = boost::posix_time::ptime (boost::posix_time::not_a_date_time); // Not scoring.

        // Score again if needed.
        scoreNext (false);
    }

    void doFetch ()
    {
        // Time to check for another fetch.
        WriteLog (lsTRACE, UniqueNodeList) << "fetchTimerHandler";
        fetchNext ();
    }

    void onDeadlineTimer (beast::DeadlineTimer& timer)
    {
        if (timer == m_scoreTimer)
        {
            getApp().getJobQueue ().addJob (jtUNL, "UNL.score",
                std::bind (&UniqueNodeListImp::doScore, this));
        }
        else if (timer == m_fetchTimer)
        {
            getApp().getJobQueue ().addJob (jtUNL, "UNL.fetch",
                std::bind (&UniqueNodeListImp::doFetch, this));
        }
    }

    //--------------------------------------------------------------------------

    // This is called when the application is started.
    // Get update times and start fetching and scoring as needed.
    void start ()
    {
        miscLoad ();

        WriteLog (lsDEBUG, UniqueNodeList) << "Validator fetch updated: " << mtpFetchUpdated;
        WriteLog (lsDEBUG, UniqueNodeList) << "Validator score updated: " << mtpScoreUpdated;

        fetchNext ();           // Start fetching.
        scoreNext (false);      // Start scoring.
    }

    //--------------------------------------------------------------------------

    // Add a trusted node.  Called by RPC or other source.
    void nodeAddPublic (const RippleAddress& naNodePublic, ValidatorSource vsWhy, const std::string& strComment)
    {
        seedNode    snCurrent;

        bool        bFound      = getSeedNodes (naNodePublic, snCurrent);
        bool        bChanged    = false;

        if (!bFound)
        {
            snCurrent.naPublicKey   = naNodePublic;
            snCurrent.tpNext        = boost::posix_time::second_clock::universal_time ();
        }

        // Promote source, if needed.
        if (!bFound /*|| iSourceScore (vsWhy) >= iSourceScore (snCurrent.vsSource)*/)
        {
            snCurrent.vsSource      = vsWhy;
            snCurrent.strComment    = strComment;
            bChanged                = true;
        }

        if (vsManual == vsWhy)
        {
            // A manual add forces immediate scan.
            snCurrent.tpNext        = boost::posix_time::second_clock::universal_time ();
            bChanged                = true;
        }

        if (bChanged)
            setSeedNodes (snCurrent, true);
    }

    //--------------------------------------------------------------------------

    // Queue a domain for a single attempt fetch a ripple.txt.
    // --> strComment: only used on vsManual
    // YYY As a lot of these may happen at once, would be nice to wrap multiple calls in a transaction.
    void nodeAddDomain (std::string strDomain, ValidatorSource vsWhy, const std::string& strComment)
    {
        boost::trim (strDomain);
        boost::to_lower (strDomain);

        // YYY Would be best to verify strDomain is a valid domain.
        // WriteLog (lsTRACE) << str(boost::format("nodeAddDomain: '%s' %c '%s'")
        //  % strDomain
        //  % vsWhy
        //  % strComment);

        seedDomain  sdCurrent;

        bool        bFound      = getSeedDomains (strDomain, sdCurrent);
        bool        bChanged    = false;

        if (!bFound)
        {
            sdCurrent.strDomain     = strDomain;
            sdCurrent.tpNext        = boost::posix_time::second_clock::universal_time ();
        }

        // Promote source, if needed.
        if (!bFound || iSourceScore (vsWhy) >= iSourceScore (sdCurrent.vsSource))
        {
            sdCurrent.vsSource      = vsWhy;
            sdCurrent.strComment    = strComment;
            bChanged                = true;
        }

        if (vsManual == vsWhy)
        {
            // A manual add forces immediate scan.
            sdCurrent.tpNext        = boost::posix_time::second_clock::universal_time ();
            bChanged                = true;
        }

        if (bChanged)
            setSeedDomains (sdCurrent, true);
    }

    //--------------------------------------------------------------------------

    void nodeRemovePublic (const RippleAddress& naNodePublic)
    {
        {
            Database* db = getApp().getWalletDB ()->getDB ();
            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

            db->executeSQL (str (boost::format ("DELETE FROM SeedNodes WHERE PublicKey=%s") % sqlEscape (naNodePublic.humanNodePublic ())));
            db->executeSQL (str (boost::format ("DELETE FROM TrustedNodes WHERE PublicKey=%s") % sqlEscape (naNodePublic.humanNodePublic ())));
        }

        // YYY Only dirty on successful delete.
        fetchDirty ();

        ScopedUNLLockType sl (mUNLLock);
        mUNL.erase (naNodePublic.humanNodePublic ());
    }

    //--------------------------------------------------------------------------

    void nodeRemoveDomain (std::string strDomain)
    {
        boost::trim (strDomain);
        boost::to_lower (strDomain);

        {
            Database* db = getApp().getWalletDB ()->getDB ();
            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

            db->executeSQL (str (boost::format ("DELETE FROM SeedDomains WHERE Domain=%s") % sqlEscape (strDomain)));
        }

        // YYY Only dirty on successful delete.
        fetchDirty ();
    }

    //--------------------------------------------------------------------------

    void nodeReset ()
    {
        {
            Database* db = getApp().getWalletDB ()->getDB ();

            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

            // XXX Check results.
            db->executeSQL ("DELETE FROM SeedDomains");
            db->executeSQL ("DELETE FROM SeedNodes");
        }

        fetchDirty ();
    }

    //--------------------------------------------------------------------------

    // For debugging, schedule forced scoring.
    void nodeScore ()
    {
        scoreNext (true);
    }

    //--------------------------------------------------------------------------

    bool nodeInUNL (const RippleAddress& naNodePublic)
    {
        ScopedUNLLockType sl (mUNLLock);

        return mUNL.end () != mUNL.find (naNodePublic.humanNodePublic ());
    }

    //--------------------------------------------------------------------------

    bool nodeInCluster (const RippleAddress& naNodePublic)
    {
        ScopedUNLLockType sl (mUNLLock);
        return m_clusterNodes.end () != m_clusterNodes.find (naNodePublic);
    }

    //--------------------------------------------------------------------------

    bool nodeInCluster (const RippleAddress& naNodePublic, std::string& name)
    {
        ScopedUNLLockType sl (mUNLLock);
        std::map<RippleAddress, ClusterNodeStatus>::iterator it = m_clusterNodes.find (naNodePublic);

        if (it == m_clusterNodes.end ())
            return false;

        name = it->second.getName();
        return true;
    }

    //--------------------------------------------------------------------------

    bool nodeUpdate (const RippleAddress& naNodePublic, ClusterNodeStatus const& cnsStatus)
    {
        ScopedUNLLockType sl (mUNLLock);
        return m_clusterNodes[naNodePublic].update(cnsStatus);
    }

    //--------------------------------------------------------------------------

    std::map<RippleAddress, ClusterNodeStatus> getClusterStatus ()
    {
        std::map<RippleAddress, ClusterNodeStatus> ret;
        {
            ScopedUNLLockType sl (mUNLLock);
            ret = m_clusterNodes;
        }
        return ret;
    }

    //--------------------------------------------------------------------------

    std::uint32_t getClusterFee ()
    {
        int thresh = getApp().getOPs().getNetworkTimeNC() - 90;

        std::vector<std::uint32_t> fees;
        {
            ScopedUNLLockType sl (mUNLLock);
            {
                for (std::map<RippleAddress, ClusterNodeStatus>::iterator it = m_clusterNodes.begin(),
                    end = m_clusterNodes.end(); it != end; ++it)
                {
                    if (it->second.getReportTime() >= thresh)
                        fees.push_back(it->second.getLoadFee());
                }
            }
        }

        if (fees.empty())
            return 0;
        std::sort (fees.begin(), fees.end());
        return fees[fees.size() / 2];
    }

    //--------------------------------------------------------------------------

    void addClusterStatus (Json::Value& obj)
    {
        ScopedUNLLockType sl (mUNLLock);
        if (m_clusterNodes.size() > 1) // nodes other than us
        {
            int          now   = getApp().getOPs().getNetworkTimeNC();
            std::uint32_t ref   = getApp().getFeeTrack().getLoadBase();
            Json::Value& nodes = (obj["cluster"] = Json::objectValue);

            for (std::map<RippleAddress, ClusterNodeStatus>::iterator it = m_clusterNodes.begin(),
                end = m_clusterNodes.end(); it != end; ++it)
            {
                if (it->first != getApp().getLocalCredentials().getNodePublic())
                {
                    Json::Value& node = nodes[it->first.humanNodePublic()];

                    if (!it->second.getName().empty())
                        node["tag"] = it->second.getName();

                    if ((it->second.getLoadFee() != ref) && (it->second.getLoadFee() != 0))
                        node["fee"] = static_cast<double>(it->second.getLoadFee()) / ref;

                    if (it->second.getReportTime() != 0)
                        node["age"] = (it->second.getReportTime() >= now) ? 0 : (now - it->second.getReportTime());
                }
            }
        }
    }

    //--------------------------------------------------------------------------

    void nodeBootstrap ()
    {
        int         iDomains    = 0;
        int         iNodes      = 0;

#if 0
        {
            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
            Database* db = getApp().getWalletDB ()->getDB ();

            if (db->executeSQL (str (boost::format ("SELECT COUNT(*) AS Count FROM SeedDomains WHERE Source='%s' OR Source='%c';") % vsManual % vsValidator)) && db->startIterRows ())
                iDomains    = db->getInt ("Count");

            db->endIterRows ();

            if (db->executeSQL (str (boost::format ("SELECT COUNT(*) AS Count FROM SeedNodes WHERE Source='%s' OR Source='%c';") % vsManual % vsValidator)) && db->startIterRows ())
                iNodes      = db->getInt ("Count");

            db->endIterRows ();
        }
#endif

        bool    bLoaded = iDomains || iNodes;

        // Always merge in the file specified in the config.
        if (!getConfig ().VALIDATORS_FILE.empty ())
        {
            WriteLog (lsINFO, UniqueNodeList) << "Bootstrapping UNL: loading from unl_default.";

            bLoaded = nodeLoad (getConfig ().VALIDATORS_FILE);
        }

        // If never loaded anything try the current directory.
        if (!bLoaded && getConfig ().VALIDATORS_FILE.empty ())
        {
            WriteLog (lsINFO, UniqueNodeList) << boost::str (boost::format ("Bootstrapping UNL: loading from '%s'.")
                                              % getConfig ().VALIDATORS_BASE);

            bLoaded = nodeLoad (getConfig ().VALIDATORS_BASE);
        }

        // Always load from rippled.cfg
        if (!getConfig ().validators.empty ())
        {
            RippleAddress   naInvalid;  // Don't want a referrer on added entries.

            WriteLog (lsINFO, UniqueNodeList) << boost::str (boost::format ("Bootstrapping UNL: loading from '%s'.")
                                              % getConfig ().CONFIG_FILE);

            if (processValidators ("local", getConfig ().CONFIG_FILE.string (), naInvalid, vsConfig, &getConfig ().validators))
                bLoaded = true;
        }

        if (!bLoaded)
        {
            WriteLog (lsINFO, UniqueNodeList) << boost::str (boost::format ("Bootstrapping UNL: loading from '%s'.")
                                              % getConfig ().VALIDATORS_SITE);

            nodeNetwork ();
        }
    }

    //--------------------------------------------------------------------------

    bool nodeLoad (boost::filesystem::path pConfig)
    {
        if (pConfig.empty ())
        {
            WriteLog (lsINFO, UniqueNodeList) << Config::Helpers::getValidatorsFileName() <<
                " path not specified.";

            return false;
        }

        if (!boost::filesystem::exists (pConfig))
        {
            WriteLog (lsWARNING, UniqueNodeList) << Config::Helpers::getValidatorsFileName() <<
                " not found: " << pConfig;

            return false;
        }

        if (!boost::filesystem::is_regular_file (pConfig))
        {
            WriteLog (lsWARNING, UniqueNodeList) << Config::Helpers::getValidatorsFileName() <<
                " not regular file: " << pConfig;

            return false;
        }

        std::ifstream   ifsDefault (pConfig.native ().c_str (), std::ios::in);

        if (!ifsDefault)
        {
            WriteLog (lsFATAL, UniqueNodeList) << Config::Helpers::getValidatorsFileName() <<
                " failed to open: " << pConfig;

            return false;
        }

        std::string     strValidators;

        strValidators.assign ((std::istreambuf_iterator<char> (ifsDefault)),
                              std::istreambuf_iterator<char> ());

        if (ifsDefault.bad ())
        {
            WriteLog (lsFATAL, UniqueNodeList) << Config::Helpers::getValidatorsFileName() <<
            "Failed to read: " << pConfig;

            return false;
        }

        nodeProcess ("local", strValidators, pConfig.string ());

        WriteLog (lsTRACE, UniqueNodeList) << str (boost::format ("Processing: %s") % pConfig);

        return true;
    }

    //--------------------------------------------------------------------------

    void nodeNetwork ()
    {
        if (!getConfig ().VALIDATORS_SITE.empty ())
        {
            HTTPClient::get (
                true,
                getApp().getIOService (),
                getConfig ().VALIDATORS_SITE,
                443,
                getConfig ().VALIDATORS_URI,
                VALIDATORS_FILE_BYTES_MAX,
                boost::posix_time::seconds (VALIDATORS_FETCH_SECONDS),
                std::bind (&UniqueNodeListImp::validatorsResponse, this,
                           std::placeholders::_1,
                           std::placeholders::_2,
                           std::placeholders::_3));
        }
    }

    //--------------------------------------------------------------------------

    Json::Value getUnlJson ()
    {
        Database* db = getApp().getWalletDB ()->getDB ();

        Json::Value ret (Json::arrayValue);

        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
        SQL_FOREACH (db, "SELECT * FROM TrustedNodes;")
        {
            Json::Value node (Json::objectValue);

            node["publicKey"]   = db->getStrBinary ("PublicKey");
            node["comment"]     = db->getStrBinary ("Comment");

            ret.append (node);
        }

        return ret;
    }

    //--------------------------------------------------------------------------

    // For each kind of source, have a starting number of points to be distributed.
    int iSourceScore (ValidatorSource vsWhy)
    {
        int     iScore  = 0;

        switch (vsWhy)
        {
        case vsConfig:
            iScore  = 1500;
            break;

        case vsInbound:
            iScore  =    0;
            break;

        case vsManual:
            iScore  = 1500;
            break;

        case vsReferral:
            iScore  =    0;
            break;

        case vsTold:
            iScore  =    0;
            break;

        case vsValidator:
            iScore  = 1000;
            break;

        case vsWeb:
            iScore  =  200;
            break;

        default:
            throw std::runtime_error ("Internal error: bad ValidatorSource.");
        }

        return iScore;
    }

    //--------------------------------------------------------------------------
private:
    // Load information about when we last updated.
    bool miscLoad ()
    {
        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
        Database* db = getApp().getWalletDB ()->getDB ();

        if (!db->executeSQL ("SELECT * FROM Misc WHERE Magic=1;")) return false;

        bool    bAvail  = !!db->startIterRows ();

        mtpFetchUpdated = ptFromSeconds (bAvail ? db->getInt ("FetchUpdated") : -1);
        mtpScoreUpdated = ptFromSeconds (bAvail ? db->getInt ("ScoreUpdated") : -1);

        db->endIterRows ();

        trustedLoad ();

        return true;
    }

    //--------------------------------------------------------------------------

    // Persist update information.
    bool miscSave ()
    {
        Database*   db = getApp().getWalletDB ()->getDB ();
        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

        db->executeSQL (str (boost::format ("REPLACE INTO Misc (Magic,FetchUpdated,ScoreUpdated) VALUES (1,%d,%d);")
                             % iToSeconds (mtpFetchUpdated)
                             % iToSeconds (mtpScoreUpdated)));

        return true;
    }

    //--------------------------------------------------------------------------

    void trustedLoad ()
    {
        boost::regex rNode ("\\`\\s*(\\S+)[\\s]*(.*)\\'");
        BOOST_FOREACH (const std::string & c, getConfig ().CLUSTER_NODES)
        {
            boost::smatch match;

            if (boost::regex_match (c, match, rNode))
            {
                RippleAddress a = RippleAddress::createNodePublic (match[1]);

                if (a.isValid ())
                    m_clusterNodes.insert (std::make_pair (a, ClusterNodeStatus(match[2])));
            }
            else
                WriteLog (lsWARNING, UniqueNodeList) << "Entry in cluster list invalid: '" << c << "'";
        }

        Database*   db = getApp().getWalletDB ()->getDB ();
        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
        ScopedUNLLockType slUNL (mUNLLock);

        mUNL.clear ();

        // XXX Needs to limit by quanity and quality.
        SQL_FOREACH (db, "SELECT PublicKey FROM TrustedNodes WHERE Score != 0;")
        {
            mUNL.insert (db->getStrBinary ("PublicKey"));
        }
    }

    //--------------------------------------------------------------------------

    // For a round of scoring we destribute points from a node to nodes it refers to.
    // Returns true, iff scores were distributed.
    //
    bool scoreRound (std::vector<scoreNode>& vsnNodes)
    {
        bool    bDist   = false;

        // For each node, distribute roundSeed to roundScores.
        BOOST_FOREACH (scoreNode & sn, vsnNodes)
        {
            int     iEntries    = sn.viReferrals.size ();

            if (sn.iRoundSeed && iEntries)
            {
                score   iTotal  = (iEntries + 1) * iEntries / 2;
                score   iBase   = sn.iRoundSeed * iEntries / iTotal;

                // Distribute the current entires' seed score to validators prioritized by mention order.
                for (int i = 0; i != iEntries; i++)
                {
                    score   iPoints = iBase * (iEntries - i) / iEntries;

                    vsnNodes[sn.viReferrals[i]].iRoundScore += iPoints;
                }
            }
        }

        if (ShouldLog (lsTRACE, UniqueNodeList))
        {
            WriteLog (lsTRACE, UniqueNodeList) << "midway: ";
            BOOST_FOREACH (scoreNode & sn, vsnNodes)
            {
                WriteLog (lsTRACE, UniqueNodeList) << str (boost::format ("%s| %d, %d, %d: [%s]")
                                                   % sn.strValidator
                                                   % sn.iScore
                                                   % sn.iRoundScore
                                                   % sn.iRoundSeed
                                                   % strJoin (sn.viReferrals.begin (), sn.viReferrals.end (), ","));
            }
        }

        // Add roundScore to score.
        // Make roundScore new roundSeed.
        BOOST_FOREACH (scoreNode & sn, vsnNodes)
        {
            if (!bDist && sn.iRoundScore)
                bDist   = true;

            sn.iScore       += sn.iRoundScore;
            sn.iRoundSeed   = sn.iRoundScore;
            sn.iRoundScore  = 0;
        }

        if (ShouldLog (lsTRACE, UniqueNodeList))
        {
            WriteLog (lsTRACE, UniqueNodeList) << "finish: ";
            BOOST_FOREACH (scoreNode & sn, vsnNodes)
            {
                WriteLog (lsTRACE, UniqueNodeList) << str (boost::format ("%s| %d, %d, %d: [%s]")
                                                   % sn.strValidator
                                                   % sn.iScore
                                                   % sn.iRoundScore
                                                   % sn.iRoundSeed
                                                   % strJoin (sn.viReferrals.begin (), sn.viReferrals.end (), ","));
            }
        }

        return bDist;
    }

    //--------------------------------------------------------------------------

    // From SeedDomains and ValidatorReferrals compute scores and update TrustedNodes.
    //
    // VFALCO TODO Shrink this function, break it up
    //
    void scoreCompute ()
    {
        strIndex                umPulicIdx;     // Map of public key to index.
        strIndex                umDomainIdx;    // Map of domain to index.
        std::vector<scoreNode>  vsnNodes;       // Index to scoring node.

        Database*   db = getApp().getWalletDB ()->getDB ();

        // For each entry in SeedDomains with a PublicKey:
        // - Add an entry in umPulicIdx, umDomainIdx, and vsnNodes.
        {
            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

            SQL_FOREACH (db, "SELECT Domain,PublicKey,Source FROM SeedDomains;")
            {
                if (db->getNull ("PublicKey"))
                {
                    // We ignore entries we don't have public keys for.
                }
                else
                {
                    std::string strDomain       = db->getStrBinary ("Domain");
                    std::string strPublicKey    = db->getStrBinary ("PublicKey");
                    std::string strSource       = db->getStrBinary ("Source");
                    int         iScore          = iSourceScore (static_cast<ValidatorSource> (strSource[0]));
                    strIndex::iterator  siOld   = umPulicIdx.find (strPublicKey);

                    if (siOld == umPulicIdx.end ())
                    {
                        // New node
                        int         iNode       = vsnNodes.size ();

                        umPulicIdx[strPublicKey]    = iNode;
                        umDomainIdx[strDomain]      = iNode;

                        scoreNode   snCurrent;

                        snCurrent.strValidator  = strPublicKey;
                        snCurrent.iScore        = iScore;
                        snCurrent.iRoundSeed    = snCurrent.iScore;
                        snCurrent.iRoundScore   = 0;
                        snCurrent.iSeen         = -1;

                        vsnNodes.push_back (snCurrent);
                    }
                    else
                    {
                        scoreNode&  snOld   = vsnNodes[siOld->second];

                        if (snOld.iScore < iScore)
                        {
                            // Update old node

                            snOld.iScore        = iScore;
                            snOld.iRoundSeed    = snOld.iScore;
                        }
                    }
                }
            }
        }

        // For each entry in SeedNodes:
        // - Add an entry in umPulicIdx, umDomainIdx, and vsnNodes.
        {
            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

            SQL_FOREACH (db, "SELECT PublicKey,Source FROM SeedNodes;")
            {
                std::string strPublicKey    = db->getStrBinary ("PublicKey");
                std::string strSource       = db->getStrBinary ("Source");
                int         iScore          = iSourceScore (static_cast<ValidatorSource> (strSource[0]));
                strIndex::iterator  siOld   = umPulicIdx.find (strPublicKey);

                if (siOld == umPulicIdx.end ())
                {
                    // New node
                    int         iNode       = vsnNodes.size ();

                    umPulicIdx[strPublicKey]    = iNode;

                    scoreNode   snCurrent;

                    snCurrent.strValidator  = strPublicKey;
                    snCurrent.iScore        = iScore;
                    snCurrent.iRoundSeed    = snCurrent.iScore;
                    snCurrent.iRoundScore   = 0;
                    snCurrent.iSeen         = -1;

                    vsnNodes.push_back (snCurrent);
                }
                else
                {
                    scoreNode&  snOld   = vsnNodes[siOld->second];

                    if (snOld.iScore < iScore)
                    {
                        // Update old node

                        snOld.iScore        = iScore;
                        snOld.iRoundSeed    = snOld.iScore;
                    }
                }
            }
        }

        // For debugging, print out initial scores.
        if (ShouldLog (lsTRACE, UniqueNodeList))
        {
            BOOST_FOREACH (scoreNode & sn, vsnNodes)
            {
                WriteLog (lsTRACE, UniqueNodeList) << str (boost::format ("%s| %d, %d, %d")
                                                   % sn.strValidator
                                                   % sn.iScore
                                                   % sn.iRoundScore
                                                   % sn.iRoundSeed);
            }
        }

        // WriteLog (lsTRACE, UniqueNodeList) << str(boost::format("vsnNodes.size=%d") % vsnNodes.size());

        // Step through growing list of nodes adding each validation list.
        // - Each validator may have provided referals.  Add those referals as validators.
        for (int iNode = 0; iNode != vsnNodes.size (); ++iNode)
        {
            scoreNode&          sn              = vsnNodes[iNode];
            std::string&        strValidator    = sn.strValidator;
            std::vector<int>&   viReferrals     = sn.viReferrals;

            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

            SQL_FOREACH (db, boost::str (boost::format ("SELECT Referral FROM ValidatorReferrals WHERE Validator=%s ORDER BY Entry;")
                                         % sqlEscape (strValidator)))
            {
                std::string strReferral = db->getStrBinary ("Referral");
                int         iReferral;

                strIndex::iterator  itEntry;

                RippleAddress       na;

                if (na.setNodePublic (strReferral))
                {
                    // Referring a public key.
                    itEntry     = umPulicIdx.find (strReferral);

                    if (itEntry == umPulicIdx.end ())
                    {
                        // Not found add public key to list of nodes.
                        iReferral               = vsnNodes.size ();

                        umPulicIdx[strReferral] = iReferral;

                        scoreNode   snCurrent;

                        snCurrent.strValidator  = strReferral;
                        snCurrent.iScore        = iSourceScore (vsReferral);
                        snCurrent.iRoundSeed    = snCurrent.iScore;
                        snCurrent.iRoundScore   = 0;
                        snCurrent.iSeen         = -1;

                        vsnNodes.push_back (snCurrent);
                    }
                    else
                    {
                        iReferral   =  itEntry->second;
                    }

                    // WriteLog (lsTRACE, UniqueNodeList) << str(boost::format("%s: Public=%s iReferral=%d") % strValidator % strReferral % iReferral);

                }
                else
                {
                    // Referring a domain.
                    itEntry     = umDomainIdx.find (strReferral);
                    iReferral   = itEntry == umDomainIdx.end ()
                                  ? -1            // We ignore domains we can't find entires for.
                                  : itEntry->second;

                    // WriteLog (lsTRACE, UniqueNodeList) << str(boost::format("%s: Domain=%s iReferral=%d") % strValidator % strReferral % iReferral);
                }

                if (iReferral >= 0 && iNode != iReferral)
                    viReferrals.push_back (iReferral);
            }
        }

        //
        // Distribute the points from the seeds.
        //
        bool    bDist   = true;

        for (int i = SCORE_ROUNDS; bDist && i--;)
            bDist   = scoreRound (vsnNodes);

        if (ShouldLog (lsTRACE, UniqueNodeList))
        {
            WriteLog (lsTRACE, UniqueNodeList) << "Scored:";
            BOOST_FOREACH (scoreNode & sn, vsnNodes)
            {
                WriteLog (lsTRACE, UniqueNodeList) << str (boost::format ("%s| %d, %d, %d: [%s]")
                                                   % sn.strValidator
                                                   % sn.iScore
                                                   % sn.iRoundScore
                                                   % sn.iRoundSeed
                                                   % strJoin (sn.viReferrals.begin (), sn.viReferrals.end (), ","));
            }
        }

        // Persist validator scores.
        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

        db->executeSQL ("BEGIN;");
        db->executeSQL ("UPDATE TrustedNodes SET Score = 0 WHERE Score != 0;");

        if (!vsnNodes.empty ())
        {
            // Load existing Seens from DB.
            std::vector<std::string>    vstrPublicKeys;

            vstrPublicKeys.resize (vsnNodes.size ());

            for (int iNode = vsnNodes.size (); iNode--;)
            {
                vstrPublicKeys[iNode]   = sqlEscape (vsnNodes[iNode].strValidator);
            }

            SQL_FOREACH (db, str (boost::format ("SELECT PublicKey,Seen FROM TrustedNodes WHERE PublicKey IN (%s);")
                                  % strJoin (vstrPublicKeys.begin (), vstrPublicKeys.end (), ",")))
            {
                vsnNodes[umPulicIdx[db->getStrBinary ("PublicKey")]].iSeen   = db->getNull ("Seen") ? -1 : db->getInt ("Seen");
            }
        }

        boost::unordered_set<std::string>   usUNL;

        if (!vsnNodes.empty ())
        {
            // Update the score old entries and add new entries as needed.
            std::vector<std::string>    vstrValues;

            vstrValues.resize (vsnNodes.size ());

            for (int iNode = vsnNodes.size (); iNode--;)
            {
                scoreNode&  sn      = vsnNodes[iNode];
                std::string strSeen = sn.iSeen >= 0 ? str (boost::format ("%d") % sn.iSeen) : "NULL";

                vstrValues[iNode]   = str (boost::format ("(%s,%s,%s)")
                                           % sqlEscape (sn.strValidator)
                                           % sn.iScore
                                           % strSeen);

                usUNL.insert (sn.strValidator);
            }

            db->executeSQL (str (boost::format ("REPLACE INTO TrustedNodes (PublicKey,Score,Seen) VALUES %s;")
                                 % strJoin (vstrValues.begin (), vstrValues.end (), ",")));
        }

        {
            ScopedUNLLockType sl (mUNLLock);

            // XXX Should limit to scores above a certain minimum and limit to a certain number.
            mUNL.swap (usUNL);
        }

        ripple::unordered_map<std::string, int>  umValidators;

        if (!vsnNodes.empty ())
        {
            std::vector<std::string> vstrPublicKeys;

            // For every IpReferral add a score for the IP and PORT.
            SQL_FOREACH (db, "SELECT Validator,COUNT(*) AS Count FROM IpReferrals GROUP BY Validator;")
            {
                umValidators[db->getStrBinary ("Validator")] = db->getInt ("Count");

                // WriteLog (lsTRACE, UniqueNodeList) << strValidator << ":" << db->getInt("Count");
            }
        }

        // For each validator, get each referral and add its score to ip's score.
        // map of pair<IP,Port> :: score
        epScore umScore;

        typedef ripple::unordered_map<std::string, int>::value_type vcType;
        BOOST_FOREACH (vcType & vc, umValidators)
        {
            std::string strValidator    = vc.first;

            strIndex::iterator  itIndex = umPulicIdx.find (strValidator);

            if (itIndex != umPulicIdx.end ())
            {
                int         iSeed           = vsnNodes[itIndex->second].iScore;
                int         iEntries        = vc.second;
                score       iTotal          = (iEntries + 1) * iEntries / 2;
                score       iBase           = iSeed * iEntries / iTotal;
                int         iEntry          = 0;

                SQL_FOREACH (db, str (boost::format ("SELECT IP,Port FROM IpReferrals WHERE Validator=%s ORDER BY Entry;")
                                      % sqlEscape (strValidator)))
                {
                    score       iPoints = iBase * (iEntries - iEntry) / iEntries;
                    int         iPort;

                    iPort       = db->getNull ("Port") ? -1 : db->getInt ("Port");

                    std::pair< std::string, int>    ep  = std::make_pair (db->getStrBinary ("IP"), iPort);

                    epScore::iterator   itEp    = umScore.find (ep);

                    umScore[ep] = itEp == umScore.end () ? iPoints :  itEp->second + iPoints;
                    iEntry++;
                }
            }
        }

        db->executeSQL ("COMMIT;");
    }

    //--------------------------------------------------------------------------

    // Start a timer to update scores.
    // <-- bNow: true, to force scoring for debugging.
    void scoreNext (bool bNow)
    {
        // WriteLog (lsTRACE, UniqueNodeList) << str(boost::format("scoreNext: mtpFetchUpdated=%s mtpScoreStart=%s mtpScoreUpdated=%s mtpScoreNext=%s") % mtpFetchUpdated % mtpScoreStart % mtpScoreUpdated % mtpScoreNext);
        bool    bCanScore   = mtpScoreStart.is_not_a_date_time ()                           // Not scoring.
                              && !mtpFetchUpdated.is_not_a_date_time ();                  // Something to score.

        bool    bDirty  =
            (mtpScoreUpdated.is_not_a_date_time () || mtpScoreUpdated <= mtpFetchUpdated)   // Not already scored.
            && (mtpScoreNext.is_not_a_date_time ()                                          // Timer is not fine.
                || mtpScoreNext < mtpFetchUpdated + boost::posix_time::seconds (SCORE_DELAY_SECONDS));

        if (bCanScore && (bNow || bDirty))
        {
            // Need to update or set timer.
            double const secondsFromNow = bNow ? 0 : SCORE_DELAY_SECONDS;
            mtpScoreNext = boost::posix_time::second_clock::universal_time ()                   // Past now too.
                           + boost::posix_time::seconds (secondsFromNow);

            // WriteLog (lsTRACE, UniqueNodeList) << str(boost::format("scoreNext: @%s") % mtpScoreNext);
            m_scoreTimer.setExpiration (secondsFromNow);
        }
    }

    //--------------------------------------------------------------------------

    // Given a ripple.txt, process it.
    //
    // VFALCO TODO Can't we take a filename or stream instead of a string?
    //
    bool responseFetch (const std::string& strDomain, const boost::system::error_code& err, int iStatus, const std::string& strSiteFile)
    {
        bool    bReject = !err && iStatus != 200;

        if (!bReject)
        {
            Section             secSite = ParseSection (strSiteFile, true);
            bool                bGood   = !err;

            if (bGood)
            {
                WriteLog (lsTRACE, UniqueNodeList) << boost::format ("Validator: '%s' received " NODE_FILE_NAME ".") % strDomain;
            }
            else
            {
                WriteLog (lsTRACE, UniqueNodeList)
                        << boost::format ("Validator: '%s' unable to retrieve " NODE_FILE_NAME ": %s")
                        % strDomain
                        % err.message ();
            }

            //
            // Verify file domain
            //
            std::string strSite;

            if (bGood && !SectionSingleB (secSite, SECTION_DOMAIN, strSite))
            {
                bGood   = false;

                WriteLog (lsTRACE, UniqueNodeList)
                        << boost::format ("Validator: '%s' bad " NODE_FILE_NAME " missing single entry for " SECTION_DOMAIN ".")
                        % strDomain;
            }

            if (bGood && strSite != strDomain)
            {
                bGood   = false;

                WriteLog (lsTRACE, UniqueNodeList)
                        << boost::format ("Validator: '%s' bad " NODE_FILE_NAME " " SECTION_DOMAIN " does not match: %s")
                        % strDomain
                        % strSite;
            }

            //
            // Process public key
            //
            std::string     strNodePublicKey;

            if (bGood && !SectionSingleB (secSite, SECTION_PUBLIC_KEY, strNodePublicKey))
            {
                // Bad [validation_public_key] Section.
                bGood   = false;

                WriteLog (lsTRACE, UniqueNodeList)
                        << boost::format ("Validator: '%s' bad " NODE_FILE_NAME " " SECTION_PUBLIC_KEY " does not have single entry.")
                        % strDomain;
            }

            RippleAddress   naNodePublic;

            if (bGood && !naNodePublic.setNodePublic (strNodePublicKey))
            {
                // Bad public key.
                bGood   = false;

                WriteLog (lsTRACE, UniqueNodeList)
                        << boost::format ("Validator: '%s' bad " NODE_FILE_NAME " " SECTION_PUBLIC_KEY " is bad: ")
                        % strDomain
                        % strNodePublicKey;
            }

            if (bGood)
            {
                // WriteLog (lsTRACE, UniqueNodeList) << boost::format("naNodePublic: '%s'") % naNodePublic.humanNodePublic();

                seedDomain  sdCurrent;

                bool        bFound      = getSeedDomains (strDomain, sdCurrent);

                assert (bFound);

                uint256     iSha256     = Serializer::getSHA512Half (strSiteFile);
                bool        bChangedB   = sdCurrent.iSha256 != iSha256;

                sdCurrent.strDomain     = strDomain;
                // XXX If the node public key is changing, delete old public key information?
                // XXX Only if no other refs to keep it arround, other wise we have an attack vector.
                sdCurrent.naPublicKey   = naNodePublic;

                // WriteLog (lsTRACE, UniqueNodeList) << boost::format("sdCurrent.naPublicKey: '%s'") % sdCurrent.naPublicKey.humanNodePublic();

                sdCurrent.tpFetch       = boost::posix_time::second_clock::universal_time ();
                sdCurrent.iSha256       = iSha256;

                setSeedDomains (sdCurrent, true);

                if (bChangedB)
                {
                    WriteLog (lsTRACE, UniqueNodeList) << boost::format ("Validator: '%s' processing new " NODE_FILE_NAME ".") % strDomain;
                    processFile (strDomain, naNodePublic, secSite);
                }
                else
                {
                    WriteLog (lsTRACE, UniqueNodeList) << boost::format ("Validator: '%s' no change for " NODE_FILE_NAME ".") % strDomain;
                    fetchFinish ();
                }
            }
            else
            {
                // Failed: Update

                // XXX If we have public key, perhaps try look up in CAS?
                fetchFinish ();
            }
        }

        return bReject;
    }

    //--------------------------------------------------------------------------

    // Try to process the next fetch of a ripple.txt.
    void fetchNext ()
    {
        bool    bFull;

        {
            ScopedFetchLockType sl (mFetchLock);

            bFull = (mFetchActive == NODE_FETCH_JOBS);
        }

        if (!bFull)
        {
            // Determine next scan.
            std::string strDomain;
            boost::posix_time::ptime tpNext (boost::posix_time::min_date_time);
            boost::posix_time::ptime tpNow (boost::posix_time::second_clock::universal_time ());

            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
            Database* db = getApp().getWalletDB ()->getDB ();

            if (db->executeSQL ("SELECT Domain,Next FROM SeedDomains INDEXED BY SeedDomainNext ORDER BY Next LIMIT 1;")
                    && db->startIterRows ())
            {
                int iNext (db->getInt ("Next"));

                tpNext  = ptFromSeconds (iNext);

                WriteLog (lsTRACE, UniqueNodeList) << str (boost::format ("fetchNext: iNext=%s tpNext=%s tpNow=%s") % iNext % tpNext % tpNow);
                strDomain = db->getStrBinary ("Domain");

                db->endIterRows ();
            }

            if (!strDomain.empty ())
            {
                ScopedFetchLockType sl (mFetchLock);

                bFull = (mFetchActive == NODE_FETCH_JOBS);

                if (!bFull && tpNext <= tpNow)
                {
                    mFetchActive++;
                }
            }

            if (strDomain.empty () || bFull)
            {
                WriteLog (lsTRACE, UniqueNodeList) << str (boost::format ("fetchNext: strDomain=%s bFull=%d") % strDomain % bFull);
            }
            else if (tpNext > tpNow)
            {
                WriteLog (lsTRACE, UniqueNodeList) << str (boost::format ("fetchNext: set timer : strDomain=%s") % strDomain);
                // Fetch needs to happen in the future.  Set a timer to wake us.
                mtpFetchNext    = tpNext;

                double seconds = (tpNext - tpNow).seconds ();

                // VFALCO check this.
                if (seconds == 0)
                    seconds = 1;

                m_fetchTimer.setExpiration (seconds);
            }
            else
            {
                WriteLog (lsTRACE, UniqueNodeList) << str (boost::format ("fetchNext: fetch now: strDomain=%s tpNext=%s tpNow=%s") % strDomain % tpNext % tpNow);
                // Fetch needs to happen now.
                mtpFetchNext    = boost::posix_time::ptime (boost::posix_time::not_a_date_time);

                seedDomain  sdCurrent;
                bool        bFound  = getSeedDomains (strDomain, sdCurrent);

                assert (bFound);

                // Update time of next fetch and this scan attempt.
                sdCurrent.tpScan        = tpNow;

                // XXX Use a longer duration if we have lots of validators.
                sdCurrent.tpNext        = sdCurrent.tpScan + boost::posix_time::hours (7 * 24);

                setSeedDomains (sdCurrent, false);

                WriteLog (lsTRACE, UniqueNodeList) << "Validator: '" << strDomain << "' fetching " NODE_FILE_NAME ".";

                fetchProcess (strDomain);   // Go get it.

                fetchNext ();               // Look for more.
            }
        }
    }

    //--------------------------------------------------------------------------

    // Called when we need to update scores.
    void fetchDirty ()
    {
        // Note update.
        mtpFetchUpdated = boost::posix_time::second_clock::universal_time ();
        miscSave ();

        // Update scores.
        scoreNext (false);
    }


    //--------------------------------------------------------------------------

    void fetchFinish ()
    {
        {
            ScopedFetchLockType sl (mFetchLock);
            mFetchActive--;
        }

        fetchNext ();
    }

    //--------------------------------------------------------------------------

    // Get the ripple.txt and process it.
    void fetchProcess (std::string strDomain)
    {
        WriteLog (lsTRACE, UniqueNodeList) << "Fetching '" NODE_FILE_NAME "' from '" << strDomain << "'.";

        std::deque<std::string> deqSites;

        // Order searching from most specifically for purpose to generic.
        // This order allows the client to take the most burden rather than the servers.
        deqSites.push_back (str (boost::format (SYSTEM_NAME ".%s") % strDomain));
        deqSites.push_back (str (boost::format ("www.%s") % strDomain));
        deqSites.push_back (strDomain);

        HTTPClient::get (
            true,
            getApp().getIOService (),
            deqSites,
            443,
            NODE_FILE_PATH,
            NODE_FILE_BYTES_MAX,
            boost::posix_time::seconds (NODE_FETCH_SECONDS),
            std::bind (&UniqueNodeListImp::responseFetch, this, strDomain,
                       std::placeholders::_1, std::placeholders::_2,
                       std::placeholders::_3));
    }

    //--------------------------------------------------------------------------

    void fetchTimerHandler (const boost::system::error_code& err)
    {
        if (!err)
        {
            onDeadlineTimer (m_fetchTimer);
        }
    }


    //--------------------------------------------------------------------------

    // Process Section [validators_url].
    void getValidatorsUrl (const RippleAddress& naNodePublic, Section secSite)
    {
        std::string strValidatorsUrl;
        std::string strScheme;
        std::string strDomain;
        int         iPort;
        std::string strPath;

        if (SectionSingleB (secSite, SECTION_VALIDATORS_URL, strValidatorsUrl)
                && !strValidatorsUrl.empty ()
                && parseUrl (strValidatorsUrl, strScheme, strDomain, iPort, strPath)
                && -1 == iPort
                && strScheme == "https")
        {
            HTTPClient::get (
                true,
                getApp().getIOService (),
                strDomain,
                443,
                strPath,
                NODE_FILE_BYTES_MAX,
                boost::posix_time::seconds (NODE_FETCH_SECONDS),
                std::bind (&UniqueNodeListImp::responseValidators, this,
                           strValidatorsUrl, naNodePublic, secSite, strDomain,
                           std::placeholders::_1, std::placeholders::_2,
                           std::placeholders::_3));
        }
        else
        {
            getIpsUrl (naNodePublic, secSite);
        }
    }

    //--------------------------------------------------------------------------

    // Process Section [ips_url].
    // If we have a Section with a single entry, fetch the url and process it.
    void getIpsUrl (const RippleAddress& naNodePublic, Section secSite)
    {
        std::string strIpsUrl;
        std::string strScheme;
        std::string strDomain;
        int         iPort;
        std::string strPath;

        if (SectionSingleB (secSite, SECTION_IPS_URL, strIpsUrl)
                && !strIpsUrl.empty ()
                && parseUrl (strIpsUrl, strScheme, strDomain, iPort, strPath)
                && -1 == iPort
                && strScheme == "https")
        {
            HTTPClient::get (
                true,
                getApp().getIOService (),
                strDomain,
                443,
                strPath,
                NODE_FILE_BYTES_MAX,
                boost::posix_time::seconds (NODE_FETCH_SECONDS),
                std::bind (&UniqueNodeListImp::responseIps, this, strDomain,
                           naNodePublic, std::placeholders::_1,
                           std::placeholders::_2, std::placeholders::_3));
        }
        else
        {
            fetchFinish ();
        }
    }


    //--------------------------------------------------------------------------

    // Given a Section with IPs, parse and persist it for a validator.
    bool responseIps (const std::string& strSite, const RippleAddress& naNodePublic, const boost::system::error_code& err, int iStatus, const std::string& strIpsFile)
    {
        bool    bReject = !err && iStatus != 200;

        if (!bReject)
        {
            if (!err)
            {
                Section         secFile     = ParseSection (strIpsFile, true);

                processIps (strSite, naNodePublic, SectionEntries (secFile, SECTION_IPS));
            }

            fetchFinish ();
        }

        return bReject;
    }

    // After fetching a ripple.txt from a web site, given a Section with validators, parse and persist it.
    bool responseValidators (const std::string& strValidatorsUrl, const RippleAddress& naNodePublic, Section secSite, const std::string& strSite, const boost::system::error_code& err, int iStatus, const std::string& strValidatorsFile)
    {
        bool    bReject = !err && iStatus != 200;

        if (!bReject)
        {
            if (!err)
            {
                Section     secFile     = ParseSection (strValidatorsFile, true);

                processValidators (strSite, strValidatorsUrl, naNodePublic, vsValidator, SectionEntries (secFile, SECTION_VALIDATORS));
            }

            getIpsUrl (naNodePublic, secSite);
        }

        return bReject;
    }


    //--------------------------------------------------------------------------

    // Persist the IPs refered to by a Validator.
    // --> strSite: source of the IPs (for debugging)
    // --> naNodePublic: public key of the validating node.
    void processIps (const std::string& strSite, const RippleAddress& naNodePublic, Section::mapped_type* pmtVecStrIps)
    {
        Database*   db = getApp().getWalletDB ()->getDB ();

        std::string strEscNodePublic    = sqlEscape (naNodePublic.humanNodePublic ());

        WriteLog (lsDEBUG, UniqueNodeList)
                << str (boost::format ("Validator: '%s' processing %d ips.")
                        % strSite % ( pmtVecStrIps ? pmtVecStrIps->size () : 0));

        // Remove all current Validator's entries in IpReferrals
        {
            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
            db->executeSQL (str (boost::format ("DELETE FROM IpReferrals WHERE Validator=%s;") % strEscNodePublic));
            // XXX Check result.
        }

        // Add new referral entries.
        if (pmtVecStrIps && !pmtVecStrIps->empty ())
        {
            std::vector<std::string>    vstrValues;

            vstrValues.resize (std::min ((int) pmtVecStrIps->size (), REFERRAL_IPS_MAX));

            int iValues = 0;
            BOOST_FOREACH (const std::string & strReferral, *pmtVecStrIps)
            {
                if (iValues == REFERRAL_VALIDATORS_MAX)
                    break;

                std::string     strIP;
                int             iPort;
                bool            bValid  = parseIpPort (strReferral, strIP, iPort);

                // XXX Filter out private network ips.
                // XXX http://en.wikipedia.org/wiki/Private_network

                if (bValid)
                {
                    vstrValues[iValues] = str (boost::format ("(%s,%d,%s,%d)")
                                               % strEscNodePublic % iValues % sqlEscape (strIP) % iPort);
                    iValues++;
                }
                else
                {
                    WriteLog (lsTRACE, UniqueNodeList)
                            << str (boost::format ("Validator: '%s' [" SECTION_IPS "]: rejecting '%s'")
                                    % strSite % strReferral);
                }
            }

            if (iValues)
            {
                vstrValues.resize (iValues);

                DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());
                db->executeSQL (str (boost::format ("INSERT INTO IpReferrals (Validator,Entry,IP,Port) VALUES %s;")
                                     % strJoin (vstrValues.begin (), vstrValues.end (), ",")));
                // XXX Check result.
            }
        }

        fetchDirty ();
    }

    //--------------------------------------------------------------------------

    // Persist ValidatorReferrals.
    // --> strSite: source site for display
    // --> strValidatorsSrc: source details for display
    // --> naNodePublic: remote source public key - not valid for local
    // --> vsWhy: reason for adding validator to SeedDomains or SeedNodes.
    int processValidators (const std::string& strSite, const std::string& strValidatorsSrc, const RippleAddress& naNodePublic, ValidatorSource vsWhy, Section::mapped_type* pmtVecStrValidators)
    {
        Database*   db              = getApp().getWalletDB ()->getDB ();
        std::string strNodePublic   = naNodePublic.isValid () ? naNodePublic.humanNodePublic () : strValidatorsSrc;
        int         iValues         = 0;

        WriteLog (lsTRACE, UniqueNodeList)
                << str (boost::format ("Validator: '%s' : '%s' : processing %d validators.")
                        % strSite
                        % strValidatorsSrc
                        % ( pmtVecStrValidators ? pmtVecStrValidators->size () : 0));

        // Remove all current Validator's entries in ValidatorReferrals
        {
            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

            db->executeSQL (str (boost::format ("DELETE FROM ValidatorReferrals WHERE Validator='%s';") % strNodePublic));
            // XXX Check result.
        }

        // Add new referral entries.
        if (pmtVecStrValidators && pmtVecStrValidators->size ())
        {
            std::vector<std::string> vstrValues;

            vstrValues.reserve (std::min ((int) pmtVecStrValidators->size (), REFERRAL_VALIDATORS_MAX));

            BOOST_FOREACH (const std::string & strReferral, *pmtVecStrValidators)
            {
                if (iValues == REFERRAL_VALIDATORS_MAX)
                    break;

                boost::smatch   smMatch;

                // domain comment?
                // public_key comment?
                static boost::regex reReferral ("\\`\\s*(\\S+)(?:\\s+(.+))?\\s*\\'");

                if (!boost::regex_match (strReferral, smMatch, reReferral))
                {
                    WriteLog (lsWARNING, UniqueNodeList) << str (boost::format ("Bad validator: syntax error: %s: %s") % strSite % strReferral);
                }
                else
                {
                    std::string     strRefered  = smMatch[1];
                    std::string     strComment  = smMatch[2];
                    RippleAddress   naValidator;

                    if (naValidator.setSeedGeneric (strRefered))
                    {
                        WriteLog (lsWARNING, UniqueNodeList) << str (boost::format ("Bad validator: domain or public key required: %s %s") % strRefered % strComment);
                    }
                    else if (naValidator.setNodePublic (strRefered))
                    {
                        // A public key.
                        // XXX Schedule for CAS lookup.
                        nodeAddPublic (naValidator, vsWhy, strComment);

                        WriteLog (lsINFO, UniqueNodeList) << str (boost::format ("Node Public: %s %s") % strRefered % strComment);

                        if (naNodePublic.isValid ())
                            vstrValues.push_back (str (boost::format ("('%s',%d,'%s')") % strNodePublic % iValues % naValidator.humanNodePublic ()));

                        iValues++;
                    }
                    else
                    {
                        // A domain: need to look it up.
                        nodeAddDomain (strRefered, vsWhy, strComment);

                        WriteLog (lsINFO, UniqueNodeList) << str (boost::format ("Node Domain: %s %s") % strRefered % strComment);

                        if (naNodePublic.isValid ())
                            vstrValues.push_back (str (boost::format ("('%s',%d,%s)") % strNodePublic % iValues % sqlEscape (strRefered)));

                        iValues++;
                    }
                }
            }

            if (!vstrValues.empty ())
            {
                std::string strSql  = str (boost::format ("INSERT INTO ValidatorReferrals (Validator,Entry,Referral) VALUES %s;")
                                           % strJoin (vstrValues.begin (), vstrValues.end (), ","));

                DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

                db->executeSQL (strSql);
                // XXX Check result.
            }
        }

        fetchDirty ();

        return iValues;
    }

    //--------------------------------------------------------------------------

    // Process a ripple.txt.
    void processFile (const std::string& strDomain, const RippleAddress& naNodePublic, Section secSite)
    {
        //
        // Process Validators
        //
        processValidators (strDomain, NODE_FILE_NAME, naNodePublic, vsReferral, SectionEntries (secSite, SECTION_VALIDATORS));

        //
        // Process ips
        //
        processIps (strDomain, naNodePublic, SectionEntries (secSite, SECTION_IPS));

        //
        // Process currencies
        //
        Section::mapped_type*   pvCurrencies;

        if ((pvCurrencies = SectionEntries (secSite, SECTION_CURRENCIES)) && pvCurrencies->size ())
        {
            // XXX Process currencies.
            WriteLog (lsWARNING, UniqueNodeList) << "Ignoring currencies: not implemented.";
        }

        getValidatorsUrl (naNodePublic, secSite);
    }

    //--------------------------------------------------------------------------

    // Retrieve a SeedDomain from DB.
    bool getSeedDomains (const std::string& strDomain, seedDomain& dstSeedDomain)
    {
        bool        bResult;
        Database*   db = getApp().getWalletDB ()->getDB ();

        std::string strSql  = boost::str (boost::format ("SELECT * FROM SeedDomains WHERE Domain=%s;")
                                          % sqlEscape (strDomain));

        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

        bResult = db->executeSQL (strSql) && db->startIterRows ();

        if (bResult)
        {
            std::string     strPublicKey;
            int             iNext;
            int             iScan;
            int             iFetch;
            std::string     strSha256;

            dstSeedDomain.strDomain = db->getStrBinary ("Domain");

            if (!db->getNull ("PublicKey") && db->getStr ("PublicKey", strPublicKey))
            {
                dstSeedDomain.naPublicKey.setNodePublic (strPublicKey);
            }
            else
            {
                dstSeedDomain.naPublicKey.clear ();
            }

            std::string     strSource   = db->getStrBinary ("Source");
            dstSeedDomain.vsSource  = static_cast<ValidatorSource> (strSource[0]);

            iNext   = db->getInt ("Next");
            dstSeedDomain.tpNext    = ptFromSeconds (iNext);
            iScan   = db->getInt ("Scan");
            dstSeedDomain.tpScan    = ptFromSeconds (iScan);
            iFetch  = db->getInt ("Fetch");
            dstSeedDomain.tpFetch   = ptFromSeconds (iFetch);

            if (!db->getNull ("Sha256") && db->getStr ("Sha256", strSha256))
            {
                dstSeedDomain.iSha256.SetHex (strSha256);
            }
            else
            {
                dstSeedDomain.iSha256.zero ();
            }

            dstSeedDomain.strComment    = db->getStrBinary ("Comment");

            db->endIterRows ();
        }

        return bResult;
    }

    //--------------------------------------------------------------------------

    // Persist a SeedDomain.
    void setSeedDomains (const seedDomain& sdSource, bool bNext)
    {
        Database*   db = getApp().getWalletDB ()->getDB ();

        int     iNext   = iToSeconds (sdSource.tpNext);
        int     iScan   = iToSeconds (sdSource.tpScan);
        int     iFetch  = iToSeconds (sdSource.tpFetch);

        // WriteLog (lsTRACE) << str(boost::format("setSeedDomains: iNext=%s tpNext=%s") % iNext % sdSource.tpNext);

        std::string strSql  = boost::str (boost::format ("REPLACE INTO SeedDomains (Domain,PublicKey,Source,Next,Scan,Fetch,Sha256,Comment) VALUES (%s, %s, %s, %d, %d, %d, '%s', %s);")
                                          % sqlEscape (sdSource.strDomain)
                                          % (sdSource.naPublicKey.isValid () ? sqlEscape (sdSource.naPublicKey.humanNodePublic ()) : "NULL")
                                          % sqlEscape (std::string (1, static_cast<char> (sdSource.vsSource)))
                                          % iNext
                                          % iScan
                                          % iFetch
                                          % to_string (sdSource.iSha256)
                                          % sqlEscape (sdSource.strComment)
                                         );

        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

        if (!db->executeSQL (strSql))
        {
            // XXX Check result.
            WriteLog (lsWARNING, UniqueNodeList) << "setSeedDomains: failed.";
        }

        if (bNext && (mtpFetchNext.is_not_a_date_time () || mtpFetchNext > sdSource.tpNext))
        {
            // Schedule earlier wake up.
            fetchNext ();
        }
    }


    //--------------------------------------------------------------------------

    // Retrieve a SeedNode from DB.
    bool getSeedNodes (const RippleAddress& naNodePublic, seedNode& dstSeedNode)
    {
        bool        bResult;
        Database*   db = getApp().getWalletDB ()->getDB ();

        std::string strSql  = str (boost::format ("SELECT * FROM SeedNodes WHERE PublicKey='%s';")
                                   % naNodePublic.humanNodePublic ());

        DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

        bResult = db->executeSQL (strSql) && db->startIterRows ();

        if (bResult)
        {
            std::string     strPublicKey;
            std::string     strSource;
            int             iNext;
            int             iScan;
            int             iFetch;
            std::string     strSha256;

            if (!db->getNull ("PublicKey") && db->getStr ("PublicKey", strPublicKey))
            {
                dstSeedNode.naPublicKey.setNodePublic (strPublicKey);
            }
            else
            {
                dstSeedNode.naPublicKey.clear ();
            }

            strSource   = db->getStrBinary ("Source");
            dstSeedNode.vsSource    = static_cast<ValidatorSource> (strSource[0]);

            iNext   = db->getInt ("Next");
            dstSeedNode.tpNext  = ptFromSeconds (iNext);
            iScan   = db->getInt ("Scan");
            dstSeedNode.tpScan  = ptFromSeconds (iScan);
            iFetch  = db->getInt ("Fetch");
            dstSeedNode.tpFetch = ptFromSeconds (iFetch);

            if (!db->getNull ("Sha256") && db->getStr ("Sha256", strSha256))
            {
                dstSeedNode.iSha256.SetHex (strSha256);
            }
            else
            {
                dstSeedNode.iSha256.zero ();
            }

            dstSeedNode.strComment  = db->getStrBinary ("Comment");

            db->endIterRows ();
        }

        return bResult;
    }

    //--------------------------------------------------------------------------

    // Persist a SeedNode.
    // <-- bNext: true, to do fetching if needed.
    void setSeedNodes (const seedNode& snSource, bool bNext)
    {
        Database*   db = getApp().getWalletDB ()->getDB ();

        int     iNext   = iToSeconds (snSource.tpNext);
        int     iScan   = iToSeconds (snSource.tpScan);
        int     iFetch  = iToSeconds (snSource.tpFetch);

        // WriteLog (lsTRACE) << str(boost::format("setSeedNodes: iNext=%s tpNext=%s") % iNext % sdSource.tpNext);

        assert (snSource.naPublicKey.isValid ());

        std::string strSql  = str (boost::format ("REPLACE INTO SeedNodes (PublicKey,Source,Next,Scan,Fetch,Sha256,Comment) VALUES ('%s', '%c', %d, %d, %d, '%s', %s);")
                                   % snSource.naPublicKey.humanNodePublic ()
                                   % static_cast<char> (snSource.vsSource)
                                   % iNext
                                   % iScan
                                   % iFetch
                                   % to_string (snSource.iSha256)
                                   % sqlEscape (snSource.strComment)
                                  );

        {
            DeprecatedScopedLock sl (getApp().getWalletDB ()->getDBLock ());

            if (!db->executeSQL (strSql))
            {
                // XXX Check result.
                WriteLog (lsTRACE, UniqueNodeList) << "setSeedNodes: failed.";
            }
        }

    #if 0

        // YYY When we have a cas schedule lookups similar to this.
        if (bNext && (mtpFetchNext.is_not_a_date_time () || mtpFetchNext > snSource.tpNext))
        {
            // Schedule earlier wake up.
            fetchNext ();
        }

    #else
        fetchDirty ();
    #endif
    }

    //--------------------------------------------------------------------------

    bool validatorsResponse (const boost::system::error_code& err, int iStatus, std::string strResponse)
    {
        bool    bReject = !err && iStatus != 200;

        if (!bReject)
        {
            WriteLog (lsTRACE, UniqueNodeList) <<
                "Fetch '" <<
                Config::Helpers::getValidatorsFileName () <<
                "' complete.";

            if (!err)
            {
                nodeProcess ("network", strResponse, getConfig ().VALIDATORS_SITE);
            }
            else
            {
                WriteLog (lsWARNING, UniqueNodeList) << "Error: " << err.message ();
            }
        }

        return bReject;
    }

    //--------------------------------------------------------------------------

    // Process a validators.txt.
    // --> strSite: source of validators
    // --> strValidators: contents of a validators.txt
    //
    // VFALCO TODO Can't we name this processValidatorList?
    //
    void nodeProcess (const std::string& strSite, const std::string& strValidators, const std::string& strSource)
    {
        Section secValidators   = ParseSection (strValidators, true);

        Section::mapped_type*   pmtEntries  = SectionEntries (secValidators, SECTION_VALIDATORS);

        if (pmtEntries)
        {
            RippleAddress   naInvalid;  // Don't want a referrer on added entries.

            // YYY Unspecified might be bootstrap or rpc command
            processValidators (strSite, strSource, naInvalid, vsValidator, pmtEntries);
        }
        else
        {
            WriteLog (lsWARNING, UniqueNodeList) << boost::str (boost::format ("'%s' missing [" SECTION_VALIDATORS "].")
                                                 % getConfig ().VALIDATORS_BASE);
        }
    }
private:
    typedef RippleMutex FetchLockType;
    typedef std::lock_guard <FetchLockType> ScopedFetchLockType;
    FetchLockType mFetchLock;

    typedef RippleRecursiveMutex UNLLockType;
    typedef std::lock_guard <UNLLockType> ScopedUNLLockType;
    UNLLockType mUNLLock;

    // VFALCO TODO Replace ptime with beast::Time
    // Misc persistent information
    boost::posix_time::ptime        mtpScoreUpdated;
    boost::posix_time::ptime        mtpFetchUpdated;

    // XXX Make this faster, make this the contents vector unsigned char or raw public key.
    // XXX Contents needs to based on score.
    boost::unordered_set<std::string>   mUNL;

    boost::posix_time::ptime        mtpScoreNext;       // When to start scoring.
    boost::posix_time::ptime        mtpScoreStart;      // Time currently started scoring.
    beast::DeadlineTimer m_scoreTimer;                  // Timer to start scoring.

    int                             mFetchActive;       // Count of active fetches.

    boost::posix_time::ptime        mtpFetchNext;       // Time of to start next fetch.
    beast::DeadlineTimer m_fetchTimer;                  // Timer to start fetching.

    std::map<RippleAddress, ClusterNodeStatus> m_clusterNodes;
};

//------------------------------------------------------------------------------

UniqueNodeList::UniqueNodeList (Stoppable& parent)
    : Stoppable ("UniqueNodeList", parent)
{
}

//------------------------------------------------------------------------------

UniqueNodeList* UniqueNodeList::New (Stoppable& parent)
{
    return new UniqueNodeListImp (parent);
}

} // ripple
