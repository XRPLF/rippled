# Copyright (c) 2012, Robert Escriva
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of this project nor the names of its contributors may
#       be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# This macro enables many compiler warnings for C++ that generally catch bugs in
# code.  It offers the "--enable-wanal-cxxflags" option which defaults to "no".

AC_DEFUN([ANAL_WARNINGS_CXX],
    [WANAL_CXXFLAGS=""
    AC_ARG_ENABLE([wanal-cxxflags], [AS_HELP_STRING([--enable-wanal-cxxflags],
              [enable many warnings @<:@default: no@:>@])],
              [wanal_cxxflags=${enableval}], [wanal_cxxflags=no])
    if test x"${wanal_cxxflags}" = xyes; then
        AX_CHECK_COMPILE_FLAG([-pedantic],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -pedantic"],,)
        AX_CHECK_COMPILE_FLAG([-Wall],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wall"],,)
        AX_CHECK_COMPILE_FLAG([-Wextra],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wextra"],,)
        AX_CHECK_COMPILE_FLAG([-Wabi],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wabi"],,)
        AX_CHECK_COMPILE_FLAG([-Waddress],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Waddress"],,)
        #AX_CHECK_COMPILE_FLAG([-Waggregate-return],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Waggregate-return"],,)
        AX_CHECK_COMPILE_FLAG([-Warray-bounds],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Warray-bounds"],,)
        AX_CHECK_COMPILE_FLAG([-Wc++0x-compat],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wc++0x-compat"],,)
        AX_CHECK_COMPILE_FLAG([-Wcast-align],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wcast-align"],,)
        AX_CHECK_COMPILE_FLAG([-Wcast-qual],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wcast-qual"],,)
        AX_CHECK_COMPILE_FLAG([-Wchar-subscripts],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wchar-subscripts"],,)
        AX_CHECK_COMPILE_FLAG([-Wclobbered],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wclobbered"],,)
        AX_CHECK_COMPILE_FLAG([-Wcomment],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wcomment"],,)
        AX_CHECK_COMPILE_FLAG([-Wconversion],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wconversion"],,)
        AX_CHECK_COMPILE_FLAG([-Wctor-dtor-privacy],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wctor-dtor-privacy"],,)
        AX_CHECK_COMPILE_FLAG([-Wdisabled-optimization],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wdisabled-optimization"],,)
        AX_CHECK_COMPILE_FLAG([-Weffc++],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Weffc++"],,)
        AX_CHECK_COMPILE_FLAG([-Wempty-body],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wempty-body"],,)
        AX_CHECK_COMPILE_FLAG([-Wenum-compare],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wenum-compare"],,)
        AX_CHECK_COMPILE_FLAG([-Wfloat-equal],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wfloat-equal"],,)
        AX_CHECK_COMPILE_FLAG([-Wformat],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wformat"],,)
        AX_CHECK_COMPILE_FLAG([-Wformat=2],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wformat=2"],,)
        AX_CHECK_COMPILE_FLAG([-Wformat-nonliteral],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wformat-nonliteral"],,)
        AX_CHECK_COMPILE_FLAG([-Wformat-security],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wformat-security"],,)
        AX_CHECK_COMPILE_FLAG([-Wformat-y2k],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wformat-y2k"],,)
        AX_CHECK_COMPILE_FLAG([-Wframe-larger-than=8192],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wframe-larger-than=8192"],,)
        AX_CHECK_COMPILE_FLAG([-Wignored-qualifiers],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wignored-qualifiers"],,)
        AX_CHECK_COMPILE_FLAG([-Wimplicit],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wimplicit"],,)
        AX_CHECK_COMPILE_FLAG([-Winit-self],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Winit-self"],,)
        AX_CHECK_COMPILE_FLAG([-Winline],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Winline"],,)
        AX_CHECK_COMPILE_FLAG([-Wlarger-than=4096],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wlarger-than=4096"],,)
        AX_CHECK_COMPILE_FLAG([-Wlogical-op],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wlogical-op"],,)
        AX_CHECK_COMPILE_FLAG([-Wmain],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wmain"],,)
        AX_CHECK_COMPILE_FLAG([-Wmissing-braces],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wmissing-braces"],,)
        AX_CHECK_COMPILE_FLAG([-Wmissing-declarations],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wmissing-declarations"],,)
        AX_CHECK_COMPILE_FLAG([-Wmissing-field-initializers],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wmissing-field-initializers"],,)
        AX_CHECK_COMPILE_FLAG([-Wmissing-format-attribute],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wmissing-format-attribute"],,)
        AX_CHECK_COMPILE_FLAG([-Wmissing-include-dirs],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wmissing-include-dirs"],,)
        AX_CHECK_COMPILE_FLAG([-Wmissing-noreturn],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wmissing-noreturn"],,)
        AX_CHECK_COMPILE_FLAG([-Wnon-virtual-dtor],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wnon-virtual-dtor"],,)
        AX_CHECK_COMPILE_FLAG([-Wold-style-cast],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wold-style-cast"],,)
        AX_CHECK_COMPILE_FLAG([-Woverlength-strings],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Woverlength-strings"],,)
        AX_CHECK_COMPILE_FLAG([-Woverloaded-virtual],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Woverloaded-virtual"],,)
        AX_CHECK_COMPILE_FLAG([-Wpacked],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wpacked"],,)
        AX_CHECK_COMPILE_FLAG([-Wpacked-bitfield-compat],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wpacked-bitfield-compat"],,)
        AX_CHECK_COMPILE_FLAG([-Wpadded],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wpadded"],,)
        AX_CHECK_COMPILE_FLAG([-Wparentheses],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wparentheses"],,)
        AX_CHECK_COMPILE_FLAG([-Wpointer-arith],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wpointer-arith"],,)
        AX_CHECK_COMPILE_FLAG([-Wredundant-decls],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wredundant-decls"],,)
        AX_CHECK_COMPILE_FLAG([-Wreorder],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wreorder"],,)
        AX_CHECK_COMPILE_FLAG([-Wreturn-type],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wreturn-type"],,)
        AX_CHECK_COMPILE_FLAG([-Wsequence-point],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wsequence-point"],,)
        AX_CHECK_COMPILE_FLAG([-Wshadow],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wshadow"],,)
        AX_CHECK_COMPILE_FLAG([-Wsign-compare],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wsign-compare"],,)
        AX_CHECK_COMPILE_FLAG([-Wsign-conversion],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wsign-conversion"],,)
        AX_CHECK_COMPILE_FLAG([-Wsign-promo],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wsign-promo"],,)
        AX_CHECK_COMPILE_FLAG([-Wstack-protector],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wstack-protector"],,)
        AX_CHECK_COMPILE_FLAG([-Wstrict-aliasing],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wstrict-aliasing"],,)
        AX_CHECK_COMPILE_FLAG([-Wstrict-aliasing=3],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wstrict-aliasing=3"],,)
        AX_CHECK_COMPILE_FLAG([-Wstrict-null-sentinel],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wstrict-null-sentinel"],,)
        AX_CHECK_COMPILE_FLAG([-Wstrict-overflow],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wstrict-overflow"],,)
        AX_CHECK_COMPILE_FLAG([-Wstrict-overflow=4],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wstrict-overflow=4"],,)
        AX_CHECK_COMPILE_FLAG([-Wswitch],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wswitch"],,)
        AX_CHECK_COMPILE_FLAG([-Wswitch-default],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wswitch-default"],,)
        AX_CHECK_COMPILE_FLAG([-Wswitch-enum],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wswitch-enum"],,)
        AX_CHECK_COMPILE_FLAG([-Wtrigraphs],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wtrigraphs"],,)
        AX_CHECK_COMPILE_FLAG([-Wtype-limits],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wtype-limits"],,)
        AX_CHECK_COMPILE_FLAG([-Wundef],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wundef"],,)
        AX_CHECK_COMPILE_FLAG([-Wuninitialized],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wuninitialized"],,)
        #AX_CHECK_COMPILE_FLAG([-Wunreachable-code],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wunreachable-code"],,)
        AX_CHECK_COMPILE_FLAG([-Wunsafe-loop-optimizations],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wunsafe-loop-optimizations"],,)
        AX_CHECK_COMPILE_FLAG([-Wunused],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wunused"],,)
        AX_CHECK_COMPILE_FLAG([-Wunused-function],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wunused-function"],,)
        AX_CHECK_COMPILE_FLAG([-Wunused-label],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wunused-label"],,)
        AX_CHECK_COMPILE_FLAG([-Wunused-parameter],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wunused-parameter"],,)
        AX_CHECK_COMPILE_FLAG([-Wunused-value],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wunused-value"],,)
        AX_CHECK_COMPILE_FLAG([-Wunused-variable],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wunused-variable"],,)
        AX_CHECK_COMPILE_FLAG([-Wvolatile-register-var],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wvolatile-register-var"],,)
        AX_CHECK_COMPILE_FLAG([-Wwrite-strings],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wwrite-strings"],,)
        AX_CHECK_COMPILE_FLAG([-Wno-long-long],[WANAL_CXXFLAGS="${WANAL_CXXFLAGS} -Wno-long-long"],,)
    fi
    AC_SUBST([WANAL_CXXFLAGS], [${WANAL_CXXFLAGS}])
])
