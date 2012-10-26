#include "Config.h"

#include "utils.h"

#include <boost/lexical_cast.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>

#define SECTION_ACCOUNT_PROBE_MAX		"account_probe_max"
#define SECTION_DEBUG_LOGFILE			"debug_logfile"
#define SECTION_FEE_ACCOUNT_CREATE		"fee_account_create"
#define SECTION_FEE_DEFAULT				"fee_default"
#define SECTION_FEE_NICKNAME_CREATE		"fee_nickname_create"
#define SECTION_FEE_OFFER				"fee_offer"
#define SECTION_FEE_OPERATION			"fee_operation"
#define SECTION_FULL_HISTORY			"full_history"
#define SECTION_IPS						"ips"
#define SECTION_NETWORK_QUORUM			"network_quorum"
#define SECTION_PEER_CONNECT_LOW_WATER	"peer_connect_low_water"
#define SECTION_PEER_IP					"peer_ip"
#define SECTION_PEER_PORT				"peer_port"
#define SECTION_PEER_SCAN_INTERVAL_MIN	"peer_scan_interval_min"
#define SECTION_PEER_SSL_CIPHER_LIST	"peer_ssl_cipher_list"
#define SECTION_PEER_START_MAX			"peer_start_max"
#define SECTION_RPC_ALLOW_REMOTE		"rpc_allow_remote"
#define SECTION_RPC_IP					"rpc_ip"
#define SECTION_RPC_PORT				"rpc_port"
#define SECTION_SNTP					"sntp_servers"
#define SECTION_UNL_DEFAULT				"unl_default"
#define SECTION_VALIDATION_QUORUM		"validation_quorum"
#define SECTION_VALIDATION_SEED			"validation_seed"
#define SECTION_WEBSOCKET_IP			"websocket_ip"
#define SECTION_WEBSOCKET_PORT			"websocket_port"
#define SECTION_VALIDATORS				"validators"
#define SECTION_VALIDATORS_SITE			"validators_site"

// Fees are in XNB.
#define DEFAULT_FEE_DEFAULT				100
#define DEFAULT_FEE_ACCOUNT_CREATE		1000
#define DEFAULT_FEE_NICKNAME_CREATE		1000
#define DEFAULT_FEE_OFFER				DEFAULT_FEE_DEFAULT
#define DEFAULT_FEE_OPERATION			1

Config theConfig;

void Config::setup(const std::string& strConf)
{
	boost::system::error_code	ec;

	//
	// Determine the config and data directories.
	// If the config file is found in the current working directory, use the current working directory as the config directory and
	// that with "db" as the data directory.
	//

	if (!strConf.empty())
	{
		// --conf=<path> : everything is relative that file.
		CONFIG_FILE				= strConf;
		CONFIG_DIR				= CONFIG_FILE;
			CONFIG_DIR.remove_filename();
		DATA_DIR				= CONFIG_DIR / "db";
	}
	else
	{
		CONFIG_DIR				= boost::filesystem::current_path();
		CONFIG_FILE				= CONFIG_DIR / CONFIG_FILE_NAME;
		DATA_DIR				= CONFIG_DIR / "db";

		if (exists(CONFIG_FILE)
			// Can we figure out XDG dirs?
			|| (!getenv("HOME") && (!getenv("XDG_CONFIG_HOME") || !getenv("XDG_DATA_HOME"))))
		{
			// Current working directory is fine, put dbs in a subdir.
			nothing();
		}
		else
		{
			// Construct XDG config and data home.
			// http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
			std::string	strHome				= strGetEnv("HOME");
			std::string	strXdgConfigHome	= strGetEnv("XDG_CONFIG_HOME");
			std::string	strXdgDataHome		= strGetEnv("XDG_DATA_HOME");

			if (strXdgConfigHome.empty())
			{
				// $XDG_CONFIG_HOME was not set, use default based on $HOME.
				strXdgConfigHome	= str(boost::format("%s/.config") % strHome);
			}

			if (strXdgDataHome.empty())
			{
				// $XDG_DATA_HOME was not set, use default based on $HOME.
				strXdgDataHome	= str(boost::format("%s/.local/share") % strHome);
			}

			CONFIG_DIR			= str(boost::format("%s/" SYSTEM_NAME) % strXdgConfigHome);
			CONFIG_FILE			= CONFIG_DIR / CONFIG_FILE_NAME;
			DATA_DIR			= str(boost::format("%s/" SYSTEM_NAME) % strXdgDataHome);

			boost::filesystem::create_directories(CONFIG_DIR, ec);

			if (ec)
				throw std::runtime_error(str(boost::format("Can not create %s") % CONFIG_DIR));
		}
	}

	boost::filesystem::create_directories(DATA_DIR, ec);

	if (ec)
		throw std::runtime_error(str(boost::format("Can not create %s") % DATA_DIR));

	// std::cerr << "CONFIG FILE: " << CONFIG_FILE << std::endl;
	// std::cerr << "CONFIG DIR: " << CONFIG_DIR << std::endl;
	// std::cerr << "DATA DIR: " << DATA_DIR << std::endl;

	//
	// Defaults
	//

	NETWORK_START_TIME		= 1319844908;

	PEER_PORT				= SYSTEM_PEER_PORT;
	RPC_PORT				= 5001;
	WEBSOCKET_PORT			= SYSTEM_WEBSOCKET_PORT;
	NUMBER_CONNECTIONS		= 30;

	// a new ledger every minute
	LEDGER_SECONDS			= 60;
	LEDGER_CREATOR			= false;

	RPC_USER				= "admin";
	RPC_PASSWORD			= "pass";
	RPC_ALLOW_REMOTE		= false;

	PEER_SSL_CIPHER_LIST	= DEFAULT_PEER_SSL_CIPHER_LIST;
	PEER_SCAN_INTERVAL_MIN	= DEFAULT_PEER_SCAN_INTERVAL_MIN;

	PEER_START_MAX			= DEFAULT_PEER_START_MAX;
	PEER_CONNECT_LOW_WATER	= DEFAULT_PEER_CONNECT_LOW_WATER;

	TRANSACTION_FEE_BASE	= 1000;

	NETWORK_QUORUM			= 0;	// Don't need to see other nodes
	VALIDATION_QUORUM		= 1;	// Only need one node to vouch

	FEE_ACCOUNT_CREATE		= DEFAULT_FEE_ACCOUNT_CREATE;
	FEE_NICKNAME_CREATE		= DEFAULT_FEE_NICKNAME_CREATE;
	FEE_OFFER				= DEFAULT_FEE_OFFER;
	FEE_DEFAULT				= DEFAULT_FEE_DEFAULT;
	FEE_CONTRACT_OPERATION  = DEFAULT_FEE_OPERATION;

	FULL_HISTORY			= false;

	ACCOUNT_PROBE_MAX		= 10;

	VALIDATORS_SITE			= DEFAULT_VALIDATORS_SITE;

	RUN_STANDALONE			= false;
	START_UP				= NORMAL;

	load();
}

void Config::load()
{
	std::ifstream	ifsConfig(CONFIG_FILE.c_str(), std::ios::in);

	if (!ifsConfig)
	{
		std::cerr << "Failed to open '" << CONFIG_FILE << "'." << std::endl;
	}
	else
	{
		std::string	strConfigFile;

		strConfigFile.assign((std::istreambuf_iterator<char>(ifsConfig)),
			std::istreambuf_iterator<char>());

		if (ifsConfig.bad())
		{
			std::cerr << "Failed to read '" << CONFIG_FILE << "'." << std::endl;
		}
		else
		{
			section		secConfig	= ParseSection(strConfigFile, true);
			std::string	strTemp;

			// XXX Leak
			section::mapped_type*	smtTmp;

			smtTmp	= sectionEntries(secConfig, SECTION_VALIDATORS);
			if (smtTmp)
			{
				VALIDATORS	= *smtTmp;
				// sectionEntriesPrint(&VALIDATORS, SECTION_VALIDATORS);
			}

			smtTmp	= sectionEntries(secConfig, SECTION_IPS);
			if (smtTmp)
			{
				IPS	= *smtTmp;
				// sectionEntriesPrint(&IPS, SECTION_IPS);
			}

			smtTmp = sectionEntries(secConfig, SECTION_SNTP);
			if (smtTmp)
			{
				SNTP_SERVERS = *smtTmp;
			}

			(void) sectionSingleB(secConfig, SECTION_VALIDATORS_SITE, VALIDATORS_SITE);

			(void) sectionSingleB(secConfig, SECTION_PEER_IP, PEER_IP);

			if (sectionSingleB(secConfig, SECTION_PEER_PORT, strTemp))
				PEER_PORT			= boost::lexical_cast<int>(strTemp);

			(void) sectionSingleB(secConfig, SECTION_RPC_IP, RPC_IP);

			if (sectionSingleB(secConfig, SECTION_RPC_PORT, strTemp))
				RPC_PORT = boost::lexical_cast<int>(strTemp);

			if (sectionSingleB(secConfig, "ledger_creator" , strTemp))
				LEDGER_CREATOR = boost::lexical_cast<bool>(strTemp);

			if (sectionSingleB(secConfig, SECTION_RPC_ALLOW_REMOTE, strTemp))
				RPC_ALLOW_REMOTE	= boost::lexical_cast<bool>(strTemp);

			(void) sectionSingleB(secConfig, SECTION_WEBSOCKET_IP, WEBSOCKET_IP);

			if (sectionSingleB(secConfig, SECTION_WEBSOCKET_PORT, strTemp))
				WEBSOCKET_PORT		= boost::lexical_cast<int>(strTemp);

			if (sectionSingleB(secConfig, SECTION_VALIDATION_SEED, strTemp))
			{
				VALIDATION_SEED.setSeedGeneric(strTemp);
				if (VALIDATION_SEED.isValid())
				{
					VALIDATION_PUB = RippleAddress::createNodePublic(VALIDATION_SEED);
					VALIDATION_PRIV = RippleAddress::createNodePrivate(VALIDATION_SEED);
				}
			}

			(void) sectionSingleB(secConfig, SECTION_PEER_SSL_CIPHER_LIST, PEER_SSL_CIPHER_LIST);

			if (sectionSingleB(secConfig, SECTION_PEER_SCAN_INTERVAL_MIN, strTemp))
				// Minimum for min is 60 seconds.
				PEER_SCAN_INTERVAL_MIN = std::max(60, boost::lexical_cast<int>(strTemp));

			if (sectionSingleB(secConfig, SECTION_PEER_START_MAX, strTemp))
				PEER_START_MAX		= std::max(1, boost::lexical_cast<int>(strTemp));

			if (sectionSingleB(secConfig, SECTION_PEER_CONNECT_LOW_WATER, strTemp))
				PEER_CONNECT_LOW_WATER = std::max(1, boost::lexical_cast<int>(strTemp));

			if (sectionSingleB(secConfig, SECTION_NETWORK_QUORUM, strTemp))
				NETWORK_QUORUM		= std::max(0, boost::lexical_cast<int>(strTemp));

			if (sectionSingleB(secConfig, SECTION_VALIDATION_QUORUM, strTemp))
				VALIDATION_QUORUM	= std::max(0, boost::lexical_cast<int>(strTemp));

			if (sectionSingleB(secConfig, SECTION_FEE_ACCOUNT_CREATE, strTemp))
				FEE_ACCOUNT_CREATE	= boost::lexical_cast<int>(strTemp);

			if (sectionSingleB(secConfig, SECTION_FEE_NICKNAME_CREATE, strTemp))
				FEE_NICKNAME_CREATE	= boost::lexical_cast<int>(strTemp);

			if (sectionSingleB(secConfig, SECTION_FEE_OFFER, strTemp))
				FEE_OFFER			= boost::lexical_cast<int>(strTemp);

			if (sectionSingleB(secConfig, SECTION_FEE_DEFAULT, strTemp))
				FEE_DEFAULT			= boost::lexical_cast<int>(strTemp);

			if (sectionSingleB(secConfig, SECTION_FEE_OPERATION, strTemp))
				FEE_CONTRACT_OPERATION	= boost::lexical_cast<int>(strTemp);

			if (sectionSingleB(secConfig, SECTION_FULL_HISTORY, strTemp))
				FULL_HISTORY = boost::lexical_cast<bool>(strTemp);

			if (sectionSingleB(secConfig, SECTION_ACCOUNT_PROBE_MAX, strTemp))
				ACCOUNT_PROBE_MAX	= boost::lexical_cast<int>(strTemp);

			if (sectionSingleB(secConfig, SECTION_UNL_DEFAULT, strTemp))
				UNL_DEFAULT			= strTemp;

			if (sectionSingleB(secConfig, SECTION_DEBUG_LOGFILE, strTemp))
				DEBUG_LOGFILE		= strTemp;
		}
	}
}

// vim:ts=4
