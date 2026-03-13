cmake_minimum_required(VERSION 3.21)
project(ThreeTTY LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(ENABLE_LIBSSH "Enable libssh-based SSH backend" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets WebChannel)

if(ENABLE_LIBSSH)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBSSH QUIET IMPORTED_TARGET libssh)
  endif()
endif()

add_executable(three_tty
  assets/terminal_assets.qrc
  include/session/SessionBackend.hpp
  include/session/PtySession.hpp
  include/net/OllamaClient.hpp
  include/ui/MainWindow.hpp
  include/ui/QuadTerminalView.hpp
  include/ui/TerminalTab.hpp
  include/ui/MasterPasswordDialog.hpp
  include/ui/ProfileEditorDialog.hpp
  include/ui/ProxyEditorDialog.hpp
  include/ui/SecretsEditorDialog.hpp
  src/main.cpp
  src/data/Models.cpp
  src/ui/MainWindow.cpp
  src/ui/QuadTerminalView.cpp
  src/ui/ProfileEditorDialog.cpp
  src/ui/ProxyEditorDialog.cpp
  src/ui/SecretsEditorDialog.cpp
  src/ui/MasterPasswordDialog.cpp
  src/ui/TerminalTab.cpp
  src/core/AppController.cpp
  src/core/ConfigStore.cpp
  src/core/Crypto.cpp
  src/core/SessionStore.cpp
  src/session/PtySession.cpp
  src/net/OllamaClient.cpp
)

target_include_directories(three_tty PRIVATE include)

target_link_libraries(three_tty PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::WebEngineWidgets Qt6::WebChannel)
set_target_properties(three_tty PROPERTIES OUTPUT_NAME "3TTY")

if(LIBSSH_FOUND)
  target_compile_definitions(three_tty PRIVATE HAS_LIBSSH=1)
  target_link_libraries(three_tty PRIVATE PkgConfig::LIBSSH)
else()
  message(WARNING "libssh not found; SSH backend will be stubbed")
endif()

install(TARGETS three_tty RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
  add_executable(config_tests
    tests/config_tests.cpp
    src/data/Models.cpp
    src/core/Crypto.cpp
    src/core/ConfigStore.cpp
  )
  target_include_directories(config_tests PRIVATE include)
  target_link_libraries(config_tests PRIVATE Qt6::Core)
  add_test(NAME config_tests COMMAND config_tests)
endif()
