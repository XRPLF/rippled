#pragma once

#include <xrpl/json/json_value.h>

namespace xrpl::jss {

// JSON static strings

#define JSS(x) constexpr ::Json::StaticString x(#x)

/* These "StaticString" field names are used instead of string literals to
   optimize the performance of accessing properties of Json::Value objects.

   Most strings have a trailing comment. Here is the legend:

   in: Read by the given RPC handler from its `Json::Value` parameter.
   out: Assigned by the given RPC handler in the `Json::Value` it returns.
   field: A field of at least one type of transaction.
   RPC: Common properties of RPC requests and responses.
   error: Common properties of RPC error responses.
*/

JSS(kAL_SIZE);                     // out: GetCounts
JSS(kAL_HIT_RATE);                 // out: GetCounts
JSS(kACCEPTED_CREDENTIALS);        // out: AccountObjects
JSS(kACCOUNT_SET_FLAGS);           // out: RPC server_definitions
JSS(kACCOUNT);                     // in: TransactionSign; field.
JSS(kAMMID);                       // field
JSS(kAMOUNT);                      // in: TransactionSign; field.
JSS(kAMOUNT2);                     // in/out: AMM IOU/XRP pool, deposit, withdraw amount
JSS(kASSET);                       // in: AMM Asset1
JSS(kASSET2);                      // in: AMM Asset2
JSS(kASSET_CLASS);                 // in: Oracle
JSS(kASSET_PRICE);                 // in: Oracle
JSS(kAUTH_ACCOUNT);                // in: AMM Auction Slot
JSS(kAUTH_ACCOUNTS);               // in: AMM Auction Slot
JSS(kBASE_ASSET);                  // in: Oracle
JSS(kBID_MAX);                     // in: AMM Bid
JSS(kBID_MIN);                     // in: AMM Bid
JSS(kCLEAR_FLAG);                  // field.
JSS(kCOUNTERPARTY);                // field.
JSS(kCOUNTERPARTY_SIGNATURE);      // field.
JSS(kDELIVER_MAX);                 // out: alias to Amount
JSS(kDELIVER_MIN);                 // in: TransactionSign
JSS(kDESTINATION);                 // in: TransactionSign; field.
JSS(kE_PRICE);                     // in: AMM Deposit option
JSS(kFEE);                         // in/out: TransactionSign; field.
JSS(kFLAGS);                       // in/out: TransactionSign; field.
JSS(kHOLDER);                      // field.
JSS(kINVALID);                     //
JSS(kISSUER);                      // in: Credential transactions
JSS(kISSUING_CHAIN_DOOR);          // field.
JSS(kISSUING_CHAIN_ISSUE);         // field.
JSS(kLAST_LEDGER_SEQUENCE);        // in: TransactionSign; field
JSS(kLAST_UPDATE_TIME);            // field.
JSS(kLIMIT_AMOUNT);                // field.
JSS(kLOCKING_CHAIN_DOOR);          // field.
JSS(kLOCKING_CHAIN_ISSUE);         // field.
JSS(kNETWORK_ID);                  // field.
JSS(kLP_TOKEN_OUT);                // in: AMM Liquidity Provider deposit tokens
JSS(kLP_TOKEN_IN);                 // in: AMM Liquidity Provider withdraw tokens
JSS(kLP_TOKEN);                    // out: AMM Liquidity Provider tokens info
JSS(kOFFER_SEQUENCE);              // field.
JSS(kORACLE_DOCUMENT_ID);          // field
JSS(kOWNER);                       // field
JSS(kPATHS);                       // in/out: TransactionSign
JSS(kPRICE_DATA_SERIES);           // field.
JSS(kPRICE_DATA);                  // field.
JSS(kPROVIDER);                    // field.
JSS(kQUOTE_ASSET);                 // in: Oracle.
JSS(kRAW_TRANSACTION);             // in: Batch
JSS(kRAW_TRANSACTIONS);            // in: Batch
JSS(kSLE_HIT_RATE);                // out: GetCounts.
JSS(kSCALE);                       // field.
JSS(kSETTLE_DELAY);                // in: TransactionSign
JSS(kSEND_MAX);                    // in: TransactionSign
JSS(kSEQUENCE);                    // in/out: TransactionSign; field.
JSS(kSET_FLAG);                    // field.
JSS(kSIGNER);                      // field.
JSS(kSIGNERS);                     // field.
JSS(kSIGNING_PUB_KEY);             // field.
JSS(kSUBJECT);                     // in: Credential transactions
JSS(kTAKER_GETS);                  // field.
JSS(kTAKER_PAYS);                  // field.
JSS(kTRADING_FEE);                 // in/out: AMM trading fee
JSS(kTRANSACTION_TYPE);            // in: TransactionSign.
JSS(kTRANSFER_RATE);               // in: TransferRate.
JSS(kTXN_SIGNATURE);               // field.
JSS(kURI);                         // field.
JSS(kVOTE_SLOTS);                  // out: AMM Vote
JSS(kABORTED);                     // out: InboundLedger
JSS(kACCEPTED);                    // out: LedgerToJson, OwnerInfo, SubmitTransaction
JSS(kACCOUNT_STATE);               // out: LedgerToJson
JSS(kACCOUNT_TREE_HASH);           // out: ledger/Ledger.cpp
JSS(kACCOUNT_DATA);                // out: AccountInfo
JSS(kACCOUNT_FLAGS);               // out: AccountInfo
JSS(kACCOUNT_HASH);                // out: LedgerToJson
JSS(kACCOUNT_ID);                  // out: WalletPropose
JSS(kACCOUNT_NFTS);                // out: AccountNFTs
JSS(kACCOUNT_OBJECTS);             // out: AccountObjects
JSS(kACCOUNT_ROOT);                // in: LedgerEntry
JSS(kACCOUNT_SEQUENCE_NEXT);       // out: SubmitTransaction
JSS(kACCOUNT_SEQUENCE_AVAILABLE);  // out: SubmitTransaction
JSS(kACCOUNT_HISTORY_TX_STREAM);   // in: Subscribe, Unsubscribe
JSS(kACCOUNT_HISTORY_TX_INDEX);    // out: Account txn history subscribe
JSS(kACCOUNT_HISTORY_TX_FIRST);    // out: Account txn history subscribe
JSS(kACCOUNT_HISTORY_BOUNDARY);    // out: Account txn history subscribe
JSS(kACCOUNTS);                    // in: LedgerEntry, Subscribe, handlers/Ledger, Unsubscribe
JSS(kACCOUNTS_PROPOSED);           // in: Subscribe, Unsubscribe
JSS(kACTION);                      //
JSS(kACTIVE);                      // out: OverlayImpl
JSS(kACQUIRING);                   // out: LedgerRequest
JSS(kADDRESS);                     // out: PeerImp
JSS(kAFFECTED);                    // out: AcceptedLedgerTx
JSS(kAGE);                         // out: NetworkOPs, Peers
JSS(kALTERNATIVES);                // out: PathRequest, RipplePathFind
JSS(kAMENDMENT_BLOCKED);           // out: NetworkOPs
JSS(kAMM_ACCOUNT);                 // in: amm_info
JSS(kAMOUNT);                      // out: AccountChannels, amm_info
JSS(kAMOUNT2);                     // out: amm_info
JSS(kAPI_VERSION);                 // in: many, out: Version
JSS(kAPI_VERSION_LOW);             // out: Version
JSS(kAPPLIED);                     // out: SubmitTransaction
JSS(kASKS);                        // out: Subscribe
JSS(kASSET);                       // in: amm_info
JSS(kASSET2);                      // in: amm_info
JSS(kASSETS);                      // out: GatewayBalances
JSS(kASSET_FROZEN);                // out: amm_info
JSS(kASSET2_FROZEN);               // out: amm_info
JSS(kATTESTATIONS);                //
JSS(kATTESTATION_REWARD_ACCOUNT);  //
JSS(kAUCTION_SLOT);                // out: amm_info
JSS(kAUTHORIZED);                  // out: AccountLines
JSS(kAUTHORIZE);                   // out: delegate
JSS(kAUTHORIZED_CREDENTIALS);      // in: ledger_entry DepositPreauth
JSS(kAUTH_ACCOUNTS);               // out: amm_info
JSS(kAUTH_CHANGE);                 // out: AccountInfo
JSS(kAUTH_CHANGE_QUEUED);          // out: AccountInfo
JSS(kAVAILABLE);                   // out: ValidatorList
JSS(kAVG_BPS_RECV);                // out: Peers
JSS(kAVG_BPS_SENT);                // out: Peers
JSS(kBALANCE);                     // out: AccountLines
JSS(kBALANCES);                    // out: GatewayBalances
JSS(kBASE);                        // out: LogLevel
JSS(kBASE_ASSET);                  // in: get_aggregate_price
JSS(kBASE_FEE);                    // out: NetworkOPs
JSS(kBASE_FEE_XRP);                // out: NetworkOPs
JSS(kBIDS);                        // out: Subscribe
JSS(kBINARY);                      // in: AccountTX, LedgerEntry, AccountTxOld, Tx LedgerData
JSS(kBLOB);                        // out: ValidatorList
JSS(kBLOBS_V2);                    // out: ValidatorList
                                   // in: UNL
JSS(kBOOKS);                       // in: Subscribe, Unsubscribe
JSS(kBOTH);                        // in: Subscribe, Unsubscribe
JSS(kBOTH_SIDES);                  // in: Subscribe, Unsubscribe
JSS(kBRANCH);                      // out: server_info
JSS(kBROADCAST);                   // out: SubmitTransaction
JSS(kBRIDGE_ACCOUNT);              // in: LedgerEntry
JSS(kBUILD_PATH);                  // in: TransactionSign
JSS(kBUILD_VERSION);               // out: NetworkOPs
JSS(kCANCEL_AFTER);                // out: AccountChannels
JSS(kCAN_DELETE);                  // out: CanDelete
JSS(kMPT_AMOUNT);                  // out: mpt_holders
JSS(kMPT_ISSUANCE_ID);             // in: Payment, mpt_holders
JSS(kMPTOKEN_INDEX);               // out: mpt_holders
JSS(kCHANGES);                     // out: BookChanges
JSS(kCHANNEL_ID);                  // out: AccountChannels
JSS(kCHANNELS);                    // out: AccountChannels
JSS(kCHECK_NODES);                 // in: LedgerCleaner
JSS(kCLEAR);                       // in/out: FetchInfo
JSS(kCLOSE);                       // out: BookChanges
JSS(kCLOSE_FLAGS);                 // out: LedgerToJson
JSS(kCLOSE_TIME);                  // in: Application, out: NetworkOPs, RCLCxPeerPos, LedgerToJson
JSS(kCLOSE_TIME_ISO);              // out: Tx, NetworkOPs, TransactionEntry AccountTx, LedgerToJson
JSS(kCLOSE_TIME_ESTIMATED);        // in: Application, out: LedgerToJson
JSS(kCLOSE_TIME_HUMAN);            // out: LedgerToJson
JSS(kCLOSE_TIME_OFFSET);           // out: NetworkOPs
JSS(kCLOSE_TIME_RESOLUTION);       // in: Application; out: LedgerToJson
JSS(kCLOSED);                      // out: NetworkOPs, LedgerToJson, handlers/Ledger
JSS(kCLOSED_LEDGER);               // out: NetworkOPs
JSS(kCLUSTER);                     // out: PeerImp
JSS(kCODE);                        // out: errors
JSS(kCOMMAND);                     // in: RPCHandler
JSS(kCOMMON);                      // out: RPC server_definitions
JSS(kCOMPLETE);                    // out: NetworkOPs, InboundLedger
JSS(kCOMPLETE_LEDGERS);            // out: NetworkOPs, PeerImp
JSS(kCONSENSUS);                   // out: NetworkOPs, LedgerConsensus
JSS(kCONVERGE_TIME);               // out: NetworkOPs
JSS(kCONVERGE_TIME_S);             // out: NetworkOPs
JSS(kCOOKIE);                      // out: NetworkOPs
JSS(kCOUNT);                       // in: AccountTx*, ValidatorList
JSS(kCOUNTERS);                    // in/out: retrieve counters
JSS(kCREDENTIALS);                 // in: deposit_authorized
JSS(kCREDENTIAL_TYPE);             // in: LedgerEntry DepositPreauth
JSS(kCTID);                        // in/out: Tx RPC
JSS(kCURRENCY_A);                  // out: BookChanges
JSS(kCURRENCY_B);                  // out: BookChanges
JSS(kCURRENCY);                    // in: paths/PathRequest, STAmount
                                   // out: STPathSet, STAmount, AccountLines
JSS(kCURRENT);                     // out: OwnerInfo
JSS(kCURRENT_ACTIVITIES);          //
JSS(kCURRENT_LEDGER_SIZE);         // out: TxQ
JSS(kCURRENT_QUEUE_SIZE);          // out: TxQ
JSS(kDATA);                        // out: LedgerData
JSS(kDATE);                        // out: tx/Transaction, NetworkOPs
JSS(kDB_KB_LEDGER);                // out: getCounts
JSS(kDB_KB_TOTAL);                 // out: getCounts
JSS(kDB_KB_TRANSACTION);           // out: getCounts
JSS(kDEBUG_SIGNING);               // in: TransactionSign
JSS(kDELETION_BLOCKERS_ONLY);      // in: AccountObjects
JSS(kDELIVERED_AMOUNT);            // out: insertDeliveredAmount
JSS(kDEPOSIT_AUTHORIZED);          // out: deposit_authorized
JSS(kDEPRECATED);                  //
JSS(kDESCENDING);                  // in: AccountTx*
JSS(kDESCRIPTION);                 // in/out: Reservations
JSS(kDESTINATION);                 // in: nft_buy_offers, nft_sell_offers
JSS(kDESTINATION_ACCOUNT);         // in: PathRequest, RipplePathFind, account_lines
                                   // out: AccountChannels
JSS(kDESTINATION_AMOUNT);          // in: PathRequest, RipplePathFind
JSS(kDESTINATION_CURRENCIES);      // in: PathRequest, RipplePathFind
JSS(kDESTINATION_TAG);             // in: PathRequest
                                   // out: AccountChannels
JSS(kDETAILS);                     // out: Manifest, server_info
JSS(kDIR_ENTRY);                   // out: DirectoryEntryIterator
JSS(kDIR_INDEX);                   // out: DirectoryEntryIterator
JSS(kDIR_ROOT);                    // out: DirectoryEntryIterator
JSS(kDISCOUNTED_FEE);              // out: amm_info
JSS(kDOMAIN);                      // out: ValidatorInfo, Manifest
JSS(kDROPS);                       // out: TxQ
JSS(kDURATION_US);                 // out: NetworkOPs
JSS(kEFFECTIVE);                   // out: ValidatorList
                                   // in: UNL
JSS(kENABLED);                     // out: AmendmentTable
JSS(kENGINE_RESULT);               // out: NetworkOPs, TransactionSign, Submit
JSS(kENGINE_RESULT_CODE);          // out: NetworkOPs, TransactionSign, Submit
JSS(kENGINE_RESULT_MESSAGE);       // out: NetworkOPs, TransactionSign, Submit
JSS(kENTIRE_SET);                  // out: get_aggregate_price
JSS(kEPHEMERAL_KEY);               // out: ValidatorInfo
                                   // in/out: Manifest
JSS(kERROR);                       // out: error
JSS(kERRORED);                     //
JSS(kERROR_CODE);                  // out: error
JSS(kERROR_EXCEPTION);             // out: Submit
JSS(kERROR_MESSAGE);               // out: error
JSS(kEXPAND);                      // in: handler/Ledger
JSS(kEXPECTED_DATE);               // out: any (warnings)
JSS(kEXPECTED_DATE_UTC);           // out: any (warnings)
JSS(kEXPECTED_LEDGER_SIZE);        // out: TxQ
JSS(kEXPIRATION);                  // out: AccountOffers, AccountChannels, ValidatorList, amm_info
JSS(kFAIL_HARD);                   // in: Sign, Submit
JSS(kFAILED);                      // out: InboundLedger
JSS(kFEATURE);                     // in: Feature
JSS(kFEATURES);                    // out: Feature
JSS(kFEE_BASE);                    // out: NetworkOPs
JSS(kFEE_DIV_MAX);                 // in: TransactionSign
JSS(kFEE_LEVEL);                   // out: AccountInfo
JSS(kFEE_MULT_MAX);                // in: TransactionSign
JSS(kFEE_REF);                     // out: NetworkOPs, DEPRECATED
JSS(kFETCH_PACK);                  // out: NetworkOPs
JSS(kFIELDS);                      // out: RPC server_definitions
                                   // matches definitions.json format
JSS(kFIRST);                       // out: rpc/Version
JSS(kFINISHED);                    //
JSS(kFIX_TXNS);                    // in: LedgerCleaner
JSS(kFLAGS);                       // out: AccountOffers, NetworkOPs
JSS(kFORWARD);                     // in: AccountTx
JSS(kFREEZE);                      // out: AccountLines
JSS(kFREEZE_PEER);                 // out: AccountLines
JSS(kDEEP_FREEZE);                 // out: AccountLines
JSS(kDEEP_FREEZE_PEER);            // out: AccountLines
JSS(kFROZEN_BALANCES);             // out: GatewayBalances
JSS(kFULL);                        // in: LedgerClearer, handlers/Ledger
JSS(kFULL_REPLY);                  // out: PathFind
JSS(kFULLBELOW_SIZE);              // out: GetCounts
JSS(kGIT);                         // out: server_info
JSS(kGOOD);                        // out: RPCVersion
JSS(kHASH);                        // out: NetworkOPs, InboundLedger, LedgerToJson, STTx; field
JSS(kHAVE_HEADER);                 // out: InboundLedger
JSS(kHAVE_STATE);                  // out: InboundLedger
JSS(kHAVE_TRANSACTIONS);           // out: InboundLedger
JSS(kHIGH);                        // out: BookChanges
JSS(kHIGHEST_SEQUENCE);            // out: AccountInfo
JSS(kHIGHEST_TICKET);              // out: AccountInfo
JSS(kHISTORICAL_PERMINUTE);        // historical_perminute.
JSS(kHOLDERS);                     // out: MPTHolders
JSS(kHOSTID);                      // out: NetworkOPs
JSS(kHOTWALLET);                   // in: GatewayBalances
JSS(kID);                          // websocket.
JSS(kIDENT);                       // in: AccountCurrencies, AccountInfo, OwnerInfo
JSS(kIGNORE_DEFAULT);              // in: AccountLines
JSS(kIN);                          // out: OverlayImpl
JSS(kIN_LEDGER);                   // out: tx/Transaction
JSS(kINBOUND);                     // out: PeerImp
JSS(kINDEX);                       // in: LedgerEntry
                                   // out: STLedgerEntry, LedgerEntry, TxHistory, LedgerData
JSS(kINFO);                        // out: ServerInfo, ConsensusInfo, FetchInfo
JSS(kINITIAL_SYNC_DURATION_US);    //
JSS(kINTERNAL_COMMAND);            // in: Internal
JSS(kINVALID_API_VERSION);         // out: Many, when a request has an invalid version
JSS(kIO_LATENCY_MS);               // out: NetworkOPs
JSS(kIP);                          // in: Connect, out: OverlayImpl
JSS(kIS_BURNED);                   // out: nft_info (clio)
JSS(kIS_SERIALIZED);               // out: RPC server_definitions
                                   // matches definitions.json format
JSS(kIS_SIGNING_FIELD);            // out: RPC server_definitions
                                   // matches definitions.json format
JSS(kIS_VL_ENCODED);               // out: RPC server_definitions
                                   // matches definitions.json format
JSS(kISSUER);                      // in: RipplePathFind, Subscribe, Unsubscribe, BookOffers
                                   // out: STPathSet, STAmount
JSS(kJOB);                         //
JSS(kJOB_QUEUE);                   //
JSS(kJOBS);                        //
JSS(kJSONRPC);                     // json version
JSS(kJQ_TRANS_OVERFLOW);           // JobQueue transaction limit overflow.
JSS(kEPT);                         // out: SubmitTransaction
JSS(kEY);                          // out
JSS(kEY_TYPE);                     // in/out: WalletPropose, TransactionSign
JSS(kLATENCY);                     // out: PeerImp
JSS(kLAST);                        // out: RPCVersion
JSS(kLAST_CLOSE);                  // out: NetworkOPs
JSS(kLAST_REFRESH_TIME);           // out: ValidatorSite
JSS(kLAST_REFRESH_STATUS);         // out: ValidatorSite
JSS(kLAST_REFRESH_MESSAGE);        // out: ValidatorSite
JSS(kLEDGER);                      // in: NetworkOPs, LedgerCleaner, RPCHelpers
                                   // out: NetworkOPs, PeerImp
JSS(kLEDGER_CURRENT_INDEX);        // out: NetworkOPs, RPCHelpers, LedgerCurrent, LedgerAccept,
                                   //      AccountLines
JSS(kLEDGER_DATA);                 // out: LedgerHeader
JSS(kLEDGER_HASH);                 // in: RPCHelpers, LedgerRequest, RipplePathFind,
                                   //     TransactionEntry, handlers/Ledger
                                   // out: NetworkOPs, RPCHelpers, LedgerClosed, LedgerData,
                                   //      AccountLines
JSS(kLEDGER_HIT_RATE);             // out: GetCounts
JSS(kLEDGER_INDEX);                // in/out: many
JSS(kLEDGER_INDEX_MAX);            // in, out: AccountTx*
JSS(kLEDGER_INDEX_MIN);            // in, out: AccountTx*
JSS(kLEDGER_MAX);                  // in, out: AccountTx*
JSS(kLEDGER_MIN);                  // in, out: AccountTx*
JSS(kLEDGER_TIME);                 // out: NetworkOPs
JSS(kLEDGER_ENTRY_TYPES);          // out: RPC server_definitions
                                   // matches definitions.json format
JSS(kLEDGER_ENTRY_FLAGS);          // out: RPC server_definitions
JSS(kLEDGER_ENTRY_FORMATS);        // out: RPC server_definitions
JSS(kLEVELS);                      // LogLevels
JSS(kLIMIT);                // in/out: AccountTx*, AccountOffers, AccountLines, AccountObjects
                            // in: LedgerData, BookOffers
JSS(kLIMIT_PEER);           // out: AccountLines
JSS(kLINES);                // out: AccountLines
JSS(kLIST);                 // out: ValidatorList
JSS(kLOAD);                 // out: NetworkOPs, PeerImp
JSS(kLOAD_BASE);            // out: NetworkOPs
JSS(kLOAD_FACTOR);          // out: NetworkOPs
JSS(kLOAD_FACTOR_CLUSTER);  // out: NetworkOPs
JSS(kLOAD_FACTOR_FEE_ESCALATION);  // out: NetworkOPs
JSS(kLOAD_FACTOR_FEE_QUEUE);       // out: NetworkOPs
JSS(kLOAD_FACTOR_FEE_REFERENCE);   // out: NetworkOPs
JSS(kLOAD_FACTOR_LOCAL);           // out: NetworkOPs
JSS(kLOAD_FACTOR_NET);             // out: NetworkOPs
JSS(kLOAD_FACTOR_SERVER);          // out: NetworkOPs
JSS(kLOAD_FEE);                    // out: LoadFeeTrackImp, NetworkOPs
JSS(kLOAN_BROKER_ID);              // in: LedgerEntry
JSS(kLOAN_SEQ);                    // in: LedgerEntry
JSS(kLOCAL);                       // out: resource/Logic.h
JSS(kLOCAL_TXS);                   // out: GetCounts
JSS(kLOCAL_STATIC_KEYS);           // out: ValidatorList
JSS(kLOCKED);                      // out: GatewayBalances
JSS(kLOW);                         // out: BookChanges
JSS(kLOWEST_SEQUENCE);             // out: AccountInfo
JSS(kLOWEST_TICKET);               // out: AccountInfo
JSS(kLP_TOKEN);                    // out: amm_info
JSS(kMAJORITY);                    // out: RPC feature
JSS(kMANIFEST);                    // out: ValidatorInfo, Manifest
JSS(kMARKER);                     // in/out: AccountTx, AccountOffers, AccountLines, AccountObjects,
                                  //         LedgerData
                                  // in: BookOffers
JSS(kMASTER_KEY);                 // out: WalletPropose, NetworkOPs, ValidatorInfo
                                  // in/out: Manifest
JSS(kMASTER_SEED);                // out: WalletPropose
JSS(kMASTER_SEED_HEX);            // out: WalletPropose
JSS(kMASTER_SIGNATURE);           // out: pubManifest
JSS(kMAX_LEDGER);                 // in/out: LedgerCleaner
JSS(kMAX_QUEUE_SIZE);             // out: TxQ
JSS(kMAX_SPEND_DROPS);            // out: AccountInfo
JSS(kMAX_SPEND_DROPS_TOTAL);      // out: AccountInfo
JSS(kMEAN);                       // out: get_aggregate_price
JSS(kMEDIAN);                     // out: get_aggregate_price
JSS(kMEDIAN_FEE);                 // out: TxQ
JSS(kMEDIAN_LEVEL);               // out: TxQ
JSS(kMESSAGE);                    // error.
JSS(kMETA);                       // out: NetworkOPs, AccountTx*, Tx
JSS(kMETA_BLOB);                  // out: NetworkOPs, AccountTx*, Tx
JSS(kMETA_DATA);                  //
JSS(kMETADATA);                   // out: TransactionEntry
JSS(kMETHOD);                     // RPC
JSS(kMETHODS);                    //
JSS(kMETRICS);                    // out: Peers
JSS(kMIN_COUNT);                  // in: GetCounts
JSS(kMIN_LEDGER);                 // in: LedgerCleaner
JSS(kMINIMUM_FEE);                // out: TxQ
JSS(kMINIMUM_LEVEL);              // out: TxQ
JSS(kMISSING_COMMAND);            // error
JSS(kMPT_ISSUANCE_ID_A);          // out: BookChanges
JSS(kMPT_ISSUANCE_ID_B);          // out: BookChanges
JSS(kNAME);                       // out: AmendmentTableImpl, PeerImp
JSS(kNEEDED_STATE_HASHES);        // out: InboundLedger
JSS(kNEEDED_TRANSACTION_HASHES);  // out: InboundLedger
JSS(kNETWORK_ID);                 // out: NetworkOPs
JSS(kNETWORK_LEDGER);             // out: NetworkOPs
JSS(kNEXT_REFRESH_TIME);          // out: ValidatorSite
JSS(kNFT_ID);                     // in: nft_sell_offers, nft_buy_offers
JSS(kNFT_OFFER_INDEX);            // out nft_buy_offers, nft_sell_offers
JSS(kNFT_SERIAL);                 // out: account_nfts
JSS(kNFT_TAXON);                  // out: nft_info (clio)
JSS(kNFTOKEN_ID);                 // out: insertNFTokenID
JSS(kNFTOKEN_IDS);                // out: insertNFTokenID
JSS(kNO_RIPPLE);                  // out: AccountLines
JSS(kNO_RIPPLE_PEER);             // out: AccountLines
JSS(kNODE);                       // out: LedgerEntry
JSS(kNODE_BINARY);                // out: LedgerEntry
JSS(kNODE_READ_BYTES);            // out: GetCounts
JSS(kNODE_READ_ERRORS);           // out: GetCounts
JSS(kNODE_READ_RETRIES);          // out: GetCounts
JSS(kNODE_READS_HIT);             // out: GetCounts
JSS(kNODE_READS_TOTAL);           // out: GetCounts
JSS(kNODE_READS_DURATION_US);     // out: GetCounts
JSS(kNODE_SIZE);                  // out: server_info
JSS(kNODES);                      // out: VaultInfo
JSS(kNODESTORE);                  // out: GetCounts
JSS(kNODE_WRITES);                // out: GetCounts
JSS(kNODE_WRITTEN_BYTES);         // out: GetCounts
JSS(kNODE_WRITES_DURATION_US);    // out: GetCounts
JSS(kNODE_WRITE_RETRIES);         // out: GetCounts
JSS(kNODE_WRITES_DELAYED);        // out::GetCounts
JSS(kNTH);                        // out: RPC server_definitions
JSS(kOBLIGATIONS);                // out: GatewayBalances
JSS(kOFFERS);                     // out: NetworkOPs, AccountOffers, Subscribe
JSS(kOFFER_ID);                   // out: insertNFTokenOfferID
JSS(kOFFLINE);                    // in: TransactionSign
JSS(kOFFSET);                     // in/out: AccountTxOld
JSS(kOPEN);                       // out: handlers/Ledger
JSS(kOPEN_LEDGER_COST);           // out: SubmitTransaction
JSS(kOPEN_LEDGER_FEE);            // out: TxQ
JSS(kOPEN_LEDGER_LEVEL);          // out: TxQ
JSS(kOPTIONALITY);                // out: server_definitions
JSS(kORACLES);                    // in: get_aggregate_price
JSS(kORACLE_DOCUMENT_ID);         // in: get_aggregate_price
JSS(kOUT);                        // out: OverlayImpl
JSS(kOWNER);                      // in: LedgerEntry, out: NetworkOPs
JSS(kOWNER_FUNDS);                // in/out: Ledger, NetworkOPs, AcceptedLedgerTx
JSS(kPAGE_INDEX);                 //
JSS(kPARAMS);                     // RPC
JSS(kPARENT_CLOSE_TIME);          // out: LedgerToJson
JSS(kPARENT_HASH);                // out: LedgerToJson
JSS(kPARTITION);                  // in: LogLevel
JSS(kPASSPHRASE);                 // in: WalletPropose
JSS(kPASSWORD);                   // in: Subscribe
JSS(kPATHS);                      // in: RipplePathFind
JSS(kPATHS_CANONICAL);            // out: RipplePathFind
JSS(kPATHS_COMPUTED);             // out: PathRequest, RipplePathFind
JSS(kPEER);                       // in: AccountLines
JSS(kPEER_AUTHORIZED);            // out: AccountLines
JSS(kPEER_ID);                    // out: RCLCxPeerPos
JSS(kPEERS);                      // out: InboundLedger, handlers/Peers, Overlay
JSS(kPEER_DISCONNECTS);           // Severed peer connection counter.
JSS(kPEER_DISCONNECTS_RESOURCES);  // Severed peer connections because of
                                   // excess resource consumption.
JSS(kPORT);                        // in: Connect, out: NetworkOPs
JSS(kPORTS);                       // out: NetworkOPs
JSS(kPREVIOUS);                    // out: Reservations
JSS(kPREVIOUS_LEDGER);             // out: LedgerPropose
JSS(kPRICE);                       // out: amm_info, AuctionSlot
JSS(kPROOF);                       // in: BookOffers
JSS(kPROPOSE_SEQ);                 // out: LedgerPropose
JSS(kPROPOSERS);                   // out: NetworkOPs, LedgerConsensus
JSS(kPROTOCOL);                    // out: NetworkOPs, PeerImp
JSS(kPROXIED);                     // out: RPC ping
JSS(kPSEUDO_ACCOUNT);              // out: AccountInfo
JSS(kPUBKEY_NODE);                 // out: NetworkOPs
JSS(kPUBKEY_PUBLISHER);            // out: ValidatorList
JSS(kPUBKEY_VALIDATOR);            // out: NetworkOPs, ValidatorList
JSS(kPUBLIC_KEY);                  // out: OverlayImpl, PeerImp, WalletPropose, ValidatorInfo
                                   // in/out: Manifest
JSS(kPUBLIC_KEY_HEX);              // out: WalletPropose
JSS(kPUBLISHED_LEDGER);            // out: NetworkOPs
JSS(kPUBLISHER_LISTS);             // out: ValidatorList
JSS(kQUALITY);                     // out: NetworkOPs
JSS(kQUALITY_IN);                  // out: AccountLines
JSS(kQUALITY_OUT);                 // out: AccountLines
JSS(kQUEUE);                       // in: AccountInfo
JSS(kQUEUE_DATA);                  // out: AccountInfo
JSS(kQUEUED);                      // out: SubmitTransaction
JSS(kQUEUED_DURATION_US);          //
JSS(kQUOTE_ASSET);                 // in: get_aggregate_price
JSS(kRANDOM);                      // out: Random
JSS(kRAW_META);                    // out: AcceptedLedgerTx
JSS(kRECEIVE_CURRENCIES);          // out: AccountCurrencies
JSS(kREFERENCE_LEVEL);             // out: TxQ
JSS(kREFRESH_INTERVAL);            // in: UNL
JSS(kREFRESH_INTERVAL_MIN);        // out: ValidatorSites
JSS(kREGULAR_SEED);                // in/out: LedgerEntry
JSS(kREMAINING);                   // out: ValidatorList
JSS(kREMOTE);                      // out: Logic.h
JSS(kREQUEST);                     // RPC
JSS(kREQUESTED);                   // out: Manifest
JSS(kRESERVATIONS);                // out: Reservations
JSS(kRESERVE_BASE);                // out: NetworkOPs
JSS(kRESERVE_BASE_XRP);            // out: NetworkOPs
JSS(kRESERVE_INC);                 // out: NetworkOPs
JSS(kRESERVE_INC_XRP);             // out: NetworkOPs
JSS(kRESPONSE);                    // websocket
JSS(kRESULT);                      // RPC
JSS(kRIPPLE_LINES);                // out: NetworkOPs
JSS(kRIPPLE_STATE);                // in: LedgerEntr
JSS(kRIPPLERPC);                   // XRPL RPC version
JSS(kROLE);                        // out: Ping.cpp
JSS(kRPC);                         //
JSS(kRT_ACCOUNTS);                 // in: Subscribe, Unsubscribe
JSS(kRUNNING_DURATION_US);         //
JSS(kSEARCH_DEPTH);                // in: RipplePathFind
JSS(kSEARCHED_ALL);                // out: Tx
JSS(kSECRET);                      // in: TransactionSign, ValidationCreate, ValidationSeed,
                                   //     channel_authorize
JSS(kSEED);                        //
JSS(kSEED_HEX);                    // in: WalletPropose, TransactionSign
JSS(kSEND_CURRENCIES);             // out: AccountCurrencies
JSS(kSEND_MAX);                    // in: PathRequest, RipplePathFind
JSS(kSEQ);                         // in: LedgerEntry
                                   // out: NetworkOPs, RPCSub, AccountOffers, ValidatorList,
                                   //      ValidatorInfo, Manifest
JSS(kSEQUENCE);                    // in: UNL
JSS(kSEQUENCE_COUNT);              // out: AccountInfo
JSS(kSERVER_DOMAIN);               // out: NetworkOPs
JSS(kSERVER_STATE);                // out: NetworkOPs
JSS(kSERVER_STATE_DURATION_US);    // out: NetworkOPs
JSS(kSERVER_STATUS);               // out: NetworkOPs
JSS(kSERVER_VERSION);              // out: NetworkOPs
JSS(kSETTLE_DELAY);                // out: AccountChannels
JSS(kSEVERITY);                    // in: LogLevel
JSS(kSHARES);                      // out: VaultInfo
JSS(kSIGNATURE);                   // out: NetworkOPs, ChannelAuthorize
JSS(kSIGNATURE_TARGET);            // in: TransactionSign
JSS(kSIGNATURE_VERIFIED);          // out: ChannelVerify
JSS(kSIGNING_KEY);                 // out: NetworkOPs
JSS(kSIGNING_KEYS);                // out: ValidatorList
JSS(kSIGNING_TIME);                // out: NetworkOPs
JSS(kSIGNER_LISTS);                // in/out: AccountInfo
JSS(kSIZE);                        // out: get_aggregate_price
JSS(kSNAPSHOT);                    // in: Subscribe
JSS(kSOURCE_ACCOUNT);              // in: PathRequest, RipplePathFind
JSS(kSOURCE_AMOUNT);               // in: PathRequest, RipplePathFind
JSS(kSOURCE_CURRENCIES);           // in: PathRequest, RipplePathFind
JSS(kSOURCE_TAG);                  // out: AccountChannels
JSS(kSTAND_ALONE);                 // out: NetworkOPs
JSS(kSTANDARD_DEVIATION);          // out: get_aggregate_price
JSS(kSTART);                       // in: TxHistory
JSS(kSTARTED);                     //
JSS(kSTATE_ACCOUNTING);            // out: NetworkOPs
JSS(kSTATE_NOW);                   // in: Subscribe
JSS(kSTATUS);                      // error
JSS(kSTOP);                        // in: LedgerCleaner
JSS(kSTOP_HISTORY_TX_ONLY);        // in: Unsubscribe, stop history tx stream
JSS(kSTREAMS);                     // in: Subscribe, Unsubscribe
JSS(kSTRICT);                      // in: AccountCurrencies, AccountInfo
JSS(kSUB_INDEX);                   // in: LedgerEntry
JSS(kSUBCOMMAND);                  // in: PathFind
JSS(kSUBJECT);                     // in: LedgerEntry Credential
JSS(kSUCCESS);                     // rpc
JSS(kSUPPORTED);                   // out: AmendmentTableImpl
JSS(kSYNC_MODE);                   // in: Submit
JSS(kSYSTEM_TIME_OFFSET);          // out: NetworkOPs
JSS(kTAG);                         // out: Peers
JSS(kTAKER);                       // in: Subscribe, BookOffers
JSS(kTAKER_GETS);                  // in: Subscribe, Unsubscribe, BookOffers
JSS(kTAKER_GETS_FUNDED);           // out: NetworkOPs
JSS(kTAKER_PAYS);                  // in: Subscribe, Unsubscribe, BookOffers
JSS(kTAKER_PAYS_FUNDED);           // out: NetworkOPs
JSS(kTHRESHOLD);                   // in: Blacklist
JSS(kTICKET_COUNT);                // out: AccountInfo
JSS(kTICKET_SEQ);                  // in: LedgerEntry
JSS(kTIME);                        //
JSS(kTIMEOUTS);                    // out: InboundLedger
JSS(kTIME_THRESHOLD);              // in/out: Oracle aggregate
JSS(kTIME_INTERVAL);               // out: AMM Auction Slot
JSS(kTRACK);                       // out: PeerImp
JSS(kTRAFFIC);                     // out: Overlay
JSS(kTRIM);                        // in: get_aggregate_price
JSS(kTRIMMED_SET);                 // out: get_aggregate_price
JSS(kTOTAL);                       // out: counters
JSS(kTOTAL_BYTES_RECV);            // out: Peers
JSS(kTOTAL_BYTES_SENT);            // out: Peers
JSS(kTOTAL_COINS);                 // out: LedgerToJson
JSS(kTRADING_FEE);                 // out: amm_info
JSS(kTRANS_TREE_HASH);             // out: ledger/Ledger.cpp
JSS(kTRANSACTION);                 // in: Tx
                                   // out: NetworkOPs, AcceptedLedgerTx,
JSS(kTRANSACTION_HASH);            // out: RCLCxPeerPos, LedgerToJson
JSS(kTRANSACTIONS);                // out: LedgerToJson,
                                   // in: AccountTx*, Unsubscribe
JSS(kTRANSACTION_RESULTS);         // out: RPC server_definitions
                                   // matches definitions.json format
JSS(kTRANSACTION_TYPES);           // out: RPC server_definitions
                                   // matches definitions.json format
JSS(kTRANSACTION_FLAGS);           // out: RPC server_definitions
JSS(kTRANSACTION_FORMATS);         // out: RPC server_definitions
JSS(kTYPES);                       // out: RPC server_definitions
                                   // matches definitions.json format
JSS(kTRANSFER_RATE);               // out: nft_info (clio)
JSS(kTRANSITIONS);                 // out: NetworkOPs
JSS(kTREENODE_CACHE_SIZE);         // out: GetCounts
JSS(kTREENODE_TRACK_SIZE);         // out: GetCounts
JSS(kTRUSTED);                     // out: UnlList
JSS(kTRUSTED_VALIDATOR_KEYS);      // out: ValidatorList
JSS(kTX);                          // out: STTx, AccountTx*
JSS(kTX_BLOB);                     // in/out: Submit,
                                   // in: TransactionSign, AccountTx*
JSS(kTX_HASH);                     // in: TransactionEntry
JSS(kTX_JSON);                     // in/out: TransactionSign
                                   // out: TransactionEntry
JSS(kTX_SIGNING_HASH);             // out: TransactionSign
JSS(kTX_UNSIGNED);                 // out: TransactionSign
JSS(kTXN_COUNT);                   // out: NetworkOPs
JSS(kTXR_TX_CNT);                  // out: protocol message tx's count
JSS(kTXR_TX_SZ);                   // out: protocol message tx's size
JSS(kTXR_HAVE_TXS_CNT);            // out: protocol message have tx count
JSS(kTXR_HAVE_TXS_SZ);             // out: protocol message have tx size
JSS(kTXR_GET_LEDGER_CNT);          // out: protocol message get ledger count
JSS(kTXR_GET_LEDGER_SZ);           // out: protocol message get ledger size
JSS(kTXR_LEDGER_DATA_CNT);         // out: protocol message ledger data count
JSS(kTXR_LEDGER_DATA_SZ);          // out: protocol message ledger data size
JSS(kTXR_TRANSACTIONS_CNT);        // out: protocol message get object count
JSS(kTXR_TRANSACTIONS_SZ);         // out: protocol message get object size
JSS(kTXR_SELECTED_CNT);            // out: selected peers count
JSS(kTXR_SUPPRESSED_CNT);          // out: suppressed peers count
JSS(kTXR_NOT_ENABLED_CNT);         // out: peers with tx reduce-relay disabled count
JSS(kTXR_MISSING_TX_FREQ);         // out: missing tx frequency average
JSS(kTXS);                         // out: TxHistory
JSS(kTYPE);                        // in: AccountObjects
                                   // out: NetworkOPs, RPC server_definitions OverlayImpl, Logic
JSS(kTYPE_HEX);                    // out: STPathSet
JSS(kUNL);                         // out: UnlList
JSS(kUNLIMITED);                   // out: Connection.h
JSS(kUPTIME);                      // out: GetCounts
JSS(kURI);                         // out: ValidatorSites
JSS(kURL);                         // in/out: Subscribe, Unsubscribe
JSS(kURL_PASSWORD);                // in: Subscribe
JSS(kURL_USERNAME);                // in: Subscribe
JSS(kURLGRAVATAR);                 //
JSS(kUSERNAME);                    // in: Subscribe
JSS(kVALIDATED);                   // out: NetworkOPs, RPCHelpers, AccountTx*, Tx
JSS(kVALIDATOR_LIST_EXPIRES);      // out: NetworkOps, ValidatorList
JSS(kVALIDATOR_LIST);              // out: NetworkOps, ValidatorList
JSS(kVALIDATORS);                  //
JSS(kVALIDATED_HASH);              // out: NetworkOPs
JSS(kVALIDATED_LEDGER);            // out: NetworkOPs
JSS(kVALIDATED_LEDGER_INDEX);      // out: SubmitTransaction
JSS(kVALIDATED_LEDGERS);           // out: NetworkOPs
JSS(kVALIDATION_KEY);              // out: ValidationCreate, ValidationSeed
JSS(kVALIDATION_PRIVATE_KEY);      // out: ValidationCreate
JSS(kVALIDATION_PUBLIC_KEY);       // out: ValidationCreate, ValidationSeed
JSS(kVALIDATION_QUORUM);           // out: NetworkOPs
JSS(kVALIDATION_SEED);             // out: ValidationCreate, ValidationSeed
JSS(kVALIDATIONS);                 // out: AmendmentTableImpl
JSS(kVALIDATOR_LIST_THRESHOLD);    // out: ValidatorList
JSS(kVALIDATOR_SITES);             // out: ValidatorSites
JSS(kVALUE);                       // out: STAmount
JSS(kVAULT_ID);                    // in: VaultInfo
JSS(kVERSION);                     // out: RPCVersion
JSS(kVETOED);                      // out: AmendmentTableImpl
JSS(kVOLUME_A);                    // out: BookChanges
JSS(kVOLUME_B);                    // out: BookChanges
JSS(kVOTE);                        // in: Feature
JSS(kVOTE_SLOTS);                  // out: amm_info
JSS(kVOTE_WEIGHT);                 // out: amm_info
JSS(kWARNING);                     // rpc:
JSS(kWARNINGS);                    // out: server_info, server_state
JSS(kWORKERS);                     //
JSS(kWRITE_LOAD);                  // out: GetCounts

#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, ...) JSS(name);

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")

#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY
#pragma push_macro("LEDGER_ENTRY_DUPLICATE")
#undef LEDGER_ENTRY_DUPLICATE

#define LEDGER_ENTRY(tag, value, name, rpcName, ...) \
    JSS(name);                                       \
    JSS(rpcName);

#define LEDGER_ENTRY_DUPLICATE(tag, value, name, rpcName, ...) JSS(rpcName);

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY_DUPLICATE
#pragma pop_macro("LEDGER_ENTRY_DUPLICATE")

#undef JSS

}  // namespace xrpl::jss
