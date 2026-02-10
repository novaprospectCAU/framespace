cmake_minimum_required(VERSION 3.20)
project(framespace_webgpu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "This project is intended to be built with Emscripten. Use: emcmake cmake -B build")
endif()

add_executable(framespace src/main.cpp)

# Generate HTML + JS + WASM and enable WebGPU APIs via emdawnwebgpu port.
set_target_properties(framespace PROPERTIES SUFFIX ".html")

target_compile_options(framespace PRIVATE
  -O2
  -Wall
  -Wextra
  --use-port=emdawnwebgpu
)

target_link_options(framespace PRIVATE
  -O2
  --use-port=emdawnwebgpu
  -sASYNCIFY
  -sALLOW_MEMORY_GROWTH=1
   -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']
  -sEXPORTED_FUNCTIONS=['_main','_framespace_trigger_capture','_framespace_trigger_place']
  --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html
)
