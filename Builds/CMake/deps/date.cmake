#[===================================================================[
   NIH dep: date

   the main library is header-only, thus is an INTERFACE lib in CMake.

   NOTE: this has been accepted into c++20 so can likely be replaced
   when we update to that standard
#]===================================================================]

find_package (date QUIET)
if (NOT TARGET date::date)
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.14)
    FetchContent_Declare(
      hh_date_src
      GIT_REPOSITORY https://github.com/HowardHinnant/date.git
      GIT_TAG        fc4cf092f9674f2670fb9177edcdee870399b829
    )
    FetchContent_MakeAvailable(hh_date_src)
  else ()
    ExternalProject_Add (hh_date_src
      PREFIX ${nih_cache_path}
      GIT_REPOSITORY https://github.com/HowardHinnant/date.git
      GIT_TAG        fc4cf092f9674f2670fb9177edcdee870399b829
      CONFIGURE_COMMAND ""
      BUILD_COMMAND ""
      TEST_COMMAND ""
      INSTALL_COMMAND ""
    )
    ExternalProject_Get_Property (hh_date_src SOURCE_DIR)
    set (hh_date_src_SOURCE_DIR "${SOURCE_DIR}")
    file (MAKE_DIRECTORY ${hh_date_src_SOURCE_DIR}/include)
    add_library (date_interface INTERFACE)
    add_library (date::date ALIAS date_interface)
    add_dependencies (date_interface hh_date_src)
    file (TO_CMAKE_PATH "${hh_date_src_SOURCE_DIR}" hh_date_src_SOURCE_DIR)
    target_include_directories (date_interface
      SYSTEM INTERFACE
          $<BUILD_INTERFACE:${hh_date_src_SOURCE_DIR}/include>
          $<INSTALL_INTERFACE:include>)
    install (
      FILES
        ${hh_date_src_SOURCE_DIR}/include/date/date.h
      DESTINATION include/date)
    install (TARGETS date_interface
      EXPORT RippleExports
      INCLUDES DESTINATION include)
  endif ()
endif ()

