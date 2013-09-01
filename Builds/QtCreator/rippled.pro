
# Ripple protocol buffers

PROTOS = ../../modules/ripple_data/protocol/ripple.proto
PROTOS_DIR = ../../build/proto

# Google Protocol Buffers support

protobuf_h.name = protobuf header
protobuf_h.input = PROTOS
protobuf_h.output = $${PROTOS_DIR}/${QMAKE_FILE_BASE}.pb.h
protobuf_h.depends = ${QMAKE_FILE_NAME}
protobuf_h.commands = protoc --cpp_out=$${PROTOS_DIR} --proto_path=${QMAKE_FILE_PATH} ${QMAKE_FILE_NAME}
protobuf_h.variable_out = HEADERS
QMAKE_EXTRA_COMPILERS += protobuf_h

protobuf_cc.name = protobuf implementation
protobuf_cc.input = PROTOS
protobuf_cc.output = $${PROTOS_DIR}/${QMAKE_FILE_BASE}.pb.cc
protobuf_cc.depends = $${PROTOS_DIR}/${QMAKE_FILE_BASE}.pb.h
protobuf_cc.commands = $$escape_expand(\\n)
#protobuf_cc.variable_out = SOURCES
QMAKE_EXTRA_COMPILERS += protobuf_cc

# Ripple compilation

DESTDIR = ../../build/QtCreator
OBJECTS_DIR = ../../build/QtCreator/obj

TEMPLATE = app
CONFIG += console thread warn_off
CONFIG -= qt gui

linux-g++:QMAKE_CXXFLAGS += \
    -Wall \
    -Wno-sign-compare \
    -Wno-char-subscripts \
    -Wno-invalid-offsetof \
    -Wno-unused-parameter \
    -Wformat \
    -O0 \
    -std=c++11 \
    -pthread

INCLUDEPATH += \
    "../.." \
    "../../Subtrees" \
    "../../Subtrees/leveldb/" \
    "../../Subtrees/leveldb/port" \
    "../../Subtrees/leveldb/include" \
    $${PROTOS_DIR}

OTHER_FILES += \
#   $$files(../../Subtrees/beast/*) \
#   $$files(../../Subtrees/beast/modules/beast_basics/diagnostic/*)
#   $$files(../../Subtrees/beast/modules/beast_core/, true)
#   $$files(../../modules/*, true) \

UI_HEADERS_DIR += ../../modules/ripple_basics

SOURCES += \
    ../../Subtrees/beast/modules/beast_asio/beast_asio.cpp \
    ../../Subtrees/beast/modules/beast_boost/beast_boost.cpp \
    ../../Subtrees/beast/modules/beast_core/beast_core.cpp \
    ../../Subtrees/beast/modules/beast_crypto/beast_crypto.cpp \
    ../../Subtrees/beast/modules/beast_db/beast_db.cpp \
    ../../Subtrees/beast/modules/beast_sqdb/beast_sqdb.cpp \
    ../../Subtrees/beast/modules/beast_sqlite/beast_sqlite.c \
    ../../modules/ripple_app/ripple_app_pt1.cpp \
    ../../modules/ripple_app/ripple_app_pt2.cpp \
    ../../modules/ripple_app/ripple_app_pt3.cpp \
    ../../modules/ripple_app/ripple_app_pt4.cpp \
    ../../modules/ripple_app/ripple_app_pt5.cpp \
    ../../modules/ripple_app/ripple_app_pt6.cpp \
    ../../modules/ripple_app/ripple_app_pt7.cpp \
    ../../modules/ripple_app/ripple_app_pt8.cpp \
    ../../modules/ripple_asio/ripple_asio.cpp \
    ../../modules/ripple_basics/ripple_basics.cpp \
    ../../modules/ripple_core/ripple_core.cpp \
    ../../modules/ripple_client/ripple_client.cpp \
    ../../modules/ripple_data/ripple_data.cpp \
    ../../modules/ripple_hyperleveldb/ripple_hyperleveldb.cpp \
    ../../modules/ripple_leveldb/ripple_leveldb.cpp \
    ../../modules/ripple_mdb/ripple_mdb.c \
    ../../modules/ripple_net/ripple_net.cpp \
    ../../modules/ripple_websocket/ripple_websocket.cpp

LIBS += \
    -lboost_date_time-mt\
    -lboost_filesystem-mt \
    -lboost_program_options-mt \
    -lboost_regex-mt \
    -lboost_system-mt \
    -lboost_thread-mt \
    -lboost_random-mt \
    -lprotobuf \
    -lssl \
    -lrt
