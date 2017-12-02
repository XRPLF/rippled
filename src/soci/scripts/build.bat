@echo off
rem Runs CMake to configure SOCI for Visual Studio 2017.
rem Runs MSBuild to build the generated solution.
rem
rem Usage:
rem 1. Copy build.bat to build.locale.bat (git ignored file)
rem 2. Make your adjustments in the CONFIGURATION section below
rem 3. Run build.local.bat 32|64
rem 4. Optionally, run devenv.exe SOCI{32|64}.sln from command line

rem ### CONFIGURATION #####################################
rem ### Connection strings for tests (alternatively, use command line-c option)
rem ### For example, SQL Server LocalDB instance, MySQL and PostgreSQL on the Vagrant VM.
set TEST_CONNSTR_MSSQL=""
set TEST_CONNSTR_MYSQL=""
set TEST_CONNSTR_PGSQL=""
setlocal
set BOOST_ROOT=C:/local/boost_1_59_0
rem #######################################################

set U=""
if /I "%2"=="U"  set U=U
if [%1]==[32] goto :32
if [%1]==[64] goto :64
goto :Usage

:32
set P=32
set MSBUILDP=Win32
set GENERATOR="Visual Studio 15 2017"
goto :Build

:64
set P=64
set MSBUILDP=x64
set GENERATOR="Visual Studio 15 2017 Win64"
goto :Build

:Build
set BUILDDIR=_build%P%%U%
mkdir %BUILDDIR%
pushd %BUILDDIR%
cmake.exe ^
    -G %GENERATOR% ^
    -DWITH_BOOST=ON ^
    -DWITH_DB2=ON ^
    -DWITH_FIREBIRD=ON ^
    -DWITH_MYSQL=ON ^
    -DWITH_ODBC=ON ^
    -DWITH_ORACLE=ON ^
    -DWITH_POSTGRESQL=ON ^
    -DWITH_SQLITE3=ON ^
    -DSOCI_EMPTY=ON ^
    -DSOCI_EMPTY_TEST_CONNSTR="" ^
    -DSOCI_DB2=ON ^
    -DSOCI_DB2_TEST_CONNSTR="" ^
    -DSOCI_FIREBIRD=ON ^
    -DSOCI_FIREBIRD_TEST_CONNSTR="" ^
    -DSOCI_MYSQL=ON ^
    -DSOCI_MYSQL_TEST_CONNSTR="" ^
    -DSOCI_ODBC=ON ^
    -DSOCI_ODBC_TEST_MYSQL_CONNSTR="" ^
    -DSOCI_ODBC_TEST_POSTGRESQL_CONNSTR="" ^
    -DSOCI_ORACLE=ON ^
    -DSOCI_ORACLE_TEST_CONNSTR="" ^
    -DSOCI_POSTGRESQL=ON ^
    -DSOCI_POSTGRESQL_TEST_CONNSTR="" ^
    -DSOCI_SQLITE3=ON ^
    -DSOCI_SQLITE3_TEST_CONNSTR="" ^
    ..
move SOCI.sln SOCI%P%%U%.sln
rem msbuild.exe SOCI%P%%U%.sln /p:Configuration=Release /p:Platform=%MSBUILDP%
popd
goto :EOF

:Usage
@echo build.bat
@echo Usage: build.bat [32 or 64]
exit /B 1
