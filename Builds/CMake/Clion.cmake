if ($ENV{CLION_IDE})
    # Include additional directories or files here for CLion's understanding
    file(GLOB_RECURSE HEADER_FILES "src/ripple/*.h")
    # TODO: is there a better way of doing both .h and .cc at once? surely!??!
    # Maybe I just wasn't patient enough after trying `*.{cc,h}` for the IDE to update?
    file(GLOB_RECURSE PROTO_GEN "build/build/Debug/proto_gen_grpc/ripple/*.cc")
    file(GLOB_RECURSE PROTO_GEN_H "build/build/Debug/proto_gen_grpc/ripple/*.h")

    include_directories("build/build/Debug/proto_gen_grpc")
    include_directories("build/build/Debug/proto_gen_grpc/ripple/proto")

    target_sources(xrpl_core PRIVATE
            ${HEADER_FILES}
            ${PROTO_GEN}
            ${PROTO_GEN_H}
            build/build/Debug/proto_gen/src/ripple/proto/ripple.pb.cc
            build/build/Debug/proto_gen/src/ripple/proto/ripple.pb.h)
endif ()
