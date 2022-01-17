if(resource_report)
    find_library(pfs NAMES pfs)
    if(NOT pfs)
        message("pfs not found. will build")
        add_library(pfs_lib STATIC IMPORTED GLOBAL)
        ExternalProject_Add(pfs_src
            PREFIX ${nih_cache_path}
            GIT_REPOSITORY https://github.com/dtrugman/pfs.git
            GIT_TAG v0.4.5
            CMAKE_ARGS
            -DBUILD_SHARED_LIBS=OFF
            TEST_COMMAND ""
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS <BINARY_DIR>/lib/${ep_lib_prefix}pfs.a
            LOG_BUILD ON
            LOG_CONFIGURE ON
            )

        ExternalProject_Get_Property (pfs_src SOURCE_DIR)
        ExternalProject_Get_Property (pfs_src BINARY_DIR)
        set (pfs_src_SOURCE_DIR "${SOURCE_DIR}")
        file (MAKE_DIRECTORY ${pfs_src_SOURCE_DIR}/include)

        set_target_properties (pfs_lib PROPERTIES
            IMPORTED_LOCATION
            ${BINARY_DIR}/lib/${ep_lib_prefix}pfs.a
            INTERFACE_INCLUDE_DIRECTORIES
            ${SOURCE_DIR}/include)
        add_dependencies(pfs_lib pfs_src)
        file(TO_CMAKE_PATH "${pfs_src_SOURCE_DIR}" pfs_src_SOURCE_DIR)

        target_link_libraries (ripple_libs INTERFACE pfs_lib)
        exclude_if_included (pfs_lib)
    endif()
endif()
