if(DL_INCLUDE_DIR)
  set(DL_FIND_QUIETLY TRUE)
endif()

find_path(DL_INCLUDE_DIR dlfcn.h)
find_library(DL_LIBRARY NAMES dl)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DL DEFAULT_MSG DL_LIBRARY DL_INCLUDE_DIR)

if(NOT DL_FOUND)
    # if dlopen can be found without linking in dl then,
    # dlopen is part of libc, so don't need to link extra libs.
    include(CheckFunctionExists)
    check_function_exists(dlopen DL_FOUND)
    set(DL_LIBRARY "")
endif()

set(DL_LIBRARIES ${DL_LIBRARY})

mark_as_advanced(DL_LIBRARY DL_INCLUDE_DIR)
