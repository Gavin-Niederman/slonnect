cmake_minimum_required(VERSION 3.21)
project(slonnect VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui DBus Network Qml Quick QuickControls2 Concurrent)
qt_standard_project_setup(REQUIRES 6.7)
qt_policy(SET QTP0004 NEW)
qt_policy(SET QTP0001 NEW)

qt_add_executable(slonnect
    src/main.cpp
    src/kdeconnect.h src/kdeconnect.cpp
    src/messagetypes.h src/messagetypes.cpp
    src/smstypes.h src/smstypes.cpp
    src/messagingbackend.h
    src/kdeconnectbackend.h src/kdeconnectbackend.cpp
    src/gmessagesbackend.h src/gmessagesbackend.cpp
    src/deviceplugin.h src/deviceplugin.cpp
    src/device.h src/device.cpp
    src/devicemanager.h src/devicemanager.cpp
    src/imagestore.h src/imagestore.cpp
    src/contactstore.h src/contactstore.cpp
    src/shareplugin.h src/shareplugin.cpp
    src/clipboardplugin.h src/clipboardplugin.cpp
    src/mediaplugin.h src/mediaplugin.cpp
    src/conversationsmodel.h src/conversationsmodel.cpp
    src/messagesmodel.h src/messagesmodel.cpp
    src/smsplugin.h src/smsplugin.cpp
    src/app.h src/app.cpp
    src/portalfiledialog.h src/portalfiledialog.cpp
    src/screenmirror.h src/screenmirror.cpp
    src/nodetypes.h src/nodetypes.cpp
    src/actionrun.h src/actionrun.cpp
    src/actiongraph.h src/actiongraph.cpp
    src/actionstore.h src/actionstore.cpp
    src/actionservice.h src/actionservice.cpp
    src/runcommand.h src/runcommand.cpp
    src/nodes_p.h src/systemnodes.cpp
    src/dbusintrospect.h src/dbusintrospect.cpp
    src/completer.h src/completer.cpp
    src/notificationsplugin.h src/notificationsplugin.cpp
    src/remoteplugins.h src/remoteplugins.cpp
    src/settings.h src/settings.cpp
)

target_compile_definitions(slonnect PRIVATE
    SLONNECT_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
)

file(GLOB_RECURSE QML_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
# Material Icons SVGs (Apache-2.0), fetched by tools/fetch-icons.sh.
file(GLOB ICON_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/icons/outlined/*.svg qml/icons/filled/*.svg)
set_source_files_properties(qml/Theme.qml qml/Features.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(slonnect
    URI Slonnect
    VERSION 1.0
    QML_FILES ${QML_FILES}
    RESOURCES ${ICON_FILES}
)

target_include_directories(slonnect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(slonnect PRIVATE
    Qt6::Core Qt6::Gui Qt6::DBus Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent
)

include(GNUInstallDirs)
install(TARGETS slonnect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/dev.slonnect.Slonnect.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
