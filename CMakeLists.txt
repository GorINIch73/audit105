cmake_minimum_required(VERSION 3.20)

project(audit105 VERSION 0.1 LANGUAGES C CXX)

if(CMAKE_HOST_UNIX AND NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Default install path")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# --- ImGui (docking branch) ---
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()

# --- System dependencies ---
find_package(glfw3 3.3 REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenGL REQUIRED)
if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
endif()

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
)
target_link_libraries(imgui_lib PUBLIC glfw)

set(APP_SOURCES
    src/main.cpp
    src/App.cpp
    src/Database.cpp
    src/Widgets.cpp
    src/views/SettingsView.cpp
    src/ImGuiFileDialog.cpp
)

add_executable(audit105 ${APP_SOURCES})

target_include_directories(audit105 PRIVATE
    src
    src/views
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(audit105 PRIVATE USE_STD_FILESYSTEM)

target_link_libraries(audit105 PRIVATE
    glfw
    OpenGL::GL
    SQLite::SQLite3
    ZLIB::ZLIB
    imgui_lib
)

if(UNIX AND NOT APPLE)
    target_link_libraries(audit105 PRIVATE X11::X11)
endif()

add_custom_command(TARGET audit105 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/data
            $<TARGET_FILE_DIR:audit105>/data
)

# --- Tests ---
add_executable(test_db tests/test_db.cpp src/Database.cpp)
target_include_directories(test_db PRIVATE src)
target_link_libraries(test_db PRIVATE SQLite3::SQLite3)


# --- Установка ---
include(GNUInstallDirs)

set(INSTALL_DATA_DIR ${CMAKE_INSTALL_DATADIR}/audit105)

install(TARGETS audit105 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY data/ DESTINATION ${INSTALL_DATA_DIR})
install(FILES data/audit105.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME audit105.svg)
install(FILES audit105.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

if(WIN32)
  target_compile_definitions(audit105 PRIVATE INSTALL_DATA_DIR="data")
else()
  target_compile_definitions(audit105 PRIVATE
      INSTALL_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/audit105")
endif()
