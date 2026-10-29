cmake_minimum_required(VERSION 3.20)
project(rbs_engine VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
#  RBS Engine - retro boomer shooter engine
#  Dependencies are fetched automatically (raylib, Lua, nlohmann/json).
#  For offline builds point FETCHCONTENT_SOURCE_DIR_<NAME> at local checkouts,
#  e.g. -DFETCHCONTENT_SOURCE_DIR_RAYLIB=/path/to/raylib
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# raylib -------------------------------------------------------------------
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE)

# Lua 5.4 -------------------------------------------------------------------
FetchContent_Declare(lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG v5.4.7
  GIT_SHALLOW TRUE)

# nlohmann/json -------------------------------------------------------------
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(raylib)
# Formats raylib's config.h leaves disabled but modders expect (TGA textures, FLAC sounds).
target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_TGA=1 SUPPORT_FILEFORMAT_FLAC=1)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
  FetchContent_Populate(lua)
endif()
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
  FetchContent_Populate(json)
endif()

# Lua static library ----------------------------------------------------------
set(LUA_CORE lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes
    lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib ldblib
    liolib lmathlib loadlib loslib lstrlib ltablib lutf8lib linit)
set(LUA_SOURCES "")
foreach(f ${LUA_CORE})
  list(APPEND LUA_SOURCES ${lua_SOURCE_DIR}/${f}.c)
endforeach()
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
set_target_properties(lua_static PROPERTIES LINKER_LANGUAGE C)
if(UNIX AND NOT APPLE)
  target_compile_definitions(lua_static PRIVATE LUA_USE_LINUX)
elseif(APPLE)
  target_compile_definitions(lua_static PRIVATE LUA_USE_MACOSX)
endif()

# Engine ----------------------------------------------------------------------
file(GLOB_RECURSE RBS_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(rbs ${RBS_SOURCES})
target_include_directories(rbs PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/sdk/include
  ${json_SOURCE_DIR}/single_include)
find_package(Threads REQUIRED)
target_link_libraries(rbs PRIVATE raylib lua_static Threads::Threads ${CMAKE_DL_LIBS})
target_compile_definitions(rbs PRIVATE RBS_VERSION="${PROJECT_VERSION}" RAYMATH_DISABLE_CPP_OPERATORS)

if(MSVC)
  target_compile_options(rbs PRIVATE /W3 /utf-8 /permissive-)
  target_compile_definitions(rbs PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(rbs PRIVATE -Wall -Wno-missing-field-initializers -Wno-unused-parameter)
endif()
if(APPLE)
  target_link_libraries(rbs PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

# The executable looks for the game data folder (base/) next to itself and then
# in parent folders, so running it straight from build/ uses the repo's base/.
# Package the game with:  cmake --install build --component game --prefix dist
# (the component skips the static raylib library and headers raylib installs too).
install(TARGETS rbs RUNTIME DESTINATION . COMPONENT game)
install(DIRECTORY base mods sdk docs DESTINATION . COMPONENT game
        PATTERN "cache" EXCLUDE PATTERN "target" EXCLUDE PATTERN "bin" EXCLUDE PATTERN "obj" EXCLUDE PATTERN "build" EXCLUDE)
install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION . COMPONENT game)
