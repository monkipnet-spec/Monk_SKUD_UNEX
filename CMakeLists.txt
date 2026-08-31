cmake_minimum_required(VERSION 3.16)
project(Monk_SKUD_UNEX VERSION 0.1.3 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
add_executable(monk-skud-unex
  src/main.cpp
  src/app/App.cpp
  src/config/Config.cpp
  src/utils/Util.cpp
  src/storage/FileStore.cpp
  src/users/UserManager.cpp
  src/departments/DepartmentManager.cpp
  src/attendance/AttendanceEngine.cpp
  src/serial/SerialPort.cpp
  src/unex/Unex721Protocol.cpp
  src/controllers/ControllerManager.cpp
  src/telegram/TelegramNotifier.cpp
  src/web/WebServer.cpp
)
target_include_directories(monk-skud-unex PRIVATE include)
target_link_libraries(monk-skud-unex PRIVATE OpenSSL::Crypto Threads::Threads)
target_compile_options(monk-skud-unex PRIVATE -Wall -Wextra -Wpedantic)

# Keep the web UI next to the binary as well as in the source tree.
# This lets the application serve static files even when started from build/.
file(COPY "${CMAKE_SOURCE_DIR}/web" DESTINATION "${CMAKE_BINARY_DIR}")
install(TARGETS monk-skud-unex RUNTIME DESTINATION bin)
