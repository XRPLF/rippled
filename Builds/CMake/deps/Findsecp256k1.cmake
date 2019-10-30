find_package (PkgConfig REQUIRED)
pkg_search_module (secp256k1_PC QUIET libsecp256k1)

if(static)
  set(SECP256K1_LIB libsecp256k1.a)
else()
  set(SECP256K1_LIB secp256k1)
endif()

find_library(secp256k1
  NAMES ${SECP256K1_LIB}
  HINTS
    ${secp256k1_PC_LIBDIR}
    ${secp256k1_PC_LIBRARY_PATHS}
  NO_DEFAULT_PATH)

find_path (SECP256K1_INCLUDE_DIR
  NAMES secp256k1.h
  HINTS
    ${secp256k1_PC_INCLUDEDIR}
    ${secp256k1_PC_INCLUDEDIRS}
  NO_DEFAULT_PATH)
