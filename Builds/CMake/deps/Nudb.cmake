#[===================================================================[
   NIH dep: nudb

   NuDB is header-only, thus is an INTERFACE lib in CMake.
   TODO: move the library definition into NuDB repo and add
   proper targets and export/install
#]===================================================================]

if (is_root_project) # NuDB not needed in the case of xrpl_core inclusion build
  add_library (nudb INTERFACE)
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.11)
    FetchContent_Declare(
      nudb_src
      GIT_REPOSITORY https://github.com/CPPAlliance/NuDB.git
      GIT_TAG        2.0.5
    )
    FetchContent_GetProperties(nudb_src)
    if(NOT nudb_src_POPULATED)
      message (STATUS "Pausing to download NuDB...")
      FetchContent_Populate(nudb_src)
    endif()
  else ()
    ExternalProject_Add (nudb_src
      PREFIX ${nih_cache_path}
      GIT_REPOSITORY https://github.com/CPPAlliance/NuDB.git
      GIT_TAG 2.0.5
      CONFIGURE_COMMAND ""
      BUILD_COMMAND ""
      TEST_COMMAND ""
      INSTALL_COMMAND ""
    )
    ExternalProject_Get_Property (nudb_src SOURCE_DIR)
    set (nudb_src_SOURCE_DIR "${SOURCE_DIR}")
    file (MAKE_DIRECTORY ${nudb_src_SOURCE_DIR}/include)
    add_dependencies (nudb nudb_src)
  endif ()

  file(TO_CMAKE_PATH "${nudb_src_SOURCE_DIR}" nudb_src_SOURCE_DIR)
# specify as system includes so as to avoid warnings
  target_include_directories (nudb SYSTEM INTERFACE ${nudb_src_SOURCE_DIR}/include)
  target_link_libraries (nudb
    INTERFACE
      Boost::thread
      Boost::system)
  add_library (NIH::nudb ALIAS nudb)
  target_link_libraries (ripple_libs INTERFACE NIH::nudb)
endif ()
