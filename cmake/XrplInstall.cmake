#[===================================================================[
   install stuff
#]===================================================================]

include(create_symbolic_link)

install (
  TARGETS
    common
    opts
    xrpl_boost
    xrpl_libs
    xrpl_syslibs
    xrpl.imports.main
    xrpl.libpb
    xrpl.libxrpl
    xrpl.libxrpl.basics
    xrpl.libxrpl.beast
    xrpl.libxrpl.crypto
    xrpl.libxrpl.json
    xrpl.libxrpl.ledger
    xrpl.libxrpl.net
    xrpl.libxrpl.nodestore
    xrpl.libxrpl.protocol
    xrpl.libxrpl.resource
    xrpl.libxrpl.server
    xrpl.libxrpl.shamap
    antithesis-sdk-cpp
  EXPORT XrplExports
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include)

install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(CODE "
  set(CMAKE_MODULE_PATH \"${CMAKE_MODULE_PATH}\")
  include(create_symbolic_link)
  create_symbolic_link(xrpl \
    \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/xrpl)
")

install (EXPORT XrplExports
  FILE XrplTargets.cmake
  NAMESPACE Xrpl::
  DESTINATION lib/cmake/xrpl)
include (CMakePackageConfigHelpers)
write_basic_package_version_file (
  XrplConfigVersion.cmake
  VERSION ${xrpld_version}
  COMPATIBILITY SameMajorVersion)

if (is_root_project AND TARGET xrpld)
  install (TARGETS xrpld RUNTIME DESTINATION bin)
  set_target_properties(xrpld PROPERTIES INSTALL_RPATH_USE_LINK_PATH ON)
  # sample configs should not overwrite existing files
  # install if-not-exists workaround as suggested by
  # https://cmake.org/Bug/view.php?id=12646
  install(CODE "
    macro (copy_if_not_exists SRC DEST NEWNAME)
      if (NOT EXISTS \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/\${DEST}/\${NEWNAME}\")
        file (INSTALL FILE_PERMISSIONS OWNER_READ OWNER_WRITE DESTINATION \"\${CMAKE_INSTALL_PREFIX}/\${DEST}\" FILES \"\${SRC}\" RENAME \"\${NEWNAME}\")
      else ()
        message (\"-- Skipping : \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/\${DEST}/\${NEWNAME}\")
      endif ()
    endmacro()
    copy_if_not_exists(\"${CMAKE_CURRENT_SOURCE_DIR}/cfg/rippled-example.cfg\" etc rippled.cfg)
    copy_if_not_exists(\"${CMAKE_CURRENT_SOURCE_DIR}/cfg/validators-example.txt\" etc validators.txt)
  ")
  install(CODE "
    set(CMAKE_MODULE_PATH \"${CMAKE_MODULE_PATH}\")
    include(create_symbolic_link)
    create_symbolic_link(xrpld${suffix} \
       \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/xrpld${suffix})
  ")
endif ()

install (
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/XrplConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/XrplConfigVersion.cmake
  DESTINATION lib/cmake/xrpl)
