# Copyright (c) 2009-2010 Satoshi Nakamoto
# Distributed under the MIT/X11 software license, see the accompanying
# file license.txt or http://www.opensource.org/licenses/mit-license.php.

CXX=g++ -I/packages/openssl-1.0.0/include

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
   -l dl \
   -l sqlite3

DEBUGFLAGS=-g
CXXFLAGS=-O2 -Wno-invalid-offsetof -Wformat $(DEBUGFLAGS) $(DEFS)
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
    Wallet.h \
    newcoin.pb.h

SRCS= \
 test.cpp Hanko.cpp Transaction.cpp SHAMap.cpp \
 Application.cpp     HttpReply.cpp      main.cpp            RPCCommands.cpp        \
 BitcoinUtil.cpp     keystore.cpp       NewcoinAddress.cpp  rpc.cpp                UniqueNodeList.cpp \
 CallRPC.cpp         KnownNodeList.cpp  PackedMessage.cpp   RPCDoor.cpp            ValidationCollection.cpp \
 Config.cpp          Ledger.cpp         Peer.cpp            RPCServer.cpp          Wallet.cpp \
 ConnectionPool.cpp  LedgerHistory.cpp  PeerDoor.cpp        TimingService.cpp \
 Conversion.cpp      LedgerMaster.cpp   RequestParser.cpp   TransactionBundle.cpp  util/pugixml.o \
 database/SqliteDatabase.cpp database/database.cpp
# database/linux/mysqldatabase.cpp database/database.cpp database/SqliteDatabase.cpp

OBJS= $(SRCS:%.cpp=obj/%.o) obj/newcoin.pb.o cryptopp/obj/sha.o cryptopp/obj/cpu.o

all: newcoind

obj/%.o: %.cpp $(HEADERS)
	$(CXX) -c $(CXXFLAGS) -o $@ $<

cryptopp/obj/%.o: cryptopp/%.cpp
	$(CXX) -c $(CXXFLAGS) -O3 -o $@ $<

newcoin.pb.h:	newcoin.proto
	protoc --cpp_out=. newcoin.proto

obj/newcoin.pb.o:	newcoin.pb.h
	$(CXX) -c $(CXXFLAGS) -o $@ newcoin.pb.cc

newcoind: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

clean:
	-rm -f newcoind
	-rm -f obj/*.o
	-rm -f obj/test/*.o
	-rm -f cryptopp/obj/*.o
	-rm -f headers.h.gch
	-rm -f newcoin.pb.*

