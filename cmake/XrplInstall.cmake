#[===================================================================[
   install stuff
#]===================================================================]

include(GNUInstallDirs)

if(is_root_project AND TARGET xrpld)
    install(
        TARGETS xrpld
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT runtime
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/cfg/xrpld-example.cfg"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/xrpld"
        RENAME xrpld.cfg
        COMPONENT runtime
    )

    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/cfg/validators-example.txt"
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/xrpld"
        RENAME validators.txt
        COMPONENT runtime
    )
endif()

install(
    TARGETS xrpl.libpb xrpl.libxrpl
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT development
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT development
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT development
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT development
)
