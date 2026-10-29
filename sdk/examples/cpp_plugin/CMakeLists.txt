cmake_minimum_required(VERSION 3.16)
project(rbs_cpp_plugin CXX)
set(CMAKE_CXX_STANDARD 17)

# Path to the engine's sdk/include folder
set(RBS_SDK_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../include CACHE PATH "RBS SDK include directory")
# Where to put the built plugin (default: the demo mod)
set(RBS_PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../mods/plugin_demo/plugins CACHE PATH "Output folder")

add_library(cpp_turret SHARED turret.cpp)
target_include_directories(cpp_turret PRIVATE ${RBS_SDK_INCLUDE})
set_target_properties(cpp_turret PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
add_custom_command(TARGET cpp_turret POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${RBS_PLUGIN_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cpp_turret> ${RBS_PLUGIN_DIR}/)
