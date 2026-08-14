cmake_minimum_required(VERSION 3.16)
project(haptic-device LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CHAI3D_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Path to the parent CHAI3D source tree")
set(CHAI3D_BUILD_DIR "${CHAI3D_ROOT}/build" CACHE PATH "Path to the parent CHAI3D build directory")
set(CHAI3D_GLFW_BUILD_DIR "${CHAI3D_BUILD_DIR}/extras/GLFW" CACHE PATH "Path to the parent GLFW package config")

if(NOT EXISTS "${CHAI3D_BUILD_DIR}/chai3dConfig.cmake")
  message(FATAL_ERROR
    "Missing CHAI3D CMake package config at ${CHAI3D_BUILD_DIR}/chai3dConfig.cmake.\n"
    "Build the parent CHAI3D tree first, for example:\n"
    "  cmake -S .. -B ../build -DCMAKE_BUILD_TYPE=Release\n"
    "  cmake --build ../build -j")
endif()

if(NOT EXISTS "${CHAI3D_GLFW_BUILD_DIR}/GLFWConfig.cmake")
  message(FATAL_ERROR
    "Missing GLFW CMake package config at ${CHAI3D_GLFW_BUILD_DIR}/GLFWConfig.cmake.\n"
    "Build the parent CHAI3D tree first so its bundled GLFW library is generated.")
endif()

find_package(Python3 REQUIRED COMPONENTS Development)
find_package(chai3d CONFIG REQUIRED PATHS "${CHAI3D_BUILD_DIR}" NO_DEFAULT_PATH)
find_package(GLFW CONFIG REQUIRED PATHS "${CHAI3D_GLFW_BUILD_DIR}" NO_DEFAULT_PATH)

set(HAPTIC_DEVICE_SOURCES
  LJ.cpp
  atom.cpp
  inputHandling.cpp
  ipcServer.cpp
  potentials.cpp
  utility.cpp
  slider.cpp
)

# Custom 1-DOF serial haptic device uses termios - POSIX only.
if(NOT CMAKE_SYSTEM_NAME MATCHES "Windows")
  list(APPEND HAPTIC_DEVICE_SOURCES
    customHapticDevice.cpp
    hapticDeviceSerial.cpp
  )
endif()

add_executable(haptic-device ${HAPTIC_DEVICE_SOURCES})

# potentials.cpp uses std::filesystem, which needs C++17.
target_compile_features(haptic-device PRIVATE cxx_std_17)

target_include_directories(haptic-device PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  ${CHAI3D_INCLUDE_DIRS}
  ${GLFW_INCLUDE_DIRS}
)

# chai3dConfig.cmake's CHAI3D_DEFINITIONS is always empty (upstream gap) even
# though CHAI3D's own headers switch on these per-platform macros (e.g.
# CShapeCylinder.h's OpenGL/glu.h vs GL/glu.h branch) - set them ourselves so
# consumers of the installed package see the same defines the library itself
# was built with.
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  target_compile_definitions(haptic-device PRIVATE WIN32)
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  target_compile_definitions(haptic-device PRIVATE MACOSX)
else()
  target_compile_definitions(haptic-device PRIVATE LINUX)
endif()

target_link_directories(haptic-device PRIVATE ${CHAI3D_LIBRARY_DIRS})

target_link_libraries(haptic-device PRIVATE
  ${CHAI3D_LIBRARIES}
  ${GLFW_LIBRARIES}
  Python3::Python
)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  target_compile_options(haptic-device PRIVATE -fno-PIE -no-pie)
  target_link_options(haptic-device PRIVATE -no-pie)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  target_link_libraries(haptic-device PRIVATE ws2_32)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  set(_chai_bin_subdir "win-$<IF:$<BOOL:${CMAKE_CL_64}>,x64,Win32>")
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  set(_chai_bin_subdir "mac-${CMAKE_HOST_SYSTEM_PROCESSOR}")
else()
  set(_chai_bin_subdir "lin-${CMAKE_SYSTEM_PROCESSOR}")
endif()

set_target_properties(haptic-device PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CHAI3D_ROOT}/bin/${_chai_bin_subdir}"
)

# Standalone diagnostic tools for the custom haptic device - POSIX only, same
# reason customHapticDevice.cpp/hapticDeviceSerial.cpp are excluded on
# Windows above. chai3d_device_test prints raw decoded position/velocity with
# no GLFW window; chai3d_visualizer renders a cursor tracking shaft position
# against a fixed reference track. Both are quick sanity checks for the
# sensor/firmware/protocol chain independent of the full LJ.cpp simulation.
if(NOT CMAKE_SYSTEM_NAME MATCHES "Windows")
  add_executable(chai3d_device_test
    tools/chai3d_device_test.cpp
    customHapticDevice.cpp
    hapticDeviceSerial.cpp
  )
  add_executable(chai3d_visualizer
    tools/chai3d_visualizer.cpp
    customHapticDevice.cpp
    hapticDeviceSerial.cpp
  )

  target_link_libraries(chai3d_device_test PRIVATE ${CHAI3D_LIBRARIES})
  target_link_libraries(chai3d_visualizer PRIVATE ${CHAI3D_LIBRARIES} ${GLFW_LIBRARIES})

  foreach(_diag_target chai3d_device_test chai3d_visualizer)
    target_compile_features(${_diag_target} PRIVATE cxx_std_11)
    target_include_directories(${_diag_target} PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      ${CHAI3D_INCLUDE_DIRS}
      ${GLFW_INCLUDE_DIRS}
    )
    target_link_directories(${_diag_target} PRIVATE ${CHAI3D_LIBRARY_DIRS})
    if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
      target_compile_definitions(${_diag_target} PRIVATE MACOSX)
    else()
      target_compile_definitions(${_diag_target} PRIVATE LINUX)
    endif()
    # Repo root, not CHAI3D_ROOT/bin - these are ad-hoc dev tools, not part
    # of the installed simulation. Matches the .gitignore entries already
    # written for /chai3d_device_test and /chai3d_visualizer.
    set_target_properties(${_diag_target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
  endforeach()
endif()
