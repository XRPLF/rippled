include(FetchContent)

FetchContent_Declare(
  Corrosion
  GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
  GIT_TAG v0.6.1)
FetchContent_MakeAvailable(Corrosion)

corrosion_import_crate(MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml)
corrosion_add_cxxbridge(rs_hello_world_cxxbridge CRATE rs_hello_world FILES
                        lib.rs)

# CMake 4.x validates PUBLIC interface sources when target_link_libraries is
# called by a consuming target, but the generated headers don't exist yet at
# configure time. Clear INTERFACE_SOURCES so the existence check is skipped;
# build-time ordering is still enforced by the custom commands inside the target.
set_target_properties(rs_hello_world_cxxbridge PROPERTIES INTERFACE_SOURCES "")
