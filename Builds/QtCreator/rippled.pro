
# Ripple protocol buffers

PROTOS = ../../src/ripple_data/protocol/ripple.proto
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

DEFINES += _DEBUG

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
    "../../src/leveldb/" \
    "../../src/leveldb/port" \
    "../../src/leveldb/include" \
    $${PROTOS_DIR}

OTHER_FILES += \
#   $$files(../../src/*, true) \
#   $$files(../../src/beast/*) \
#   $$files(../../src/beast/modules/beast_basics/diagnostic/*)
#   $$files(../../src/beast/modules/beast_core/, true)

UI_HEADERS_DIR += ../../src/ripple_basics

# ---------
# New style
#
SOURCES += \
    ../../src/ripple/beast/ripple_beast.unity.cpp \
    ../../src/ripple/beast/ripple_beastc.c \
    ../../src/ripple/common/ripple_common.unity.cpp \
    ../../src/ripple/http/ripple_http.unity.cpp \
    ../../src/ripple/json/ripple_json.unity.cpp \
    ../../src/ripple/peerfinder/ripple_peerfinder.unity.cpp \
    ../../src/ripple/radmap/ripple_radmap.unity.cpp \
    ../../src/ripple/resource/ripple_resource.unity.cpp \
    ../../src/ripple/sitefiles/ripple_sitefiles.unity.cpp \
    ../../src/ripple/sslutil/ripple_sslutil.unity.cpp \
    ../../src/ripple/testoverlay/ripple_testoverlay.unity.cpp \
    ../../src/ripple/types/ripple_types.unity.cpp \
    ../../src/ripple/validators/ripple_validators.unity.cpp

# ---------
# Old style
#
SOURCES += \
    ../../src/ripple_app/ripple_app.unity.cpp \
    ../../src/ripple_app/ripple_app_pt1.unity.cpp \
    ../../src/ripple_app/ripple_app_pt2.unity.cpp \
    ../../src/ripple_app/ripple_app_pt3.unity.cpp \
    ../../src/ripple_app/ripple_app_pt4.unity.cpp \
    ../../src/ripple_app/ripple_app_pt5.unity.cpp \
    ../../src/ripple_app/ripple_app_pt6.unity.cpp \
    ../../src/ripple_app/ripple_app_pt7.unity.cpp \
    ../../src/ripple_app/ripple_app_pt8.unity.cpp \
    ../../src/ripple_basics/ripple_basics.unity.cpp \
    ../../src/ripple_core/ripple_core.unity.cpp \
    ../../src/ripple_data/ripple_data.unity.cpp \
    ../../src/ripple_hyperleveldb/ripple_hyperleveldb.unity.cpp \
    ../../src/ripple_leveldb/ripple_leveldb.unity.cpp \
    ../../src/ripple_net/ripple_net.unity.cpp \
    ../../src/ripple_overlay/ripple_overlay.unity.cpp \
    ../../src/ripple_rpc/ripple_rpc.unity.cpp \
    ../../src/ripple_websocket/ripple_websocket.unity.cpp

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
