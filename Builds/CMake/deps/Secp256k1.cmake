#[===================================================================[
   NIH dep: secp256k1
#]===================================================================]

add_library (secp256k1_lib STATIC IMPORTED GLOBAL)

if (NOT WIN32)
  find_package(secp256k1)
endif()

if(secp256k1)
  set_target_properties (secp256k1_lib PROPERTIES
    IMPORTED_LOCATION_DEBUG
      ${secp256k1}
    IMPORTED_LOCATION_RELEASE
      ${secp256k1}
    INTERFACE_INCLUDE_DIRECTORIES
      ${SECP256K1_INCLUDE_DIR})

  add_library (secp256k1 ALIAS secp256k1_lib)
  add_library (NIH::secp256k1 ALIAS secp256k1_lib)

else()
  set(INSTALL_SECP256K1 true)

  add_library (secp256k1 STATIC
    src/secp256k1/src/secp256k1.c)
  target_compile_definitions (secp256k1
    PRIVATE
      USE_NUM_NONE
      USE_FIELD_10X26
      USE_FIELD_INV_BUILTIN
      USE_SCALAR_8X32
      USE_SCALAR_INV_BUILTIN)
  target_include_directories (secp256k1
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
      $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/secp256k1)
  target_compile_options (secp256k1
    PRIVATE
      $<$<BOOL:${MSVC}>:-wd4319>
      $<$<NOT:$<BOOL:${MSVC}>>:
        -Wno-deprecated-declarations
        -Wno-unused-function
      >
      $<$<BOOL:${is_gcc}>:-Wno-nonnull-compare>)
  target_link_libraries (ripple_libs INTERFACE NIH::secp256k1)
#[===========================[
     headers installation
#]===========================]
  install (
    FILES
      src/secp256k1/include/secp256k1.h
  DESTINATION include/secp256k1/include)

  add_library (NIH::secp256k1 ALIAS secp256k1)
endif()
