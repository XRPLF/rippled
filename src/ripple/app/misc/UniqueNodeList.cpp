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

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/main/LocalCredentials.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/UniqueNodeList.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/digest.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/Time.h>
#include <ripple/core/Config.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/crypto/Base58.h>
#include <ripple/net/HTTPClient.h>
#include <ripple/protocol/JsonFields.h>
#include <beast/module/core/thread/DeadlineTimer.h>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <boost/format.hpp>
#include <boost/regex.hpp>
#include <boost/optional.hpp>
#include <fstream>
#include <memory>
#include <mutex>

namespace ripple {

// XXX Dynamically limit fetching by distance.
// XXX Want a limit of 2000 validators.

// Guarantees minimum throughput of 1 node per second.
#define NODE_FETCH_JOBS         10
#define NODE_FETCH_SECONDS      10
#define NODE_FILE_BYTES_MAX     (50<<10)    // 50k

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

template<class Iterator>
static
std::string
strJoin (Iterator first, Iterator last, std::string strSeparator)
{
    std::ostringstream ossValues;

    for (Iterator start = first; first != last; ++first)
    {
        ossValues << str (boost::format ("%s%s") % (start == first ? "" : strSeparator) % *first);
    }

    return ossValues.str ();
}

template <size_t I, class String>
void selectBlobsIntoStrings (
    soci::session& s,
    String&& sql,
    std::vector<std::array<boost::optional<std::string>, I>>& columns)
{
    columns.clear ();
    columns.reserve (32);

    std::vector<soci::blob> blobs;
    blobs.reserve (I);
    for (int i = 0; i < I; ++i)
        blobs.emplace_back (s);
    std::array<soci::indicator, I> indicators;
    std::string str;
    soci::statement st = [&]
    {
        auto&& tmp = s.prepare << sql;
        for (int i = 0; i < I; ++i)
            tmp.operator, (soci::into (blobs[i], indicators[i]));
        return tmp;
    }();

    st.execute ();
    while (st.fetch ())
    {
        columns.emplace_back ();
        for (int i = 0; i < I; ++i)
        {
            if (soci::i_ok == indicators[i])
            {
                convert (blobs[i], str);
                columns.back ()[i] = str;
            }
        }
    }
}

template<class TOther, class String>
void selectBlobsIntoStrings (
    soci::session& s,
    String&& sql,
    std::vector<std::tuple<boost::optional<std::string>, boost::optional<TOther>>>& columns)
{
    columns.clear ();
    columns.reserve (32);

    soci::blob blob(s);
    soci::indicator ind;
    boost::optional<TOther> other;
    std::string str;
    soci::statement st =
            (s.prepare << sql, soci::into(blob, ind), soci::into(other));

    st.execute ();
    while (st.fetch ())
    {
        columns.emplace_back ();
        if (soci::i_ok == ind)
        {
            convert (blob, str);
            get<0>(columns.back ()) = str;
        }
        get<1>(columns.back ()) = other;
    }
}

//------------------------------------------------------------------------------

class UniqueNodeListImp
    : public UniqueNodeList
    , public beast::DeadlineTimer::Listener
{
private:
    struct seedDomain
    {
        std::string                 strDomain;
        RippleAddress               naPublicKey;
        ValidatorSource             vsSource;
        boost::posix_time::ptime    tpNext;
        boost::posix_time::ptime    tpScan;
        boost::posix_time::ptime    tpFetch;
        uint256                     iSha256;
        std::string                 strComment;
    };

    struct seedNode
    {
        RippleAddress               naPublicKey;
        ValidatorSource             vsSource;
        boost::posix_time::ptime    tpNext;
        boost::posix_time::ptime    tpScan;
        boost::posix_time::ptime    tpFetch;
        uint256                     iSha256;
        std::string                 strComment;
    };

    // Used to distribute scores.
    struct scoreNode
    {
        int                 iScore;
        int                 iRoundScore;
        int                 iRoundSeed;
        int                 iSeen;
        std::string         strValidator;   // The public key.
        std::vector<int>    viReferrals;
    };

private:
    Application& app_;

    std::mutex mFetchLock;
    std::recursive_mutex mUNLLock;

    // VFALCO TODO Replace ptime with beast::Time
    // Misc persistent information
    boost::posix_time::ptime        mtpScoreUpdated;
    boost::posix_time::ptime        mtpFetchUpdated;

    // XXX Make this faster, make this the contents vector unsigned char or raw public key.
    // XXX Contents needs to based on score.
    hash_set<std::string>   mUNL;
    hash_map<PublicKey, std::string>  ephemeralValidatorKeys_;

    boost::posix_time::ptime        mtpScoreNext;       // When to start scoring.
    boost::posix_time::ptime        mtpScoreStart;      // Time currently started scoring.
    beast::DeadlineTimer m_scoreTimer;                  // Timer to start scoring.

    int                             mFetchActive;       // Count of active fetches.

    boost::posix_time::ptime        mtpFetchNext;       // Time of to start next fetch.
    beast::DeadlineTimer m_fetchTimer;                  // Timer to start fetching.

    std::string node_file_name_;
    std::string node_file_path_;

    beast::Journal j_;
public:
    UniqueNodeListImp (Application& app, Stoppable& parent);

    void onStop();

    void doScore();

    void doFetch();

    void onDeadlineTimer (beast::DeadlineTimer& timer);

    // This is called when the application is started.
    // Get update times and start fetching and scoring as needed.
    void start();

    void insertEphemeralKey (PublicKey pk, std::string comment);
    void deleteEphemeralKey (PublicKey const& pk);

    // Add a trusted node.  Called by RPC or other source.
    void nodeAddPublic (RippleAddress const& naNodePublic, ValidatorSource vsWhy, std::string const& strComment);

    // Queue a domain for a single attempt fetch a ripple.txt.
    // --> strComment: only used on vsManual
    // YYY As a lot of these may happen at once, would be nice to wrap multiple calls in a transaction.
    void nodeAddDomain (std::string strDomain, ValidatorSource vsWhy, std::string const& strComment);

    void nodeRemovePublic (RippleAddress const& naNodePublic);

    void nodeRemoveDomain (std::string strDomain);

    void nodeReset();

    // For debugging, schedule forced scoring.
    void nodeScore();

    bool nodeInUNL (RippleAddress const& naNodePublic);

    void nodeBootstrap();

    bool nodeLoad (boost::filesystem::path pConfig);

    void nodeNetwork();

    Json::Value getUnlJson();

    // For each kind of source, have a starting number of points to be distributed.
    int iSourceScore (ValidatorSource vsWhy);

    //--------------------------------------------------------------------------
private:
    // Load information about when we last updated.
    bool miscLoad();

    // Persist update information.
    bool miscSave();

    void trustedLoad();

    // For a round of scoring we destribute points from a node to nodes it refers to.
    // Returns true, iff scores were distributed.
    //
    bool scoreRound (std::vector<scoreNode>& vsnNodes);

    // From SeedDomains and ValidatorReferrals compute scores and update TrustedNodes.
    //
    // VFALCO TODO Shrink this function, break it up
    //
    void scoreCompute();

    // Start a timer to update scores.
    // <-- bNow: true, to force scoring for debugging.
    void scoreNext (bool bNow);

    // Given a ripple.txt, process it.
    //
    // VFALCO TODO Can't we take a filename or stream instead of a string?
    //
    bool responseFetch (std::string const& strDomain, const boost::system::error_code& err, int iStatus, std::string const& strSiteFile);

    // Try to process the next fetch of a ripple.txt.
    void fetchNext();

    // Called when we need to update scores.
    void fetchDirty();


    void fetchFinish();

    // Get the ripple.txt and process it.
    void fetchProcess (std::string strDomain);

    // Process IniFileSections [validators_url].
    void getValidatorsUrl (RippleAddress const& naNodePublic,
        IniFileSections secSite);

    // Process IniFileSections [ips_url].
    // If we have a IniFileSections with a single entry, fetch the url and process it.
    void getIpsUrl (RippleAddress const& naNodePublic, IniFileSections secSite);


    // Given a IniFileSections with IPs, parse and persist it for a validator.
    bool responseIps (std::string const& strSite, RippleAddress const& naNodePublic, const boost::system::error_code& err, int iStatus, std::string const& strIpsFile);;

    // After fetching a ripple.txt from a web site, given a IniFileSections with validators, parse and persist it.
    bool responseValidators (std::string const& strValidatorsUrl, RippleAddress const& naNodePublic, IniFileSections secSite, std::string const& strSite, const boost::system::error_code& err, int iStatus, std::string const& strValidatorsFile);


    // Persist the IPs refered to by a Validator.
    // --> strSite: source of the IPs (for debugging)
    // --> naNodePublic: public key of the validating node.
    void processIps (std::string const& strSite, RippleAddress const& naNodePublic, IniFileSections::mapped_type* pmtVecStrIps);

    // Persist ValidatorReferrals.
    // --> strSite: source site for display
    // --> strValidatorsSrc: source details for display
    // --> naNodePublic: remote source public key - not valid for local
    // --> vsWhy: reason for adding validator to SeedDomains or SeedNodes.
    int processValidators (std::string const& strSite, std::string const& strValidatorsSrc, RippleAddress const& naNodePublic, ValidatorSource vsWhy, IniFileSections::mapped_type const* pmtVecStrValidators);

    // Process a ripple.txt.
    void processFile (std::string const& strDomain, RippleAddress const& naNodePublic, IniFileSections secSite);

    // Retrieve a SeedDomain from DB.
    bool getSeedDomains (std::string const& strDomain, seedDomain& dstSeedDomain);

    // Persist a SeedDomain.
    void setSeedDomains (const seedDomain& sdSource, bool bNext);


    // Retrieve a SeedNode from DB.
    bool getSeedNodes (RippleAddress const& naNodePublic, seedNode& dstSeedNode);

    // Persist a SeedNode.
    // <-- bNext: true, to do fetching if needed.
    void setSeedNodes (const seedNode& snSource, bool bNext);

    bool validatorsResponse (const boost::system::error_code& err, int iStatus, std::string strResponse);

    // Process a validators.txt.
    // --> strSite: source of validators
    // --> strValidators: contents of a validators.txt
    //
    // VFALCO TODO Can't we name this processValidatorList?
    //
    void nodeProcess (std::string const& strSite, std::string const& strValidators, std::string const& strSource);
};

//------------------------------------------------------------------------------

UniqueNodeList::UniqueNodeList (Stoppable& parent)
    : Stoppable ("UniqueNodeList", parent)
{
}

//------------------------------------------------------------------------------

UniqueNodeListImp::UniqueNodeListImp (Application& app, Stoppable& parent)
    : UniqueNodeList (parent)
    , app_ (app)
    , m_scoreTimer (this)
    , mFetchActive (0)
    , m_fetchTimer (this)
    , j_ (app.journal ("UniqueNodeList"))
{
    node_file_name_ = std::string (systemName ()) + ".txt";
    node_file_path_ = "/" + node_file_name_;
}

void UniqueNodeListImp::onStop()
{
    m_fetchTimer.cancel ();
    m_scoreTimer.cancel ();

    stopped ();
}

void UniqueNodeListImp::doScore()
{
    mtpScoreNext    = boost::posix_time::ptime (boost::posix_time::not_a_date_time); // Timer not set.
    mtpScoreStart   = boost::posix_time::second_clock::universal_time ();           // Scoring.

    JLOG (j_.trace) << "Scoring: Start";

    scoreCompute ();

    JLOG (j_.trace) << "Scoring: End";

    // Save update time.
    mtpScoreUpdated = mtpScoreStart;
    miscSave ();

    mtpScoreStart   = boost::posix_time::ptime (boost::posix_time::not_a_date_time); // Not scoring.

    // Score again if needed.
    scoreNext (false);
}

void UniqueNodeListImp::doFetch()
{
    // Time to check for another fetch.
    JLOG (j_.trace) << "fetchTimerHandler";
    fetchNext ();
}

void UniqueNodeListImp::onDeadlineTimer (beast::DeadlineTimer& timer)
{
    if (timer == m_scoreTimer)
    {
        app_.getJobQueue ().addJob (
            jtUNL, "UNL.score",
            [this] (Job&) { doScore(); });
    }
    else if (timer == m_fetchTimer)
    {
        app_.getJobQueue ().addJob (jtUNL, "UNL.fetch",
            [this] (Job&) { doFetch(); });
    }
}

// This is called when the application is started.
// Get update times and start fetching and scoring as needed.
void UniqueNodeListImp::start()
{
    miscLoad ();

    JLOG (j_.debug) << "Validator fetch updated: " << mtpFetchUpdated;
    JLOG (j_.debug) << "Validator score updated: " << mtpScoreUpdated;

    fetchNext ();           // Start fetching.
    scoreNext (false);      // Start scoring.
}

//--------------------------------------------------------------------------

void UniqueNodeListImp::insertEphemeralKey (PublicKey pk, std::string comment)
{
    std::lock_guard <std::recursive_mutex> sl (mUNLLock);

    ephemeralValidatorKeys_.insert (std::make_pair(std::move(pk), std::move(comment)));
}

void UniqueNodeListImp::deleteEphemeralKey (PublicKey const& pk)
{
    std::lock_guard <std::recursive_mutex> sl (mUNLLock);

    ephemeralValidatorKeys_.erase (pk);
}

//--------------------------------------------------------------------------

// Add a trusted node.  Called by RPC or other source.
void UniqueNodeListImp::nodeAddPublic (RippleAddress const& naNodePublic, ValidatorSource vsWhy, std::string const& strComment)
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
void UniqueNodeListImp::nodeAddDomain (std::string strDomain, ValidatorSource vsWhy, std::string const& strComment)
{
    boost::trim (strDomain);
    boost::to_lower (strDomain);

    // YYY Would be best to verify strDomain is a valid domain.
    // JLOG (lsTRACE) << str(boost::format("nodeAddDomain: '%s' %c '%s'")
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

void UniqueNodeListImp::nodeRemovePublic (RippleAddress const& naNodePublic)
{
    {
        auto db = app_.getWalletDB ().checkoutDb ();

        *db << str (
            boost::format ("DELETE FROM SeedNodes WHERE PublicKey=%s;") %
            sqlEscape (naNodePublic.humanNodePublic ()));
        *db << str (
            boost::format ("DELETE FROM TrustedNodes WHERE PublicKey=%s;") %
            sqlEscape (naNodePublic.humanNodePublic ()));
    }

    // YYY Only dirty on successful delete.
    fetchDirty ();

    std::lock_guard <std::recursive_mutex> sl (mUNLLock);
    mUNL.erase (naNodePublic.humanNodePublic ());
}

//--------------------------------------------------------------------------

void UniqueNodeListImp::nodeRemoveDomain (std::string strDomain)
{
    boost::trim (strDomain);
    boost::to_lower (strDomain);

    {
        auto db = app_.getWalletDB ().checkoutDb ();

        *db << str (boost::format ("DELETE FROM SeedDomains WHERE Domain=%s;") % sqlEscape (strDomain));
    }

    // YYY Only dirty on successful delete.
    fetchDirty ();
}

//--------------------------------------------------------------------------

void UniqueNodeListImp::nodeReset()
{
    {
        auto db = app_.getWalletDB ().checkoutDb ();

        *db << "DELETE FROM SeedDomains;";
        *db << "DELETE FROM SeedNodes;";
    }

    fetchDirty ();
}

//--------------------------------------------------------------------------

// For debugging, schedule forced scoring.
void UniqueNodeListImp::nodeScore()
{
    scoreNext (true);
}

//--------------------------------------------------------------------------

bool UniqueNodeListImp::nodeInUNL (RippleAddress const& naNodePublic)
{
    auto const& blob = naNodePublic.getNodePublic();
    PublicKey const pk (Slice(blob.data(), blob.size()));

    std::lock_guard <std::recursive_mutex> sl (mUNLLock);

    if (ephemeralValidatorKeys_.find (pk) != ephemeralValidatorKeys_.end())
    {
        return true;
    }

    return mUNL.find (naNodePublic.humanNodePublic()) != mUNL.end();
}

//--------------------------------------------------------------------------

void UniqueNodeListImp::nodeBootstrap()
{
    int         iDomains    = 0;
    int         iNodes      = 0;

#if 0
    {
        auto sl (app_.getWalletDB ().lock ());
        auto db = app_.getWalletDB ().getDB ();

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
    if (!app_.config().VALIDATORS_FILE.empty ())
    {
        JLOG (j_.info) << "Bootstrapping UNL: loading from unl_default.";

        bLoaded = nodeLoad (app_.config().VALIDATORS_FILE);
    }

    // If never loaded anything try the current directory.
    if (!bLoaded && app_.config().VALIDATORS_FILE.empty ())
    {
        JLOG (j_.info) << boost::str (boost::format ("Bootstrapping UNL: loading from '%s'.")
                                          % app_.config().VALIDATORS_BASE);

        bLoaded = nodeLoad (app_.config().VALIDATORS_BASE);
    }

    // Always load from rippled.cfg
    if (!app_.config().validators.empty ())
    {
        RippleAddress   naInvalid;  // Don't want a referrer on added entries.

        JLOG (j_.info) << boost::str (boost::format ("Bootstrapping UNL: loading from '%s'.")
                                          % app_.config().CONFIG_FILE);

        if (processValidators ("local",
            app_.config().CONFIG_FILE.string (), naInvalid,
            vsConfig, &(app_.config().validators)))
            bLoaded = true;
    }

    if (!bLoaded)
    {
        JLOG (j_.info) << boost::str (boost::format ("Bootstrapping UNL: loading from '%s'.")
                                          % app_.config().VALIDATORS_SITE);

        nodeNetwork ();
    }
}

//--------------------------------------------------------------------------

bool UniqueNodeListImp::nodeLoad (boost::filesystem::path pConfig)
{
    if (pConfig.empty ())
    {
        JLOG (j_.info) << Config::Helpers::getValidatorsFileName() <<
            " path not specified.";

        return false;
    }

    if (!boost::filesystem::exists (pConfig))
    {
        JLOG (j_.warning) << Config::Helpers::getValidatorsFileName() <<
            " not found: " << pConfig;

        return false;
    }

    if (!boost::filesystem::is_regular_file (pConfig))
    {
        JLOG (j_.warning) << Config::Helpers::getValidatorsFileName() <<
            " not regular file: " << pConfig;

        return false;
    }

    std::ifstream   ifsDefault (pConfig.native ().c_str (), std::ios::in);

    if (!ifsDefault)
    {
        JLOG (j_.fatal) << Config::Helpers::getValidatorsFileName() <<
            " failed to open: " << pConfig;

        return false;
    }

    std::string     strValidators;

    strValidators.assign ((std::istreambuf_iterator<char> (ifsDefault)),
                          std::istreambuf_iterator<char> ());

    if (ifsDefault.bad ())
    {
        JLOG (j_.fatal) << Config::Helpers::getValidatorsFileName() <<
        "Failed to read: " << pConfig;

        return false;
    }

    nodeProcess ("local", strValidators, pConfig.string ());

    JLOG (j_.trace) << str (boost::format ("Processing: %s") % pConfig);

    return true;
}

//--------------------------------------------------------------------------

void UniqueNodeListImp::nodeNetwork()
{
    if (!app_.config().VALIDATORS_SITE.empty ())
    {
        HTTPClient::get (
            true,
            app_.getIOService (),
            app_.config().VALIDATORS_SITE,
            443,
            app_.config().VALIDATORS_URI,
            VALIDATORS_FILE_BYTES_MAX,
            boost::posix_time::seconds (VALIDATORS_FETCH_SECONDS),
            std::bind (&UniqueNodeListImp::validatorsResponse, this,
                       std::placeholders::_1,
                       std::placeholders::_2,
                       std::placeholders::_3),
            app_.logs());
    }
}

//--------------------------------------------------------------------------

Json::Value UniqueNodeListImp::getUnlJson()
{

    Json::Value ret (Json::arrayValue);

    auto db = app_.getWalletDB ().checkoutDb ();


    std::vector<std::array<boost::optional<std::string>, 2>> columns;
    selectBlobsIntoStrings(*db,
                           "SELECT PublicKey, Comment FROM TrustedNodes;",
                           columns);
    for(auto const& strArray : columns)
    {
        Json::Value node (Json::objectValue);

        node["publicKey"]   = strArray[0].value_or("");
        node["comment"]     = strArray[1].value_or("");

        ret.append (node);
    }

    std::lock_guard <std::recursive_mutex> sl (mUNLLock);

    for (auto const& key : ephemeralValidatorKeys_)
    {
        Json::Value node (Json::objectValue);

        node["publicKey"]   = toBase58(TokenType::TOKEN_NODE_PUBLIC, key.first);
        node["comment"]     = key.second;

        ret.append (node);
    }

    return ret;
}

//--------------------------------------------------------------------------

// For each kind of source, have a starting number of points to be distributed.
int UniqueNodeListImp::iSourceScore (ValidatorSource vsWhy)
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
        Throw<std::runtime_error> ("Internal error: bad ValidatorSource.");
    }

    return iScore;
}

// Load information about when we last updated.
bool UniqueNodeListImp::miscLoad()
{
    auto db = app_.getWalletDB ().checkoutDb ();

    boost::optional<int> suO, fuO;

    *db << "SELECT ScoreUpdated, FetchUpdated FROM Misc WHERE Magic=1;",
            soci::into(suO), soci::into(fuO);

    if (!db->got_data() )
        return false;

    mtpFetchUpdated = ptFromSeconds (fuO.value_or(-1));
    mtpScoreUpdated = ptFromSeconds (suO.value_or(-1));

    trustedLoad ();

    return true;
}

// Persist update information.
bool UniqueNodeListImp::miscSave()
{
    auto db = app_.getWalletDB ().checkoutDb ();

    *db << str (boost::format ("REPLACE INTO Misc (Magic,FetchUpdated,ScoreUpdated) VALUES (1,%d,%d);")
                % iToSeconds (mtpFetchUpdated)
                % iToSeconds (mtpScoreUpdated));

    return true;
}

//--------------------------------------------------------------------------

void UniqueNodeListImp::trustedLoad()
{
    auto db = app_.getWalletDB ().checkoutDb ();
    std::lock_guard <std::recursive_mutex> slUNL (mUNLLock);

    mUNL.clear ();

    std::vector<std::array<boost::optional<std::string>, 1>> columns;
    selectBlobsIntoStrings(*db,
                            "SELECT PublicKey FROM TrustedNodes WHERE Score != 0;",
                           columns);
    for(auto const& strArray : columns)
    {
        mUNL.insert (strArray[0].value_or(""));
    }
}

//--------------------------------------------------------------------------

// For a round of scoring we destribute points from a node to nodes it refers to.
// Returns true, iff scores were distributed.
//
bool UniqueNodeListImp::scoreRound (std::vector<scoreNode>& vsnNodes)
{
    bool    bDist   = false;

    // For each node, distribute roundSeed to roundScores.
    for (auto& sn : vsnNodes)
    {
        int     iEntries    = sn.viReferrals.size ();

        if (sn.iRoundSeed && iEntries)
        {
            score   iTotal  = (iEntries + 1) * iEntries / 2;
            score   iBase   = sn.iRoundSeed * iEntries / iTotal;

            // Distribute the current entires' seed score to validators
            // prioritized by mention order.
            for (int i = 0; i != iEntries; i++)
            {
                score   iPoints = iBase * (iEntries - i) / iEntries;

                vsnNodes[sn.viReferrals[i]].iRoundScore += iPoints;
            }
        }
    }

    if (ShouldLog (lsTRACE, UniqueNodeList))
    {
        JLOG (j_.trace) << "midway: ";
        for (auto& sn : vsnNodes)
        {
            JLOG (j_.trace) << str (boost::format ("%s| %d, %d, %d: [%s]")
                                               % sn.strValidator
                                               % sn.iScore
                                               % sn.iRoundScore
                                               % sn.iRoundSeed
                                               % strJoin (sn.viReferrals.begin (), sn.viReferrals.end (), ","));
        }
    }

    // Add roundScore to score.
    // Make roundScore new roundSeed.
    for (auto& sn : vsnNodes)
    {
        if (!bDist && sn.iRoundScore)
            bDist   = true;

        sn.iScore       += sn.iRoundScore;
        sn.iRoundSeed   = sn.iRoundScore;
        sn.iRoundScore  = 0;
    }

    if (ShouldLog (lsTRACE, UniqueNodeList))
    {
        JLOG (j_.trace) << "finish: ";
        for (auto& sn : vsnNodes)
        {
            JLOG (j_.trace) << str (boost::format ("%s| %d, %d, %d: [%s]")
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
void UniqueNodeListImp::scoreCompute()
{
    hash_map<std::string, int> umPulicIdx;     // Map of public key to index.
    hash_map<std::string, int> umDomainIdx;    // Map of domain to index.
    std::vector<scoreNode>  vsnNodes;       // Index to scoring node.

    // For each entry in SeedDomains with a PublicKey:
    // - Add an entry in umPulicIdx, umDomainIdx, and vsnNodes.
    {
        auto db = app_.getWalletDB ().checkoutDb ();

        std::vector<std::array<boost::optional<std::string>, 3>> columns;
        selectBlobsIntoStrings(*db,
                               "SELECT Domain,PublicKey,Source FROM SeedDomains;",
                               columns);
        for(auto const& strArray : columns)
        {
            if (!strArray[1])
                // We ignore entries we don't have public keys for.
                continue;

            std::string const strDomain = strArray[0].value_or("");
            std::string const strPublicKey = *strArray[1];
            std::string const strSource = strArray[2].value_or("");

            assert (!strSource.empty ());

            int         const iScore       = iSourceScore (static_cast<ValidatorSource> (strSource[0]));
            auto siOld   = umPulicIdx.find (strPublicKey);

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

    // For each entry in SeedNodes:
    // - Add an entry in umPulicIdx, umDomainIdx, and vsnNodes.
    {
        auto db = app_.getWalletDB ().checkoutDb ();

        std::vector<std::array<boost::optional<std::string>, 2>> columns;
        selectBlobsIntoStrings(*db,
                               "SELECT PublicKey,Source FROM SeedNodes;",
                               columns);
        for(auto const& strArray : columns)
        {
            std::string strPublicKey    = strArray[0].value_or("");
            std::string strSource       = strArray[1].value_or("");
            assert (!strSource.empty ());
            int         iScore          = iSourceScore (static_cast<ValidatorSource> (strSource[0]));
            auto siOld   = umPulicIdx.find (strPublicKey);

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
        for (auto& sn : vsnNodes)
        {
            JLOG (j_.trace) << str (boost::format ("%s| %d, %d, %d")
                                               % sn.strValidator
                                               % sn.iScore
                                               % sn.iRoundScore
                                               % sn.iRoundSeed);
        }
    }

    // JLOG (j_.trace) << str(boost::format("vsnNodes.size=%d") % vsnNodes.size());

    // Step through growing list of nodes adding each validation list.
    // - Each validator may have provided referals.  Add those referals as validators.
    for (int iNode = 0; iNode != vsnNodes.size (); ++iNode)
    {
        scoreNode&          sn              = vsnNodes[iNode];
        std::string&        strValidator    = sn.strValidator;
        std::vector<int>&   viReferrals     = sn.viReferrals;

        auto db = app_.getWalletDB ().checkoutDb ();

        std::vector<std::array<boost::optional<std::string>, 1>> columns;
        selectBlobsIntoStrings(*db,
                               boost::str (boost::format (
                                   "SELECT Referral FROM ValidatorReferrals "
                                   "WHERE Validator=%s ORDER BY Entry;") %
                                           sqlEscape (strValidator)),
                               columns);
        std::string strReferral;
        for(auto const& strArray : columns)
        {
            strReferral = strArray[0].value_or("");

            int         iReferral;

            RippleAddress       na;

            if (na.setNodePublic (strReferral))
            {
                // Referring a public key.
                auto itEntry = umPulicIdx.find (strReferral);

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
            }
            else
            {
                // Referring a domain.
                auto itEntry = umDomainIdx.find (strReferral);
                iReferral   = itEntry == umDomainIdx.end ()
                              ? -1            // We ignore domains we can't find entires for.
                              : itEntry->second;
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
        JLOG (j_.trace) << "Scored:";
        for (auto& sn : vsnNodes)
        {
            JLOG (j_.trace) << str (boost::format ("%s| %d, %d, %d: [%s]")
                                               % sn.strValidator
                                               % sn.iScore
                                               % sn.iRoundScore
                                               % sn.iRoundSeed
                                               % strJoin (sn.viReferrals.begin (), sn.viReferrals.end (), ","));
        }
    }

    // Persist validator scores.
    auto db = app_.getWalletDB ().checkoutDb ();

    soci::transaction tr(*db);
    *db << "UPDATE TrustedNodes SET Score = 0 WHERE Score != 0;";

    if (!vsnNodes.empty ())
    {
        // Load existing Seens from DB.
        std::vector<std::string>    vstrPublicKeys;

        vstrPublicKeys.resize (vsnNodes.size ());

        for (int iNode = vsnNodes.size (); iNode--;)
        {
            vstrPublicKeys[iNode]   = sqlEscape (vsnNodes[iNode].strValidator);
        }

        // Iterate through the result rows with a fectch b/c putting a
        // column of type DATETIME into a boost::tuple can throw when the
        // datetime column is invalid (even if the value as int is valid).
        std::vector<std::tuple<boost::optional<std::string>,
                               boost::optional<int>>> columns;
        selectBlobsIntoStrings (
            *db,
            str (boost::format (
                     "SELECT PublicKey,Seen FROM TrustedNodes WHERE "
                     "PublicKey IN (%s);") %
                 strJoin (
                     vstrPublicKeys.begin (), vstrPublicKeys.end (), ",")),
            columns);
        std::string pk;
        for(auto const& col : columns)
        {
            pk = get<0>(col).value_or ("");

            vsnNodes[umPulicIdx[pk]].iSeen   = get<1>(col).value_or (-1);
        }
    }

    hash_set<std::string>   usUNL;

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

        *db << str (boost::format ("REPLACE INTO TrustedNodes (PublicKey,Score,Seen) VALUES %s;")
                    % strJoin (vstrValues.begin (), vstrValues.end (), ","));
    }

    {
        std::lock_guard <std::recursive_mutex> sl (mUNLLock);

        // XXX Should limit to scores above a certain minimum and limit to a certain number.
        mUNL.swap (usUNL);
    }

    hash_map<std::string, int>  umValidators;

    if (!vsnNodes.empty ())
    {
        // For every IpReferral add a score for the IP and PORT.
        std::vector<std::tuple<boost::optional<std::string>,
                               boost::optional<std::int32_t>>> columns;
        selectBlobsIntoStrings (
            *db,
            "SELECT Validator,COUNT(*) AS Count FROM "
            "IpReferrals GROUP BY Validator;",
            columns);
        for(auto const& col : columns)
        {
            umValidators[get<0>(col).value_or("")] = get<1>(col).value_or(0);

            // JLOG (j_.trace) << strValidator << ":" << db->getInt("Count");
        }
    }

    // For each validator, get each referral and add its score to ip's score.
    // map of pair<IP,Port> :: score
    hash_map<std::pair<std::string, int>, score> umScore;

    for (auto& vc : umValidators)
    {
        std::string strValidator    = vc.first;

        auto itIndex = umPulicIdx.find (strValidator);

        if (itIndex != umPulicIdx.end ())
        {
            int         iSeed           = vsnNodes[itIndex->second].iScore;
            int         iEntries        = vc.second;
            score       iTotal          = (iEntries + 1) * iEntries / 2;
            score       iBase           = iSeed * iEntries / iTotal;
            int         iEntry          = 0;

            std::vector<std::tuple<boost::optional<std::string>,
                                   boost::optional<std::int32_t>>> columns;
            selectBlobsIntoStrings (
                *db,
                str (boost::format (
                    "SELECT IP,Port FROM IpReferrals WHERE "
                    "Validator=%s ORDER BY Entry;") %
                     sqlEscape (strValidator)),
                columns);
            for(auto const& col : columns)
            {
                score       iPoints = iBase * (iEntries - iEntry) / iEntries;
                int         iPort;

                iPort       = get<1>(col).value_or(0);

                std::pair< std::string, int>    ep  = std::make_pair (get<0>(col).value_or(""), iPort);

                auto itEp    = umScore.find (ep);

                umScore[ep] = itEp == umScore.end () ? iPoints :  itEp->second + iPoints;
                iEntry++;
            }
        }
    }

    tr.commit ();
}

//--------------------------------------------------------------------------

// Start a timer to update scores.
// <-- bNow: true, to force scoring for debugging.
void UniqueNodeListImp::scoreNext (bool bNow)
{
    // JLOG (j_.trace) << str(boost::format("scoreNext: mtpFetchUpdated=%s mtpScoreStart=%s mtpScoreUpdated=%s mtpScoreNext=%s") % mtpFetchUpdated % mtpScoreStart % mtpScoreUpdated % mtpScoreNext);
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

        // JLOG (j_.trace) << str(boost::format("scoreNext: @%s") % mtpScoreNext);
        m_scoreTimer.setExpiration (secondsFromNow);
    }
}

//--------------------------------------------------------------------------

// Given a ripple.txt, process it.
//
// VFALCO TODO Can't we take a filename or stream instead of a string?
//
bool UniqueNodeListImp::responseFetch (std::string const& strDomain, const boost::system::error_code& err, int iStatus, std::string const& strSiteFile)
{
    bool    bReject = !err && iStatus != 200;

    if (!bReject)
    {
        IniFileSections secSite = parseIniFile (strSiteFile, true);
        bool bGood   = !err;

        if (bGood)
        {
            JLOG (j_.trace) << strDomain
                << ": retrieved configuration";
        }
        else
        {
            JLOG (j_.trace) << strDomain
                << ": unable to retrieve configuration: "
                <<  err.message ();
        }

        //
        // Verify file domain
        //
        std::string strSite;

        if (bGood && !getSingleSection (secSite, SECTION_DOMAIN, strSite, j_))
        {
            bGood   = false;

            JLOG (j_.trace) << strDomain
                << ": " << SECTION_DOMAIN
                << "entry missing.";
        }

        if (bGood && strSite != strDomain)
        {
            bGood   = false;

            JLOG (j_.trace) << strDomain
                << ": " << SECTION_DOMAIN << " does not match " << strSite;
        }

        //
        // Process public key
        //
        std::string     strNodePublicKey;

        if (bGood && !getSingleSection (secSite, SECTION_PUBLIC_KEY, strNodePublicKey, j_))
        {
            // Bad [validation_public_key] IniFileSections.
            bGood   = false;

            JLOG (j_.trace) << strDomain
                << ": " << SECTION_PUBLIC_KEY << " entry missing.";
        }

        RippleAddress   naNodePublic;

        if (bGood && !naNodePublic.setNodePublic (strNodePublicKey))
        {
            // Bad public key.
            bGood   = false;

            JLOG (j_.trace) << strDomain
                << ": " << SECTION_PUBLIC_KEY << " is not a public key: "
                << strNodePublicKey;
        }

        if (bGood)
        {
            seedDomain  sdCurrent;

            bool bFound = getSeedDomains (strDomain, sdCurrent);
            assert (bFound);
            (void) bFound;

            uint256 iSha256 =
                sha512Half(makeSlice(strSiteFile));
            bool bChangedB =
                sdCurrent.iSha256 != iSha256;

            sdCurrent.strDomain     = strDomain;
            // XXX If the node public key is changing, delete old public key information?
            // XXX Only if no other refs to keep it arround, other wise we have an attack vector.
            sdCurrent.naPublicKey   = naNodePublic;

            // JLOG (j_.trace) << boost::format("sdCurrent.naPublicKey: '%s'") % sdCurrent.naPublicKey.humanNodePublic();

            sdCurrent.tpFetch       = boost::posix_time::second_clock::universal_time ();
            sdCurrent.iSha256       = iSha256;

            setSeedDomains (sdCurrent, true);

            if (bChangedB)
            {
                JLOG (j_.trace) << strDomain
                    << ": processing new " << node_file_name_ << ".";
                processFile (strDomain, naNodePublic, secSite);
            }
            else
            {
                JLOG (j_.trace) << strDomain
                    << ": no change in " << node_file_name_ << ".";
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
void UniqueNodeListImp::fetchNext()
{
    bool    bFull;

    {
        std::lock_guard <std::mutex> sl (mFetchLock);

        bFull = (mFetchActive == NODE_FETCH_JOBS);
    }

    if (!bFull)
    {
        // Determine next scan.
        std::string strDomain;
        boost::posix_time::ptime tpNext (boost::posix_time::min_date_time);
        boost::posix_time::ptime tpNow (boost::posix_time::second_clock::universal_time ());

        auto db = app_.getWalletDB ().checkoutDb ();


        {
            soci::blob b(*db);
            soci::indicator ind;
            boost::optional<int> nO;
            *db << "SELECT Domain,Next FROM SeedDomains INDEXED BY SeedDomainNext ORDER BY Next LIMIT 1;",
                    soci::into(b, ind),
                    soci::into(nO);
            if (nO)
            {
                int iNext (*nO);

                tpNext  = ptFromSeconds (iNext);

                JLOG (j_.trace) << str (boost::format ("fetchNext: iNext=%s tpNext=%s tpNow=%s") % iNext % tpNext % tpNow);
                if (soci::i_ok == ind)
                    convert (b, strDomain);
                else
                    strDomain.clear ();
            }
        }

        if (!strDomain.empty ())
        {
            std::lock_guard <std::mutex> sl (mFetchLock);

            bFull = (mFetchActive == NODE_FETCH_JOBS);

            if (!bFull && tpNext <= tpNow)
            {
                mFetchActive++;
            }
        }

        if (strDomain.empty () || bFull)
        {
            JLOG (j_.trace) << str (boost::format ("fetchNext: strDomain=%s bFull=%d") % strDomain % bFull);
        }
        else if (tpNext > tpNow)
        {
            JLOG (j_.trace) << str (boost::format ("fetchNext: set timer : strDomain=%s") % strDomain);
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
            JLOG (j_.trace) << str (boost::format ("fetchNext: fetch now: strDomain=%s tpNext=%s tpNow=%s") % strDomain % tpNext % tpNow);
            // Fetch needs to happen now.
            mtpFetchNext    = boost::posix_time::ptime (boost::posix_time::not_a_date_time);

            seedDomain  sdCurrent;
            bool bFound  = getSeedDomains (strDomain, sdCurrent);
            assert (bFound);
            (void) bFound;

            // Update time of next fetch and this scan attempt.
            sdCurrent.tpScan        = tpNow;

            // XXX Use a longer duration if we have lots of validators.
            sdCurrent.tpNext        = sdCurrent.tpScan + boost::posix_time::hours (7 * 24);

            setSeedDomains (sdCurrent, false);

            JLOG (j_.trace) << strDomain
                << " fetching " << node_file_name_ << ".";

            fetchProcess (strDomain);   // Go get it.

            fetchNext ();               // Look for more.
        }
    }
}

//--------------------------------------------------------------------------

// Called when we need to update scores.
void UniqueNodeListImp::fetchDirty()
{
    // Note update.
    mtpFetchUpdated = boost::posix_time::second_clock::universal_time ();
    miscSave ();

    // Update scores.
    scoreNext (false);
}


//--------------------------------------------------------------------------

void UniqueNodeListImp::fetchFinish()
{
    {
        std::lock_guard <std::mutex> sl (mFetchLock);
        mFetchActive--;
    }

    fetchNext ();
}

//--------------------------------------------------------------------------

// Get the ripple.txt and process it.
void UniqueNodeListImp::fetchProcess (std::string strDomain)
{
    JLOG (j_.trace) << strDomain
        << ": fetching " << node_file_name_ << ".";

    std::deque<std::string> deqSites;

    // Order searching from most specifically for purpose to generic.
    // This order allows the client to take the most burden rather than the servers.
    deqSites.push_back (systemName () + strDomain);
    deqSites.push_back ("www." + strDomain);
    deqSites.push_back (strDomain);

    HTTPClient::get (
        true,
        app_.getIOService (),
        deqSites,
        443,
        node_file_path_,
        NODE_FILE_BYTES_MAX,
        boost::posix_time::seconds (NODE_FETCH_SECONDS),
        std::bind (&UniqueNodeListImp::responseFetch, this, strDomain,
                   std::placeholders::_1, std::placeholders::_2,
                   std::placeholders::_3),
        app_.logs ());
}

// Process IniFileSections [validators_url].
void UniqueNodeListImp::getValidatorsUrl (RippleAddress const& naNodePublic,
    IniFileSections secSite)
{
    std::string strValidatorsUrl;
    std::string strScheme;
    std::string strDomain;
    int         iPort;
    std::string strPath;

    if (getSingleSection (secSite, SECTION_VALIDATORS_URL, strValidatorsUrl, j_)
            && !strValidatorsUrl.empty ()
            && parseUrl (strValidatorsUrl, strScheme, strDomain, iPort, strPath)
            && -1 == iPort
            && strScheme == "https")
    {
        HTTPClient::get (
            true,
            app_.getIOService (),
            strDomain,
            443,
            strPath,
            NODE_FILE_BYTES_MAX,
            boost::posix_time::seconds (NODE_FETCH_SECONDS),
            std::bind (&UniqueNodeListImp::responseValidators, this,
                       strValidatorsUrl, naNodePublic, secSite, strDomain,
                       std::placeholders::_1, std::placeholders::_2,
                       std::placeholders::_3),
            app_.logs ());
    }
    else
    {
        getIpsUrl (naNodePublic, secSite);
    }
}

//--------------------------------------------------------------------------

// Process IniFileSections [ips_url].
// If we have a IniFileSections with a single entry, fetch the url and process it.
void UniqueNodeListImp::getIpsUrl (RippleAddress const& naNodePublic, IniFileSections secSite)
{
    std::string strIpsUrl;
    std::string strScheme;
    std::string strDomain;
    int         iPort;
    std::string strPath;

    if (getSingleSection (secSite, SECTION_IPS_URL, strIpsUrl, j_)
            && !strIpsUrl.empty ()
            && parseUrl (strIpsUrl, strScheme, strDomain, iPort, strPath)
            && -1 == iPort
            && strScheme == "https")
    {
        HTTPClient::get (
            true,
            app_.getIOService (),
            strDomain,
            443,
            strPath,
            NODE_FILE_BYTES_MAX,
            boost::posix_time::seconds (NODE_FETCH_SECONDS),
            std::bind (&UniqueNodeListImp::responseIps, this, strDomain,
                       naNodePublic, std::placeholders::_1,
                       std::placeholders::_2, std::placeholders::_3),
            app_.logs ());
    }
    else
    {
        fetchFinish ();
    }
}


//--------------------------------------------------------------------------

// Given a IniFileSections with IPs, parse and persist it for a validator.
bool UniqueNodeListImp::responseIps (std::string const& strSite, RippleAddress const& naNodePublic, const boost::system::error_code& err, int iStatus, std::string const& strIpsFile)
{
    bool    bReject = !err && iStatus != 200;

    if (!bReject)
    {
        if (!err)
        {
            IniFileSections         secFile     = parseIniFile (strIpsFile, true);

            processIps (strSite, naNodePublic, getIniFileSection (secFile, SECTION_IPS));
        }

        fetchFinish ();
    }

    return bReject;
}

// After fetching a ripple.txt from a web site, given a IniFileSections with validators, parse and persist it.
bool UniqueNodeListImp::responseValidators (std::string const& strValidatorsUrl, RippleAddress const& naNodePublic, IniFileSections secSite, std::string const& strSite, const boost::system::error_code& err, int iStatus, std::string const& strValidatorsFile)
{
    bool    bReject = !err && iStatus != 200;

    if (!bReject)
    {
        if (!err)
        {
            IniFileSections     secFile     = parseIniFile (strValidatorsFile, true);

            processValidators (strSite, strValidatorsUrl, naNodePublic, vsValidator, getIniFileSection (secFile, SECTION_VALIDATORS));
        }

        getIpsUrl (naNodePublic, secSite);
    }

    return bReject;
}


//--------------------------------------------------------------------------

// Persist the IPs refered to by a Validator.
// --> strSite: source of the IPs (for debugging)
// --> naNodePublic: public key of the validating node.
void UniqueNodeListImp::processIps (std::string const& strSite, RippleAddress const& naNodePublic, IniFileSections::mapped_type* pmtVecStrIps)
{
    std::string strEscNodePublic    = sqlEscape (naNodePublic.humanNodePublic ());

    JLOG (j_.debug)
            << str (boost::format ("Validator: '%s' processing %d ips.")
                    % strSite % ( pmtVecStrIps ? pmtVecStrIps->size () : 0));

    // Remove all current Validator's entries in IpReferrals
    {
        auto db = app_.getWalletDB ().checkoutDb ();
        *db << str (boost::format ("DELETE FROM IpReferrals WHERE Validator=%s;") % strEscNodePublic);
    }

    // Add new referral entries.
    if (pmtVecStrIps && !pmtVecStrIps->empty ())
    {
        std::vector<std::string>    vstrValues;

        vstrValues.resize (std::min ((int) pmtVecStrIps->size (), REFERRAL_IPS_MAX));

        int iValues = 0;
        for (auto const& strReferral : *pmtVecStrIps)
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
                JLOG (j_.trace)
                        << str (boost::format ("Validator: '%s' [" SECTION_IPS "]: rejecting '%s'")
                                % strSite % strReferral);
            }
        }

        if (iValues)
        {
            vstrValues.resize (iValues);

            auto db = app_.getWalletDB ().checkoutDb ();
            *db << str (boost::format ("INSERT INTO IpReferrals (Validator,Entry,IP,Port) VALUES %s;")
                        % strJoin (vstrValues.begin (), vstrValues.end (), ","));
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
int UniqueNodeListImp::processValidators (std::string const& strSite, std::string const& strValidatorsSrc, RippleAddress const& naNodePublic, ValidatorSource vsWhy, IniFileSections::mapped_type const* pmtVecStrValidators)
{
    std::string strNodePublic   = naNodePublic.isValid () ? naNodePublic.humanNodePublic () : strValidatorsSrc;
    int         iValues         = 0;

    JLOG (j_.trace)
            << str (boost::format ("Validator: '%s' : '%s' : processing %d validators.")
                    % strSite
                    % strValidatorsSrc
                    % ( pmtVecStrValidators ? pmtVecStrValidators->size () : 0));

    // Remove all current Validator's entries in ValidatorReferrals
    {
        auto db = app_.getWalletDB ().checkoutDb ();

        *db << str (boost::format ("DELETE FROM ValidatorReferrals WHERE Validator='%s';") % strNodePublic);
        // XXX Check result.
    }

    // Add new referral entries.
    if (pmtVecStrValidators && pmtVecStrValidators->size ())
    {
        std::vector<std::string> vstrValues;

        vstrValues.reserve (std::min ((int) pmtVecStrValidators->size (), REFERRAL_VALIDATORS_MAX));

        for (auto const& strReferral : *pmtVecStrValidators)
        {
            if (iValues == REFERRAL_VALIDATORS_MAX)
                break;

            boost::smatch   smMatch;

            // domain comment?
            // public_key comment?
            static boost::regex reReferral ("\\`\\s*(\\S+)(?:\\s+(.+))?\\s*\\'");

            if (!boost::regex_match (strReferral, smMatch, reReferral))
            {
                JLOG (j_.warning) << str (boost::format ("Bad validator: syntax error: %s: %s") % strSite % strReferral);
            }
            else
            {
                std::string     strRefered  = smMatch[1];
                std::string     strComment  = smMatch[2];
                RippleAddress   naValidator;

                if (naValidator.setSeedGeneric (strRefered))
                {
                    JLOG (j_.warning) << str (boost::format ("Bad validator: domain or public key required: %s %s") % strRefered % strComment);
                }
                else if (naValidator.setNodePublic (strRefered))
                {
                    // A public key.
                    // XXX Schedule for CAS lookup.
                    nodeAddPublic (naValidator, vsWhy, strComment);

                    JLOG (j_.info) << str (boost::format ("Node Public: %s %s") % strRefered % strComment);

                    if (naNodePublic.isValid ())
                        vstrValues.push_back (str (boost::format ("('%s',%d,'%s')") % strNodePublic % iValues % naValidator.humanNodePublic ()));

                    iValues++;
                }
                else
                {
                    // A domain: need to look it up.
                    nodeAddDomain (strRefered, vsWhy, strComment);

                    JLOG (j_.info) << str (boost::format ("Node Domain: %s %s") % strRefered % strComment);

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

            auto db = app_.getWalletDB ().checkoutDb ();

            *db << strSql;
            // XXX Check result.
        }
    }

    fetchDirty ();

    return iValues;
}

//--------------------------------------------------------------------------

// Process a ripple.txt.
void UniqueNodeListImp::processFile (std::string const& strDomain, RippleAddress const& naNodePublic, IniFileSections secSite)
{
    //
    // Process Validators
    //
    processValidators (strDomain, node_file_name_, naNodePublic,
        vsReferral, getIniFileSection (secSite, SECTION_VALIDATORS));

    //
    // Process ips
    //
    processIps (strDomain, naNodePublic, getIniFileSection (secSite, SECTION_IPS));

    //
    // Process currencies
    //
    IniFileSections::mapped_type*   pvCurrencies;

    if ((pvCurrencies = getIniFileSection (secSite, SECTION_CURRENCIES)) && pvCurrencies->size ())
    {
        // XXX Process currencies.
        JLOG (j_.warning) << "Ignoring currencies: not implemented.";
    }

    getValidatorsUrl (naNodePublic, secSite);
}

//--------------------------------------------------------------------------

// Retrieve a SeedDomain from DB.
bool UniqueNodeListImp::getSeedDomains (std::string const& strDomain, seedDomain& dstSeedDomain)
{
    bool        bResult = false;

    std::string strSql = boost::str (
        boost::format (
            "SELECT Domain, PublicKey, Source, Next, Scan, Fetch, Sha256, "
            "Comment FROM SeedDomains WHERE Domain=%s;") %
        sqlEscape (strDomain));

    auto db = app_.getWalletDB ().checkoutDb ();

    // Iterate through the result rows with a fectch b/c putting a
    // column of type DATETIME into a boost::tuple can throw when the
    // datetime column is invalid (even if the value as int is valid).
    soci::blob                       domainBlob(*db);
    soci::indicator                  di;
    boost::optional<std::string>     strPublicKey;
    soci:: blob                      sourceBlob(*db);
    soci::indicator                  si;
    std::string                      strSource;
    boost::optional<int>             iNext;
    boost::optional<int>             iScan;
    boost::optional<int>             iFetch;
    boost::optional<std::string>     strSha256;
    soci::blob                       commentBlob(*db);
    soci::indicator                  ci;
    boost::optional<std::string>     strComment;

    soci::statement st = (db->prepare << strSql,
                          soci::into (domainBlob, di),
                          soci::into (strPublicKey),
                          soci::into (sourceBlob, si),
                          soci::into (iNext),
                          soci::into (iScan),
                          soci::into (iFetch),
                          soci::into (strSha256),
                          soci::into (commentBlob, ci));

    st.execute ();
    while (st.fetch ())
    {
        bResult = true;

        if (soci::i_ok == di)
            convert (domainBlob, dstSeedDomain.strDomain);

        if (strPublicKey && !strPublicKey->empty ())
            dstSeedDomain.naPublicKey.setNodePublic (*strPublicKey);
        else
            dstSeedDomain.naPublicKey.clear ();

        if (soci::i_ok == si)
        {
            convert (sourceBlob, strSource);
            dstSeedDomain.vsSource  = static_cast<ValidatorSource> (strSource[0]);
        }
        else
        {
            assert (0);
        }

        dstSeedDomain.tpNext    = ptFromSeconds (iNext.value_or (0));
        dstSeedDomain.tpScan    = ptFromSeconds (iScan.value_or (0));
        dstSeedDomain.tpFetch   = ptFromSeconds (iFetch.value_or (0));

        if (strSha256 && !strSha256->empty ())
            dstSeedDomain.iSha256.SetHex (*strSha256);
        else
            dstSeedDomain.iSha256.zero ();

        if (soci::i_ok == ci)
            convert (commentBlob, dstSeedDomain.strComment);
        else
            dstSeedDomain.strComment.clear ();
    }

    return bResult;
}

//--------------------------------------------------------------------------

// Persist a SeedDomain.
void UniqueNodeListImp::setSeedDomains (const seedDomain& sdSource, bool bNext)
{
    int     iNext   = iToSeconds (sdSource.tpNext);
    int     iScan   = iToSeconds (sdSource.tpScan);
    int     iFetch  = iToSeconds (sdSource.tpFetch);

    // JLOG (lsTRACE) << str(boost::format("setSeedDomains: iNext=%s tpNext=%s") % iNext % sdSource.tpNext);

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

    auto db = app_.getWalletDB ().checkoutDb ();

    try
    {
        *db << strSql;
    }
    catch (soci::soci_error& e)
    {
        // XXX Check result.
        JLOG (j_.warning) << "setSeedDomains: failed. Error: " << e.what();
    }

    if (bNext && (mtpFetchNext.is_not_a_date_time () || mtpFetchNext > sdSource.tpNext))
    {
        // Schedule earlier wake up.
        fetchNext ();
    }
}


//--------------------------------------------------------------------------

// Retrieve a SeedNode from DB.
bool UniqueNodeListImp::getSeedNodes (RippleAddress const& naNodePublic, seedNode& dstSeedNode)
{
    std::string strSql =
            str (boost::format (
                "SELECT PublicKey, Source, Next, Scan, Fetch, Sha256, "
                "Comment FROM SeedNodes WHERE PublicKey='%s';") %
                 naNodePublic.humanNodePublic ());

    auto db = app_.getWalletDB ().checkoutDb ();

    std::string                      strPublicKey;
    std::string                      strSource;
    soci::blob                       sourceBlob(*db);
    soci::indicator                  si;
    boost::optional<int>             iNext;
    boost::optional<int>             iScan;
    boost::optional<int>             iFetch;
    boost::optional<std::string>     strSha256;
    soci::blob                       commentBlob(*db);
    soci::indicator                  ci;

    *db << strSql,
            soci::into (strPublicKey),
            soci::into (sourceBlob, si),
            soci::into (iNext),
            soci::into (iScan),
            soci::into (iFetch),
            soci::into (strSha256),
            soci::into (commentBlob, ci);

    if (!db->got_data ())
        return false;

    if (!strPublicKey.empty ())
        dstSeedNode.naPublicKey.setNodePublic (strPublicKey);
    else
        dstSeedNode.naPublicKey.clear ();

    if (soci::i_ok == si)
    {
        convert (sourceBlob, strSource);
        dstSeedNode.vsSource    = static_cast<ValidatorSource> (strSource[0]);
    }
    else
        assert (0);

    dstSeedNode.tpNext  = ptFromSeconds (iNext.value_or(0));
    dstSeedNode.tpScan  = ptFromSeconds (iScan.value_or(0));
    dstSeedNode.tpFetch = ptFromSeconds (iFetch.value_or(0));

    if (strSha256 && !strSha256->empty ())
        dstSeedNode.iSha256.SetHex (*strSha256);
    else
        dstSeedNode.iSha256.zero ();

    if (soci::i_ok == ci)
        convert (commentBlob, dstSeedNode.strComment);
    else
        dstSeedNode.strComment.clear ();

    return true;
}

//--------------------------------------------------------------------------

// Persist a SeedNode.
// <-- bNext: true, to do fetching if needed.
void UniqueNodeListImp::setSeedNodes (const seedNode& snSource, bool bNext)
{
    int     iNext   = iToSeconds (snSource.tpNext);
    int     iScan   = iToSeconds (snSource.tpScan);
    int     iFetch  = iToSeconds (snSource.tpFetch);

    // JLOG (lsTRACE) << str(boost::format("setSeedNodes: iNext=%s tpNext=%s") % iNext % sdSource.tpNext);

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
        auto db = app_.getWalletDB ().checkoutDb ();

        try
        {
            *db << strSql;
        }
        catch(soci::soci_error& e)
        {
            JLOG (j_.trace) << "setSeedNodes: failed. Error: " << e.what ();
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

bool UniqueNodeListImp::validatorsResponse (const boost::system::error_code& err, int iStatus, std::string strResponse)
{
    bool    bReject = !err && iStatus != 200;

    if (!bReject)
    {
        JLOG (j_.trace) <<
            "Fetch '" <<
            Config::Helpers::getValidatorsFileName () <<
            "' complete.";

        if (!err)
        {
            nodeProcess ("network", strResponse, app_.config().VALIDATORS_SITE);
        }
        else
        {
            JLOG (j_.warning) << "Error: " << err.message ();
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
void UniqueNodeListImp::nodeProcess (std::string const& strSite, std::string const& strValidators, std::string const& strSource)
{
    IniFileSections secValidators   = parseIniFile (strValidators, true);

    IniFileSections::mapped_type*   pmtEntries  = getIniFileSection (secValidators, SECTION_VALIDATORS);

    if (pmtEntries)
    {
        RippleAddress   naInvalid;  // Don't want a referrer on added entries.

        // YYY Unspecified might be bootstrap or rpc command
        processValidators (strSite, strSource, naInvalid, vsValidator, pmtEntries);
    }
    else
    {
        JLOG (j_.warning) << boost::str (boost::format ("'%s' missing [" SECTION_VALIDATORS "].")
                                             % app_.config().VALIDATORS_BASE);
    }
}

//------------------------------------------------------------------------------

std::unique_ptr<UniqueNodeList>
make_UniqueNodeList (Application& app, beast::Stoppable& parent)
{
    return std::make_unique<UniqueNodeListImp> (app, parent);
}

} // ripple
