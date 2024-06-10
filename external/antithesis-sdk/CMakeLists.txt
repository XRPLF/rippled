cmake_minimum_required(VERSION 3.25)
project(antithesis-sdk-cpp VERSION $ENV{version} LANGUAGES CXX)

add_library(antithesis-sdk-cpp INTERFACE antithesis_sdk.h)
