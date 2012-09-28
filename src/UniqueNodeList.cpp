// XXX Dynamically limit fetching by distance.
// XXX Want a limit of 2000 validators.

#include "Application.h"
#include "HttpsClient.h"
#include "Log.h"
#include "ParseSection.h"
#include "Serializer.h"
#include "UniqueNodeList.h"
#include "utils.h"

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/mem_fn.hpp>
#include <boost/regex.hpp>

#include <fstream>
#include <iostream>

#define VALIDATORS_FETCH_SECONDS	30
#define VALIDATORS_FILE_PATH		"/" VALIDATORS_FILE_NAME
#define VALIDATORS_FILE_BYTES_MAX	(50 << 10)

// Gather string constants.
#define SECTION_CURRENCIES		"currencies"
#define SECTION_DOMAIN			"domain"
#define SECTION_IPS				"ips"
#define SECTION_IPS_URL			"ips_url"
#define SECTION_PUBLIC_KEY		"validation_public_key"
#define SECTION_VALIDATORS		"validators"
#define SECTION_VALIDATORS_URL	"validators_url"

// Limit pollution of database.
// YYY Move to config file.
#define REFERRAL_VALIDATORS_MAX	50
#define REFERRAL_IPS_MAX		50

#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif

UniqueNodeList::UniqueNodeList(boost::asio::io_service& io_service) :
	mdtScoreTimer(io_service),
	mFetchActive(0),
	mdtFetchTimer(io_service)
{
}

// This is called when the application is started.
// Get update times and start fetching and scoring as needed.
void UniqueNodeList::start()
{
	miscLoad();

	std::cerr << "Validator fetch updated: " << mtpFetchUpdated << std::endl;
	std::cerr << "Validator score updated: " << mtpScoreUpdated << std::endl;

	fetchNext();			// Start fetching.
	scoreNext(false);		// Start scoring.
}

// Load information about when we last updated.
bool UniqueNodeList::miscLoad()
{
	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	Database *db=theApp->getWalletDB()->getDB();

	if (!db->executeSQL("SELECT * FROM Misc WHERE Magic=1;")) return false;

	bool	bAvail	= !!db->startIterRows();

	mtpFetchUpdated	= ptFromSeconds(bAvail ? db->getInt("FetchUpdated") : -1);
	mtpScoreUpdated	= ptFromSeconds(bAvail ? db->getInt("ScoreUpdated") : -1);

	db->endIterRows();

	trustedLoad();

	return true;
}

// Persist update information.
bool UniqueNodeList::miscSave()
{
	Database*	db=theApp->getWalletDB()->getDB();
	ScopedLock	sl(theApp->getWalletDB()->getDBLock());

	db->executeSQL(str(boost::format("REPLACE INTO Misc (Magic,FetchUpdated,ScoreUpdated) VALUES (1,%d,%d);")
		% iToSeconds(mtpFetchUpdated)
		% iToSeconds(mtpScoreUpdated)));

	return true;
}

void UniqueNodeList::trustedLoad()
{
	Database*	db=theApp->getWalletDB()->getDB();
	ScopedLock	sl(theApp->getWalletDB()->getDBLock());
	ScopedLock	slUNL(mUNLLock);

	mUNL.clear();

	// XXX Needs to limit by quanity and quality.
	SQL_FOREACH(db, "SELECT PublicKey FROM TrustedNodes WHERE Score != 0;")
	{
		mUNL.insert(db->getStrBinary("PublicKey"));
	}
}

// For a round of scoring we destribute points from a node to nodes it refers to.
// Returns true, iff scores were distributed.
bool UniqueNodeList::scoreRound(std::vector<scoreNode>& vsnNodes)
{
    bool    bDist   = false;

    // For each node, distribute roundSeed to roundScores.
    BOOST_FOREACH(scoreNode& sn, vsnNodes) {
		int		iEntries	= sn.viReferrals.size();

		if (sn.iRoundSeed && iEntries)
		{
			score	iTotal	= (iEntries + 1) * iEntries / 2;
			score	iBase	= sn.iRoundSeed * iEntries / iTotal;

			// Distribute the current entires' seed score to validators prioritized by mention order.
			for (int i=0; i != iEntries; i++) {
				score	iPoints	= iBase * (iEntries - i) / iEntries;

				vsnNodes[sn.viReferrals[i]].iRoundScore	+= iPoints;
			}
		}
    }

	std::cerr << "midway: " << std::endl;

	BOOST_FOREACH(scoreNode& sn, vsnNodes)
	{
		std::cerr << str(boost::format("%s| %d, %d, %d: [%s]")
			% sn.strValidator
			% sn.iScore
			% sn.iRoundScore
			% sn.iRoundSeed
			% strJoin(sn.viReferrals.begin(), sn.viReferrals.end(), ","))
			<< std::endl;
	}

    // Add roundScore to score.
    // Make roundScore new roundSeed.
    BOOST_FOREACH(scoreNode& sn, vsnNodes) {
		if (!bDist && sn.iRoundScore)
			bDist   = true;

		sn.iScore		+= sn.iRoundScore;
		sn.iRoundSeed	= sn.iRoundScore;
		sn.iRoundScore	= 0;
    }

	std::cerr << "finish: " << std::endl;

	BOOST_FOREACH(scoreNode& sn, vsnNodes)
	{
		std::cerr << str(boost::format("%s| %d, %d, %d: [%s]")
			% sn.strValidator
			% sn.iScore
			% sn.iRoundScore
			% sn.iRoundSeed
			% strJoin(sn.viReferrals.begin(), sn.viReferrals.end(), ","))
			<< std::endl;
	}

    return bDist;
}

// From SeedDomains and ValidatorReferrals compute scores and update TrustedNodes.
void UniqueNodeList::scoreCompute()
{
	strIndex				umPulicIdx;		// Map of public key to index.
	strIndex				umDomainIdx;	// Map of domain to index.
	std::vector<scoreNode>	vsnNodes;		// Index to scoring node.

	Database*	db=theApp->getWalletDB()->getDB();

	// For each entry in SeedDomains with a PublicKey:
	// - Add an entry in umPulicIdx, umDomainIdx, and vsnNodes.
	{
		ScopedLock	sl(theApp->getWalletDB()->getDBLock());

		SQL_FOREACH(db, "SELECT Domain,PublicKey,Source FROM SeedDomains;")
		{
			if (db->getNull("PublicKey"))
			{
				nothing();	// We ignore entries we don't have public keys for.
			}
			else
			{
				std::string	strDomain		= db->getStrBinary("Domain");
				std::string	strPublicKey	= db->getStrBinary("PublicKey");
				std::string	strSource		= db->getStrBinary("Source");
				int			iScore			= iSourceScore(static_cast<validatorSource>(strSource[0]));
				strIndex::iterator	siOld	= umPulicIdx.find(strPublicKey);

				if (siOld == umPulicIdx.end())
				{
					// New node
					int			iNode		= vsnNodes.size();

					umPulicIdx[strPublicKey]	= iNode;
					umDomainIdx[strDomain]		= iNode;

					scoreNode	snCurrent;

					snCurrent.strValidator	= strPublicKey;
					snCurrent.iScore		= iScore;
					snCurrent.iRoundSeed	= snCurrent.iScore;
					snCurrent.iRoundScore	= 0;
					snCurrent.iSeen			= -1;

					vsnNodes.push_back(snCurrent);
				}
				else
				{
					scoreNode&	snOld	= vsnNodes[siOld->second];

					if (snOld.iScore < iScore)
					{
						// Update old node

						snOld.iScore		= iScore;
						snOld.iRoundSeed	= snOld.iScore;
					}
				}
			}
		}
	}

	// For each entry in SeedNodes:
	// - Add an entry in umPulicIdx, umDomainIdx, and vsnNodes.
	{
		ScopedLock	sl(theApp->getWalletDB()->getDBLock());

		SQL_FOREACH(db, "SELECT PublicKey,Source FROM SeedNodes;")
		{
			std::string	strPublicKey	= db->getStrBinary("PublicKey");
			std::string	strSource		= db->getStrBinary("Source");
			int			iScore			= iSourceScore(static_cast<validatorSource>(strSource[0]));
			strIndex::iterator	siOld	= umPulicIdx.find(strPublicKey);

			if (siOld == umPulicIdx.end())
			{
				// New node
				int			iNode		= vsnNodes.size();

				umPulicIdx[strPublicKey]	= iNode;

				scoreNode	snCurrent;

				snCurrent.strValidator	= strPublicKey;
				snCurrent.iScore		= iScore;
				snCurrent.iRoundSeed	= snCurrent.iScore;
				snCurrent.iRoundScore	= 0;
				snCurrent.iSeen			= -1;

				vsnNodes.push_back(snCurrent);
			}
			else
			{
				scoreNode&	snOld	= vsnNodes[siOld->second];

				if (snOld.iScore < iScore)
				{
					// Update old node

					snOld.iScore		= iScore;
					snOld.iRoundSeed	= snOld.iScore;
				}
			}
		}
	}

	// For debugging, print out initial scores.
	BOOST_FOREACH(scoreNode& sn, vsnNodes)
	{
		std::cerr << str(boost::format("%s| %d, %d, %d")
			% sn.strValidator
			% sn.iScore
			% sn.iRoundScore
			% sn.iRoundSeed)
			<< std::endl;
	}

	// std::cerr << str(boost::format("vsnNodes.size=%d") % vsnNodes.size()) << std::endl;

	// Step through growing list of nodes adding each validation list.
	// - Each validator may have provided referals.  Add those referals as validators.
	for (int iNode=0; iNode != vsnNodes.size(); iNode++)
	{
		scoreNode&			sn				= vsnNodes[iNode];
		std::string&		strValidator	= sn.strValidator;
		std::vector<int>&	viReferrals		= sn.viReferrals;

		ScopedLock	sl(theApp->getWalletDB()->getDBLock());

		SQL_FOREACH(db, str(boost::format("SELECT Referral FROM ValidatorReferrals WHERE Validator=%s ORDER BY Entry;")
					% db->escape(strValidator)))
		{
			std::string	strReferral	= db->getStrBinary("Referral");
			int			iReferral;

			strIndex::iterator	itEntry;

			NewcoinAddress		na;

			if (na.setNodePublic(strReferral))
			{
				// Referring a public key.
				itEntry		= umPulicIdx.find(strReferral);

				if (itEntry == umPulicIdx.end())
				{
					// Not found add public key to list of nodes.
					iReferral				= vsnNodes.size();

					umPulicIdx[strReferral]	= iReferral;

					scoreNode	snCurrent;

					snCurrent.strValidator	= strReferral;
					snCurrent.iScore		= iSourceScore(vsReferral);
					snCurrent.iRoundSeed	= snCurrent.iScore;
					snCurrent.iRoundScore	= 0;
					snCurrent.iSeen			= -1;

					vsnNodes.push_back(snCurrent);
				}
				else
				{
					iReferral	=  itEntry->second;
				}

				// std::cerr << str(boost::format("%s: Public=%s iReferral=%d") % strValidator % strReferral % iReferral) << std::endl;

			}
			else
			{
				// Referring a domain.
				itEntry		= umDomainIdx.find(strReferral);
				iReferral	= itEntry == umDomainIdx.end()
								? -1			// We ignore domains we can't find entires for.
								: itEntry->second;

				// std::cerr << str(boost::format("%s: Domain=%s iReferral=%d") % strValidator % strReferral % iReferral) << std::endl;
			}

			if (iReferral >= 0 && iNode != iReferral)
				viReferrals.push_back(iReferral);
		}
	}

	//
	// Distribute the points from the seeds.
	//
	bool	bDist	= true;

    for (int i = SCORE_ROUNDS; bDist && i--;)
		bDist	= scoreRound(vsnNodes);

	std::cerr << "Scored:" << std::endl;

	BOOST_FOREACH(scoreNode& sn, vsnNodes)
	{
		std::cerr << str(boost::format("%s| %d, %d, %d: [%s]")
			% sn.strValidator
			% sn.iScore
			% sn.iRoundScore
			% sn.iRoundSeed
			% strJoin(sn.viReferrals.begin(), sn.viReferrals.end(), ","))
			<< std::endl;
	}

	// Persist validator scores.
	ScopedLock sl(theApp->getWalletDB()->getDBLock());

	db->executeSQL("BEGIN;");
	db->executeSQL("UPDATE TrustedNodes SET Score = 0 WHERE Score != 0;");

	if (!vsnNodes.empty())
	{
		// Load existing Seens from DB.
		std::vector<std::string>	vstrPublicKeys;

		vstrPublicKeys.resize(vsnNodes.size());

		for (int iNode=vsnNodes.size(); iNode--;)
		{
			vstrPublicKeys[iNode]	= db->escape(vsnNodes[iNode].strValidator);
		}

		SQL_FOREACH(db, str(boost::format("SELECT PublicKey,Seen FROM TrustedNodes WHERE PublicKey IN (%s);")
				% strJoin(vstrPublicKeys.begin(), vstrPublicKeys.end(), ",")))
		{
			vsnNodes[umPulicIdx[db->getStrBinary("PublicKey")]].iSeen	= db->getNull("Seen") ? -1 : db->getInt("Seen");
		}
	}

	boost::unordered_set<std::string>	usUNL;

	if (!vsnNodes.empty())
	{
		// Update the score old entries and add new entries as needed.
		std::vector<std::string>	vstrValues;

		vstrValues.resize(vsnNodes.size());

		for (int iNode=vsnNodes.size(); iNode--;)
		{
			scoreNode&	sn		= vsnNodes[iNode];
			std::string	strSeen	= sn.iSeen >= 0 ? str(boost::format("%d") % sn.iSeen) : "NULL";

			vstrValues[iNode]	= str(boost::format("(%s,%s,%s)")
				% sqlEscape(sn.strValidator)
				% sn.iScore
				% strSeen);

			usUNL.insert(sn.strValidator);
		}

		db->executeSQL(str(boost::format("REPLACE INTO TrustedNodes (PublicKey,Score,Seen) VALUES %s;")
				% strJoin(vstrValues.begin(), vstrValues.end(), ",")));
	}

	{
		ScopedLock	sl(mUNLLock);

		// XXX Should limit to scores above a certain minimum and limit to a certain number.
		mUNL.swap(usUNL);
	}

	// Score IPs.
	db->executeSQL("UPDATE PeerIps SET Score = 0 WHERE Score != 0;");

	boost::unordered_map<std::string, int>	umValidators;

	if (!vsnNodes.empty())
	{
		std::vector<std::string> vstrPublicKeys;

		// For every IpReferral add a score for the IP and PORT.
		SQL_FOREACH(db, "SELECT Validator,COUNT(*) AS Count FROM IpReferrals GROUP BY Validator;")
		{
			umValidators[db->getStrBinary("Validator")]	= db->getInt("Count");

			// std::cerr << strValidator << ":" << db->getInt("Count") << std::endl;
		}
	}

	// For each validator, get each referral and add its score to ip's score.
	// map of pair<IP,Port> :: score
	epScore	umScore;

	std::pair< std::string, int> vc;
	BOOST_FOREACH(vc, umValidators)
	{
		std::string	strValidator	= vc.first;

		strIndex::iterator	itIndex	= umPulicIdx.find(strValidator);
		if (itIndex != umPulicIdx.end()) {
			int			iSeed			= vsnNodes[itIndex->second].iScore;
			int			iEntries		= vc.second;
			score		iTotal			= (iEntries + 1) * iEntries / 2;
			score		iBase			= iSeed * iEntries / iTotal;
			int			iEntry			= 0;

			SQL_FOREACH(db, str(boost::format("SELECT IP,Port FROM IpReferrals WHERE Validator=%s ORDER BY Entry;")
				% db->escape(strValidator)))
			{
				score		iPoints	= iBase * (iEntries - iEntry) / iEntries;
				int			iPort;

				iPort		= db->getNull("Port") ? -1 : db->getInt("Port");

				std::pair< std::string, int>	ep	= std::make_pair(db->getStrBinary("IP"), iPort);

				epScore::iterator	itEp	= umScore.find(ep);

				umScore[ep]	= itEp == umScore.end() ? iPoints :  itEp->second + iPoints;
				iEntry++;
			}
		}
	}

	// Apply validator scores to each IP.
	if (umScore.size())
	{
		std::vector<std::string>	vstrValues;

		vstrValues.reserve(umScore.size());

		std::pair< ipPort, score>	ipScore;
		BOOST_FOREACH(ipScore, umScore)
		{
			ipPort		ipEndpoint	= ipScore.first;
			std::string	strIpPort	= str(boost::format("%s %d") % ipEndpoint.first % ipEndpoint.second);
			score		iPoints		= ipScore.second;

			vstrValues.push_back(str(boost::format("(%s,%d,'%c')")
				% db->escape(strIpPort)
				% iPoints
				% vsValidator));
		}

		// Set scores for each IP.
		db->executeSQL(str(boost::format("REPLACE INTO PeerIps (IpPort,Score,Source) VALUES %s;")
				% strJoin(vstrValues.begin(), vstrValues.end(), ",")));
	}

	db->executeSQL("COMMIT;");
}

// Begin scoring if timer was not cancelled.
void UniqueNodeList::scoreTimerHandler(const boost::system::error_code& err)
{
	if (!err)
	{
		mtpScoreNext	= boost::posix_time::ptime(boost::posix_time::not_a_date_time);	// Timer not set.
		mtpScoreStart	= boost::posix_time::second_clock::universal_time();			// Scoring.

		std::cerr << "Scoring: Start" << std::endl;

		scoreCompute();

		std::cerr << "Scoring: End" << std::endl;

		// Save update time.
		mtpScoreUpdated	= mtpScoreStart;
		miscSave();

		mtpScoreStart	= boost::posix_time::ptime(boost::posix_time::not_a_date_time);	// Not scoring.

		// Score again if needed.
		scoreNext(false);

		// Scan may be dirty due to new ips.
		theApp->getConnectionPool().scanRefresh();
	}
}

// Start a timer to update scores.
// <-- bNow: true, to force scoring for debugging.
void UniqueNodeList::scoreNext(bool bNow)
{
	// std::cerr << str(boost::format("scoreNext: mtpFetchUpdated=%s mtpScoreStart=%s mtpScoreUpdated=%s mtpScoreNext=%s") % mtpFetchUpdated % mtpScoreStart % mtpScoreUpdated % mtpScoreNext)  << std::endl;
	bool	bCanScore	= mtpScoreStart.is_not_a_date_time()							// Not scoring.
							&& !mtpFetchUpdated.is_not_a_date_time();					// Something to score.

	bool	bDirty	=
		(mtpScoreUpdated.is_not_a_date_time() || mtpScoreUpdated <= mtpFetchUpdated)	// Not already scored.
		&& (mtpScoreNext.is_not_a_date_time()											// Timer is not fine.
			|| mtpScoreNext < mtpFetchUpdated + boost::posix_time::seconds(SCORE_DELAY_SECONDS));

	if (!bCanScore)
	{
		nothing();
	}
	else if (bNow || bDirty)
	{
		// Need to update or set timer.
		mtpScoreNext = boost::posix_time::second_clock::universal_time()					// Past now too.
							+ boost::posix_time::seconds(bNow ? 0 : SCORE_DELAY_SECONDS);

		// std::cerr << str(boost::format("scoreNext: @%s") % mtpScoreNext)  << std::endl;
		mdtScoreTimer.expires_at(mtpScoreNext);
		mdtScoreTimer.async_wait(boost::bind(&UniqueNodeList::scoreTimerHandler, this, _1));
	}
}

// For debugging, schedule forced scoring.
void UniqueNodeList::nodeScore()
{
	scoreNext(true);
}

void UniqueNodeList::fetchFinish()
{
	{
		boost::mutex::scoped_lock sl(mFetchLock);
		mFetchActive--;
	}

	fetchNext();
}

// Called when we need to update scores.
void UniqueNodeList::fetchDirty()
{
	// Note update.
	mtpFetchUpdated	= boost::posix_time::second_clock::universal_time();
	miscSave();

	// Update scores.
	scoreNext(false);
}

// Persist the IPs refered to by a Validator.
// --> strSite: source of the IPs (for debugging)
// --> naNodePublic: public key of the validating node.
void UniqueNodeList::processIps(const std::string& strSite, const NewcoinAddress& naNodePublic, section::mapped_type* pmtVecStrIps)
{
	Database*	db=theApp->getWalletDB()->getDB();

	std::string strEscNodePublic	= sqlEscape(naNodePublic.humanNodePublic());

	Log(lsINFO)
		<< str(boost::format("Validator: '%s' processing %d ips.")
			% strSite % ( pmtVecStrIps ? pmtVecStrIps->size() : 0));

	// Remove all current Validator's entries in IpReferrals
	{
		ScopedLock	sl(theApp->getWalletDB()->getDBLock());
		db->executeSQL(str(boost::format("DELETE FROM IpReferrals WHERE Validator=%s;") % strEscNodePublic));
		// XXX Check result.
	}

	// Add new referral entries.
	if (pmtVecStrIps && !pmtVecStrIps->empty()) {
		std::vector<std::string> vstrValues;

		vstrValues.resize(MIN(pmtVecStrIps->size(), REFERRAL_IPS_MAX));

		int	iValues = 0;
		BOOST_FOREACH(std::string strReferral, *pmtVecStrIps)
		{
			if (iValues == REFERRAL_VALIDATORS_MAX)
				break;

			std::string		strIP;
			int				iPort;
			bool			bValid	= parseIpPort(strReferral, strIP, iPort);

			// XXX Filter out private network ips.
			// XXX http://en.wikipedia.org/wiki/Private_network

			if (bValid)
			{
				vstrValues[iValues]	= str(boost::format("(%s,%d,%s,%d)")
					% strEscNodePublic % iValues % db->escape(strIP) % iPort);
				iValues++;
			}
			else
			{
				std::cerr
					<< str(boost::format("Validator: '%s' ["SECTION_IPS"]: rejecting '%s'")
						% strSite % strReferral)
					<< std::endl;
			}
		}

		if (iValues)
		{
			vstrValues.resize(iValues);

			ScopedLock sl(theApp->getWalletDB()->getDBLock());
			db->executeSQL(str(boost::format("INSERT INTO IpReferrals (Validator,Entry,IP,Port) VALUES %s;")
				% strJoin(vstrValues.begin(), vstrValues.end(), ",")));
			// XXX Check result.
		}
	}

	fetchDirty();
}

// Persist ValidatorReferrals.
// --> strSite: source site for display
// --> strValidatorsSrc: source details for display
// --> naNodePublic: remote source public key - not valid for local
// --> vsWhy: reason for adding validator to SeedDomains or SeedNodes.
int UniqueNodeList::processValidators(const std::string& strSite, const std::string& strValidatorsSrc, const NewcoinAddress& naNodePublic, validatorSource vsWhy, section::mapped_type* pmtVecStrValidators)
{
	Database*	db				= theApp->getWalletDB()->getDB();
	std::string strNodePublic	= naNodePublic.isValid() ? naNodePublic.humanNodePublic() : strValidatorsSrc;
	int			iValues			= 0;

	std::cerr
		<< str(boost::format("Validator: '%s' : '%s' : processing %d validators.")
			% strSite
			% strValidatorsSrc
			% ( pmtVecStrValidators ? pmtVecStrValidators->size() : 0))
		<< std::endl;

	// Remove all current Validator's entries in ValidatorReferrals
	{
		ScopedLock	sl(theApp->getWalletDB()->getDBLock());

		db->executeSQL(str(boost::format("DELETE FROM ValidatorReferrals WHERE Validator='%s';") % strNodePublic));
		// XXX Check result.
	}

	// Add new referral entries.
	if (pmtVecStrValidators && pmtVecStrValidators->size()) {
		std::vector<std::string> vstrValues;

		vstrValues.reserve(MIN(pmtVecStrValidators->size(), REFERRAL_VALIDATORS_MAX));

		BOOST_FOREACH(std::string strReferral, *pmtVecStrValidators)
		{
			if (iValues == REFERRAL_VALIDATORS_MAX)
				break;

			boost::smatch	smMatch;

			// domain comment?
			// public_key comment?
			static boost::regex	reReferral("\\`\\s*(\\S+)(?:\\s+(.+))?\\s*\\'");

			if (!boost::regex_match(strReferral, smMatch, reReferral))
			{
				Log(lsWARNING) << str(boost::format("Bad validator: syntax error: %s: %s") % strSite % strReferral);
			}
			else
			{
				std::string		strRefered	= smMatch[1];
				std::string		strComment	= smMatch[2];
				NewcoinAddress	naValidator;

				if (naValidator.setSeedGeneric(strRefered))
				{

					Log(lsWARNING) << str(boost::format("Bad validator: domain or public key required: %s %s") % strRefered % strComment);
				}
				else if (naValidator.setNodePublic(strRefered))
				{
					// A public key.
					// XXX Schedule for CAS lookup.
					nodeAddPublic(naValidator, vsWhy, strComment);

					Log(lsINFO) << str(boost::format("Node Public: %s %s") % strRefered % strComment);

					if (naNodePublic.isValid())
						vstrValues.push_back(str(boost::format("('%s',%d,'%s')") % strNodePublic % iValues % naValidator.humanNodePublic()));

					iValues++;
				}
				else
				{
					// A domain: need to look it up.
					nodeAddDomain(strRefered, vsWhy, strComment);

					Log(lsINFO) << str(boost::format("Node Domain: %s %s") % strRefered % strComment);

					if (naNodePublic.isValid())
						vstrValues.push_back(str(boost::format("('%s',%d,%s)") % strNodePublic % iValues % sqlEscape(strRefered)));

					iValues++;
				}
			}
		}

		if (!vstrValues.empty())
		{
			std::string strSql	= str(boost::format("INSERT INTO ValidatorReferrals (Validator,Entry,Referral) VALUES %s;")
				% strJoin(vstrValues.begin(), vstrValues.end(), ","));

			ScopedLock sl(theApp->getWalletDB()->getDBLock());

			db->executeSQL(strSql);
			// XXX Check result.
		}
	}

	fetchDirty();

	return iValues;
}

// Given a section with IPs, parse and persist it for a validator.
void UniqueNodeList::responseIps(const std::string& strSite, const NewcoinAddress& naNodePublic, const boost::system::error_code& err, const std::string& strIpsFile)
{
	if (!err)
	{
		section			secFile		= ParseSection(strIpsFile, true);

		processIps(strSite, naNodePublic, sectionEntries(secFile, SECTION_IPS));
	}

	fetchFinish();
}

// Process section [ips_url].
// If we have a section with a single entry, fetch the url and process it.
void UniqueNodeList::getIpsUrl(const NewcoinAddress& naNodePublic, section secSite)
{
	std::string	strIpsUrl;
	std::string	strDomain;
	std::string	strPath;

	if (sectionSingleB(secSite, SECTION_IPS_URL, strIpsUrl)
		&& !strIpsUrl.empty()
		&& HttpsClient::httpsParseUrl(strIpsUrl, strDomain, strPath))
	{
		HttpsClient::httpsGet(
			theApp->getIOService(),
			strDomain,
			443,
			strPath,
			NODE_FILE_BYTES_MAX,
			boost::posix_time::seconds(NODE_FETCH_SECONDS),
			boost::bind(&UniqueNodeList::responseIps, this, strDomain, naNodePublic, _1, _2));
	}
	else
	{
		fetchFinish();
	}
}

// After fetching a newcoin.txt from a web site, given a section with validators, parse and persist it.
void UniqueNodeList::responseValidators(const std::string& strValidatorsUrl, const NewcoinAddress& naNodePublic, section secSite, const std::string& strSite, const boost::system::error_code& err, const std::string& strValidatorsFile)
{
	if (!err)
	{
		section		secFile		= ParseSection(strValidatorsFile, true);

		processValidators(strSite, strValidatorsUrl, naNodePublic, vsValidator, sectionEntries(secFile, SECTION_VALIDATORS));
	}

	getIpsUrl(naNodePublic, secSite);
}

// Process section [validators_url].
void UniqueNodeList::getValidatorsUrl(const NewcoinAddress& naNodePublic, section secSite)
{
	std::string strValidatorsUrl;
	std::string	strDomain;
	std::string	strPath;

	if (sectionSingleB(secSite, SECTION_VALIDATORS_URL, strValidatorsUrl)
		&& !strValidatorsUrl.empty()
		&& HttpsClient::httpsParseUrl(strValidatorsUrl, strDomain, strPath))
	{
		HttpsClient::httpsGet(
			theApp->getIOService(),
			strDomain,
			443,
			strPath,
			NODE_FILE_BYTES_MAX,
			boost::posix_time::seconds(NODE_FETCH_SECONDS),
			boost::bind(&UniqueNodeList::responseValidators, this, strValidatorsUrl, naNodePublic, secSite, strDomain, _1, _2));
	}
	else
	{
		getIpsUrl(naNodePublic, secSite);
	}
}

// Process a newcoin.txt.
void UniqueNodeList::processFile(const std::string& strDomain, const NewcoinAddress& naNodePublic, section secSite)
{
	//
	// Process Validators
	//
	processValidators(strDomain, NODE_FILE_NAME, naNodePublic, vsReferral, sectionEntries(secSite, SECTION_VALIDATORS));

	//
	// Process ips
	//
	processIps(strDomain, naNodePublic, sectionEntries(secSite, SECTION_IPS));

	//
	// Process currencies
	//
	section::mapped_type*	pvCurrencies;

	if ((pvCurrencies = sectionEntries(secSite, SECTION_CURRENCIES)) && pvCurrencies->size())
	{
		// XXX Process currencies.
		std::cerr << "Ignoring currencies: not implemented." << std::endl;
	}

	getValidatorsUrl(naNodePublic, secSite);
}

// Given a newcoin.txt, process it.
void UniqueNodeList::responseFetch(const std::string& strDomain, const boost::system::error_code& err, const std::string& strSiteFile)
{
	section				secSite	= ParseSection(strSiteFile, true);
	bool				bGood	= !err;

	if (bGood)
	{
		std::cerr << boost::format("Validator: '%s' received " NODE_FILE_NAME ".") % strDomain << std::endl;
	}
	else
	{
		std::cerr
			<< boost::format("Validator: '%s' unable to retrieve " NODE_FILE_NAME ": %s")
				% strDomain
				% err.message()
			<< std::endl;
	}

	//
	// Verify file domain
	//
	std::string	strSite;

	if (bGood && !sectionSingleB(secSite, SECTION_DOMAIN, strSite))
	{
		bGood	= false;

		std::cerr
			<< boost::format("Validator: '%s' bad " NODE_FILE_NAME " missing single entry for " SECTION_DOMAIN ".")
				% strDomain
			<< std::endl;
	}

	if (bGood && strSite != strDomain)
	{
		bGood	= false;

		std::cerr
			<< boost::format("Validator: '%s' bad " NODE_FILE_NAME " " SECTION_DOMAIN " does not match: %s")
				% strDomain
				% strSite
			<< std::endl;
	}

	//
	// Process public key
	//
	std::string		strNodePublicKey;

	if (bGood && !sectionSingleB(secSite, SECTION_PUBLIC_KEY, strNodePublicKey))
	{
		// Bad [validation_public_key] section.
		bGood	= false;

		std::cerr
			<< boost::format("Validator: '%s' bad " NODE_FILE_NAME " " SECTION_PUBLIC_KEY " does not have single entry.")
				% strDomain
			<< std::endl;
	}

	NewcoinAddress	naNodePublic;

	if (bGood && !naNodePublic.setNodePublic(strNodePublicKey))
	{
		// Bad public key.
		bGood	= false;

		std::cerr
			<< boost::format("Validator: '%s' bad " NODE_FILE_NAME " " SECTION_PUBLIC_KEY " is bad: ")
				% strDomain
				% strNodePublicKey
			<< std::endl;
	}

	if (bGood)
	{
// std::cerr << boost::format("naNodePublic: '%s'") % naNodePublic.humanNodePublic() << std::endl;

		seedDomain	sdCurrent;

		bool		bFound		= getSeedDomains(strDomain, sdCurrent);

		assert(bFound);

		uint256		iSha256		= Serializer::getSHA512Half(strSiteFile);
		bool		bChangedB	= sdCurrent.iSha256	!= iSha256;

		sdCurrent.strDomain		= strDomain;
		// XXX If the node public key is changing, delete old public key information?
		// XXX Only if no other refs to keep it arround, other wise we have an attack vector.
		sdCurrent.naPublicKey	= naNodePublic;

// std::cerr << boost::format("sdCurrent.naPublicKey: '%s'") % sdCurrent.naPublicKey.humanNodePublic() << std::endl;

		sdCurrent.tpFetch		= boost::posix_time::second_clock::universal_time();
		sdCurrent.iSha256		= iSha256;

		setSeedDomains(sdCurrent, true);

		if (bChangedB)
		{
			std::cerr << boost::format("Validator: '%s' processing new " NODE_FILE_NAME ".") % strDomain << std::endl;
			processFile(strDomain, naNodePublic, secSite);
		}
		else
		{
			std::cerr << boost::format("Validator: '%s' no change for " NODE_FILE_NAME ".") % strDomain << std::endl;
			fetchFinish();
		}
	}
	else
	{
		// Failed: Update

		// XXX If we have public key, perhaps try look up in CAS?
		fetchFinish();
	}
}

// Get the newcoin.txt and process it.
void UniqueNodeList::fetchProcess(std::string strDomain)
{
	std::cerr << "Fetching '" NODE_FILE_NAME "' from '" << strDomain << "'." << std::endl;

	std::deque<std::string>	deqSites;

	// Order searching from most specifically for purpose to generic.
	// This order allows the client to take the most burden rather than the servers.
	deqSites.push_back(str(boost::format(SYSTEM_NAME ".%s") % strDomain));
	deqSites.push_back(str(boost::format("www.%s") % strDomain));
	deqSites.push_back(strDomain);

	HttpsClient::httpsGet(
		theApp->getIOService(),
		deqSites,
		443,
		NODE_FILE_PATH,
		NODE_FILE_BYTES_MAX,
		boost::posix_time::seconds(NODE_FETCH_SECONDS),
		boost::bind(&UniqueNodeList::responseFetch, this, strDomain, _1, _2));
}

void UniqueNodeList::fetchTimerHandler(const boost::system::error_code& err)
{
	if (!err)
	{
		// Time to check for another fetch.
		std::cerr << "fetchTimerHandler" << std::endl;
		fetchNext();
	}
}

// Try to process the next fetch of a newcoin.txt.
void UniqueNodeList::fetchNext()
{
	bool	bFull;

	{
		boost::mutex::scoped_lock sl(mFetchLock);

		bFull	= mFetchActive == NODE_FETCH_JOBS;
	}

	if (!bFull)
	{
		// Determine next scan.
		std::string					strDomain;
		boost::posix_time::ptime	tpNext;
		boost::posix_time::ptime	tpNow;

		ScopedLock sl(theApp->getWalletDB()->getDBLock());
		Database *db=theApp->getWalletDB()->getDB();

		if (db->executeSQL("SELECT Domain,Next FROM SeedDomains INDEXED BY SeedDomainNext ORDER BY Next LIMIT 1;")
			&& db->startIterRows())
		{
			int			iNext	= db->getInt("Next");

			tpNext	= ptFromSeconds(iNext);
			tpNow	= boost::posix_time::second_clock::universal_time();

			std::cerr << str(boost::format("fetchNext: iNext=%s tpNext=%s tpNow=%s") % iNext % tpNext % tpNow) << std::endl;
			strDomain	= db->getStrBinary("Domain");

			db->endIterRows();
		}

		if (!strDomain.empty())
		{
			boost::mutex::scoped_lock sl(mFetchLock);

			bFull	= mFetchActive == NODE_FETCH_JOBS;

			if (!bFull && tpNext <= tpNow) {
				mFetchActive++;
			}
		}

		if (strDomain.empty() || bFull)
		{
			std::cerr << str(boost::format("fetchNext: strDomain=%s bFull=%d") % strDomain % bFull) << std::endl;

			nothing();
		}
		else if (tpNext > tpNow)
		{
			std::cerr << str(boost::format("fetchNext: set timer : strDomain=%s") % strDomain) << std::endl;
			// Fetch needs to happen in the future.  Set a timer to wake us.
			mtpFetchNext	= tpNext;

			mdtFetchTimer.expires_at(mtpFetchNext);
			mdtFetchTimer.async_wait(boost::bind(&UniqueNodeList::fetchTimerHandler, this, _1));
		}
		else
		{
			std::cerr << str(boost::format("fetchNext: fetch now: strDomain=%s tpNext=%s tpNow=%s") % strDomain % tpNext %tpNow) << std::endl;
			// Fetch needs to happen now.
			mtpFetchNext	= boost::posix_time::ptime(boost::posix_time::not_a_date_time);

			seedDomain	sdCurrent;
			bool		bFound	= getSeedDomains(strDomain, sdCurrent);

			assert(bFound);

			// Update time of next fetch and this scan attempt.
			sdCurrent.tpScan		= tpNow;

			// XXX Use a longer duration if we have lots of validators.
			sdCurrent.tpNext		= sdCurrent.tpScan+boost::posix_time::hours(7*24);

			setSeedDomains(sdCurrent, false);

			std::cerr << "Validator: '" << strDomain << "' fetching " NODE_FILE_NAME "." << std::endl;

			fetchProcess(strDomain);	// Go get it.

			fetchNext();				// Look for more.
		}
	}
}

// For each kind of source, have a starting number of points to be distributed.
int UniqueNodeList::iSourceScore(validatorSource vsWhy)
{
	int		iScore	= 0;

	switch (vsWhy) {
	case vsConfig:		iScore	= 1500; break;
	case vsInbound:		iScore	=    0; break;
	case vsManual:		iScore	= 1500; break;
	case vsReferral:	iScore	=    0; break;
	case vsTold:		iScore	=    0; break;
	case vsValidator:	iScore	= 1000; break;
	case vsWeb:			iScore	=  200; break;
	default:
		throw std::runtime_error("Internal error: bad validatorSource.");
	}

	return iScore;
}

// Retrieve a SeedDomain from DB.
bool UniqueNodeList::getSeedDomains(const std::string& strDomain, seedDomain& dstSeedDomain)
{
	bool		bResult;
	Database*	db=theApp->getWalletDB()->getDB();

	std::string strSql	= str(boost::format("SELECT * FROM SeedDomains WHERE Domain=%s;")
		% db->escape(strDomain));

	ScopedLock	sl(theApp->getWalletDB()->getDBLock());

	bResult	= db->executeSQL(strSql) && db->startIterRows();
	if (bResult)
	{
		std::string		strPublicKey;
		int				iNext;
		int				iScan;
		int				iFetch;
		std::string		strSha256;

		dstSeedDomain.strDomain	= db->getStrBinary("Domain");

		if (!db->getNull("PublicKey") && db->getStr("PublicKey", strPublicKey))
		{
			dstSeedDomain.naPublicKey.setNodePublic(strPublicKey);
		}
		else
		{
			dstSeedDomain.naPublicKey.clear();
		}

		std::string		strSource	= db->getStrBinary("Source");
			dstSeedDomain.vsSource	= static_cast<validatorSource>(strSource[0]);

		iNext	= db->getInt("Next");
			dstSeedDomain.tpNext	= ptFromSeconds(iNext);
		iScan	= db->getInt("Scan");
			dstSeedDomain.tpScan	= ptFromSeconds(iScan);
		iFetch	= db->getInt("Fetch");
			dstSeedDomain.tpFetch	= ptFromSeconds(iFetch);

		if (!db->getNull("Sha256") && db->getStr("Sha256", strSha256))
		{
			dstSeedDomain.iSha256.SetHex(strSha256);
		}
		else
		{
			dstSeedDomain.iSha256.zero();
		}
		dstSeedDomain.strComment	= db->getStrBinary("Comment");

		db->endIterRows();
	}

	return bResult;
}

// Persist a SeedDomain.
void UniqueNodeList::setSeedDomains(const seedDomain& sdSource, bool bNext)
{
	Database*	db=theApp->getWalletDB()->getDB();

	int		iNext	= iToSeconds(sdSource.tpNext);
	int		iScan	= iToSeconds(sdSource.tpScan);
	int		iFetch	= iToSeconds(sdSource.tpFetch);

	// std::cerr << str(boost::format("setSeedDomains: iNext=%s tpNext=%s") % iNext % sdSource.tpNext) << std::endl;

	std::string strSql	= str(boost::format("REPLACE INTO SeedDomains (Domain,PublicKey,Source,Next,Scan,Fetch,Sha256,Comment) VALUES (%s, %s, %s, %d, %d, %d, '%s', %s);")
		% db->escape(sdSource.strDomain)
		% (sdSource.naPublicKey.isValid() ? db->escape(sdSource.naPublicKey.humanNodePublic()) : "NULL")
		% sqlEscape(std::string(1, static_cast<char>(sdSource.vsSource)))
		% iNext
		% iScan
		% iFetch
		% sdSource.iSha256.GetHex()
		% db->escape(sdSource.strComment)
		);

	ScopedLock	sl(theApp->getWalletDB()->getDBLock());

	if (!db->executeSQL(strSql))
	{
		// XXX Check result.
		std::cerr << "setSeedDomains: failed." << std::endl;
	}

	if (bNext && (mtpFetchNext.is_not_a_date_time() || mtpFetchNext > sdSource.tpNext))
	{
		// Schedule earlier wake up.
		fetchNext();
	}
}

// Queue a domain for a single attempt fetch a newcoin.txt.
// --> strComment: only used on vsManual
// YYY As a lot of these may happen at once, would be nice to wrap multiple calls in a transaction.
void UniqueNodeList::nodeAddDomain(std::string strDomain, validatorSource vsWhy, const std::string& strComment)
{
	boost::trim(strDomain);
	boost::to_lower(strDomain);

	// YYY Would be best to verify strDomain is a valid domain.
	// std::cerr << str(boost::format("nodeAddDomain: '%s' %c '%s'")
	//	% strDomain
	//	% vsWhy
	//	% strComment) << std::endl;

	seedDomain	sdCurrent;

	bool		bFound		= getSeedDomains(strDomain, sdCurrent);
	bool		bChanged	= false;

	if (!bFound)
	{
		sdCurrent.strDomain		= strDomain;
		sdCurrent.tpNext		= boost::posix_time::second_clock::universal_time();
	}

	// Promote source, if needed.
	if (!bFound || iSourceScore(vsWhy) >= iSourceScore(sdCurrent.vsSource))
	{
		sdCurrent.vsSource		= vsWhy;
		sdCurrent.strComment	= strComment;
		bChanged				= true;
	}

	if (vsManual == vsWhy)
	{
		// A manual add forces immediate scan.
		sdCurrent.tpNext		= boost::posix_time::second_clock::universal_time();
		bChanged				= true;
	}

	if (bChanged)
		setSeedDomains(sdCurrent, true);
}

// Retrieve a SeedNode from DB.
bool UniqueNodeList::getSeedNodes(const NewcoinAddress& naNodePublic, seedNode& dstSeedNode)
{
	bool		bResult;
	Database*	db=theApp->getWalletDB()->getDB();

	std::string strSql	= str(boost::format("SELECT * FROM SeedNodes WHERE PublicKey='%s';")
		% naNodePublic.humanNodePublic());

	ScopedLock	sl(theApp->getWalletDB()->getDBLock());

	bResult	= db->executeSQL(strSql) && db->startIterRows();
	if (bResult)
	{
		std::string		strPublicKey;
		std::string		strSource;
		int				iNext;
		int				iScan;
		int				iFetch;
		std::string		strSha256;

		if (!db->getNull("PublicKey") && db->getStr("PublicKey", strPublicKey))
		{
			dstSeedNode.naPublicKey.setNodePublic(strPublicKey);
		}
		else
		{
			dstSeedNode.naPublicKey.clear();
		}

		strSource	= db->getStrBinary("Source");
			dstSeedNode.vsSource	= static_cast<validatorSource>(strSource[0]);

		iNext	= db->getInt("Next");
			dstSeedNode.tpNext	= ptFromSeconds(iNext);
		iScan	= db->getInt("Scan");
			dstSeedNode.tpScan	= ptFromSeconds(iScan);
		iFetch	= db->getInt("Fetch");
			dstSeedNode.tpFetch	= ptFromSeconds(iFetch);

		if (!db->getNull("Sha256") && db->getStr("Sha256", strSha256))
		{
			dstSeedNode.iSha256.SetHex(strSha256);
		}
		else
		{
			dstSeedNode.iSha256.zero();
		}
		dstSeedNode.strComment	= db->getStrBinary("Comment");

		db->endIterRows();
	}

	return bResult;
}

// Persist a SeedNode.
// <-- bNext: true, to do fetching if needed.
void UniqueNodeList::setSeedNodes(const seedNode& snSource, bool bNext)
{
	Database*	db=theApp->getWalletDB()->getDB();

	int		iNext	= iToSeconds(snSource.tpNext);
	int		iScan	= iToSeconds(snSource.tpScan);
	int		iFetch	= iToSeconds(snSource.tpFetch);

	// std::cerr << str(boost::format("setSeedNodes: iNext=%s tpNext=%s") % iNext % sdSource.tpNext) << std::endl;

	assert(snSource.naPublicKey.isValid());

	std::string strSql	= str(boost::format("REPLACE INTO SeedNodes (PublicKey,Source,Next,Scan,Fetch,Sha256,Comment) VALUES ('%s', '%c', %d, %d, %d, '%s', %s);")
		% snSource.naPublicKey.humanNodePublic()
		% static_cast<char>(snSource.vsSource)
		% iNext
		% iScan
		% iFetch
		% snSource.iSha256.GetHex()
		% sqlEscape(snSource.strComment)
		);

	{
		ScopedLock	sl(theApp->getWalletDB()->getDBLock());

		if (!db->executeSQL(strSql))
		{
			// XXX Check result.
			std::cerr << "setSeedNodes: failed." << std::endl;
		}
	}

#if 0
	// YYY When we have a cas schedule lookups similar to this.
	if (bNext && (mtpFetchNext.is_not_a_date_time() || mtpFetchNext > snSource.tpNext))
	{
		// Schedule earlier wake up.
		fetchNext();
	}
#else
	fetchDirty();
#endif
}

// Add a trusted node.  Called by RPC or other source.
void UniqueNodeList::nodeAddPublic(const NewcoinAddress& naNodePublic, validatorSource vsWhy, const std::string& strComment)
{
	seedNode	snCurrent;

	bool		bFound		= getSeedNodes(naNodePublic, snCurrent);
	bool		bChanged	= false;

	if (!bFound)
	{
		snCurrent.naPublicKey	= naNodePublic;
		snCurrent.tpNext		= boost::posix_time::second_clock::universal_time();
	}

	// Promote source, if needed.
	if (!bFound || iSourceScore(vsWhy) >= iSourceScore(snCurrent.vsSource))
	{
		snCurrent.vsSource		= vsWhy;
		snCurrent.strComment	= strComment;
		bChanged				= true;
	}

	if (vsManual == vsWhy)
	{
		// A manual add forces immediate scan.
		snCurrent.tpNext		= boost::posix_time::second_clock::universal_time();
		bChanged				= true;
	}

	if (bChanged)
		setSeedNodes(snCurrent, true);
}

void UniqueNodeList::nodeRemovePublic(const NewcoinAddress& naNodePublic)
{
	{
		Database* db=theApp->getWalletDB()->getDB();
		ScopedLock sl(theApp->getWalletDB()->getDBLock());

		db->executeSQL(str(boost::format("DELETE FROM SeedNodes WHERE PublicKey=%s") % naNodePublic.humanNodePublic()));
	}

	// YYY Only dirty on successful delete.
	fetchDirty();
}

void UniqueNodeList::nodeRemoveDomain(std::string strDomain)
{
	boost::trim(strDomain);
	boost::to_lower(strDomain);

	{
		Database* db=theApp->getWalletDB()->getDB();
		ScopedLock sl(theApp->getWalletDB()->getDBLock());

		db->executeSQL(str(boost::format("DELETE FROM SeedDomains WHERE Domain=%s") % sqlEscape(strDomain)));
	}

	// YYY Only dirty on successful delete.
	fetchDirty();
}

void UniqueNodeList::nodeReset()
{
	{
		Database* db=theApp->getWalletDB()->getDB();

		ScopedLock sl(theApp->getWalletDB()->getDBLock());

		// XXX Check results.
		db->executeSQL("DELETE FROM SeedDomains");
		db->executeSQL("DELETE FROM SeedNodes");
	}

	fetchDirty();
}

Json::Value UniqueNodeList::getUnlJson()
{
	Database* db=theApp->getWalletDB()->getDB();

    Json::Value ret(Json::arrayValue);

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	SQL_FOREACH(db, "SELECT * FROM TrustedNodes;")
	{
		Json::Value node(Json::objectValue);

		node["publicKey"]	= db->getStrBinary("PublicKey");
		node["comment"]		= db->getStrBinary("Comment");

		ret.append(node);
	}

	return ret;
}

bool UniqueNodeList::nodeLoad(boost::filesystem::path pConfig)
{
	if (pConfig.empty())
	{
		std::cerr << VALIDATORS_FILE_NAME " path not specified." << std::endl;

		return false;
	}

	if (!boost::filesystem::exists(pConfig))
	{
		std::cerr << str(boost::format(VALIDATORS_FILE_NAME " not found: %s") % pConfig) << std::endl;

		return false;
	}

	if (!boost::filesystem::is_regular_file(pConfig))
	{
		std::cerr << str(boost::format(VALIDATORS_FILE_NAME " not regular file: %s") % pConfig) << std::endl;

		return false;
	}

	std::ifstream	ifsDefault(pConfig.native().c_str(), std::ios::in);

	if (!ifsDefault)
	{
		std::cerr << str(boost::format(VALIDATORS_FILE_NAME " failed to open: %s") % pConfig) << std::endl;

		return false;
	}

	std::string		strValidators;

	strValidators.assign((std::istreambuf_iterator<char>(ifsDefault)),
		std::istreambuf_iterator<char>());

	if (ifsDefault.bad())
	{
		std::cerr << str(boost::format("Failed to read: %s") % pConfig) << std::endl;

		return false;
	}

	nodeProcess("local", strValidators, pConfig.string());

	std::cerr << str(boost::format("Processing: %s") % pConfig) << std::endl;

	return true;
}

void UniqueNodeList::validatorsResponse(const boost::system::error_code& err, std::string strResponse)
{
	std::cerr << "Fetch '" VALIDATORS_FILE_NAME "' complete." << std::endl;

	if (!err)
	{
		nodeProcess("network", strResponse, theConfig.VALIDATORS_SITE);
	}
	else
	{
		std::cerr << "Error: " << err.message() << std::endl;
	}
}

void UniqueNodeList::nodeNetwork()
{
	HttpsClient::httpsGet(
		theApp->getIOService(),
		theConfig.VALIDATORS_SITE,
		443,
		VALIDATORS_FILE_PATH,
		VALIDATORS_FILE_BYTES_MAX,
		boost::posix_time::seconds(VALIDATORS_FETCH_SECONDS),
		boost::bind(&UniqueNodeList::validatorsResponse, this, _1, _2));
}

void UniqueNodeList::nodeBootstrap()
{
	int			iDomains	= 0;
	int			iNodes		= 0;
	Database*	db			= theApp->getWalletDB()->getDB();

	{
		ScopedLock sl(theApp->getWalletDB()->getDBLock());

		if (db->executeSQL(str(boost::format("SELECT COUNT(*) AS Count FROM SeedDomains WHERE Source='%s' OR Source='%c';") % vsManual % vsValidator)) && db->startIterRows())
			iDomains	= db->getInt("Count");

		if (db->executeSQL(str(boost::format("SELECT COUNT(*) AS Count FROM SeedNodes WHERE Source='%s' OR Source='%c';") % vsManual % vsValidator)) && db->startIterRows())
			iNodes		= db->getInt("Count");
	}

	bool	bLoaded	= iDomains || iNodes;

	// Always merge in the file specified in the config.
	if (!theConfig.UNL_DEFAULT.empty())
	{
		Log(lsINFO) << "Bootstrapping UNL: loading from unl_default.";

		bLoaded	= nodeLoad(theConfig.UNL_DEFAULT);
	}

	// If never loaded anything try the current directory.
	if (!bLoaded && theConfig.UNL_DEFAULT.empty())
	{
		Log(lsINFO) << "Bootstrapping UNL: loading from '" VALIDATORS_FILE_NAME "'.";

		bLoaded	= nodeLoad(VALIDATORS_FILE_NAME);
	}

	// Always load from newcoind.cfg
	if (!theConfig.VALIDATORS.empty())
	{
		NewcoinAddress	naInvalid;	// Don't want a referrer on added entries.

		Log(lsINFO) << "Bootstrapping UNL: loading from " CONFIG_FILE_NAME ".";

		if (processValidators("local", CONFIG_FILE_NAME, naInvalid, vsConfig, &theConfig.VALIDATORS))
			bLoaded	= true;
	}

	if (!bLoaded)
	{
		Log(lsINFO) << "Bootstrapping UNL: loading from " << theConfig.VALIDATORS_SITE << ".";

		nodeNetwork();
	}

	if (!theConfig.IPS.empty())
	{
		std::vector<std::string>	vstrValues;

		vstrValues.reserve(theConfig.IPS.size());

		BOOST_FOREACH(const std::string& strPeer, theConfig.IPS)
		{
			std::string		strIP;
			int				iPort;

			if (parseIpPort(strPeer, strIP, iPort))
			{
				vstrValues.push_back(str(boost::format("(%s,'%c')")
					% sqlEscape(str(boost::format("%s %d") % strIP % iPort))
					% static_cast<char>(vsConfig)));
			}
		}

		if (!vstrValues.empty())
		{
			ScopedLock sl(theApp->getWalletDB()->getDBLock());

			db->executeSQL(str(boost::format("REPLACE INTO PeerIps (IpPort,Source) VALUES %s;")
					% strJoin(vstrValues.begin(), vstrValues.end(), ",")));
		}

		fetchDirty();
	}
}

// Process a validators.txt.
// --> strSite: source of validators
// --> strValidators: contents of a validators.txt
void UniqueNodeList::nodeProcess(const std::string& strSite, const std::string& strValidators, const std::string& strSource) {
	section secValidators	= ParseSection(strValidators, true);

	section::mapped_type*	pmtEntries	= sectionEntries(secValidators, SECTION_VALIDATORS);
	if (pmtEntries)
	{
		NewcoinAddress	naInvalid;	// Don't want a referrer on added entries.

		// YYY Unspecified might be bootstrap or rpc command
		processValidators(strSite, strSource, naInvalid, vsValidator, pmtEntries);
	}
	else
	{
		std::cerr << "WARNING: '" VALIDATORS_FILE_NAME "' missing [" SECTION_VALIDATORS "]." << std::endl;
	}
}

bool UniqueNodeList::nodeInUNL(const NewcoinAddress& naNodePublic)
{
	ScopedLock	sl(mUNLLock);

	return mUNL.end() != mUNL.find(naNodePublic.humanNodePublic());
}
// vim:ts=4
