# src/render/vulkan/backend.cmake -- the Vulkan renderer backend (BANISHED_RENDER_BACKEND=vulkan;
# Windows as the development bed, Linux, Android). docs/vulkan/PLAN.md.
#
# Included by the top-level CMakeLists.txt from src/render/${BANISHED_RENDER_BACKEND}/backend.cmake,
# before the targets exist. The contract (src/render/common/README.md):
#   RENDER_SRCS              the backend's own sources
#   RENDER_BACKEND_INCLUDES  the backend's include directories
#   RENDER_BACKEND_READY     ON when OpenBA can be configured with this backend on this host
# No RENDER_BACKEND_LINK_LIBRARIES: nothing links against the Vulkan loader. SDL opens it when
# the window is created and the backend resolves every entry point through
# vkGetInstanceProcAddr (src/render/vulkan/vk_api.h). The headers are the pinned submodule
# third_party/Vulkan-Headers (Apache-2.0 / MIT; THIRD_PARTY_NOTICES.md), used with
# VK_NO_PROTOTYPES, as -isystem: they are Khronos's, not ours.

set(BANISHED_VULKAN_HEADERS ${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/include)
if(NOT EXISTS ${BANISHED_VULKAN_HEADERS}/vulkan/vulkan.h)
  set(RENDER_SRCS)
  set(RENDER_BACKEND_INCLUDES)
  set(RENDER_BACKEND_READY OFF)
  message(STATUS "OpenBA not configured: BANISHED_RENDER_BACKEND=vulkan needs the third_party/Vulkan-Headers submodule (git submodule update --init third_party/Vulkan-Headers)")
  return()
endif()

file(GLOB_RECURSE RENDER_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/vulkan/*.cpp)
set(RENDER_BACKEND_INCLUDES ${CMAKE_SOURCE_DIR}/src/render/vulkan)
set(RENDER_BACKEND_READY ON)
set_source_files_properties(${RENDER_SRCS} PROPERTIES
                            COMPILE_OPTIONS "-isystem;${BANISHED_VULKAN_HEADERS}")
