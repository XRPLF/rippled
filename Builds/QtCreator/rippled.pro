# Google Protocol Buffers support

PROTOPATH += .
PROTOPATH += ../Protocol
for(p, PROTOPATH):PROTOPATHS += --proto_path=$${p}

protobuf_decl.name = protobuf header
protobuf_decl.input = PROTOS
protobuf_decl.output = ${QMAKE_FILE_BASE}.pb.h
protobuf_decl.commands = protoc --cpp_out="../../build/proto/" --proto_path="../../src/cpp/ripple" ${QMAKE_FILE_NAME}
protobuf_decl.variable_out = GENERATED_FILES
QMAKE_EXTRA_COMPILERS += protobuf_decl

protobuf_impl.name = protobuf implementation
protobuf_impl.input = PROTOS
protobuf_impl.output = ${QMAKE_FILE_BASE}.pb.cc
protobuf_impl.depends = ${QMAKE_FILE_BASE}.pb.h
protobuf_impl.commands = $$escape_expand(\n)
protobuf_impl.variable_out = GENERATED_SOURCES
QMAKE_EXTRA_COMPILERS += protobuf_impl

# Ripple compilation

DESTDIR = ../../build/QtCreator
OBJECTS_DIR = ../../build/QtCreator/obj

TEMPLATE = app
CONFIG += console thread
CONFIG -= qt gui

QMAKE_CXXFLAGS += \
    -Wno-sign-compare \
    -Wno-char-subscripts \
    -Wno-invalid-offsetof \
    -Wno-unused-parameter \
    -Wformat \
    -O0 \
    -pthread

INCLUDEPATH += \
    "../.." \
    "../../build/proto" \
    "../../Subtrees" \
    "../../Subtrees/leveldb/" \
    "../../Subtrees/leveldb/port" \
    "../../Subtrees/leveldb/include"

SOURCES += \
    ../../Subtrees/beast/modules/beast_basics/beast_basics.cpp \
    ../../Subtrees/beast/modules/beast_core/beast_core.cpp \
    ../../modules/ripple_app/ripple_app_pt1.cpp \
    ../../modules/ripple_app/ripple_app_pt2.cpp \
    ../../modules/ripple_app/ripple_app_pt3.cpp \
    ../../modules/ripple_app/ripple_app_pt4.cpp \
    ../../modules/ripple_app/ripple_app_pt5.cpp \
    ../../modules/ripple_app/ripple_app_pt6.cpp \
    ../../modules/ripple_app/ripple_app_pt7.cpp \
    ../../modules/ripple_app/ripple_app_pt8.cpp \
    ../../modules/ripple_basics/ripple_basics.cpp \
    ../../modules/ripple_core/ripple_core.cpp \
    ../../modules/ripple_client/ripple_client.cpp \
    ../../modules/ripple_data/ripple_data.cpp \
    ../../modules/ripple_json/ripple_json.cpp \
    ../../modules/ripple_leveldb/ripple_leveldb.cpp \
    ../../modules/ripple_sqlite/ripple_sqlite.c \
    ../../modules/ripple_websocket/ripple_websocket.cpp \
    ../../build/proto/ripple.pb.cc

PROTOS = ../../src/cpp/ripple/ripple.proto

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




