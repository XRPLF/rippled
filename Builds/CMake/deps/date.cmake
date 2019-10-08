#[===================================================================[
   NIH dep: date

   the main library is header-only, thus is an INTERFACE lib in CMake.

   NOTE: this has been accepted into c++20 so can likely be replaced
   when we update to that standard
#]===================================================================]

add_library (hh_date INTERFACE)
if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.11)
FetchContent_Declare(
  hh_date_src
  GIT_REPOSITORY https://github.com/HowardHinnant/date.git
  # TODO pick next stable tagged release after 2.4.1
  GIT_TAG        23fa1bb86d24550d99b9b259aa1005d53e5c46ce
)
FetchContent_GetProperties(hh_date_src)
if(NOT hh_date_src_POPULATED)
  message (STATUS "Pausing to download date lib...")
  FetchContent_Populate(hh_date_src)
endif()
else ()
ExternalProject_Add (hh_date_src
  PREFIX ${nih_cache_path}
  GIT_REPOSITORY https://github.com/HowardHinnant/date.git
  GIT_TAG        23fa1bb86d24550d99b9b259aa1005d53e5c46ce
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  TEST_COMMAND ""
  INSTALL_COMMAND ""
)
ExternalProject_Get_Property (hh_date_src SOURCE_DIR)
set (hh_date_src_SOURCE_DIR "${SOURCE_DIR}")
file (MAKE_DIRECTORY ${hh_date_src_SOURCE_DIR}/include)
add_dependencies (hh_date hh_date_src)
endif ()

file(TO_CMAKE_PATH "${hh_date_src_SOURCE_DIR}" hh_date_src_SOURCE_DIR)
target_include_directories (hh_date
SYSTEM INTERFACE
    $<BUILD_INTERFACE:${hh_date_src_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
add_library (NIH::date ALIAS hh_date)
install (
FILES
  ${hh_date_src_SOURCE_DIR}/include/date/date.h
DESTINATION include/date)

