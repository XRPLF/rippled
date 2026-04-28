set(CORROSION_VERSION 0.6.1)

find_package(Corrosion ${CORROSION_VERSION} QUIET)
if(NOT Corrosion_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Corrosion
        GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
        GIT_TAG v${CORROSION_VERSION})
    FetchContent_MakeAvailable(Corrosion)
endif()

corrosion_import_crate(MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml)
corrosion_add_cxxbridge(rs_hello_world_cxxbridge CRATE rs_hello_world FILES
                        lib.rs)

# CMake 4.x validates PUBLIC interface sources when target_link_libraries is
# called by a consuming target, but the generated headers don't exist yet at
# configure time. Clear INTERFACE_SOURCES so the existence check is skipped;
# build-time ordering is still enforced by the custom commands inside the target.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set_target_properties(rs_hello_world_cxxbridge PROPERTIES INTERFACE_SOURCES "")
endif()
