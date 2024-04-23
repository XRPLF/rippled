#[===================================================================[
   xrpl_core
   core functionality, useable by some client software perhaps
#]===================================================================]

include(target_protobuf_sources)

file (GLOB_RECURSE rb_headers
  src/ripple/beast/*.h)

# Protocol buffers cannot participate in a unity build,
# because all the generated sources
# define a bunch of `static const` variables with the same names,
# so we just build them as a separate library.
add_library(xrpl.libpb)
target_protobuf_sources(xrpl.libpb ripple/proto
  LANGUAGE cpp
  IMPORT_DIRS src/ripple/proto
  PROTOS src/ripple/proto/ripple.proto
)

file(GLOB_RECURSE protos "src/ripple/proto/org/*.proto")
target_protobuf_sources(xrpl.libpb ripple/proto
  LANGUAGE cpp
  IMPORT_DIRS src/ripple/proto
  PROTOS "${protos}"
)
target_protobuf_sources(xrpl.libpb ripple/proto
  LANGUAGE grpc
  IMPORT_DIRS src/ripple/proto
  PROTOS "${protos}"
  PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
)

target_compile_options(xrpl.libpb
  PUBLIC
    $<$<BOOL:${MSVC}>:-wd4996>
    $<$<BOOL:${XCODE}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >
  PRIVATE
    $<$<BOOL:${MSVC}>:-wd4065>
    $<$<NOT:$<BOOL:${MSVC}>>:-Wno-deprecated-declarations>
)

target_link_libraries(xrpl.libpb
  PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
)

add_library (xrpl_core
  ${rb_headers}) ## headers added here for benefit of IDEs
if (unity)
  set_target_properties(xrpl_core PROPERTIES UNITY_BUILD ON)
endif ()

add_library(libxrpl INTERFACE)
target_link_libraries(libxrpl INTERFACE xrpl_core)
add_library(xrpl::libxrpl ALIAS libxrpl)

add_library(xrpl_plugin ${rb_headers})


#[===============================[
    beast/legacy FILES:
    TODO: review these sources for removal or replacement
#]===============================]
# BEGIN LIBXRPL SOURCES
target_sources (xrpl_core PRIVATE
  src/ripple/beast/clock/basic_seconds_clock.cpp
  src/ripple/beast/core/CurrentThreadName.cpp
  src/ripple/beast/core/SemanticVersion.cpp
  src/ripple/beast/insight/impl/Collector.cpp
  src/ripple/beast/insight/impl/Groups.cpp
  src/ripple/beast/insight/impl/Hook.cpp
  src/ripple/beast/insight/impl/Metric.cpp
  src/ripple/beast/insight/impl/NullCollector.cpp
  src/ripple/beast/insight/impl/StatsDCollector.cpp
  src/ripple/beast/net/impl/IPAddressConversion.cpp
  src/ripple/beast/net/impl/IPAddressV4.cpp
  src/ripple/beast/net/impl/IPAddressV6.cpp
  src/ripple/beast/net/impl/IPEndpoint.cpp
  src/ripple/beast/utility/src/beast_Journal.cpp
  src/ripple/beast/utility/src/beast_PropertyStream.cpp)

#[===============================[
    core sources
#]===============================]
target_sources (xrpl_core PRIVATE
  #[===============================[
    main sources:
      subdir: basics
  #]===============================]
  src/ripple/basics/impl/Archive.cpp
  src/ripple/basics/impl/base64.cpp
  src/ripple/basics/impl/BasicConfig.cpp
  src/ripple/basics/impl/contract.cpp
  src/ripple/basics/impl/CountedObject.cpp
  src/ripple/basics/impl/FileUtilities.cpp
  src/ripple/basics/impl/IOUAmount.cpp
  src/ripple/basics/impl/Log.cpp
  src/ripple/basics/impl/make_SSLContext.cpp
  src/ripple/basics/impl/mulDiv.cpp
  src/ripple/basics/impl/Number.cpp
  src/ripple/basics/impl/partitioned_unordered_map.cpp
  src/ripple/basics/impl/ResolverAsio.cpp
  src/ripple/basics/impl/StringUtilities.cpp
  src/ripple/basics/impl/UptimeClock.cpp
  #[===============================[
    main sources:
      subdir: json
  #]===============================]
  src/ripple/json/impl/JsonPropertyStream.cpp
  src/ripple/json/impl/Object.cpp
  src/ripple/json/impl/Output.cpp
  src/ripple/json/impl/Writer.cpp
  src/ripple/json/impl/json_reader.cpp
  src/ripple/json/impl/json_value.cpp
  src/ripple/json/impl/json_valueiterator.cpp
  src/ripple/json/impl/json_writer.cpp
  src/ripple/json/impl/to_string.cpp
  #[===============================[
    main sources:
      subdir: protocol
  #]===============================]
  src/ripple/protocol/impl/AccountID.cpp
  src/ripple/protocol/impl/AMMCore.cpp
  src/ripple/protocol/impl/Book.cpp
  src/ripple/protocol/impl/BuildInfo.cpp
  src/ripple/protocol/impl/ErrorCodes.cpp
  src/ripple/protocol/impl/Feature.cpp
  src/ripple/protocol/impl/Indexes.cpp
  src/ripple/protocol/impl/InnerObjectFormats.cpp
  src/ripple/protocol/impl/Issue.cpp
  src/ripple/protocol/impl/STIssue.cpp
  src/ripple/protocol/impl/Keylet.cpp
  src/ripple/protocol/impl/LedgerFormats.cpp
  src/ripple/protocol/impl/LedgerHeader.cpp
  src/ripple/protocol/impl/PublicKey.cpp
  src/ripple/protocol/impl/Quality.cpp
  src/ripple/protocol/impl/QualityFunction.cpp
  src/ripple/protocol/impl/RPCErr.cpp
  src/ripple/protocol/impl/Rate2.cpp
  src/ripple/protocol/impl/Rules.cpp
  src/ripple/protocol/impl/SField.cpp
  src/ripple/protocol/impl/SOTemplate.cpp
  src/ripple/protocol/impl/STAccount.cpp
  src/ripple/protocol/impl/STAmount.cpp
  src/ripple/protocol/impl/STArray.cpp
  src/ripple/protocol/impl/STBase.cpp
  src/ripple/protocol/impl/STBlob.cpp
  src/ripple/protocol/impl/STCurrency.cpp
  src/ripple/protocol/impl/STInteger.cpp
  src/ripple/protocol/impl/STLedgerEntry.cpp
  src/ripple/protocol/impl/STObject.cpp
  src/ripple/protocol/impl/STParsedJSON.cpp
  src/ripple/protocol/impl/STPathSet.cpp
  src/ripple/protocol/impl/STPluginType.cpp
  src/ripple/protocol/impl/STXChainBridge.cpp
  src/ripple/protocol/impl/STTx.cpp
  src/ripple/protocol/impl/XChainAttestations.cpp
  src/ripple/protocol/impl/STValidation.cpp
  src/ripple/protocol/impl/STVar.cpp
  src/ripple/protocol/impl/STVector256.cpp
  src/ripple/protocol/impl/SecretKey.cpp
  src/ripple/protocol/impl/Seed.cpp
  src/ripple/protocol/impl/Serializer.cpp
  src/ripple/protocol/impl/Sign.cpp
  src/ripple/protocol/impl/TER.cpp
  src/ripple/protocol/impl/TxFormats.cpp
  src/ripple/protocol/impl/TxMeta.cpp
  src/ripple/protocol/impl/UintTypes.cpp
  src/ripple/protocol/impl/digest.cpp
  src/ripple/protocol/impl/tokens.cpp
  src/ripple/protocol/impl/NFTSyntheticSerializer.cpp
  src/ripple/protocol/impl/NFTokenID.cpp
  src/ripple/protocol/impl/NFTokenOfferID.cpp
  #[===============================[
     main sources:
       subdir: resource
  #]===============================]
  src/ripple/resource/impl/Charge.cpp
  src/ripple/resource/impl/Consumer.cpp
  src/ripple/resource/impl/Fees.cpp
  src/ripple/resource/impl/ResourceManager.cpp
  #[===============================[
     main sources:
       subdir: server
  #]===============================]
  src/ripple/server/impl/JSONRPCUtil.cpp
  src/ripple/server/impl/Port.cpp
  #[===============================[
    main sources:
      subdir: crypto
  #]===============================]
  src/ripple/crypto/impl/RFC1751.cpp
  src/ripple/crypto/impl/csprng.cpp
  src/ripple/crypto/impl/secure_erase.cpp)
# END LIBXRPL SOURCES

add_library (Ripple::xrpl_core ALIAS xrpl_core)
target_include_directories (xrpl_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>)

target_compile_definitions(xrpl_core
  PUBLIC
    BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT
    BOOST_CONTAINER_FWD_BAD_DEQUE
    HAS_UNCAUGHT_EXCEPTIONS=1)
target_compile_options (xrpl_core
  PUBLIC
    $<$<BOOL:${is_gcc}>:-Wno-maybe-uninitialized>)
target_link_libraries (xrpl_core
  PUBLIC
    date::date
    ed25519::ed25519
    LibArchive::LibArchive
    OpenSSL::Crypto
    Ripple::boost
    Ripple::opts
    Ripple::syslibs
    secp256k1::secp256k1
    xrpl.libpb
    xxHash::xxhash
  )

#[=================================[
   xrpl_plugin installation
#]=================================]
target_sources (xrpl_plugin PRIVATE
  src/ripple/app/tx/impl/validity.cpp
  src/ripple/app/tx/impl/TxConsequences.cpp
  src/ripple/ledger/impl/ApplyStateTable.cpp
  src/ripple/ledger/impl/ApplyView.cpp
  src/ripple/ledger/impl/ApplyViewBase.cpp
  src/ripple/ledger/impl/ApplyViewImpl.cpp
  src/ripple/ledger/impl/OpenView.cpp
  src/ripple/ledger/impl/RawStateTable.cpp
  src/ripple/ledger/impl/ReadView.cpp
  src/ripple/ledger/impl/View.cpp
  src/ripple/app/tx/impl/InvariantCheck.cpp
  src/ripple/app/tx/impl/details/NFTokenUtils.cpp
  src/ripple/app/tx/impl/ApplyContext.cpp
  src/ripple/app/misc/impl/LoadFeeTrack.cpp
  src/ripple/app/misc/HashRouter.cpp
  src/ripple/app/tx/impl/PreflightContext.cpp
  src/ripple/app/tx/impl/SignerEntries.cpp
  src/ripple/ledger/impl/Directory.cpp
)

add_library (Ripple::xrpl_plugin ALIAS xrpl_plugin)
target_link_libraries(xrpl_plugin PUBLIC
  Ripple::xrpl_core
  xxHash::xxhash
)
#[=================================[
   main/core headers installation
#]=================================]
# BEGIN LIBXRPL HEADERS
install (
  FILES
    src/ripple/basics/Archive.h
    src/ripple/basics/BasicConfig.h
    src/ripple/basics/Blob.h
    src/ripple/basics/Buffer.h
    src/ripple/basics/ByteUtilities.h
    src/ripple/basics/CompressionAlgorithms.h
    src/ripple/basics/CountedObject.h
    src/ripple/basics/DecayingSample.h
    src/ripple/basics/Expected.h
    src/ripple/basics/FeeUnits.h
    src/ripple/basics/FileUtilities.h
    src/ripple/basics/IOUAmount.h
    src/ripple/basics/KeyCache.h
    src/ripple/basics/LocalValue.h
    src/ripple/basics/Log.h
    src/ripple/basics/MathUtilities.h
    src/ripple/basics/Number.h
    src/ripple/basics/PerfLog.h
    src/ripple/basics/README.md
    src/ripple/basics/RangeSet.h
    src/ripple/basics/Resolver.h
    src/ripple/basics/ResolverAsio.h
    src/ripple/basics/SHAMapHash.h
    src/ripple/basics/Slice.h
    src/ripple/basics/StringUtilities.h
    src/ripple/basics/TaggedCache.h
    src/ripple/basics/ThreadSafetyAnalysis.h
    src/ripple/basics/ToString.h
    src/ripple/basics/UnorderedContainers.h
    src/ripple/basics/UptimeClock.h
    src/ripple/basics/XRPAmount.h
    src/ripple/basics/algorithm.h
    src/ripple/basics/base64.h
    src/ripple/basics/base_uint.h
    src/ripple/basics/chrono.h
    src/ripple/basics/comparators.h
    src/ripple/basics/contract.h
    src/ripple/basics/hardened_hash.h
    src/ripple/basics/join.h
    src/ripple/basics/make_SSLContext.h
    src/ripple/basics/mulDiv.h
    src/ripple/basics/partitioned_unordered_map.h
    src/ripple/basics/random.h
    src/ripple/basics/safe_cast.h
    src/ripple/basics/scope.h
    src/ripple/basics/spinlock.h
    src/ripple/basics/strHex.h
    src/ripple/basics/tagged_integer.h
  DESTINATION include/ripple/basics)
install (
  FILES
    src/ripple/crypto/RFC1751.h
    src/ripple/crypto/csprng.h
    src/ripple/crypto/secure_erase.h
  DESTINATION include/ripple/crypto)
install (
  FILES
    src/ripple/json/JsonPropertyStream.h
    src/ripple/json/Object.h
    src/ripple/json/Output.h
    src/ripple/json/Writer.h
    src/ripple/json/json_forwards.h
    src/ripple/json/json_reader.h
    src/ripple/json/json_value.h
    src/ripple/json/json_writer.h
    src/ripple/json/to_string.h
  DESTINATION include/ripple/json)
install (
  FILES
    src/ripple/json/impl/json_assert.h
  DESTINATION include/ripple/json/impl)
install (
  FILES
    src/ripple/protocol/AccountID.h
    src/ripple/protocol/AMMCore.h
    src/ripple/protocol/AmountConversions.h
    src/ripple/protocol/ApiVersion.h
    src/ripple/protocol/Book.h
    src/ripple/protocol/BuildInfo.h
    src/ripple/protocol/ErrorCodes.h
    src/ripple/protocol/Feature.h
    src/ripple/protocol/Fees.h
    src/ripple/protocol/HashPrefix.h
    src/ripple/protocol/Indexes.h
    src/ripple/protocol/InnerObjectFormats.h
    src/ripple/protocol/Issue.h
    src/ripple/protocol/json_get_or_throw.h
    src/ripple/protocol/Keylet.h
    src/ripple/protocol/KeyType.h
    src/ripple/protocol/KnownFormats.h
    src/ripple/protocol/LedgerFormats.h
    src/ripple/protocol/LedgerHeader.h
    src/ripple/protocol/MultiApiJson.h
    src/ripple/protocol/NFTSyntheticSerializer.h
    src/ripple/protocol/NFTokenID.h
    src/ripple/protocol/NFTokenOfferID.h
    src/ripple/protocol/NFTSyntheticSerializer.h
    src/ripple/protocol/Protocol.h
    src/ripple/protocol/PublicKey.h
    src/ripple/protocol/Quality.h
    src/ripple/protocol/QualityFunction.h
    src/ripple/protocol/Rate.h
    src/ripple/protocol/RPCErr.h
    src/ripple/protocol/Rules.h
    src/ripple/protocol/SecretKey.h
    src/ripple/protocol/Seed.h
    src/ripple/protocol/SeqProxy.h
    src/ripple/protocol/Serializer.h
    src/ripple/protocol/SField.h
    src/ripple/protocol/Sign.h
    src/ripple/protocol/SOTemplate.h
    src/ripple/protocol/STAccount.h
    src/ripple/protocol/STAmount.h
    src/ripple/protocol/STArray.h
    src/ripple/protocol/STBase.h
    src/ripple/protocol/STBitString.h
    src/ripple/protocol/STBlob.h
    src/ripple/protocol/STCurrency.h
    src/ripple/protocol/STExchange.h
    src/ripple/protocol/STInteger.h
    src/ripple/protocol/STIssue.h
    src/ripple/protocol/STLedgerEntry.h
    src/ripple/protocol/STObject.h
    src/ripple/protocol/STParsedJSON.h
    src/ripple/protocol/STPathSet.h
    src/ripple/protocol/STTx.h
    src/ripple/protocol/STValidation.h
    src/ripple/protocol/STVector256.h
    src/ripple/protocol/STXChainBridge.h
    src/ripple/protocol/SystemParameters.h
    src/ripple/protocol/TER.h
    src/ripple/protocol/TxFlags.h
    src/ripple/protocol/TxFormats.h
    src/ripple/protocol/TxMeta.h
    src/ripple/protocol/UintTypes.h
    src/ripple/protocol/XChainAttestations.h
    src/ripple/protocol/digest.h
    src/ripple/protocol/jss.h
    src/ripple/protocol/nft.h
    src/ripple/protocol/nftPageMask.h
    src/ripple/protocol/serialize.h
    src/ripple/protocol/tokens.h
  DESTINATION include/ripple/protocol)
install (
  FILES
  src/ripple/protocol/impl/STVar.h
    src/ripple/protocol/impl/b58_utils.h
    src/ripple/protocol/impl/secp256k1.h
    src/ripple/protocol/impl/token_errors.h
  DESTINATION include/ripple/protocol/impl)
install (
  FILES
    src/ripple/resource/Charge.h
    src/ripple/resource/Consumer.h
    src/ripple/resource/Disposition.h
    src/ripple/resource/Fees.h
    src/ripple/resource/Gossip.h
    src/ripple/resource/ResourceManager.h
    src/ripple/resource/Types.h
  DESTINATION include/ripple/resource)
install (
  FILES
    src/ripple/resource/impl/Entry.h
    src/ripple/resource/impl/Import.h
    src/ripple/resource/impl/Key.h
    src/ripple/resource/impl/Kind.h
    src/ripple/resource/impl/Logic.h
    src/ripple/resource/impl/Tuning.h
  DESTINATION include/ripple/resource/impl)
install (
  FILES
    src/ripple/server/Handoff.h
    src/ripple/server/Port.h
    src/ripple/server/Server.h
    src/ripple/server/Session.h
    src/ripple/server/SimpleWriter.h
    src/ripple/server/Writer.h
    src/ripple/server/WSSession.h
  DESTINATION include/ripple/server)
install (
  FILES
    src/ripple/server/impl/BaseHTTPPeer.h
    src/ripple/server/impl/BasePeer.h
    src/ripple/server/impl/BaseWSPeer.h
    src/ripple/server/impl/Door.h
    src/ripple/server/impl/JSONRPCUtil.h
    src/ripple/server/impl/LowestLayer.h
    src/ripple/server/impl/PlainHTTPPeer.h
    src/ripple/server/impl/PlainWSPeer.h
    src/ripple/server/impl/ServerImpl.h
    src/ripple/server/impl/SSLHTTPPeer.h
    src/ripple/server/impl/SSLWSPeer.h
    src/ripple/server/impl/io_list.h
  DESTINATION include/ripple/server/impl)
#[===================================[
   beast/legacy headers installation
#]===================================]
install (
  FILES
    src/ripple/beast/clock/abstract_clock.h
    src/ripple/beast/clock/basic_seconds_clock.h
    src/ripple/beast/clock/manual_clock.h
  DESTINATION include/ripple/beast/clock)
install (
  FILES
    src/ripple/beast/core/CurrentThreadName.h
    src/ripple/beast/core/LexicalCast.h
    src/ripple/beast/core/List.h
    src/ripple/beast/core/SemanticVersion.h
  DESTINATION include/ripple/beast/core)
install (
  FILES
    src/ripple/beast/hash/hash_append.h
    src/ripple/beast/hash/uhash.h
    src/ripple/beast/hash/xxhasher.h
  DESTINATION include/ripple/beast/hash)
install (
  FILES
    src/ripple/beast/net/IPAddress.h
    src/ripple/beast/net/IPAddressConversion.h
    src/ripple/beast/net/IPAddressV4.h
    src/ripple/beast/net/IPAddressV6.h
    src/ripple/beast/net/IPEndpoint.h
  DESTINATION include/ripple/beast/net)
install (
  FILES
    src/ripple/beast/rfc2616.h
    src/ripple/beast/type_name.h
    src/ripple/beast/unit_test.h
    src/ripple/beast/xor_shift_engine.h
  DESTINATION include/ripple/beast)
install (
  FILES
    src/ripple/beast/unit_test/amount.h
    src/ripple/beast/unit_test/dstream.h
    src/ripple/beast/unit_test/global_suites.h
    src/ripple/beast/unit_test/match.h
    src/ripple/beast/unit_test/recorder.h
    src/ripple/beast/unit_test/reporter.h
    src/ripple/beast/unit_test/results.h
    src/ripple/beast/unit_test/runner.h
    src/ripple/beast/unit_test/suite_info.h
    src/ripple/beast/unit_test/suite_list.h
    src/ripple/beast/unit_test/suite.h
    src/ripple/beast/unit_test/thread.h
  DESTINATION include/ripple/beast/unit_test)
install (
  FILES
    src/ripple/beast/unit_test/detail/const_container.h
  DESTINATION include/ripple/beast/unit_test/detail)
install (
  FILES
    src/ripple/beast/utility/Journal.h
    src/ripple/beast/utility/PropertyStream.h
    src/ripple/beast/utility/WrappedSink.h
    src/ripple/beast/utility/Zero.h
    src/ripple/beast/utility/rngfill.h
  DESTINATION include/ripple/beast/utility)
# END LIBXRPL HEADERS
#[===================================================================[
   rippled executable
#]===================================================================]

#[=========================================================[
   this one header is added as source just to keep older
   versions of cmake happy. cmake 3.10+ allows
   add_executable with no sources
#]=========================================================]
add_executable (rippled src/ripple/app/main/Application.h)
if (unity)
  set_target_properties(rippled PROPERTIES UNITY_BUILD ON)
endif ()
if (tests)
    target_compile_definitions(rippled PUBLIC ENABLE_TESTS)
endif()
# BEGIN XRPLD SOURCES
target_sources (rippled PRIVATE
  #[===============================[
     main sources:
       subdir: app
  #]===============================]
  src/ripple/app/consensus/RCLConsensus.cpp
  src/ripple/app/consensus/RCLCxPeerPos.cpp
  src/ripple/app/consensus/RCLValidations.cpp
  src/ripple/app/ledger/AcceptedLedger.cpp
  src/ripple/app/ledger/AcceptedLedgerTx.cpp
  src/ripple/app/ledger/AccountStateSF.cpp
  src/ripple/app/ledger/BookListeners.cpp
  src/ripple/app/ledger/ConsensusTransSetSF.cpp
  src/ripple/app/ledger/Ledger.cpp
  src/ripple/app/ledger/LedgerHistory.cpp
  src/ripple/app/ledger/OrderBookDB.cpp
  src/ripple/app/ledger/TransactionStateSF.cpp
  src/ripple/app/ledger/impl/BuildLedger.cpp
  src/ripple/app/ledger/impl/InboundLedger.cpp
  src/ripple/app/ledger/impl/InboundLedgers.cpp
  src/ripple/app/ledger/impl/InboundTransactions.cpp
  src/ripple/app/ledger/impl/LedgerCleaner.cpp
  src/ripple/app/ledger/impl/LedgerDeltaAcquire.cpp
  src/ripple/app/ledger/impl/LedgerMaster.cpp
  src/ripple/app/ledger/impl/LedgerReplay.cpp
  src/ripple/app/ledger/impl/LedgerReplayer.cpp
  src/ripple/app/ledger/impl/LedgerReplayMsgHandler.cpp
  src/ripple/app/ledger/impl/LedgerReplayTask.cpp
  src/ripple/app/ledger/impl/LedgerToJson.cpp
  src/ripple/app/ledger/impl/LocalTxs.cpp
  src/ripple/app/ledger/impl/OpenLedger.cpp
  src/ripple/app/ledger/impl/SkipListAcquire.cpp
  src/ripple/app/ledger/impl/TimeoutCounter.cpp
  src/ripple/app/ledger/impl/TransactionAcquire.cpp
  src/ripple/app/ledger/impl/TransactionMaster.cpp
  src/ripple/app/main/Application.cpp
  src/ripple/app/main/BasicApp.cpp
  src/ripple/app/main/CollectorManager.cpp
  src/ripple/app/main/GRPCServer.cpp
  src/ripple/app/main/LoadManager.cpp
  src/ripple/app/main/Main.cpp
  src/ripple/app/main/NodeIdentity.cpp
  src/ripple/app/main/NodeStoreScheduler.cpp
  src/ripple/app/main/PluginSetup.cpp
  src/ripple/app/reporting/ReportingETL.cpp
  src/ripple/app/reporting/ETLSource.cpp
  src/ripple/app/reporting/P2pProxy.cpp
  src/ripple/app/misc/impl/AMMHelpers.cpp
  src/ripple/app/misc/impl/AMMUtils.cpp
  src/ripple/app/misc/CanonicalTXSet.cpp
  src/ripple/app/misc/FeeVoteImpl.cpp
  src/ripple/app/misc/NegativeUNLVote.cpp
  src/ripple/app/misc/NetworkOPs.cpp
  src/ripple/app/misc/SHAMapStoreImp.cpp
  src/ripple/app/misc/detail/impl/WorkSSL.cpp
  src/ripple/app/misc/impl/AccountTxPaging.cpp
  src/ripple/app/misc/impl/AmendmentTable.cpp
  src/ripple/app/misc/impl/DeliverMax.cpp
  src/ripple/app/misc/impl/LoadFeeTrack.cpp
  src/ripple/app/misc/impl/Manifest.cpp
  src/ripple/app/misc/impl/Transaction.cpp
  src/ripple/app/misc/impl/TxQ.cpp
  src/ripple/app/misc/impl/ValidatorKeys.cpp
  src/ripple/app/misc/impl/ValidatorList.cpp
  src/ripple/app/misc/impl/ValidatorSite.cpp
  src/ripple/app/paths/AccountCurrencies.cpp
  src/ripple/app/paths/Credit.cpp
  src/ripple/app/paths/Flow.cpp
  src/ripple/app/paths/PathRequest.cpp
  src/ripple/app/paths/PathRequests.cpp
  src/ripple/app/paths/Pathfinder.cpp
  src/ripple/app/paths/RippleCalc.cpp
  src/ripple/app/paths/RippleLineCache.cpp
  src/ripple/app/paths/TrustLine.cpp
  src/ripple/app/paths/impl/AMMLiquidity.cpp
  src/ripple/app/paths/impl/AMMOffer.cpp
  src/ripple/app/paths/impl/BookStep.cpp
  src/ripple/app/paths/impl/DirectStep.cpp
  src/ripple/app/paths/impl/PaySteps.cpp
  src/ripple/app/paths/impl/XRPEndpointStep.cpp
  src/ripple/app/rdb/backend/detail/impl/Node.cpp
  src/ripple/app/rdb/backend/detail/impl/Shard.cpp
  src/ripple/app/rdb/backend/impl/PostgresDatabase.cpp
  src/ripple/app/rdb/backend/impl/SQLiteDatabase.cpp
  src/ripple/app/rdb/impl/Download.cpp
  src/ripple/app/rdb/impl/PeerFinder.cpp
  src/ripple/app/rdb/impl/RelationalDatabase.cpp
  src/ripple/app/rdb/impl/ShardArchive.cpp
  src/ripple/app/rdb/impl/State.cpp
  src/ripple/app/rdb/impl/UnitaryShard.cpp
  src/ripple/app/rdb/impl/Vacuum.cpp
  src/ripple/app/rdb/impl/Wallet.cpp
  src/ripple/app/tx/impl/AMMBid.cpp
  src/ripple/app/tx/impl/AMMCreate.cpp
  src/ripple/app/tx/impl/AMMDelete.cpp
  src/ripple/app/tx/impl/AMMDeposit.cpp
  src/ripple/app/tx/impl/AMMVote.cpp
  src/ripple/app/tx/impl/AMMWithdraw.cpp
  src/ripple/app/tx/impl/BookTip.cpp
  src/ripple/app/tx/impl/CancelCheck.cpp
  src/ripple/app/tx/impl/CancelOffer.cpp
  src/ripple/app/tx/impl/CashCheck.cpp
  src/ripple/app/tx/impl/Change.cpp
  src/ripple/app/tx/impl/Clawback.cpp
  src/ripple/app/tx/impl/CreateCheck.cpp
  src/ripple/app/tx/impl/CreateOffer.cpp
  src/ripple/app/tx/impl/CreateTicket.cpp
  src/ripple/app/tx/impl/DeleteAccount.cpp
  src/ripple/app/tx/impl/DeleteOracle.cpp
  src/ripple/app/tx/impl/DepositPreauth.cpp
  src/ripple/app/tx/impl/DID.cpp
  src/ripple/app/tx/impl/Escrow.cpp
  src/ripple/app/tx/impl/NFTokenAcceptOffer.cpp
  src/ripple/app/tx/impl/NFTokenBurn.cpp
  src/ripple/app/tx/impl/NFTokenCancelOffer.cpp
  src/ripple/app/tx/impl/NFTokenCreateOffer.cpp
  src/ripple/app/tx/impl/NFTokenMint.cpp
  src/ripple/app/tx/impl/OfferStream.cpp
  src/ripple/app/tx/impl/PayChan.cpp
  src/ripple/app/tx/impl/Payment.cpp
  src/ripple/app/tx/impl/SetAccount.cpp
  src/ripple/app/tx/impl/SetOracle.cpp
  src/ripple/app/tx/impl/SetRegularKey.cpp
  src/ripple/app/tx/impl/SetSignerList.cpp
  src/ripple/app/tx/impl/SetTrust.cpp
  src/ripple/app/tx/impl/XChainBridge.cpp
  src/ripple/app/tx/impl/SignerEntries.cpp
  src/ripple/app/tx/impl/Taker.cpp
  src/ripple/app/tx/impl/Transactor.cpp
  src/ripple/app/tx/impl/apply.cpp
  src/ripple/app/tx/impl/applySteps.cpp
  src/ripple/app/tx/impl/ApplyHandler.cpp
  #[===============================[
     main sources:
       subdir: conditions
  #]===============================]
  src/ripple/conditions/impl/Condition.cpp
  src/ripple/conditions/impl/Fulfillment.cpp
  src/ripple/conditions/impl/error.cpp
  #[===============================[
     main sources:
       subdir: core
  #]===============================]
  src/ripple/core/impl/Config.cpp
  src/ripple/core/impl/DatabaseCon.cpp
  src/ripple/core/impl/Job.cpp
  src/ripple/core/impl/JobQueue.cpp
  src/ripple/core/impl/LoadEvent.cpp
  src/ripple/core/impl/LoadMonitor.cpp
  src/ripple/core/impl/SociDB.cpp
  src/ripple/core/impl/Workers.cpp
  src/ripple/core/Pg.cpp
  #[===============================[
     main sources:
       subdir: consensus
  #]===============================]
  src/ripple/consensus/Consensus.cpp
  #[===============================[
     main sources:
       subdir: ledger
  #]===============================]
  src/ripple/ledger/impl/BookDirs.cpp
  src/ripple/ledger/impl/CachedView.cpp
  src/ripple/ledger/impl/PaymentSandbox.cpp
  #[===============================[
     main sources:
       subdir: net
  #]===============================]
  src/ripple/net/impl/DatabaseDownloader.cpp
  src/ripple/net/impl/HTTPClient.cpp
  src/ripple/net/impl/HTTPDownloader.cpp
  src/ripple/net/impl/HTTPStream.cpp
  src/ripple/net/impl/InfoSub.cpp
  src/ripple/net/impl/RPCCall.cpp
  src/ripple/net/impl/RPCSub.cpp
  src/ripple/net/impl/RegisterSSLCerts.cpp
  #[===============================[
     main sources:
       subdir: nodestore
  #]===============================]
  src/ripple/nodestore/backend/CassandraFactory.cpp
  src/ripple/nodestore/backend/MemoryFactory.cpp
  src/ripple/nodestore/backend/NuDBFactory.cpp
  src/ripple/nodestore/backend/NullFactory.cpp
  src/ripple/nodestore/backend/RocksDBFactory.cpp
  src/ripple/nodestore/impl/BatchWriter.cpp
  src/ripple/nodestore/impl/Database.cpp
  src/ripple/nodestore/impl/DatabaseNodeImp.cpp
  src/ripple/nodestore/impl/DatabaseRotatingImp.cpp
  src/ripple/nodestore/impl/DatabaseShardImp.cpp
  src/ripple/nodestore/impl/DeterministicShard.cpp
  src/ripple/nodestore/impl/DecodedBlob.cpp
  src/ripple/nodestore/impl/DummyScheduler.cpp
  src/ripple/nodestore/impl/ManagerImp.cpp
  src/ripple/nodestore/impl/NodeObject.cpp
  src/ripple/nodestore/impl/Shard.cpp
  src/ripple/nodestore/impl/ShardInfo.cpp
  src/ripple/nodestore/impl/TaskQueue.cpp
  #[===============================[
     main sources:
       subdir: overlay
  #]===============================]
  src/ripple/overlay/impl/Cluster.cpp
  src/ripple/overlay/impl/ConnectAttempt.cpp
  src/ripple/overlay/impl/Handshake.cpp
  src/ripple/overlay/impl/Message.cpp
  src/ripple/overlay/impl/OverlayImpl.cpp
  src/ripple/overlay/impl/PeerImp.cpp
  src/ripple/overlay/impl/PeerReservationTable.cpp
  src/ripple/overlay/impl/PeerSet.cpp
  src/ripple/overlay/impl/ProtocolVersion.cpp
  src/ripple/overlay/impl/TrafficCount.cpp
  src/ripple/overlay/impl/TxMetrics.cpp
  #[===============================[
     main sources:
       subdir: peerfinder
  #]===============================]
  src/ripple/peerfinder/impl/Bootcache.cpp
  src/ripple/peerfinder/impl/Endpoint.cpp
  src/ripple/peerfinder/impl/PeerfinderConfig.cpp
  src/ripple/peerfinder/impl/PeerfinderManager.cpp
  src/ripple/peerfinder/impl/SlotImp.cpp
  src/ripple/peerfinder/impl/SourceStrings.cpp
  #[===============================[
     main sources:
       subdir: rpc
  #]===============================]
  src/ripple/rpc/handlers/AccountChannels.cpp
  src/ripple/rpc/handlers/AccountCurrenciesHandler.cpp
  src/ripple/rpc/handlers/AccountInfo.cpp
  src/ripple/rpc/handlers/AccountLines.cpp
  src/ripple/rpc/handlers/AccountObjects.cpp
  src/ripple/rpc/handlers/AccountOffers.cpp
  src/ripple/rpc/handlers/AccountTx.cpp
  src/ripple/rpc/handlers/AMMInfo.cpp
  src/ripple/rpc/handlers/BlackList.cpp
  src/ripple/rpc/handlers/BookOffers.cpp
  src/ripple/rpc/handlers/CanDelete.cpp
  src/ripple/rpc/handlers/Connect.cpp
  src/ripple/rpc/handlers/ConsensusInfo.cpp
  src/ripple/rpc/handlers/CrawlShards.cpp
  src/ripple/rpc/handlers/DepositAuthorized.cpp
  src/ripple/rpc/handlers/DownloadShard.cpp
  src/ripple/rpc/handlers/Feature1.cpp
  src/ripple/rpc/handlers/Fee1.cpp
  src/ripple/rpc/handlers/FetchInfo.cpp
  src/ripple/rpc/handlers/GatewayBalances.cpp
  src/ripple/rpc/handlers/GetCounts.cpp
  src/ripple/rpc/handlers/GetAggregatePrice.cpp
  src/ripple/rpc/handlers/LedgerAccept.cpp
  src/ripple/rpc/handlers/LedgerCleanerHandler.cpp
  src/ripple/rpc/handlers/LedgerClosed.cpp
  src/ripple/rpc/handlers/LedgerCurrent.cpp
  src/ripple/rpc/handlers/LedgerData.cpp
  src/ripple/rpc/handlers/LedgerDiff.cpp
  src/ripple/rpc/handlers/LedgerEntry.cpp
  src/ripple/rpc/handlers/LedgerHandler.cpp
  src/ripple/rpc/handlers/LedgerHeader.cpp
  src/ripple/rpc/handlers/LedgerRequest.cpp
  src/ripple/rpc/handlers/LogLevel.cpp
  src/ripple/rpc/handlers/LogRotate.cpp
  src/ripple/rpc/handlers/Manifest.cpp
  src/ripple/rpc/handlers/NFTOffers.cpp
  src/ripple/rpc/handlers/NodeToShard.cpp
  src/ripple/rpc/handlers/NoRippleCheck.cpp
  src/ripple/rpc/handlers/OwnerInfo.cpp
  src/ripple/rpc/handlers/PathFind.cpp
  src/ripple/rpc/handlers/PayChanClaim.cpp
  src/ripple/rpc/handlers/Peers.cpp
  src/ripple/rpc/handlers/Ping.cpp
  src/ripple/rpc/handlers/Print.cpp
  src/ripple/rpc/handlers/Random.cpp
  src/ripple/rpc/handlers/Reservations.cpp
  src/ripple/rpc/handlers/RipplePathFind.cpp
  src/ripple/rpc/handlers/ServerInfo.cpp
  src/ripple/rpc/handlers/ServerState.cpp
  src/ripple/rpc/handlers/SignFor.cpp
  src/ripple/rpc/handlers/SignHandler.cpp
  src/ripple/rpc/handlers/Stop.cpp
  src/ripple/rpc/handlers/Submit.cpp
  src/ripple/rpc/handlers/SubmitMultiSigned.cpp
  src/ripple/rpc/handlers/Subscribe.cpp
  src/ripple/rpc/handlers/TransactionEntry.cpp
  src/ripple/rpc/handlers/Tx.cpp
  src/ripple/rpc/handlers/TxHistory.cpp
  src/ripple/rpc/handlers/TxReduceRelay.cpp
  src/ripple/rpc/handlers/UnlList.cpp
  src/ripple/rpc/handlers/Unsubscribe.cpp
  src/ripple/rpc/handlers/ValidationCreate.cpp
  src/ripple/rpc/handlers/ValidatorInfo.cpp
  src/ripple/rpc/handlers/ValidatorListSites.cpp
  src/ripple/rpc/handlers/Validators.cpp
  src/ripple/rpc/handlers/WalletPropose.cpp
  src/ripple/rpc/impl/DeliveredAmount.cpp
  src/ripple/rpc/impl/Handler.cpp
  src/ripple/rpc/impl/LegacyPathFind.cpp
  src/ripple/rpc/impl/RPCHandler.cpp
  src/ripple/rpc/impl/RPCHelpers.cpp
  src/ripple/rpc/impl/Role.cpp
  src/ripple/rpc/impl/ServerHandler.cpp
  src/ripple/rpc/impl/ShardArchiveHandler.cpp
  src/ripple/rpc/impl/ShardVerificationScheduler.cpp
  src/ripple/rpc/impl/Status.cpp
  src/ripple/rpc/impl/TransactionSign.cpp
  #[===============================[
     main sources:
       subdir: perflog
  #]===============================]
  src/ripple/perflog/impl/PerfLogImp.cpp
  #[===============================[
     main sources:
       subdir: shamap
  #]===============================]
  src/ripple/shamap/impl/NodeFamily.cpp
  src/ripple/shamap/impl/SHAMap.cpp
  src/ripple/shamap/impl/SHAMapDelta.cpp
  src/ripple/shamap/impl/SHAMapInnerNode.cpp
  src/ripple/shamap/impl/SHAMapLeafNode.cpp
  src/ripple/shamap/impl/SHAMapNodeID.cpp
  src/ripple/shamap/impl/SHAMapSync.cpp
  src/ripple/shamap/impl/SHAMapTreeNode.cpp
  src/ripple/shamap/impl/ShardFamily.cpp)
# END XRPLD SOURCES

  #[===============================[
     test sources:
       subdir: app
  #]===============================]
if (tests)
  target_sources (rippled PRIVATE
    src/test/app/AccountDelete_test.cpp
    src/test/app/AccountTxPaging_test.cpp
    src/test/app/AmendmentTable_test.cpp
    src/test/app/AMM_test.cpp
    src/test/app/AMMCalc_test.cpp
    src/test/app/AMMExtended_test.cpp
    src/test/app/Check_test.cpp
    src/test/app/Clawback_test.cpp
    src/test/app/CrossingLimits_test.cpp
    src/test/app/DeliverMin_test.cpp
    src/test/app/DepositAuth_test.cpp
    src/test/app/Discrepancy_test.cpp
    src/test/app/DID_test.cpp
    src/test/app/DNS_test.cpp
    src/test/app/Escrow_test.cpp
    src/test/app/FeeVote_test.cpp
    src/test/app/Flow_test.cpp
    src/test/app/Freeze_test.cpp
    src/test/app/HashRouter_test.cpp
    src/test/app/LedgerHistory_test.cpp
    src/test/app/LedgerLoad_test.cpp
    src/test/app/LedgerMaster_test.cpp
    src/test/app/LedgerReplay_test.cpp
    src/test/app/LoadFeeTrack_test.cpp
    src/test/app/Manifest_test.cpp
    src/test/app/MultiSign_test.cpp
    src/test/app/NetworkID_test.cpp
    src/test/app/NFToken_test.cpp
    src/test/app/NFTokenBurn_test.cpp
    src/test/app/NFTokenDir_test.cpp
    src/test/app/OfferStream_test.cpp
    src/test/app/Offer_test.cpp
    src/test/app/Oracle_test.cpp
    src/test/app/OversizeMeta_test.cpp
    src/test/app/Path_test.cpp
    src/test/app/PayChan_test.cpp
    src/test/app/PayStrand_test.cpp
    src/test/app/PseudoTx_test.cpp
    src/test/app/RCLCensorshipDetector_test.cpp
    src/test/app/RCLValidations_test.cpp
    src/test/app/ReducedOffer_test.cpp
    src/test/app/Regression_test.cpp
    src/test/app/SHAMapStore_test.cpp
    src/test/app/XChain_test.cpp
    src/test/app/SetAuth_test.cpp
    src/test/app/SetRegularKey_test.cpp
    src/test/app/SetTrust_test.cpp
    src/test/app/Taker_test.cpp
    src/test/app/TheoreticalQuality_test.cpp
    src/test/app/Ticket_test.cpp
    src/test/app/Transaction_ordering_test.cpp
    src/test/app/TrustAndBalance_test.cpp
    src/test/app/TxQ_test.cpp
    src/test/app/ValidatorKeys_test.cpp
    src/test/app/ValidatorList_test.cpp
    src/test/app/ValidatorSite_test.cpp
    src/test/app/tx/apply_test.cpp
    #[===============================[
       test sources:
         subdir: basics
    #]===============================]
    src/test/basics/Buffer_test.cpp
    src/test/basics/DetectCrash_test.cpp
    src/test/basics/Expected_test.cpp
    src/test/basics/FileUtilities_test.cpp
    src/test/basics/IOUAmount_test.cpp
    src/test/basics/KeyCache_test.cpp
    src/test/basics/Number_test.cpp
    src/test/basics/PerfLog_test.cpp
    src/test/basics/RangeSet_test.cpp
    src/test/basics/scope_test.cpp
    src/test/basics/Slice_test.cpp
    src/test/basics/StringUtilities_test.cpp
    src/test/basics/TaggedCache_test.cpp
    src/test/basics/XRPAmount_test.cpp
    src/test/basics/base58_test.cpp
    src/test/basics/base64_test.cpp
    src/test/basics/base_uint_test.cpp
    src/test/basics/contract_test.cpp
    src/test/basics/FeeUnits_test.cpp
    src/test/basics/hardened_hash_test.cpp
    src/test/basics/join_test.cpp
    src/test/basics/mulDiv_test.cpp
    src/test/basics/tagged_integer_test.cpp
    #[===============================[
       test sources:
         subdir: beast
    #]===============================]
    src/test/beast/IPEndpoint_test.cpp
    src/test/beast/LexicalCast_test.cpp
    src/test/beast/SemanticVersion_test.cpp
    src/test/beast/aged_associative_container_test.cpp
    src/test/beast/beast_CurrentThreadName_test.cpp
    src/test/beast/beast_Journal_test.cpp
    src/test/beast/beast_PropertyStream_test.cpp
    src/test/beast/beast_Zero_test.cpp
    src/test/beast/beast_abstract_clock_test.cpp
    src/test/beast/beast_basic_seconds_clock_test.cpp
    src/test/beast/beast_io_latency_probe_test.cpp
    src/test/beast/define_print.cpp
    #[===============================[
       test sources:
         subdir: conditions
    #]===============================]
    src/test/conditions/PreimageSha256_test.cpp
    #[===============================[
       test sources:
         subdir: consensus
    #]===============================]
    src/test/consensus/ByzantineFailureSim_test.cpp
    src/test/consensus/Consensus_test.cpp
    src/test/consensus/DistributedValidatorsSim_test.cpp
    src/test/consensus/LedgerTiming_test.cpp
    src/test/consensus/LedgerTrie_test.cpp
    src/test/consensus/NegativeUNL_test.cpp
    src/test/consensus/ScaleFreeSim_test.cpp
    src/test/consensus/Validations_test.cpp
    #[===============================[
       test sources:
         subdir: core
    #]===============================]
    src/test/core/ClosureCounter_test.cpp
    src/test/core/Config_test.cpp
    src/test/core/Coroutine_test.cpp
    src/test/core/CryptoPRNG_test.cpp
    src/test/core/JobQueue_test.cpp
    src/test/core/SociDB_test.cpp
    src/test/core/Workers_test.cpp
    #[===============================[
       test sources:
         subdir: csf
    #]===============================]
    src/test/csf/BasicNetwork_test.cpp
    src/test/csf/Digraph_test.cpp
    src/test/csf/Histogram_test.cpp
    src/test/csf/Scheduler_test.cpp
    src/test/csf/impl/Sim.cpp
    src/test/csf/impl/ledgers.cpp
    #[===============================[
       test sources:
         subdir: json
    #]===============================]
    src/test/json/Object_test.cpp
    src/test/json/Output_test.cpp
    src/test/json/Writer_test.cpp
    src/test/json/json_value_test.cpp
    #[===============================[
       test sources:
         subdir: jtx
    #]===============================]
    src/test/jtx/Env_test.cpp
    src/test/jtx/WSClient_test.cpp
    src/test/jtx/impl/Account.cpp
    src/test/jtx/impl/AMM.cpp
    src/test/jtx/impl/AMMTest.cpp
    src/test/jtx/impl/Env.cpp
    src/test/jtx/impl/JSONRPCClient.cpp
    src/test/jtx/impl/Oracle.cpp
    src/test/jtx/impl/TestHelpers.cpp
    src/test/jtx/impl/WSClient.cpp
    src/test/jtx/impl/acctdelete.cpp
    src/test/jtx/impl/account_txn_id.cpp
    src/test/jtx/impl/amount.cpp
    src/test/jtx/impl/attester.cpp
    src/test/jtx/impl/balance.cpp
    src/test/jtx/impl/check.cpp
    src/test/jtx/impl/delivermin.cpp
    src/test/jtx/impl/deposit.cpp
    src/test/jtx/impl/did.cpp
    src/test/jtx/impl/envconfig.cpp
    src/test/jtx/impl/fee.cpp
    src/test/jtx/impl/flags.cpp
    src/test/jtx/impl/invoice_id.cpp
    src/test/jtx/impl/jtx_json.cpp
    src/test/jtx/impl/last_ledger_sequence.cpp
    src/test/jtx/impl/memo.cpp
    src/test/jtx/impl/multisign.cpp
    src/test/jtx/impl/offer.cpp
    src/test/jtx/impl/owners.cpp
    src/test/jtx/impl/paths.cpp
    src/test/jtx/impl/pay.cpp
    src/test/jtx/impl/quality2.cpp
    src/test/jtx/impl/rate.cpp
    src/test/jtx/impl/regkey.cpp
    src/test/jtx/impl/sendmax.cpp
    src/test/jtx/impl/seq.cpp
    src/test/jtx/impl/xchain_bridge.cpp
    src/test/jtx/impl/sig.cpp
    src/test/jtx/impl/tag.cpp
    src/test/jtx/impl/ticket.cpp
    src/test/jtx/impl/token.cpp
    src/test/jtx/impl/trust.cpp
    src/test/jtx/impl/txflags.cpp
    src/test/jtx/impl/utility.cpp

    #[===============================[
       test sources:
         subdir: ledger
    #]===============================]
    src/test/ledger/BookDirs_test.cpp
    src/test/ledger/Directory_test.cpp
    src/test/ledger/Invariants_test.cpp
    src/test/ledger/PaymentSandbox_test.cpp
    src/test/ledger/PendingSaves_test.cpp
    src/test/ledger/SkipList_test.cpp
    src/test/ledger/View_test.cpp
    #[===============================[
       test sources:
         subdir: net
    #]===============================]
    src/test/net/DatabaseDownloader_test.cpp
    #[===============================[
       test sources:
         subdir: nodestore
    #]===============================]
    src/test/nodestore/Backend_test.cpp
    src/test/nodestore/Basics_test.cpp
    src/test/nodestore/DatabaseShard_test.cpp
    src/test/nodestore/Database_test.cpp
    src/test/nodestore/Timing_test.cpp
    src/test/nodestore/import_test.cpp
    src/test/nodestore/varint_test.cpp
    #[===============================[
       test sources:
         subdir: overlay
    #]===============================]
    src/test/overlay/ProtocolVersion_test.cpp
    src/test/overlay/cluster_test.cpp
    src/test/overlay/short_read_test.cpp
    src/test/overlay/compression_test.cpp
    src/test/overlay/reduce_relay_test.cpp
    src/test/overlay/handshake_test.cpp
    src/test/overlay/tx_reduce_relay_test.cpp
    #[===============================[
       test sources:
         subdir: peerfinder
    #]===============================]
    src/test/peerfinder/Livecache_test.cpp
    src/test/peerfinder/PeerFinder_test.cpp
    #[===============================[
       test sources:
         subdir: plugin
    #]===============================]
    src/test/plugin/Plugins_test.cpp
    #[===============================[
       test sources:
         subdir: protocol
    #]===============================]
    src/test/protocol/ApiVersion_test.cpp
    src/test/protocol/BuildInfo_test.cpp
    src/test/protocol/InnerObjectFormats_test.cpp
    src/test/protocol/Issue_test.cpp
    src/test/protocol/Hooks_test.cpp
    src/test/protocol/Memo_test.cpp
    src/test/protocol/MultiApiJson_test.cpp
    src/test/protocol/PublicKey_test.cpp
    src/test/protocol/Quality_test.cpp
    src/test/protocol/STAccount_test.cpp
    src/test/protocol/STAmount_test.cpp
    src/test/protocol/STObject_test.cpp
    src/test/protocol/STTx_test.cpp
    src/test/protocol/STValidation_test.cpp
    src/test/protocol/SecretKey_test.cpp
    src/test/protocol/Seed_test.cpp
    src/test/protocol/SeqProxy_test.cpp
    src/test/protocol/TER_test.cpp
    src/test/protocol/types_test.cpp
    #[===============================[
       test sources:
         subdir: resource
    #]===============================]
    src/test/resource/Logic_test.cpp
    #[===============================[
       test sources:
         subdir: rpc
    #]===============================]
    src/test/rpc/AccountCurrencies_test.cpp
    src/test/rpc/AccountInfo_test.cpp
    src/test/rpc/AccountLinesRPC_test.cpp
    src/test/rpc/AccountObjects_test.cpp
    src/test/rpc/AccountOffers_test.cpp
    src/test/rpc/AccountSet_test.cpp
    src/test/rpc/AccountTx_test.cpp
    src/test/rpc/AmendmentBlocked_test.cpp
    src/test/rpc/AMMInfo_test.cpp
    src/test/rpc/Book_test.cpp
    src/test/rpc/DepositAuthorized_test.cpp
    src/test/rpc/DeliveredAmount_test.cpp
    src/test/rpc/Feature_test.cpp
    src/test/rpc/GatewayBalances_test.cpp
    src/test/rpc/GetAggregatePrice_test.cpp
    src/test/rpc/GetCounts_test.cpp
    src/test/rpc/JSONRPC_test.cpp
    src/test/rpc/KeyGeneration_test.cpp
    src/test/rpc/LedgerClosed_test.cpp
    src/test/rpc/LedgerData_test.cpp
    src/test/rpc/LedgerHeader_test.cpp
    src/test/rpc/LedgerRPC_test.cpp
    src/test/rpc/LedgerRequestRPC_test.cpp
    src/test/rpc/ManifestRPC_test.cpp
    src/test/rpc/NodeToShardRPC_test.cpp
    src/test/rpc/NoRippleCheck_test.cpp
    src/test/rpc/NoRipple_test.cpp
    src/test/rpc/OwnerInfo_test.cpp
    src/test/rpc/Peers_test.cpp
    src/test/rpc/ReportingETL_test.cpp
    src/test/rpc/Roles_test.cpp
    src/test/rpc/RPCCall_test.cpp
    src/test/rpc/RPCOverload_test.cpp
    src/test/rpc/RobustTransaction_test.cpp
    src/test/rpc/ServerInfo_test.cpp
    src/test/rpc/ShardArchiveHandler_test.cpp
    src/test/rpc/Status_test.cpp
    src/test/rpc/Subscribe_test.cpp
    src/test/rpc/Transaction_test.cpp
    src/test/rpc/TransactionEntry_test.cpp
    src/test/rpc/TransactionHistory_test.cpp
    src/test/rpc/ValidatorInfo_test.cpp
    src/test/rpc/ValidatorRPC_test.cpp
    src/test/rpc/Version_test.cpp
    src/test/rpc/Handler_test.cpp
    #[===============================[
       test sources:
         subdir: server
    #]===============================]
    src/test/server/ServerStatus_test.cpp
    src/test/server/Server_test.cpp
    #[===============================[
       test sources:
         subdir: shamap
    #]===============================]
    src/test/shamap/FetchPack_test.cpp
    src/test/shamap/SHAMapSync_test.cpp
    src/test/shamap/SHAMap_test.cpp
    #[===============================[
       test sources:
         subdir: unit_test
    #]===============================]
    src/test/unit_test/multi_runner.cpp)

    add_library(plugin_test_setregularkey SHARED)
    target_sources(plugin_test_setregularkey PRIVATE 
      src/test/plugin/fixtures/SetRegularKey_plugin.cpp
    )
    target_link_libraries(plugin_test_setregularkey PUBLIC Ripple::xrpl_plugin)
    if (!WIN32)
      target_compile_options (plugin_test_setregularkey PUBLIC -Wno-return-type-c-linkage)
    endif()

    add_library(plugin_test_trustset SHARED)
    target_sources(plugin_test_trustset PRIVATE 
      src/test/plugin/fixtures/TrustSet.cpp
    )
    target_link_libraries(plugin_test_trustset PUBLIC Ripple::xrpl_plugin)
    if (!WIN32)
      target_compile_options (plugin_test_trustset PUBLIC -Wno-return-type-c-linkage)
    endif()
    
    add_library(plugin_test_escrowcreate SHARED)
    target_sources(plugin_test_escrowcreate PRIVATE 
      src/test/plugin/fixtures/EscrowCreate.cpp
    )
    target_link_libraries(plugin_test_escrowcreate PUBLIC Ripple::xrpl_plugin)
    if (!WIN32)
      target_compile_options (plugin_test_escrowcreate PUBLIC -Wno-return-type-c-linkage)
    endif()

    add_library(plugin_test_badtransactor SHARED)
    target_sources(plugin_test_badtransactor PRIVATE 
      src/test/plugin/fixtures/SetRegularKeyBadTransactor.cpp
    )
    target_link_libraries(plugin_test_badtransactor PUBLIC Ripple::xrpl_plugin)
    if (!WIN32)
      target_compile_options (plugin_test_badtransactor PUBLIC -Wno-return-type-c-linkage)
    endif()

    add_library(plugin_test_badledgerentry SHARED)
    target_sources(plugin_test_badledgerentry PRIVATE 
      src/test/plugin/fixtures/EscrowCreateBadLedgerEntryType.cpp
    )
    target_link_libraries(plugin_test_badledgerentry PUBLIC Ripple::xrpl_plugin)
    if (!WIN32)
      target_compile_options (plugin_test_badledgerentry PUBLIC -Wno-return-type-c-linkage)
    endif()

    add_library(plugin_test_badstypeid SHARED)
    target_sources(plugin_test_badstypeid PRIVATE 
      src/test/plugin/fixtures/TrustSetBadSTypeID.cpp
    )
    target_link_libraries(plugin_test_badstypeid PUBLIC Ripple::xrpl_plugin)
    if (!WIN32)
      target_compile_options (plugin_test_badstypeid PUBLIC -Wno-return-type-c-linkage)
    endif()

    add_library(plugin_test_badsfieldtypeid SHARED)
    target_sources(plugin_test_badsfieldtypeid PRIVATE 
      src/test/plugin/fixtures/TrustSetBadSFieldTypeID.cpp
    )
    target_link_libraries(plugin_test_badsfieldtypeid PUBLIC Ripple::xrpl_plugin)
    if (!WIN32)
      target_compile_options (plugin_test_badsfieldtypeid PUBLIC -Wno-return-type-c-linkage)
    endif()

    add_library(plugin_test_badsfieldtypepair SHARED)
    target_sources(plugin_test_badsfieldtypepair PRIVATE 
      src/test/plugin/fixtures/TrustSetBadSFieldTypePair.cpp
    )
    target_link_libraries(plugin_test_badsfieldtypepair PUBLIC Ripple::xrpl_plugin)
    if (!WIN32)
      target_compile_options (plugin_test_badsfieldtypepair PUBLIC -Wno-return-type-c-linkage)
    endif()

    add_library(plugin_test_badtercode SHARED)
    target_sources(plugin_test_badtercode PRIVATE 
      src/test/plugin/fixtures/TrustSetBadTERcode.cpp
    )
    target_link_libraries(plugin_test_badtercode PUBLIC Ripple::xrpl_plugin)
    if (!WIN32)
      target_compile_options (plugin_test_badtercode PUBLIC -Wno-return-type-c-linkage)
    endif()

    add_library(plugin_test_badinnerobject SHARED)
    target_sources(plugin_test_badinnerobject PRIVATE 
      src/test/plugin/fixtures/TrustSetBadInnerObject.cpp
    )
    target_link_libraries(plugin_test_badinnerobject PUBLIC Ripple::xrpl_plugin)
    if (!WIN32)
      target_compile_options (plugin_test_badinnerobject PUBLIC -Wno-return-type-c-linkage)
    endif()

    set_target_properties(
      plugin_test_setregularkey
      plugin_test_trustset
      plugin_test_escrowcreate
      plugin_test_badtransactor
      plugin_test_badledgerentry
      plugin_test_badstypeid
      plugin_test_badsfieldtypeid
      plugin_test_badsfieldtypepair
      plugin_test_badtercode
      plugin_test_badinnerobject
      PROPERTIES PREFIX "" SUFFIX ".xrplugin")
    install(TARGETS plugin_test_setregularkey)
    install(TARGETS plugin_test_trustset)
    install(TARGETS plugin_test_escrowcreate)
    install(TARGETS plugin_test_badtransactor)
    install(TARGETS plugin_test_badledgerentry)
    install(TARGETS plugin_test_badstypeid)
    install(TARGETS plugin_test_badsfieldtypeid)
    install(TARGETS plugin_test_badsfieldtypepair)
    install(TARGETS plugin_test_badtercode)
    install(TARGETS plugin_test_badinnerobject)
endif () #tests

target_link_libraries (rippled
  Ripple::boost
  Ripple::opts
  Ripple::libs
  Ripple::xrpl_core
  Ripple::xrpl_plugin
  )
exclude_if_included (rippled)
# define a macro for tests that might need to
# be exluded or run differently in CI environment
if (is_ci)
  target_compile_definitions(rippled PRIVATE RIPPLED_RUNNING_IN_CI)
endif ()

if(reporting)
set_target_properties(rippled PROPERTIES OUTPUT_NAME rippled-reporting)
get_target_property(BIN_NAME rippled OUTPUT_NAME)
message(STATUS "Reporting mode build: rippled renamed ${BIN_NAME}")
  target_compile_definitions(rippled PRIVATE RIPPLED_REPORTING)
endif()

# any files that don't play well with unity should be added here
if (tests)
  set_source_files_properties(
    # these two seem to produce conflicts in beast teardown template methods
    src/test/rpc/ValidatorRPC_test.cpp
    src/test/rpc/ShardArchiveHandler_test.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)
endif () #tests
