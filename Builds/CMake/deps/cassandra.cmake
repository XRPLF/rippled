if(reporting)
    find_library(cassandra NAMES cassandra)
    if(NOT cassandra)

        message("System installed Cassandra cpp driver not found. Will build")

        find_library(zlib NAMES zlib1g-dev zlib-devel zlib z)
        if(NOT zlib)
            message("zlib not found. will build")
            add_library(zlib STATIC IMPORTED GLOBAL)
            ExternalProject_Add(zlib_src
                PREFIX ${nih_cache_path}
                GIT_REPOSITORY https://github.com/madler/zlib.git
                GIT_TAG master
                INSTALL_COMMAND ""
                BUILD_BYPRODUCTS <BINARY_DIR>/${ep_lib_prefix}z.a
                LOG_BUILD TRUE
                LOG_CONFIGURE TRUE
                )


            ExternalProject_Get_Property (zlib_src SOURCE_DIR)
            ExternalProject_Get_Property (zlib_src BINARY_DIR)
            set (zlib_src_SOURCE_DIR "${SOURCE_DIR}")
            file (MAKE_DIRECTORY ${zlib_src_SOURCE_DIR}/include)

            set_target_properties (zlib PROPERTIES
                IMPORTED_LOCATION
                ${BINARY_DIR}/${ep_lib_prefix}z.a
                INTERFACE_INCLUDE_DIRECTORIES
                ${SOURCE_DIR}/include)
            add_dependencies(zlib zlib_src)

            file(TO_CMAKE_PATH "${zlib_src_SOURCE_DIR}" zlib_src_SOURCE_DIR)
        endif()




        find_library(krb5 NAMES krb5-dev libkrb5-dev)

        if(NOT krb5)
            message("krb5 not found. will build")
            add_library(krb5 STATIC IMPORTED GLOBAL)
            ExternalProject_Add(krb5_src
                PREFIX ${nih_cache_path}
                GIT_REPOSITORY https://github.com/krb5/krb5.git
                GIT_TAG master
                UPDATE_COMMAND ""
                CONFIGURE_COMMAND autoreconf src && CFLAGS=-fcommon ./src/configure --enable-static --disable-shared > /dev/null
                BUILD_IN_SOURCE 1
                BUILD_COMMAND make
                INSTALL_COMMAND ""
                BUILD_BYPRODUCTS <SOURCE_DIR>/lib/${ep_lib_prefix}krb5.a
                LOG_BUILD TRUE
                )

            ExternalProject_Get_Property (krb5_src SOURCE_DIR)
            ExternalProject_Get_Property (krb5_src BINARY_DIR)
            set (krb5_src_SOURCE_DIR "${SOURCE_DIR}")
            file (MAKE_DIRECTORY ${krb5_src_SOURCE_DIR}/include)

            set_target_properties (krb5 PROPERTIES
                IMPORTED_LOCATION
                ${BINARY_DIR}/lib/${ep_lib_prefix}krb5.a
                INTERFACE_INCLUDE_DIRECTORIES
                ${SOURCE_DIR}/include)
            add_dependencies(krb5 krb5_src)

            file(TO_CMAKE_PATH "${krb5_src_SOURCE_DIR}" krb5_src_SOURCE_DIR)
        endif()


        find_library(libuv1 NAMES uv1 libuv1 liubuv1-dev libuv1:amd64)


        if(NOT libuv1)
            message("libuv1 not found, will build")
            add_library(libuv1 STATIC IMPORTED GLOBAL)
            ExternalProject_Add(libuv_src
                PREFIX ${nih_cache_path}
                GIT_REPOSITORY https://github.com/libuv/libuv.git
                GIT_TAG v1.x
                INSTALL_COMMAND ""
                BUILD_BYPRODUCTS <BINARY_DIR>/${ep_lib_prefix}uv_a.a
                LOG_BUILD TRUE
                LOG_CONFIGURE TRUE
                )

            ExternalProject_Get_Property (libuv_src SOURCE_DIR)
            ExternalProject_Get_Property (libuv_src BINARY_DIR)
            set (libuv_src_SOURCE_DIR "${SOURCE_DIR}")
            file (MAKE_DIRECTORY ${libuv_src_SOURCE_DIR}/include)

            set_target_properties (libuv1 PROPERTIES
                IMPORTED_LOCATION
                ${BINARY_DIR}/${ep_lib_prefix}uv_a.a
                INTERFACE_INCLUDE_DIRECTORIES
                ${SOURCE_DIR}/include)
            add_dependencies(libuv1 libuv_src)

            file(TO_CMAKE_PATH "${libuv_src_SOURCE_DIR}" libuv_src_SOURCE_DIR)
        endif()

        add_library (cassandra STATIC IMPORTED GLOBAL)
        ExternalProject_Add(cassandra_src
            PREFIX ${nih_cache_path}
            GIT_REPOSITORY https://github.com/datastax/cpp-driver.git
            GIT_TAG master
            CMAKE_ARGS
            -DLIBUV_ROOT_DIR=${BINARY_DIR}
            -DLIBUV_LIBARY=${BINARY_DIR}/libuv_a.a
            -DLIBUV_INCLUDE_DIR=${SOURCE_DIR}/include
            -DCASS_BUILD_STATIC=ON
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS <BINARY_DIR>/${ep_lib_prefix}cassandra_static.a
            LOG_BUILD TRUE
            LOG_CONFIGURE TRUE
            )

        ExternalProject_Get_Property (cassandra_src SOURCE_DIR)
        ExternalProject_Get_Property (cassandra_src BINARY_DIR)
        set (cassandra_src_SOURCE_DIR "${SOURCE_DIR}")
        file (MAKE_DIRECTORY ${cassandra_src_SOURCE_DIR}/include)

        set_target_properties (cassandra PROPERTIES
            IMPORTED_LOCATION
            ${BINARY_DIR}/${ep_lib_prefix}cassandra_static.a
            INTERFACE_INCLUDE_DIRECTORIES
            ${SOURCE_DIR}/include)
        add_dependencies(cassandra cassandra_src)

        if(NOT libuv1)
            ExternalProject_Add_StepDependencies(cassandra_src build libuv1)
            target_link_libraries(cassandra INTERFACE libuv1)
        else()
            target_link_libraries(cassandra INTERFACE ${libuv1})
        endif()
        if(NOT krb5)

            ExternalProject_Add_StepDependencies(cassandra_src build krb5)
            target_link_libraries(cassandra INTERFACE krb5)
        else()
            target_link_libraries(cassandra INTERFACE ${krb5})
        endif()

        if(NOT zlib)
            ExternalProject_Add_StepDependencies(cassandra_src build zlib)
            target_link_libraries(cassandra INTERFACE zlib)
        else()
            target_link_libraries(cassandra INTERFACE ${zlib})
        endif()

        file(TO_CMAKE_PATH "${cassandra_src_SOURCE_DIR}" cassandra_src_SOURCE_DIR)
        target_link_libraries(ripple_libs INTERFACE cassandra)
    else()
        message("Found system installed cassandra cpp driver")

        find_path(cassandra_includes NAMES cassandra.h REQUIRED)
        target_link_libraries (ripple_libs INTERFACE ${cassandra})
        target_include_directories(ripple_libs INTERFACE ${cassandra_includes})
    endif()

    exclude_if_included (cassandra)
endif()
