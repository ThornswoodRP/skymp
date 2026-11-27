enable_testing()

include(${CMAKE_SOURCE_DIR}/cmake/apply_default_settings.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/thornswood_unity_build.cmake)

#
# MpClientPlugin
#

if(WIN32)
  file(GLOB_RECURSE src "client/*")
  list(APPEND src "${CMAKE_SOURCE_DIR}/.clang-format")
  add_library(MpClientPlugin SHARED ${src})
  # THORNSWOOD (Thornswood #7444): the client takes the data types its
  # messages carry through messages (server_guest_types, below), never the
  # server's own library.
  target_link_libraries(MpClientPlugin PUBLIC mp_common)
  set_target_properties(MpClientPlugin PROPERTIES OUTPUT_NAME "MpClientPlugin")
  apply_default_settings(TARGETS MpClientPlugin)
  list(APPEND VCPKG_DEPENDENT MpClientPlugin)

  # Not mistake that skyrim-platform depends on MpClientPlugin
  # skyrim-platform is a logical target that invokes dev_service
  # dev_service wants to access built MpClientPlugin.dll
  add_dependencies(skyrim-platform MpClientPlugin)
endif()

#
# messages
#

file(GLOB_RECURSE src "${CMAKE_CURRENT_SOURCE_DIR}/messages/*")
list(APPEND src "${CMAKE_SOURCE_DIR}/.clang-format")
add_library(messages STATIC ${src})
target_include_directories(messages PUBLIC "${CMAKE_CURRENT_LIST_DIR}/messages")
target_link_libraries(messages PUBLIC viet)
target_link_libraries(messages PUBLIC papyrus-vm-lib) # Utils.h: stricmp
target_link_libraries(messages PUBLIC serialization)
target_link_libraries(messages PUBLIC server_guest_types)
apply_default_settings(TARGETS messages)
list(APPEND VCPKG_DEPENDENT messages)

#
# server_guest_types
#

# THORNSWOOD (Thornswood #7444). The four data types the messages carry, which
# the client and the server both serialize. Their sources stay in
# server_guest_lib's folder, beside the rest of the server's types, and are
# compiled here instead of in server_guest_lib.
#
# WHY. MpClientPlugin used to link the whole of server_guest_lib for these
# four. A linker takes a static library's objects whole, and while each
# source was its own object the client took these four and nothing else.
# Since server_guest_lib compiles in batches (Thornswood #7247), one object
# holds eight sources, so the client took the server code batched beside
# them, and with it everything that code calls: measured at fork commit
# 11988fae, MpClientPlugin.dll grew from 1.5 MB to 3.3 MB and its program
# database names antigo, the Mongo drivers, libzip, rsm-bsa, libespm and
# papyrus-vm, which the server alone uses. Now the client links only what
# messages needs, so how the server compiles cannot change what goes into
# the client.
set(server_guest_types_src
  "${CMAKE_CURRENT_SOURCE_DIR}/server_guest_lib/AnimationData.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/server_guest_lib/Appearance.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/server_guest_lib/Equipment.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/server_guest_lib/Inventory.cpp"
)
add_library(server_guest_types STATIC ${server_guest_types_src})
# Appearance.cpp reads JsonUtils.h, whose helpers are inline: the folder only.
target_include_directories(server_guest_types PRIVATE "${CMAKE_CURRENT_LIST_DIR}/mp_common")
target_link_libraries(server_guest_types PUBLIC serialization)
apply_default_settings(TARGETS server_guest_types)
list(APPEND VCPKG_DEPENDENT server_guest_types)

#
# mp_common
#

file(GLOB_RECURSE src "${CMAKE_CURRENT_SOURCE_DIR}/mp_common/*")
list(APPEND src "${CMAKE_SOURCE_DIR}/.clang-format")
add_library(mp_common STATIC ${src})
target_compile_definitions(mp_common PUBLIC MAX_PLAYERS=1000)
target_include_directories(mp_common PUBLIC "${CMAKE_CURRENT_LIST_DIR}/mp_common")
target_link_libraries(mp_common PUBLIC messages viet)
apply_default_settings(TARGETS mp_common)
list(APPEND VCPKG_DEPENDENT mp_common)
if(WIN32)
  target_link_libraries(mp_common PUBLIC ws2_32.lib)
endif()

#
# geo
#

# https://www.codeproject.com/Articles/1065730/Point-Inside-Convex-Polygon-in-Cplusplus
file(GLOB_RECURSE src "${CMAKE_CURRENT_SOURCE_DIR}/geo/*")
list(APPEND src "${CMAKE_SOURCE_DIR}/.clang-format")
add_library(geo STATIC ${src})
target_include_directories(geo PUBLIC "${CMAKE_CURRENT_LIST_DIR}/geo")
apply_default_settings(TARGETS geo)

#
# server_guest_lib
#

file(GLOB_RECURSE src "${CMAKE_CURRENT_SOURCE_DIR}/server_guest_lib/*")
# THORNSWOOD (Thornswood #7444): server_guest_types compiles these, and the
# server takes them from it through mp_common and messages.
foreach(f ${server_guest_types_src})
  list(FIND src "${f}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "server_guest_types names ${f}, which is not in server_guest_lib")
  endif()
endforeach()
list(REMOVE_ITEM src ${server_guest_types_src})
list(APPEND src "${CMAKE_SOURCE_DIR}/.clang-format")
add_library(server_guest_lib STATIC ${src})
target_include_directories(server_guest_lib PUBLIC "${CMAKE_CURRENT_LIST_DIR}/server_guest_lib")
apply_default_settings(TARGETS server_guest_lib)
# THORNSWOOD: compiled in batches (Thornswood #7247, cmake/thornswood_unity_build.cmake says why)
thornswood_unity_build(TARGETS server_guest_lib)
list(APPEND VCPKG_DEPENDENT server_guest_lib)
target_link_libraries(server_guest_lib PUBLIC viet mp_common espm papyrus-vm-lib geo serialization)

#
# localization-provider
#

file(GLOB src "${CMAKE_CURRENT_SOURCE_DIR}/localization_provider/*")
list(APPEND src "${CMAKE_SOURCE_DIR}/.clang-format")
add_library(localization-provider STATIC ${src})
apply_default_settings(TARGETS localization-provider)
target_include_directories(localization-provider PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/localization_provider")

#
# Link vcpkg deps
#

find_path(CATCH_INCLUDE_DIR NAMES catch.hpp PATH_SUFFIXES catch2)
get_filename_component(CATCH_INCLUDE_DIR ${CATCH_INCLUDE_DIR} DIRECTORY)

find_path(JSON_INCLUDE_DIR NAMES json.hpp PATH_SUFFIXES nlohmann)
get_filename_component(JSON_INCLUDE_DIR ${JSON_INCLUDE_DIR} DIRECTORY)

find_package(spdlog CONFIG REQUIRED)

find_package(simdjson CONFIG REQUIRED)

find_package(slikenet CONFIG REQUIRED)

find_package(ZLIB REQUIRED)

if(NOT EMSCRIPTEN)
  find_package(mongocxx REQUIRED)
endif()

find_package(unofficial-sodium CONFIG REQUIRED)

# MacOS not supported by rsm-bsa
if(NOT APPLE AND NOT EMSCRIPTEN)
  find_package(bsa CONFIG REQUIRED)
endif()

find_package(libzippp REQUIRED)

find_package(antigo REQUIRED)

foreach(target ${VCPKG_DEPENDENT})
  target_include_directories(${target} PUBLIC ${CATCH_INCLUDE_DIR})
  target_include_directories(${target} PUBLIC ${JSON_INCLUDE_DIR})
  target_link_libraries(${target} PUBLIC spdlog::spdlog)
  target_link_libraries(${target} PUBLIC simdjson::simdjson)

  target_link_libraries(${target} PUBLIC SLikeNet)

  target_link_libraries(${target} PUBLIC antigo::antigo)

  target_link_libraries(${target} PUBLIC ZLIB::ZLIB)

  if(TARGET mongo::mongocxx_static)
    target_link_libraries(${target} PUBLIC mongo::mongocxx_static mongo::bsoncxx_static mongoc::static bson::static)
  else()
    target_compile_definitions(${target} PUBLIC NO_MONGO=1)
  endif()

  target_link_libraries(${target} PUBLIC unofficial-sodium::sodium)

  if(TARGET bsa::bsa)
    target_link_libraries(${target} PUBLIC bsa::bsa)
  else()
    target_compile_definitions(${target} PUBLIC NO_BSA=1)
  endif()
  target_link_libraries(${target} PUBLIC libzippp::libzippp)

  if(APPLE)
    target_link_libraries(${target} PUBLIC pthread resolv m)
  elseif(WIN32)
    target_link_libraries(${target} PUBLIC Secur32.lib Crypt32.lib Dnsapi.lib Bcrypt.lib)
  elseif(EMSCRIPTEN)
    # ...
  elseif(UNIX)
    target_link_libraries(${target} PUBLIC pthread resolv rt m)
  endif()
endforeach()
