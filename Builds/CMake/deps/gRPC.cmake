
# currently linking to unsecure versions...if we switch, we'll
# need to add ssl as a link dependency to the grpc targets
option (use_secure_grpc "use TLS version of grpc libs." OFF)
if (use_secure_grpc)
  set (grpc_suffix "")
else ()
  set (grpc_suffix "_unsecure")
endif ()

find_package (PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
  pkg_check_modules (grpc QUIET "grpc${grpc_suffix}>=1.25" "grpc++${grpc_suffix}" gpr)
endif ()

if (grpc_FOUND)
  message (STATUS "Found gRPC using pkg-config. Using ${grpc_gpr_PREFIX}.")
endif ()

add_executable (gRPC::grpc_cpp_plugin IMPORTED)
exclude_if_included (gRPC::grpc_cpp_plugin)

if (grpc_FOUND AND NOT local_grpc)
  # use installed grpc (via pkg-config)
  macro (add_imported_grpc libname_)
    if (static)
      set (_search "${CMAKE_STATIC_LIBRARY_PREFIX}${libname_}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    else ()
      set (_search "${CMAKE_SHARED_LIBRARY_PREFIX}${libname_}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()
    find_library(_found_${libname_}
      NAMES ${_search}
      HINTS ${grpc_LIBRARY_DIRS})
    if (_found_${libname_})
      message (STATUS "importing ${libname_} as ${_found_${libname_}}")
    else ()
      message (FATAL_ERROR "using pkg-config for grpc, can't find ${_search}")
    endif ()
    add_library ("gRPC::${libname_}" STATIC IMPORTED GLOBAL)
    set_target_properties ("gRPC::${libname_}" PROPERTIES IMPORTED_LOCATION ${_found_${libname_}})
    if (grpc_INCLUDE_DIRS)
      set_target_properties ("gRPC::${libname_}" PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${grpc_INCLUDE_DIRS})
    endif ()
    target_link_libraries (ripple_libs INTERFACE "gRPC::${libname_}")
    exclude_if_included ("gRPC::${libname_}")
  endmacro ()

  set_target_properties (gRPC::grpc_cpp_plugin PROPERTIES
      IMPORTED_LOCATION "${grpc_gpr_PREFIX}/bin/grpc_cpp_plugin${CMAKE_EXECUTABLE_SUFFIX}")

  pkg_check_modules (cares QUIET libcares)
  if (cares_FOUND)
    if (static)
      set (_search "${CMAKE_STATIC_LIBRARY_PREFIX}cares${CMAKE_STATIC_LIBRARY_SUFFIX}")
    else ()
      set (_search "${CMAKE_SHARED_LIBRARY_PREFIX}cares${CMAKE_SHARED_LIBRARY_SUFFIX}")
    endif()
    find_library(_cares
      NAMES ${_search}
      HINTS ${cares_LIBRARY_DIRS})
    if (NOT _cares)
      message (FATAL_ERROR "using pkg-config for grpc, can't find c-ares")
    endif ()
    add_library (c-ares::cares STATIC IMPORTED GLOBAL)
    set_target_properties (c-ares::cares PROPERTIES IMPORTED_LOCATION ${_cares})
    if (cares_INCLUDE_DIRS)
      set_target_properties (c-ares::cares PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${cares_INCLUDE_DIRS})
    endif ()
    exclude_if_included (c-ares::cares)
  else ()
    message (FATAL_ERROR "using pkg-config for grpc, can't find c-ares")
  endif ()
else ()
  #[===========================[
     c-ares (grpc requires)
  #]===========================]
  ExternalProject_Add (c-ares_src
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/c-ares/c-ares.git
    GIT_TAG cares-1_15_0
    CMAKE_ARGS
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
      -DCMAKE_DEBUG_POSTFIX=_d
      $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
      -DCMAKE_INSTALL_PREFIX=<BINARY_DIR>/_installed_
      -DCARES_SHARED=OFF
      -DCARES_STATIC=ON
      -DCARES_STATIC_PIC=ON
      -DCARES_INSTALL=ON
      -DCARES_MSVC_STATIC_RUNTIME=ON
      $<$<BOOL:${MSVC}>:
        "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
      >
    LOG_BUILD ON
    LOG_CONFIGURE ON
    BUILD_COMMAND
      ${CMAKE_COMMAND}
        --build .
        --config $<CONFIG>
        $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
    TEST_COMMAND ""
    INSTALL_COMMAND
      ${CMAKE_COMMAND} -E env --unset=DESTDIR ${CMAKE_COMMAND} --build . --config $<CONFIG> --target install
    BUILD_BYPRODUCTS
      <BINARY_DIR>/_installed_/lib/${ep_lib_prefix}cares${ep_lib_suffix}
      <BINARY_DIR>/_installed_/lib/${ep_lib_prefix}cares_d${ep_lib_suffix}
  )
  exclude_if_included (c-ares_src)
  ExternalProject_Get_Property (c-ares_src BINARY_DIR)
  set (cares_binary_dir "${BINARY_DIR}")

  add_library (c-ares::cares STATIC IMPORTED GLOBAL)
  file (MAKE_DIRECTORY ${BINARY_DIR}/_installed_/include)
  set_target_properties (c-ares::cares PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${BINARY_DIR}/_installed_/lib/${ep_lib_prefix}cares_d${ep_lib_suffix}
    IMPORTED_LOCATION_RELEASE
      ${BINARY_DIR}/_installed_/lib/${ep_lib_prefix}cares${ep_lib_suffix}
    INTERFACE_INCLUDE_DIRECTORIES
      ${BINARY_DIR}/_installed_/include)
  add_dependencies (c-ares::cares c-ares_src)
  exclude_if_included (c-ares::cares)

  if (NOT has_zlib)
    #[===========================[
       zlib (grpc requires)
    #]===========================]
    if (MSVC)
      set (zlib_debug_postfix "d") # zlib cmake sets this internally for MSVC, so we really don't have a choice
      set (zlib_base "zlibstatic")
    else ()
      set (zlib_debug_postfix "_d")
      set (zlib_base "z")
    endif ()
    ExternalProject_Add (zlib_src
      PREFIX ${nih_cache_path}
      GIT_REPOSITORY https://github.com/madler/zlib.git
      GIT_TAG v1.2.11
      CMAKE_ARGS
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
        -DCMAKE_DEBUG_POSTFIX=${zlib_debug_postfix}
        $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
        -DCMAKE_INSTALL_PREFIX=<BINARY_DIR>/_installed_
        -DBUILD_SHARED_LIBS=OFF
        $<$<BOOL:${MSVC}>:
          "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
          "-DCMAKE_C_FLAGS_DEBUG=-MTd"
          "-DCMAKE_C_FLAGS_RELEASE=-MT"
        >
      LOG_BUILD ON
      LOG_CONFIGURE ON
      BUILD_COMMAND
        ${CMAKE_COMMAND}
          --build .
          --config $<CONFIG>
          $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
      TEST_COMMAND ""
      INSTALL_COMMAND
        ${CMAKE_COMMAND} -E env --unset=DESTDIR ${CMAKE_COMMAND} --build . --config $<CONFIG> --target install
      BUILD_BYPRODUCTS
        <BINARY_DIR>/_installed_/lib/${ep_lib_prefix}${zlib_base}${ep_lib_suffix}
        <BINARY_DIR>/_installed_/lib/${ep_lib_prefix}${zlib_base}${zlib_debug_postfix}${ep_lib_suffix}
    )
    exclude_if_included (zlib_src)
    ExternalProject_Get_Property (zlib_src BINARY_DIR)
    set (zlib_binary_dir "${BINARY_DIR}")

    add_library (ZLIB::ZLIB STATIC IMPORTED GLOBAL)
    file (MAKE_DIRECTORY ${BINARY_DIR}/_installed_/include)
    set_target_properties (ZLIB::ZLIB PROPERTIES
      IMPORTED_LOCATION_DEBUG
        ${BINARY_DIR}/_installed_/lib/${ep_lib_prefix}${zlib_base}${zlib_debug_postfix}${ep_lib_suffix}
      IMPORTED_LOCATION_RELEASE
        ${BINARY_DIR}/_installed_/lib/${ep_lib_prefix}${zlib_base}${ep_lib_suffix}
      INTERFACE_INCLUDE_DIRECTORIES
        ${BINARY_DIR}/_installed_/include)
    add_dependencies (ZLIB::ZLIB zlib_src)
    exclude_if_included (ZLIB::ZLIB)
  endif ()

  #[===========================[
     grpc
  #]===========================]
  ExternalProject_Add (grpc_src
    PREFIX ${nih_cache_path}
    GIT_REPOSITORY https://github.com/grpc/grpc.git
    GIT_TAG v1.25.0
    CMAKE_ARGS
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      $<$<BOOL:${CMAKE_VERBOSE_MAKEFILE}>:-DCMAKE_VERBOSE_MAKEFILE=ON>
      $<$<BOOL:${CMAKE_TOOLCHAIN_FILE}>:-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}>
      $<$<BOOL:${VCPKG_TARGET_TRIPLET}>:-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}>
      -DCMAKE_DEBUG_POSTFIX=_d
      $<$<NOT:$<BOOL:${is_multiconfig}>>:-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}>
      -DgRPC_BUILD_TESTS=OFF
      -DgRPC_BUILD_CSHARP_EXT=OFF
      -DgRPC_MSVC_STATIC_RUNTIME=ON
      -DgRPC_INSTALL=OFF
      -DgRPC_CARES_PROVIDER=package
      -Dc-ares_DIR=${cares_binary_dir}/_installed_/lib/cmake/c-ares
      -DgRPC_SSL_PROVIDER=package
      -DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}
      -DgRPC_PROTOBUF_PROVIDER=package
      -DProtobuf_USE_STATIC_LIBS=$<IF:$<AND:$<BOOL:${Protobuf_FOUND}>,$<NOT:$<BOOL:${static}>>>,OFF,ON>
      -DProtobuf_INCLUDE_DIR=$<JOIN:$<TARGET_PROPERTY:protobuf::libprotobuf,INTERFACE_INCLUDE_DIRECTORIES>,:_:>
      -DProtobuf_LIBRARY=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:protobuf::libprotobuf,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:protobuf::libprotobuf,IMPORTED_LOCATION_RELEASE>>
      -DProtobuf_PROTOC_LIBRARY=$<IF:$<CONFIG:Debug>,$<TARGET_PROPERTY:protobuf::libprotoc,IMPORTED_LOCATION_DEBUG>,$<TARGET_PROPERTY:protobuf::libprotoc,IMPORTED_LOCATION_RELEASE>>
      -DProtobuf_PROTOC_EXECUTABLE=$<TARGET_PROPERTY:protobuf::protoc,IMPORTED_LOCATION>
      -DgRPC_ZLIB_PROVIDER=package
      $<$<NOT:$<BOOL:${has_zlib}>>:-DZLIB_ROOT=${zlib_binary_dir}/_installed_>
      $<$<BOOL:${MSVC}>:
        "-DCMAKE_CXX_FLAGS=-GR -Gd -fp:precise -FS -EHa -MP"
        "-DCMAKE_C_FLAGS=-GR -Gd -fp:precise -FS -MP"
      >
    LOG_BUILD ON
    LOG_CONFIGURE ON
    BUILD_COMMAND
      ${CMAKE_COMMAND}
      --build .
      --config $<CONFIG>
      $<$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.12>:--parallel ${ep_procs}>
      $<$<BOOL:${is_multiconfig}>:
        COMMAND
          ${CMAKE_COMMAND} -E copy
          <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}grpc${grpc_suffix}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}grpc++${grpc_suffix}$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}address_sorting$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>/$<CONFIG>/${ep_lib_prefix}gpr$<$<CONFIG:Debug>:_d>${ep_lib_suffix}
          <BINARY_DIR>/$<CONFIG>/grpc_cpp_plugin${CMAKE_EXECUTABLE_SUFFIX}
          <BINARY_DIR>
        >
    LIST_SEPARATOR :_:
    TEST_COMMAND ""
    INSTALL_COMMAND ""
    DEPENDS c-ares_src
    BUILD_BYPRODUCTS
      <BINARY_DIR>/${ep_lib_prefix}grpc${grpc_suffix}${ep_lib_suffix}
      <BINARY_DIR>/${ep_lib_prefix}grpc${grpc_suffix}_d${ep_lib_suffix}
      <BINARY_DIR>/${ep_lib_prefix}grpc++${grpc_suffix}${ep_lib_suffix}
      <BINARY_DIR>/${ep_lib_prefix}grpc++${grpc_suffix}_d${ep_lib_suffix}
      <BINARY_DIR>/${ep_lib_prefix}address_sorting${ep_lib_suffix}
      <BINARY_DIR>/${ep_lib_prefix}address_sorting_d${ep_lib_suffix}
      <BINARY_DIR>/${ep_lib_prefix}gpr${ep_lib_suffix}
      <BINARY_DIR>/${ep_lib_prefix}gpr_d${ep_lib_suffix}
      <BINARY_DIR>/grpc_cpp_plugin${CMAKE_EXECUTABLE_SUFFIX}
  )
  if (TARGET protobuf_src)
    ExternalProject_Add_StepDependencies(grpc_src build protobuf_src)
  endif ()
  exclude_if_included (grpc_src)
  ExternalProject_Get_Property (grpc_src BINARY_DIR)
  ExternalProject_Get_Property (grpc_src SOURCE_DIR)
  set (grpc_binary_dir "${BINARY_DIR}")
  set (grpc_source_dir "${SOURCE_DIR}")
  if (CMAKE_VERBOSE_MAKEFILE)
    print_ep_logs (grpc_src)
  endif ()
  file (MAKE_DIRECTORY ${SOURCE_DIR}/include)

  macro (add_imported_grpc libname_)
    add_library ("gRPC::${libname_}" STATIC IMPORTED GLOBAL)
    set_target_properties ("gRPC::${libname_}" PROPERTIES
      IMPORTED_LOCATION_DEBUG
        ${grpc_binary_dir}/${ep_lib_prefix}${libname_}_d${ep_lib_suffix}
      IMPORTED_LOCATION_RELEASE
        ${grpc_binary_dir}/${ep_lib_prefix}${libname_}${ep_lib_suffix}
      INTERFACE_INCLUDE_DIRECTORIES
        ${grpc_source_dir}/include)
    add_dependencies ("gRPC::${libname_}" grpc_src)
    target_link_libraries (ripple_libs INTERFACE "gRPC::${libname_}")
    exclude_if_included ("gRPC::${libname_}")
  endmacro ()

  set_target_properties (gRPC::grpc_cpp_plugin PROPERTIES
      IMPORTED_LOCATION "${grpc_binary_dir}/grpc_cpp_plugin${CMAKE_EXECUTABLE_SUFFIX}")
  add_dependencies (gRPC::grpc_cpp_plugin grpc_src)
endif ()

add_imported_grpc (gpr)
add_imported_grpc ("grpc${grpc_suffix}")
add_imported_grpc ("grpc++${grpc_suffix}")
add_imported_grpc (address_sorting)

target_link_libraries ("gRPC::grpc${grpc_suffix}" INTERFACE c-ares::cares gRPC::gpr gRPC::address_sorting ZLIB::ZLIB)
target_link_libraries ("gRPC::grpc++${grpc_suffix}" INTERFACE "gRPC::grpc${grpc_suffix}" gRPC::gpr)

#[=================================[
   generate protobuf sources for
   grpc defs and bundle into a
   static lib
#]=================================]
set (GRPC_GEN_DIR "${CMAKE_BINARY_DIR}/proto_gen_grpc")
file (MAKE_DIRECTORY ${GRPC_GEN_DIR})
set (GRPC_PROTO_SRCS)
set (GRPC_PROTO_HDRS)
set (GRPC_PROTO_ROOT "${CMAKE_SOURCE_DIR}/src/ripple/proto/rpc")
file(GLOB_RECURSE GRPC_DEFINITION_FILES LIST_DIRECTORIES false "${GRPC_PROTO_ROOT}/*.proto")
foreach(file ${GRPC_DEFINITION_FILES})
  get_filename_component(_abs_file ${file} ABSOLUTE)
  get_filename_component(_abs_dir ${_abs_file} DIRECTORY)
  get_filename_component(_basename ${file} NAME_WE)
  get_filename_component(_proto_inc ${GRPC_PROTO_ROOT} DIRECTORY) # updir one level
  file(RELATIVE_PATH _rel_root_file ${_proto_inc} ${_abs_file})
  get_filename_component(_rel_root_dir ${_rel_root_file} DIRECTORY)
  file(RELATIVE_PATH _rel_dir ${CMAKE_CURRENT_SOURCE_DIR} ${_abs_dir})

  set (src_1 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.grpc.pb.cc")
  set (src_2 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.pb.cc")
  set (hdr_1 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.grpc.pb.h")
  set (hdr_2 "${GRPC_GEN_DIR}/${_rel_root_dir}/${_basename}.pb.h")
  add_custom_command(
    OUTPUT ${src_1} ${src_2} ${hdr_1} ${hdr_2}
    COMMAND protobuf::protoc
    ARGS --grpc_out=${GRPC_GEN_DIR}
         --cpp_out=${GRPC_GEN_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         -I ${_proto_inc} -I ${_rel_dir}
         ${_abs_file}
    DEPENDS ${_abs_file} protobuf::protoc gRPC::grpc_cpp_plugin
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running gRPC C++ protocol buffer compiler on ${file}"
    VERBATIM)
    set_source_files_properties(${src_1} ${src_2} ${hdr_1} ${hdr_2} PROPERTIES GENERATED TRUE)
    list(APPEND GRPC_PROTO_SRCS ${src_1} ${src_2})
    list(APPEND GRPC_PROTO_HDRS ${hdr_1} ${hdr_2})
endforeach()

add_library (grpc_pbufs STATIC ${GRPC_PROTO_SRCS} ${GRPC_PROTO_HDRS})
#target_include_directories (grpc_pbufs PRIVATE src)
target_include_directories (grpc_pbufs SYSTEM PUBLIC ${GRPC_GEN_DIR})
target_link_libraries (grpc_pbufs protobuf::libprotobuf "gRPC::grpc++${grpc_suffix}")
target_compile_options (grpc_pbufs
  PUBLIC
    $<$<BOOL:${is_xcode}>:
      --system-header-prefix="google/protobuf"
      -Wno-deprecated-dynamic-exception-spec
    >)
add_library (Ripple::grpc_pbufs ALIAS grpc_pbufs)
target_link_libraries (ripple_libs INTERFACE Ripple::grpc_pbufs)
exclude_if_included (grpc_pbufs)
