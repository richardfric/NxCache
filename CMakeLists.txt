cmake_minimum_required(VERSION 3.31)

# Enable Hot Reload for MSVC compilers if supported.
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("NxCache" VERSION 1.0 LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(NxCache NxCache.cpp NxCache.h main.cpp)
set_property(TARGET NxCache PROPERTY CXX_STANDARD 20)
target_compile_definitions(NxCache PRIVATE SVCNAME="NxCache" SVCDISPLAYNAME="NxCache Service")

if(WIN32)
  target_sources(NxCache PRIVATE winsvc.cpp winsvc.hpp)
  target_compile_definitions(NxCache PRIVATE _WIN32_WINNT=0x0A00)
else()
  find_package(boost_asio_core CONFIG REQUIRED)
  target_link_libraries(NxCache PRIVATE Boost::asio_core)
endif()

find_package(boost_program_options CONFIG REQUIRED)
target_link_libraries(NxCache PRIVATE Boost::program_options)

find_package(cpprestsdk CONFIG REQUIRED)
if(WIN32)
    target_link_libraries(NxCache PRIVATE cpprestsdk::cpprest cpprestsdk::cpprestsdk_zlib_internal cpprestsdk::cpprestsdk_brotli_internal)
else()
    target_link_libraries(NxCache PRIVATE cpprestsdk::cpprest cpprestsdk::cpprestsdk_zlib_internal cpprestsdk::cpprestsdk_boost_internal cpprestsdk::cpprestsdk_openssl_internal)
endif()

install(TARGETS NxCache RUNTIME DESTINATION bin)
