@echo off
setlocal

REM Copyright Epic Games, Inc. All Rights Reserved.

if [%1]==[] goto usage

set OPENVDB_VERSION=%1

rem Set as VS2015 for backwards compatibility even though VS2019 is used
rem when building.
set COMPILER_VERSION_NAME=VS2015
set TOOLCHAIN_NAME=vc14
set ARCH_NAME=x64

set BUILD_SCRIPT_LOCATION=%~dp0
set UE_THIRD_PARTY_LOCATION=%BUILD_SCRIPT_LOCATION%..\..\..

REM Dependency - ZLib
set ZLIB_LOCATION=%UE_THIRD_PARTY_LOCATION%\zlib\v1.2.8
set ZLIB_INCLUDE_LOCATION=%ZLIB_LOCATION%\include\Win64\%COMPILER_VERSION_NAME%
set ZLIB_LIBRARY_LOCATION_RELEASE=%ZLIB_LOCATION%\lib\Win64\%COMPILER_VERSION_NAME%\Release\zlibstatic.lib
set ZLIB_LIBRARY_LOCATION_DEBUG=%ZLIB_LOCATION%\lib\Win64\%COMPILER_VERSION_NAME%\Debug\zlibstatic.lib

REM Dependency - Intel TBB
set TBB_LOCATION=%UE_THIRD_PARTY_LOCATION%\Intel\TBB\IntelTBB-2019u8
set TBB_INCLUDE_LOCATION=%TBB_LOCATION%\include
set TBB_LIB_LOCATION=%TBB_LOCATION%\lib\Win64\%TOOLCHAIN_NAME%

REM Dependency - Blosc
set BLOSC_LOCATION=%UE_THIRD_PARTY_LOCATION%\Blosc\Deploy\c-blosc-1.21.0
set BLOSC_INCLUDE_LOCATION=%BLOSC_LOCATION%\include
set BLOSC_LIB_LOCATION=%BLOSC_LOCATION%\%COMPILER_VERSION_NAME%\%ARCH_NAME%\lib
set BLOSC_LIBRARY_LOCATION_RELEASE=%BLOSC_LIB_LOCATION%\libblosc.lib
set BLOSC_LIBRARY_LOCATION_DEBUG=%BLOSC_LIB_LOCATION%\libblosc_d.lib

REM Dependency - Boost
set BOOST_LOCATION=%UE_THIRD_PARTY_LOCATION%\Boost\boost-1_80_0
set BOOST_INCLUDE_LOCATION=%BOOST_LOCATION%\include
set BOOST_LIB_LOCATION=%BOOST_LOCATION%\lib\Win64

set UE_MODULE_LOCATION=%BUILD_SCRIPT_LOCATION%..\..

set SOURCE_LOCATION=%UE_MODULE_LOCATION%\openvdb-%OPENVDB_VERSION%

set BUILD_LOCATION=%UE_MODULE_LOCATION%\Intermediate

rem Specify all of the include/bin/lib directory variables so that CMake can
rem compute relative paths correctly for the imported targets.
set INSTALL_INCLUDEDIR=include
set INSTALL_BIN_DIR=%COMPILER_VERSION_NAME%\%ARCH_NAME%\bin
set INSTALL_LIB_DIR=%COMPILER_VERSION_NAME%\%ARCH_NAME%\lib

set INSTALL_LOCATION=%UE_MODULE_LOCATION%\Deploy\openvdb-%OPENVDB_VERSION%
set INSTALL_INCLUDE_LOCATION=%INSTALL_LOCATION%\%INSTALL_INCLUDEDIR%
set INSTALL_WIN_LOCATION=%INSTALL_LOCATION%\%COMPILER_VERSION_NAME%

if exist %BUILD_LOCATION% (
    rmdir %BUILD_LOCATION% /S /Q)
if exist %INSTALL_INCLUDE_LOCATION% (
    rmdir %INSTALL_INCLUDE_LOCATION% /S /Q)
if exist %INSTALL_WIN_LOCATION% (
    rmdir %INSTALL_WIN_LOCATION% /S /Q)

mkdir %BUILD_LOCATION%
pushd %BUILD_LOCATION%

echo Configuring build for OpenVDB version %OPENVDB_VERSION%...
cmake -G "Visual Studio 16 2019" %SOURCE_LOCATION%^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_LOCATION%"^
    -DCMAKE_INSTALL_INCLUDEDIR="%INSTALL_INCLUDEDIR%"^
    -DCMAKE_INSTALL_BINDIR="%INSTALL_BIN_DIR%"^
    -DCMAKE_INSTALL_LIBDIR="%INSTALL_LIB_DIR%"^
    -DZLIB_INCLUDE_DIR="%ZLIB_INCLUDE_LOCATION%"^
    -DZLIB_LIBRARY_RELEASE="%ZLIB_LIBRARY_LOCATION_RELEASE%"^
    -DZLIB_LIBRARY_DEBUG="%ZLIB_LIBRARY_LOCATION_DEBUG%"^
    -DTBB_INCLUDEDIR="%TBB_INCLUDE_LOCATION%"^
    -DTBB_LIBRARYDIR="%TBB_LIB_LOCATION%"^
    -DBLOSC_INCLUDEDIR="%BLOSC_INCLUDE_LOCATION%"^
    -DBLOSC_LIBRARYDIR="%BLOSC_LIB_LOCATION%"^
    -DBLOSC_USE_STATIC_LIBS=ON^
    -DBlosc_LIBRARY_RELEASE="%BLOSC_LIBRARY_LOCATION_RELEASE%"^
    -DBlosc_LIBRARY_DEBUG="%BLOSC_LIBRARY_LOCATION_DEBUG%"^
    -DBoost_NO_BOOST_CMAKE=ON^
    -DBoost_NO_SYSTEM_PATHS=ON^
    -DBOOST_INCLUDEDIR="%BOOST_INCLUDE_LOCATION%"^
    -DBOOST_LIBRARYDIR="%BOOST_LIB_LOCATION%"^
    -DUSE_PKGCONFIG=OFF^
    -DOPENVDB_BUILD_BINARIES=OFF^
    -DOPENVDB_INSTALL_CMAKE_MODULES=OFF^
    -DOPENVDB_CORE_SHARED=OFF^
    -DOPENVDB_CORE_STATIC=ON^
    -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"^
    -DCMAKE_DEBUG_POSTFIX=_d^
    -DMSVC_MP_THREAD_COUNT="8"
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building OpenVDB for Debug...
cmake --build . --config Debug -j8
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing OpenVDB for Debug...
cmake --install . --config Debug
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building OpenVDB for Release...
cmake --build . --config Release -j8
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing OpenVDB for Release...
cmake --install . --config Release
if %errorlevel% neq 0 exit /B %errorlevel%

popd

echo Done.

goto :eof

:usage
echo Usage: BuildForWindows ^<version^>
exit /B 1

endlocal
