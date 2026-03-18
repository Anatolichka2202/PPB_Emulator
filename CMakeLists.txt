cmake_minimum_required(VERSION 3.16)

project(PPB_Emulator LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

add_executable(PPB_Emulator
  main.cpp
  udpserver.h udpserver.cpp
  ppbemulator.h ppbemulator.cpp
  ../PPB_Tester_Software/core/communication/ppbprotocol.h
  ../PPB_Tester_Software/core/communication/packetbuilder.cpp ../PPB_Tester_Software/core/communication/packetbuilder.h
)
target_link_libraries(PPB_Emulator PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network)

include(GNUInstallDirs)
install(TARGETS PPB_Emulator
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
