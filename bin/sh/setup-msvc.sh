
# NOTE: must be sourced from a shell so it can export vars

cat << BATCH > ./getenv.bat
CALL %*
ENV
BATCH

while read line ; do
  IFS='"' read x path arg <<<"${line}"
  if [ -f "${path}" ] ; then
    echo "FOUND: $path"
    export VCINSTALLDIR=$(./getenv.bat "${path}" ${arg} | grep "^VCINSTALLDIR=" | sed -E "s/^VCINSTALLDIR=//g")
    if [ "${VCINSTALLDIR}" != "" ] ; then
      echo "USING ${VCINSTALLDIR}"
      export LIB=$(./getenv.bat "${path}" ${arg} | grep "^LIB=" | sed -E "s/^LIB=//g")
      export LIBPATH=$(./getenv.bat "${path}" ${arg} | grep "^LIBPATH=" | sed -E "s/^LIBPATH=//g")
      export INCLUDE=$(./getenv.bat "${path}" ${arg} | grep "^INCLUDE=" | sed -E "s/^INCLUDE=//g")
      ADDPATH=$(./getenv.bat "${path}" ${arg} | grep "^PATH=" | sed -E "s/^PATH=//g")
      export PATH="${ADDPATH}:${PATH}"
      break
    fi
  fi
done <<EOL
"C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Auxiliary/Build/vcvarsall.bat" x86_amd64 -vcvars_ver=14.24
"C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/vcvarsall.bat" x86_amd64 -vcvars_ver=14.24
"C:/Program Files (x86)/Microsoft Visual Studio/2017/BuildTools/VC/Auxiliary/Build/vcvarsall.bat" x86_amd64 -vcvars_ver=14.24
"C:/Program Files (x86)/Microsoft Visual Studio/2017/Community/VC/Auxiliary/Build/vcvarsall.bat" x86_amd64 -vcvars_ver=14.24
"C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Auxiliary/Build/vcvarsall.bat" x86_amd64
"C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/vcvarsall.bat" x86_amd64
"C:/Program Files (x86)/Microsoft Visual Studio/2017/BuildTools/VC/Auxiliary/Build/vcvarsall.bat" x86_amd64
"C:/Program Files (x86)/Microsoft Visual Studio/2017/Community/VC/Auxiliary/Build/vcvarsall.bat" x86_amd64
"C:/Program Files (x86)/Microsoft Visual Studio 15.0/VC/vcvarsall.bat" amd64
"C:/Program Files (x86)/Microsoft Visual Studio 14.0/VC/vcvarsall.bat" amd64
"C:/Program Files (x86)/Microsoft Visual Studio 13.0/VC/vcvarsall.bat" amd64
"C:/Program Files (x86)/Microsoft Visual Studio 12.0/VC/vcvarsall.bat" amd64
EOL
# TODO: update the list above as needed to support newer versions of msvc tools
# MSVC 19.25.28610.4 causes the rocksdb's compilation to fail, for VS2019, we will choose 14.24 VCTools for now
# TODO: Delete lines with -vcars_ver=14.24 once rocksdb becomes compatible with newer compiler version.

rm -f getenv.bat

if [ "${VCINSTALLDIR}" = "" ] ; then
  echo "No compatible visual studio found!"
fi
