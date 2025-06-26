# Multi-stage Dockerfile for building PostFiat on Ubuntu 24.04
FROM ubuntu:24.04 AS builder

# Avoid interactive prompts during build
ENV DEBIAN_FRONTEND=noninteractive

# Set working directory
WORKDIR /postfiat

# Install build dependencies
RUN apt update

RUN apt install --yes curl git libssl-dev pipx python3.12-dev python3-pip make g++-11 libprotobuf-dev protobuf-compiler

RUN curl --location --remote-name \
  "https://github.com/Kitware/CMake/releases/download/v3.25.1/cmake-3.25.1.tar.gz"
RUN tar -xzf cmake-3.25.1.tar.gz
RUN rm cmake-3.25.1.tar.gz
WORKDIR /postfiat/cmake-3.25.1
RUN ./bootstrap --parallel=$(nproc)
RUN make --jobs $(nproc)
RUN make install
WORKDIR /postfiat

RUN pipx install 'conan<2'
RUN pipx ensurepath
RUN export PATH=$PATH:/root/.local/bin
ENV PATH="/root/.local/bin:$PATH"

RUN conan profile new default --detect
RUN conan profile update settings.compiler.cppstd=20 default
RUN conan config set general.revisions_enabled=1
RUN conan profile update settings.compiler.libcxx=libstdc++11 default
RUN conan profile update 'conf.tools.build:cxxflags+=["-DBOOST_BEAST_USE_STD_STRING_VIEW"]' default
RUN conan profile update 'env.CXXFLAGS="-DBOOST_BEAST_USE_STD_STRING_VIEW"' default

# Copy source code
COPY . .

#Build
WORKDIR /postfiat/.build

RUN conan install .. --output-folder . --build missing --settings build_type=Debug
RUN cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -Dxrpld=ON -Dtests=ON ..
RUN cmake --build . -j $(nproc)
RUN ./postfiatd --unittest
