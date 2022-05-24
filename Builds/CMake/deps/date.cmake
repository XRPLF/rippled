#[===================================================================[
   NIH dep: date

   the main library is header-only, thus is an INTERFACE lib in CMake.

   NOTE: this has been accepted into c++20 so can likely be replaced
   when we update to that standard
#]===================================================================]

find_package (date QUIET)
if (NOT TARGET date::date)
  FetchContent_Declare(
    hh_date_src
    SOURCE_DIR ${nih_src_path}/hh_date
    STAMP_DIR ${nih_stamp_path}/hh_date
    GIT_REPOSITORY https://github.com/HowardHinnant/date.git
    GIT_TAG        fc4cf092f9674f2670fb9177edcdee870399b829
  )
  FetchContent_MakeAvailable(hh_date_src)
endif ()
