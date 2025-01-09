set(CMAKE_PROJECT_HOMEPAGE_URL "https://github.com/XRPLF/rippled.git")
set(CPACK_PACKAGE_VENDOR "Ripple Labs")
set(CPACK_PACKAGE_CONTACT "support@ripple.com") # TODO: Does this really go anywhere?
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "XRPL daemon")
# set(CPACK_PACKAGE_DESCRIPTION_FILE ${CMAKE_CURRENT_LIST_DIR}/pkg_description.txt)
# set(CPACK_RESOURCE_FILE_README README.md)
# CPACK_RESOURCE_FILE_WELCOME # Who cares?

set(CPACK_PACKAGE_DIRECTORY "packages")
# TODO: The only things that need to be in opt are the scripts and binaries
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/ripple")
### TODO: Install the license
## This probably won't work for the license
# set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
# /usr/share/licenses on linux
## This variable typically points to the share subdirectory of the installation prefix.
# By default CMAKE_INSTALL_DATADIR is set to ${CMAKE_INSTALL_PREFIX}/share
# install(FILES LICENSE.md $DATAROOTDIR)
# install(FILES LICENSE.md $CMAKE_INSTALL_DATADIR)

set(CPACK_GENERATOR "DEB;RPM")

# Don't care about any of these # TODO: Confirm these aren't redundant
set(CPACK_SOURCE_TBZ2 OFF)
set(CPACK_SOURCE_TGZ OFF)
set(CPACK_SOURCE_TXZ OFF)
set(CPACK_SOURCE_TZ OFF)
set(CPACK_SOURCE_ZIP OFF)

# set(CPACK_STRIP_FILES ON) # Files cannot be stripped for debug packages to be produced.
set(CPACK_BUILD_SOURCE_DIRS "${CMAKE_SOURCE_DIR}") # Relevant for the debug packages.

# Set package version in case we need to repackage a same binary but with different configs or scripts or something else
if(NOT DEFINED PKG_REL_VERSION)
    set(PKG_REL_VERSION 1)
endif()

set(CPACK_PACKAGE_VERSION "${rippled_version}")
set(CPACK_PACKAGE_NAME "rippled") # Defaults to the project name which is now `xrpl`.

include(common/package_files)
include(deb/deb-cpack-config)
include(rpm/rpm-cpack-config)

set(CPACK_GENERATE_SOURCE_PACKAGE OFF) # TODO: Make sure this doesn't mess up the debug packages
include(CPack)
