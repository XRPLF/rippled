#!/usr/bin/env bash

cd ..

# no extra options
scons clang.debug -j4 && \
./build/clang.debug/rippled -u && \
rm -r build && \
scons clang.debug.nounity -j4 && \
./build/clang.debug.nounity/rippled -u && \
rm -r build && \
scons clang.coverage -j4 && \
./build/clang.coverage/rippled -u && \
rm -r build && \
scons clang.coverage.nounity -j4 && \
./build/clang.coverage.nounity/rippled -u && \
rm -r build && \
scons clang.release -j4 && \
./build/clang.release/rippled -u && \
rm -r build && \
scons clang.release.nounity -j4 && \
./build/clang.release.nounity/rippled -u && \
rm -r build && \
scons clang.profile -j4 && \
./build/clang.profile/rippled -u && \
rm -r build && \
scons clang.profile.nounity -j4 && \
./build/clang.profile.nounity/rippled -u && \
rm -r build && \

scons gcc.debug -j4 && \
./build/gcc.debug/rippled -u && \
rm -r build && \
scons gcc.debug.nounity -j4 && \
./build/gcc.debug.nounity/rippled -u && \
rm -r build && \
scons gcc.coverage -j4 && \
./build/gcc.coverage/rippled -u && \
rm -r build && \
scons gcc.coverage.nounity -j4 && \
./build/gcc.coverage.nounity/rippled -u && \
rm -r build && \
scons gcc.release -j4 && \
./build/gcc.release/rippled -u && \
rm -r build && \
scons gcc.release.nounity -j4 && \
./build/gcc.release.nounity/rippled -u && \
rm -r build && \
scons gcc.profile -j4 && \
./build/gcc.profile/rippled -u && \
rm -r build && \
scons gcc.profile.nounity -j4 && \
./build/gcc.profile.nounity/rippled -u && \
rm -r build && \

# --ninja
# Also builds with Ninja
scons --ninja clang.debug -j4 && \
ninja && \
./build/clang.debug/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja clang.debug.nounity -j4 && \
ninja && \
./build/clang.debug.nounity/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja clang.coverage -j4 && \
ninja && \
./build/clang.coverage/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja clang.coverage.nounity -j4 && \
ninja && \
./build/clang.coverage.nounity/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja clang.release -j4 && \
ninja && \
./build/clang.release/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja clang.release.nounity -j4 && \
ninja && \
./build/clang.release.nounity/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja clang.profile -j4 && \
ninja && \
./build/clang.profile/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja clang.profile.nounity -j4 && \
ninja && \
./build/clang.profile.nounity/rippled -u && \
rm build.ninja && \
rm -r build && \

scons --ninja gcc.debug -j4 && \
ninja && \
./build/gcc.debug/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja gcc.debug.nounity -j4 && \
ninja && \
./build/gcc.debug.nounity/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja gcc.coverage -j4 && \
ninja && \
./build/gcc.coverage/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja gcc.coverage.nounity -j4 && \
ninja && \
./build/gcc.coverage.nounity/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja gcc.release -j4 && \
ninja && \
./build/gcc.release/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja gcc.release.nounity -j4 && \
ninja && \
./build/gcc.release.nounity/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja gcc.profile -j4 && \
ninja && \
./build/gcc.profile/rippled -u && \
rm build.ninja && \
rm -r build && \
scons --ninja gcc.profile.nounity -j4 && \
ninja && \
./build/gcc.profile.nounity/rippled -u && \
rm build.ninja && \
rm -r build && \

# --static
scons --static clang.debug -j4 && \
./build/clang.debug/rippled -u && \
rm -r build && \
scons --static clang.debug.nounity -j4 && \
./build/clang.debug.nounity/rippled -u && \
rm -r build && \
scons --static clang.coverage -j4 && \
./build/clang.coverage/rippled -u && \
rm -r build && \
scons --static clang.coverage.nounity -j4 && \
./build/clang.coverage.nounity/rippled -u && \
rm -r build && \
scons --static clang.release -j4 && \
./build/clang.release/rippled -u && \
rm -r build && \
scons --static clang.release.nounity -j4 && \
./build/clang.release.nounity/rippled -u && \
rm -r build && \
scons --static clang.profile -j4 && \
./build/clang.profile/rippled -u && \
rm -r build && \
scons --static clang.profile.nounity -j4 && \
./build/clang.profile.nounity/rippled -u && \
rm -r build && \

scons --static gcc.debug -j4 && \
./build/gcc.debug/rippled -u && \
rm -r build && \
scons --static gcc.debug.nounity -j4 && \
./build/gcc.debug.nounity/rippled -u && \
rm -r build && \
scons --static gcc.coverage -j4 && \
./build/gcc.coverage/rippled -u && \
rm -r build && \
scons --static gcc.coverage.nounity -j4 && \
./build/gcc.coverage.nounity/rippled -u && \
rm -r build && \
scons --static gcc.release -j4 && \
./build/gcc.release/rippled -u && \
rm -r build && \
scons --static gcc.release.nounity -j4 && \
./build/gcc.release.nounity/rippled -u && \
rm -r build && \
scons --static gcc.profile -j4 && \
./build/gcc.profile/rippled -u && \
rm -r build && \
scons --static gcc.profile.nounity -j4 && \
./build/gcc.profile.nounity/rippled -u && \
rm -r build && \

# --assert
scons --assert clang.debug -j4 && \
./build/clang.debug/rippled -u && \
rm -r build && \
scons --assert clang.debug.nounity -j4 && \
./build/clang.debug.nounity/rippled -u && \
rm -r build && \
scons --assert clang.coverage -j4 && \
./build/clang.coverage/rippled -u && \
rm -r build && \
scons --assert clang.coverage.nounity -j4 && \
./build/clang.coverage.nounity/rippled -u && \
rm -r build && \
scons --assert clang.release -j4 && \
./build/clang.release/rippled -u && \
rm -r build && \
scons --assert clang.release.nounity -j4 && \
./build/clang.release.nounity/rippled -u && \
rm -r build && \
scons --assert clang.profile -j4 && \
./build/clang.profile/rippled -u && \
rm -r build && \
scons --assert clang.profile.nounity -j4 && \
./build/clang.profile.nounity/rippled -u && \
rm -r build && \

scons --assert gcc.debug -j4 && \
./build/gcc.debug/rippled -u && \
rm -r build && \
scons --assert gcc.debug.nounity -j4 && \
./build/gcc.debug.nounity/rippled -u && \
rm -r build && \
scons --assert gcc.coverage -j4 && \
./build/gcc.coverage/rippled -u && \
rm -r build && \
scons --assert gcc.coverage.nounity -j4 && \
./build/gcc.coverage.nounity/rippled -u && \
rm -r build && \
scons --assert gcc.release -j4 && \
./build/gcc.release/rippled -u && \
rm -r build && \
scons --assert gcc.release.nounity -j4 && \
./build/gcc.release.nounity/rippled -u && \
rm -r build && \
scons --assert gcc.profile -j4 && \
./build/gcc.profile/rippled -u && \
rm -r build && \
scons --assert gcc.profile.nounity -j4 && \
./build/gcc.profile.nounity/rippled -u && \
rm -r build && \

# --sanitize=address
scons --sanitize=address clang.debug -j4 && \
./build/clang.debug/rippled -u && \
rm -r build && \
scons --sanitize=address clang.debug.nounity -j4 && \
./build/clang.debug.nounity/rippled -u && \
rm -r build && \
scons --sanitize=address clang.coverage -j4 && \
./build/clang.coverage/rippled -u && \
rm -r build && \
scons --sanitize=address clang.coverage.nounity -j4 && \
./build/clang.coverage.nounity/rippled -u && \
rm -r build && \
scons --sanitize=address clang.release -j4 && \
./build/clang.release/rippled -u && \
rm -r build && \
scons --sanitize=address clang.release.nounity -j4 && \
./build/clang.release.nounity/rippled -u && \
rm -r build && \
scons --sanitize=address clang.profile -j4 && \
./build/clang.profile/rippled -u && \
rm -r build && \
scons --sanitize=address clang.profile.nounity -j4 && \
./build/clang.profile.nounity/rippled -u && \
rm -r build && \

scons --sanitize=address gcc.debug -j4 && \
./build/gcc.debug/rippled -u && \
rm -r build && \
scons --sanitize=address gcc.debug.nounity -j4 && \
./build/gcc.debug.nounity/rippled -u && \
rm -r build && \
scons --sanitize=address gcc.coverage -j4 && \
./build/gcc.coverage/rippled -u && \
rm -r build && \
scons --sanitize=address gcc.coverage.nounity -j4 && \
./build/gcc.coverage.nounity/rippled -u && \
rm -r build && \
scons --sanitize=address gcc.release -j4 && \
./build/gcc.release/rippled -u && \
rm -r build && \
scons --sanitize=address gcc.release.nounity -j4 && \
./build/gcc.release.nounity/rippled -u && \
rm -r build && \
scons --sanitize=address gcc.profile -j4 && \
./build/gcc.profile/rippled -u && \
rm -r build && \
scons --sanitize=address gcc.profile.nounity -j4 && \
./build/gcc.profile.nounity/rippled -u && \
rm -r build && \

# --sanitize=thread
scons --sanitize=thread clang.debug -j4 && \
./build/clang.debug/rippled -u && \
rm -r build && \
scons --sanitize=thread clang.debug.nounity -j4 && \
./build/clang.debug.nounity/rippled -u && \
rm -r build && \
scons --sanitize=thread clang.coverage -j4 && \
./build/clang.coverage/rippled -u && \
rm -r build && \
scons --sanitize=thread clang.coverage.nounity -j4 && \
./build/clang.coverage.nounity/rippled -u && \
rm -r build && \
scons --sanitize=thread clang.release -j4 && \
./build/clang.release/rippled -u && \
rm -r build && \
scons --sanitize=thread clang.release.nounity -j4 && \
./build/clang.release.nounity/rippled -u && \
rm -r build && \
scons --sanitize=thread clang.profile -j4 && \
./build/clang.profile/rippled -u && \
rm -r build && \
scons --sanitize=thread clang.profile.nounity -j4 && \
./build/clang.profile.nounity/rippled -u && \
rm -r build && \

scons --sanitize=thread gcc.debug -j4 && \
./build/gcc.debug/rippled -u && \
rm -r build && \
scons --sanitize=thread gcc.debug.nounity -j4 && \
./build/gcc.debug.nounity/rippled -u && \
rm -r build && \
scons --sanitize=thread gcc.coverage -j4 && \
./build/gcc.coverage/rippled -u && \
rm -r build && \
scons --sanitize=thread gcc.coverage.nounity -j4 && \
./build/gcc.coverage.nounity/rippled -u && \
rm -r build && \
scons --sanitize=thread gcc.release -j4 && \
./build/gcc.release/rippled -u && \
rm -r build && \
scons --sanitize=thread gcc.release.nounity -j4 && \
./build/gcc.release.nounity/rippled -u && \
rm -r build && \
scons --sanitize=thread gcc.profile -j4 && \
./build/gcc.profile/rippled -u && \
rm -r build && \
scons --sanitize=thread gcc.profile.nounity -j4 && \
./build/gcc.profile.nounity/rippled -u && \
rm -r build && \

scons vcxproj && \

scons count
