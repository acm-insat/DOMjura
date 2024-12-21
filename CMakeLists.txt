cmake_minimum_required(VERSION 3.16)
project(DOMjura VERSION 1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Automatically enable Qt features (UIC, MOC, etc.)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Find Qt packages (Qt6 is preferred)
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Network
    OpenGL
    OpenGLWidgets
    Widgets
    Xml
)

# Set up the project according to Qt standards
qt_standard_project_setup()

# Source files
set(SOURCES
    aboutdialog.cpp aboutdialog.h aboutdialog.ui
    contest.cpp contest.h
    domjudgeapimanager.cpp domjudgeapimanager.h
    gradientcache.cpp gradientcache.h
    group.cpp group.h
    headergraphicsitem.cpp headergraphicsitem.h
    judging.cpp judging.h
    legendagraphicsitem.cpp legendagraphicsitem.h
    main.cpp
    maincontroller.cpp maincontroller.h
    maindialog.cpp maindialog.h maindialog.ui
    problem.cpp problem.h
    problemgraphicsitem.cpp problemgraphicsitem.h
    rankedteam.cpp rankedteam.h
    resultswindow.cpp resultswindow.h
    settingsdialog.cpp settingsdialog.h settingsdialog.ui
    standingscontroller.cpp standingscontroller.h
    submission.cpp submission.h
    team.cpp team.h
    teamgraphicsitem.cpp teamgraphicsitem.h
    winnergraphicsitem.cpp winnergraphicsitem.h
)

# Resources
set(RESOURCES
    "resources/image-256.png"
)

# Add executable
qt_add_executable(DOMjura WIN32 MACOSX_BUNDLE
    ${SOURCES}
)

# Link with required Qt libraries
target_link_libraries(DOMjura PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Widgets
    Qt6::Xml
)

# Add resources
qt_add_resources(DOMjura "resources"
    PREFIX "/images"
    FILES ${RESOURCES}
)

# Install targets
install(TARGETS DOMjura
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Deploy script generation for Qt
qt_generate_deploy_app_script(
    TARGET DOMjura
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)

# Install deploy script
install(SCRIPT ${deploy_script})
