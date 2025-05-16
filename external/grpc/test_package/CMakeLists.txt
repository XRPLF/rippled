cmake_minimum_required(VERSION 3.15)
project(test_package LANGUAGES CXX)


find_package(gRPC CONFIG REQUIRED)


add_executable(${PROJECT_NAME} test_package.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_14)  # grpc 1.47.0 requires c++14
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        $<IF:$<TARGET_EXISTS:gRPC::grpc++_unsecure>,gRPC::grpc++_unsecure,gRPC::grpc++>
)

if(CHECK_GRPC_CPP_PLUGIN)
    if(NOT GRPC_CPP_PLUGIN_PROGRAM)
        message(FATAL_ERROR "grpc_cpp_plugin not found")
    endif()

    if(NOT TARGET gRPC::grpc_cpp_plugin)
        message(FATAL_ERROR "grpc_cpp_plugin target not defined, but expected")
    endif()
endif()