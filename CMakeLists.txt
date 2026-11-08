cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(DoseChamber)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(ROOT REQUIRED)
include(${ROOT_USE_FILE})

find_package(boost_headers 1.73.0 REQUIRED CONFIG)
if(NOT boost_headers_FOUND)
  error("boost_headers package is not found. Program cannot be built.")
endif()

if(NOT withqt)
  if(ROOT_qt6web_FOUND)
    set(withqt 6)
  elseif(ROOT_qt5web_FOUND)
    set(withqt 5)
  else()
    error("Neither qt5web nor qt6web components were found")
  endif()
endif()

if(withqt EQUAL 6)
  find_package(Qt6 COMPONENTS Core Widgets WebEngineCore Xml OpenGL WebEngineWidgets SerialPort OpcUa OpenGLWidgets StateMachine Core5Compat CONFIG)
  set(qt_libs Qt6::Core Qt6::Widgets Qt6::WebEngineCore Qt6::WebEngineWidgets Qt6::SerialPort Qt6::OpcUa Qt6::Xml Qt6::OpenGL Qt6::OpenGLWidgets Qt6::StateMachine Qt6::Core5Compat)
  set(root_qtlib ROOT::ROOTQt6WebDisplay)
elseif(withqt EQUAL 5)
  find_package(Qt5 COMPONENTS Core Widgets WebEngine Xml OpenGL WebEngineWidgets SerialPort OpcUa REQUIRED)
  set(qt_libs Qt5::Core Qt5::Widgets Qt5::WebEngine Qt5::WebEngineWidgets Qt5::SerialPort Qt5::OpcUa Qt5::Xml Qt5::OpenGL)
  set(root_qtlib ROOT::ROOTQt5WebDisplay)
else()
  error("No matching withqt parameter found, check if qt5web or qt6web components are there")
endif()

add_executable(DoseChamber
  mainwindow.cpp
  mainwindow.ui
  TCanvasWidget.cpp
  main.cpp
)

target_link_libraries(DoseChamber
  ${root_qtlib}
  ${qt_libs}
  ${ROOT_LIBRARIES}
  ROOT::ROOTWebDisplay
  ROOT::WebGui6
  ROOT::Gpad
  ROOT::ROOTGpadv7
  ROOT::ROOTCanvasPainter
  ROOT::Geom
  ROOT::ROOTBrowserv7
  ROOT::ROOTGeomViewer
)

install(TARGETS DoseChamber DESTINATION bin)
