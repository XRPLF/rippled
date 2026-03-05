#[===================================================================[
   install stuff
#]===================================================================]

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(xrpl_cmakedir "${CMAKE_INSTALL_LIBDIR}/cmake/xrpl")

if(is_root_project AND TARGET xrpld)
  install(TARGETS xrpld
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT runtime
  )

  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cfg/xrpld-example.cfg"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/xrpld"
        RENAME xrpld.cfg
        COMPONENT runtime
  )
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cfg/validators-example.txt"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/xrpld"
        RENAME validators.txt
        COMPONENT runtime
  )
endif()

install(TARGETS common
                opts
                xrpl_boost
                xrpl_libs
                xrpl_syslibs
                xrpl.imports.main
                xrpl.libpb
                xrpl.libxrpl
                xrpl.libxrpl.basics
                xrpl.libxrpl.beast
                xrpl.libxrpl.conditions
                xrpl.libxrpl.core
                xrpl.libxrpl.crypto
                xrpl.libxrpl.git
                xrpl.libxrpl.json
                xrpl.libxrpl.rdb
                xrpl.libxrpl.ledger
                xrpl.libxrpl.net
                xrpl.libxrpl.nodestore
                xrpl.libxrpl.protocol
                xrpl.libxrpl.resource
                xrpl.libxrpl.server
                xrpl.libxrpl.shamap
                xrpl.libxrpl.tx
  EXPORT xrpl_targets
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT development
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT development
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT development
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT development
)

install(EXPORT xrpl_targets
        NAMESPACE xrpl::
        DESTINATION "${xrpl_cmakedir}"
        COMPONENT development
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/XrplConfigVersion.cmake"
  VERSION "${xrpld_version}"
  COMPATIBILITY SameMajorVersion
)

install(FILES
          "${CMAKE_CURRENT_SOURCE_DIR}/cmake/XrplConfig.cmake"
          "${CMAKE_CURRENT_BINARY_DIR}/XrplConfigVersion.cmake"
        DESTINATION "${xrpl_cmakedir}"
        COMPONENT development
)
