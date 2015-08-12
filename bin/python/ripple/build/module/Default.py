from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os

from beast.build.Build import Env, Module, compose, for_tags, not_tags

MODULE = Module(
    files=compose(
        Env.Append(CPPDEFINES=[
            'DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER',
            {'HAVE_USLEEP' : '1'},
            {'SOCI_CXX_C11' : '1'},
        ]),

        for_tags(
            'debug',
            Env.Append(CPPDEFINES=['DEBUG', '_DEBUG']),
        ),

        for_tags(
            'release',
             Env.Append(
                 CPPDEFINES=['NDEBUG'],
                 CCFLAGS=['-O3','-fno-strict-aliasing'],
             ),
        ),

        for_tags(
            'gcc',
            Env.Append(
                CCFLAGS=['-Wno-unused-but-set-variable',
                         '-Wno-unused-local-typedefs'],
            ),

            for_tags(
                'debug',
                Env.Append(
                    CPPDEFINES={'_FORTIFY_SOURCE': 2},
                    CCFLAGS=['-O0'],
                ),
            ),
        ),

        for_tags(
            'clang',
            Env.Append(
                CCFLAGS=['-Wno-redeclared-class-member'],

                CXXFLAGS=[
                    '-Wno-mismatched-tags',
                    '-Wno-deprecated-register',
                    # C and C++: TODO: add '-Wshorten-64-to-32'
                ],
            ),
        ),

        for_tags(
            'osx',
            Env.Replace(CC='clang', CXX='clang++', LINK='clang++'),

            Env.Append(
                CPPDEFINES=[
                    'DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER',
                ],

                CCFLAGS=[
                    '-Wno-deprecated',
                    '-Wno-deprecated-declarations',
                    '-Wno-unused-variable',
                    '-Wno-unused-function',
                ],

                LIBS=['crypto', 'protobuf', 'ssl'],

                FRAMEWORKS=['AppKit', 'Foundation'],
            ),
        ),

        not_tags('osx', 'msvc', Env.Append(LIBS=['rt'])),

        not_tags(
            'msvc',

            Env.Prepend(
                CFLAGS=['-Wall'],
                CXXFLAGS=['-Wall'],
            ),

            Env.Append(
                CCFLAGS=[
                    '-Wno-sign-compare',
                    '-Wno-char-subscripts',
                    '-Wno-format',
                    '-g',
                ],

                LINKFLAGS=['-rdynamic', '-g'],

                CXXFLAGS=['-frtti', '-std=c++11', '-Wno-invalid-offsetof'],

                CPPDEFINES=['_FILE_OFFSET_BITS=64', {'HAVE_USLEEP' : '1'}],
            ),

            for_tags(
                'profile',
                Env.Prepend(
                    CCFLAGS=['-p', '-pg'],
                    LINKFLAGS=['-p', '-pg'],
                ),
            ),
        ),

        for_tags(
            'msvc',

             Env.Append(
                 CCFLAGS=[
                     '/bigobj',           # Increase object file max size
                     '/EHa',              # ExceptionHandling all
                     '/fp:precise',       # Floating point behavior
                     '/Gd',               # __cdecl calling convention
                     '/Gm-',              # Minimal rebuild: disabled
                     '/GR',               # Enable RTTI
                     '/Gy-',              # Function level linking: disabled
                     '/FS',
                     '/MP',               # Multiprocessor compilation
                     '/openmp-',          # pragma omp: disabled
                     '/Zc:forScope',      # Language extension: for scope
                     '/Zi',               # Generate complete debug info
                     '/errorReport:none', # No error reporting to Internet
                     '/nologo',           # Suppress login banner
                     #'/Fd${TARGET}.pdb', # Path: Program Database (.pdb)
                     '/W3',               # Warning level 3
                     '/WX-',              # Disable warnings as errors
                     '/wd"4018"',
                     '/wd"4244"',
                     '/wd"4267"',
                     '/wd"4800"',  # Disable C4800 (int to bool performance)
                 ],

                 CPPDEFINES=[
                     {'_WIN32_WINNT' : '0x6000'},
                     '_SCL_SECURE_NO_WARNINGS',
                     '_CRT_SECURE_NO_WARNINGS',
                     'WIN32_CONSOLE',
                 ],

                 LIBS=[
                     'ssleay32.lib',
                     'libeay32.lib',
                     'Shlwapi.lib',
                     'kernel32.lib',
                     'user32.lib',
                     'gdi32.lib',
                     'winspool.lib',
                     'comdlg32.lib',
                     'advapi32.lib',
                     'shell32.lib',
                     'ole32.lib',
                     'oleaut32.lib',
                     'uuid.lib',
                     'odbc32.lib',
                     'odbccp32.lib',
                 ],

                 LINKFLAGS=[
                     '/DEBUG',
                     '/DYNAMICBASE',
                     '/ERRORREPORT:NONE',
                     #'/INCREMENTAL',
                     '/MACHINE:X64',
                     '/MANIFEST',
                #'''/MANIFESTUAC:"level='asInvoker' uiAccess='false'"''',
                     '/nologo',
                     '/NXCOMPAT',
                     '/SUBSYSTEM:CONSOLE',
                     '/TLBID:1',
                 ],
             ),

            for_tags(
                'debug',

                Env.Append(
                    CCFLAGS=[
                        '/GS',              # Buffers security check: enable
                        '/MTd',             # Language: Multi-threaded Debug CRT
                        '/Od',              # Optimization: Disabled
                        '/RTC1',            # Run-time error checks:
                    ],
                    CPPDEFINES=['_CRTDBG_MAP_ALLOC'],
                 ),
            ),

            for_tags(
                'release',

                Env.Append(
                    CCFLAGS=[
                        '/MT',              # Language: Multi-threaded CRT
                        '/Ox',
                    ],
                ),
            ),
        ),
    ),
)
