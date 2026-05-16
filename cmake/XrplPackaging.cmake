#[===================================================================[
   Linux packaging support: 'package' target.

   The packaging script (package/build_pkg.sh) installs to FHS-standard
   paths (/usr/bin, /etc/xrpld, etc.) regardless of CMAKE_INSTALL_PREFIX,
   so no prefix guard is needed here.
#]===================================================================]
if(NOT is_linux)
    message(STATUS "Packaging not supported on non-Linux hosts")
    return()
endif()

if(NOT DEFINED pkg_release)
    set(pkg_release 1)
endif()

find_program(RPMBUILD_EXECUTABLE rpmbuild)
find_program(DPKG_BUILDPACKAGE_EXECUTABLE dpkg-buildpackage)

if(NOT (RPMBUILD_EXECUTABLE OR DPKG_BUILDPACKAGE_EXECUTABLE))
    message(
        STATUS
        "Neither rpmbuild nor dpkg-buildpackage found; 'package' target not available"
    )
    return()
endif()

set(package_env
    SRC_DIR=${CMAKE_SOURCE_DIR}
    BUILD_DIR=${CMAKE_BINARY_DIR}
    PKG_VERSION=${xrpld_version}
    PKG_RELEASE=${pkg_release}
)

add_custom_target(
    package
    COMMAND
        ${CMAKE_COMMAND} -E env ${package_env}
        ${CMAKE_SOURCE_DIR}/package/build_pkg.sh
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS xrpld
    COMMENT "Building Linux package (deb/rpm inferred from host tooling)"
    VERBATIM
)
