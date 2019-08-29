#[===================================================================[
   install stuff
#]===================================================================]

install (
  TARGETS
    secp256k1
    ed25519-donna
    common
    opts
    ripple_syslibs
    ripple_boost
    xrpl_core
  EXPORT RippleExports
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include)
install (EXPORT RippleExports
  FILE RippleTargets.cmake
  NAMESPACE Ripple::
  DESTINATION lib/cmake/ripple)
include (CMakePackageConfigHelpers)
write_basic_package_version_file (
  RippleConfigVersion.cmake
  VERSION ${rippled_version}
  COMPATIBILITY SameMajorVersion)

if (is_root_project)
  install (TARGETS rippled RUNTIME DESTINATION bin)
  set_target_properties(rippled PROPERTIES INSTALL_RPATH_USE_LINK_PATH ON)
  install (
    FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake/RippleConfig.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/RippleConfigVersion.cmake
    DESTINATION lib/cmake/ripple)
  # sample configs should not overwrite existing files
  # install if-not-exists workaround as suggested by
  # https://cmake.org/Bug/view.php?id=12646
  install(CODE "
    macro (copy_if_not_exists SRC DEST NEWNAME)
      if (NOT EXISTS \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/\${DEST}/\${NEWNAME}\")
        file (INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/\${DEST}\" FILES \"\${SRC}\" RENAME \"\${NEWNAME}\")
      else ()
        message (\"-- Skipping : \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/\${DEST}/\${NEWNAME}\")
      endif ()
    endmacro()
    copy_if_not_exists(\"${CMAKE_CURRENT_SOURCE_DIR}/cfg/rippled-example.cfg\" etc rippled.cfg)
    copy_if_not_exists(\"${CMAKE_CURRENT_SOURCE_DIR}/cfg/validators-example.txt\" etc validators.txt)
  ")
endif ()
