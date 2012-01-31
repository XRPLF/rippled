# Copyright (c) 2009-2010 Satoshi Nakamoto
# Distributed under the MIT/X11 software license, see the accompanying
# file license.txt or http://www.opensource.org/licenses/mit-license.php.

CXX=g++ -I/packages/openssl-1.0.0/include -Wall -Wno-sign-compare -Wno-char-subscripts

DEFS=

LIBS= \
   -pthread \
 -Wl,-Bstatic \
   -l boost_system-mt \
   -l boost_filesystem-mt \
   -l boost_program_options-mt \
   -l boost_thread-mt \
   -l protobuf \
   /packages/openssl-1.0.0/libssl.a /packages/openssl-1.0.0/libcrypto.a

ifdef USE_UPNP
	LIBS += -l miniupnpc DEFS += -DUSE_UPNP=$(USE_UPNP)
endif

LIBS+= \
 -Wl,-Bdynamic \
   -l z \
   -l dl

DEBUGFLAGS=-g -DDEBUG
CXXFLAGS=-O0 -Wno-invalid-offsetof -Wformat $(DEBUGFLAGS) $(DEFS)
HEADERS = \
    Application.h \
    base58.h \
    bignum.h \
    BitcoinUtil.h \
    CallRPC.h \
    Config.h \
    ConnectionPool.h \
    Conversion.h \
    HttpReply.h \
    HttpRequest.h \
    key.h \
    keystore.h \
    KnownNodeList.h \
    Ledger.h \
    LedgerHistory.h \
    LedgerMaster.h \
    NetworkThread.h \
    NewcoinAddress.h \
    PackedMessage.h \
    PeerDoor.h \
    Peer.h \
    RequestParser.h \
    RPCCommands.h \
    RPCDoor.h \
    RPC.h \
    RPCServer.h \
    script.h \
    SecureAllocator.h \
    TimingService.h \
    TransactionBundle.h \
    Transaction.h \
    types.h \
    uint256.h \
    UniqueNodeList.h \
    ValidationCollection.h \
    Wallet.h

SRCS= keystore.cpp BitcoinUtil.cpp \
 main.cpp Hanko.cpp Transaction.cpp SHAMap.cpp SHAMapNodes.cpp Serializer.cpp Ledger.cpp \
 AccountState.cpp Wallet.cpp NewcoinAddress.cpp Config.cpp PackedMessage.cpp SHAMapSync.cpp \
 Application.cpp TimingService.cpp KnownNodeList.cpp ConnectionPool.cpp Peer.cpp LedgerAcquire.cpp \
 PeerDoor.cpp RPCDoor.cpp RPCServer.cpp rpc.cpp Conversion.cpp RequestParser.cpp HashedObject.cpp \
 UniqueNodeList.cpp PubKeyCache.cpp SHAMapDiff.cpp DeterministicKeys.cpp LedgerMaster.cpp \
 LedgerHistory.cpp NetworkOPs.cpp CallRPC.cpp DBInit.cpp LocalTransaction.cpp TransactionMaster.cpp

DBSRCS=	SqliteDatabase.cpp database.cpp
DBSRCC= sqlite3.c

UTILSRCS= pugixml.cpp

JSONSRCS= json_reader.cpp json_value.cpp json_writer.cpp

# Application.cpp     HttpReply.cpp      main.cpp            RPCCommands.cpp        \
# BitcoinUtil.cpp     keystore.cpp       NewcoinAddress.cpp  rpc.cpp                UniqueNodeList.cpp \
# CallRPC.cpp         KnownNodeList.cpp  PackedMessage.cpp   RPCDoor.cpp            ValidationCollection.cpp \
# Config.cpp          Ledger.cpp         Peer.cpp            RPCServer.cpp          Wallet.cpp \
#ConnectionPool.cpp  LedgerHistory.cpp  PeerDoor.cpp        TimingService.cpp \
# Conversion.cpp      LedgerMaster.cpp   RequestParser.cpp   TransactionBundle.cpp  util/pugixml.o \
# database/SqliteDatabase.cpp database/database.cpp
# database/linux/mysqldatabase.cpp database/database.cpp database/SqliteDatabase.cpp

OBJS= $(SRCS:%.cpp=%.o) $(DBSRCS:%.cpp=database/%.o) $(DBSRCC:%.c=database/%.o)
OBJS+= $(UTILSRCS:%.cpp=util/%.o) newcoin.pb.o
OBJS+= $(JSONSRCS:%.cpp=json/%.o)
#cryptopp/obj/sha.o cryptopp/obj/cpu.o

all: newcoind

newcoin.pb.h:	newcoin.proto
	protoc --cpp_out=. newcoin.proto

%.o:	%.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<


newcoind:	$(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

.dep:	newcoin.pb.h
	$(CXX) -M $(SRCS) $(CXXFLAGS) > .dep

clean:
	-rm -f newcoind
	-rm -f *.o database/*.o util/*.o
	-rm -f headers.h.gch
	-rm -f newcoin.pb.*
	-rm -f .dep

include .dep
