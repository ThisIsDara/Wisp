cmake_minimum_required(VERSION 3.25)
project(wisp VERSION 0.1.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Quick Qml Gui Core Test Widgets Concurrent Network)

qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)   # module resources at :/qt/qml/<Module> — the import system's canonical location (deploy-safe)

# Shared C++ core (Win32 firewall + policy) — linked by both the app and tests.
qt_add_library(wisp_core STATIC
    src/win/WinHotkey.cpp
    src/win/WinFullscreenGuard.cpp
    src/win/WinDirectoryWalk.cpp
    src/win/WinStartMenuEnumerator.cpp
    src/win/WinUwpEnumerator.cpp
    src/win/WinLaunch.cpp
    src/win/WinIconExtractor.cpp
    src/win/WinUwpLogo.cpp
    src/win/WinSingleInstance.cpp
    src/win/WinKeyCapture.cpp
    src/core/HotkeyManager.cpp
    src/core/LauncherController.cpp
    src/core/FuzzyMatcher.cpp
    src/core/ResultsModel.cpp
    src/core/AppCatalog.cpp
    src/core/FileSearch.cpp
    src/core/FileIndex.cpp
    src/core/ScanService.cpp
    src/core/LaunchController.cpp
    src/core/LaunchHistory.cpp
    src/core/IconCache.cpp
    src/core/IconProvider.cpp
    src/core/SettingsStore.cpp
    src/core/AutostartManager.cpp
    src/core/CurationRules.cpp
    src/core/CurationStore.cpp
    src/core/FavoritesStore.cpp
    src/core/Calculator.cpp
    src/core/AppProvider.cpp
    src/core/FileProvider.cpp
    src/core/CalculatorProvider.cpp
    src/core/CommandProvider.cpp
    src/core/UpdateService.cpp
    src/ui/HotkeyCaptureDialog.cpp
    src/ui/SettingsWindow.cpp
    src/ui/UpdateDialogs.cpp
)
target_include_directories(wisp_core PUBLIC src)
target_link_libraries(wisp_core PUBLIC Qt6::Gui Qt6::Qml Qt6::Concurrent Qt6::Network)
target_link_libraries(wisp_core PRIVATE shell32 ole32 shlwapi Qt6::Quick)
# windowsapp.lib: RoGetActivationFactory + RoOriginateLanguageException from
# the C++/WinRT SDK projection (WinUwpEnumerator) — must be in the link line
# of every consumer that pulls that object.
target_link_libraries(wisp_core PRIVATE windowsapp)

# C++/WinRT projection headers from the Windows SDK (RESEARCH §2, PATTERNS §8)
# — no NuGet package; requires the vcvars64 environment (build.ps1 guarantees
# it) so $ENV{WindowsSdkDir} is set.
file(GLOB _sdk_cppwinrt "$ENV{WindowsSdkDir}Include/*/cppwinrt")
list(SORT _sdk_cppwinrt ORDER DESCENDING)
if(_sdk_cppwinrt)
    list(GET _sdk_cppwinrt 0 _sdk_cppwinrt_latest)
    target_include_directories(wisp_core SYSTEM PRIVATE "${_sdk_cppwinrt_latest}")
endif()

qt_add_executable(wisp WIN32
    src/app/main.cpp
    src/app/wisp.rc
    src/app/FrameTimeProbe.cpp
    src/tray/TrayIcon.cpp
)

# Single version source (D-15): About box + updater compare read WISP_VERSION.
# packaging/installer.nsi VIProductVersion is bumped in lockstep at release time.
target_compile_definitions(wisp PRIVATE WISP_VERSION="${PROJECT_VERSION}")

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(wisp_qml STATIC)
qt_add_qml_module(wisp_qml
    URI wisp
    VERSION 0.1.0
    QML_FILES
        qml/MainWindow.qml
        qml/ResultsRow.qml
        qml/HotkeyCaptureDialog.qml
        qml/SettingsWindow.qml
        qml/ColorDialog.qml
        qml/UpdatePrompt.qml
        qml/Theme.qml
    RESOURCES
        qml/assets/shadow.png
)

target_link_libraries(wisp PRIVATE
    Qt6::Quick
    Qt6::Qml
    Qt6::Gui
    Qt6::Widgets
    wisp_core
    wisp_qml
    wisp_qmlplugin
)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(tst_shell tests/tst_shell.cpp)
    target_link_libraries(tst_shell PRIVATE Qt6::Quick Qt6::Qml Qt6::Gui Qt6::Test wisp_core wisp_qml wisp_qmlplugin)
    add_test(NAME tst_shell COMMAND tst_shell)

    qt_add_executable(tst_hotkey tests/tst_hotkey.cpp)
    target_link_libraries(tst_hotkey PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_hotkey COMMAND tst_hotkey)

    qt_add_executable(tst_launcher tests/tst_launcher.cpp)
    target_link_libraries(tst_launcher PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_launcher COMMAND tst_launcher)

    qt_add_executable(tst_capture tests/tst_capture.cpp)
    target_link_libraries(tst_capture PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_capture COMMAND tst_capture)

    qt_add_executable(tst_singleinstance tests/tst_singleinstance.cpp)
    target_link_libraries(tst_singleinstance PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_singleinstance COMMAND tst_singleinstance)

    qt_add_executable(tst_matcher tests/tst_matcher.cpp)
    target_link_libraries(tst_matcher PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_matcher COMMAND tst_matcher)

    qt_add_executable(tst_model tests/tst_model.cpp)
    target_link_libraries(tst_model PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_model COMMAND tst_model)

    qt_add_executable(tst_enum tests/tst_enum.cpp)
    target_link_libraries(tst_enum PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_enum COMMAND tst_enum)

    qt_add_executable(tst_catalog tests/tst_catalog.cpp)
    target_link_libraries(tst_catalog PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_catalog COMMAND tst_catalog)

    qt_add_executable(tst_launch tests/tst_launch.cpp)
    target_link_libraries(tst_launch PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_launch COMMAND tst_launch)

    qt_add_executable(tst_command tests/tst_command.cpp)
    target_link_libraries(tst_command PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_command COMMAND tst_command)

    qt_add_executable(tst_tray tests/tst_tray.cpp src/tray/TrayIcon.cpp)
    target_include_directories(tst_tray PRIVATE src)
    target_link_libraries(tst_tray PRIVATE Qt6::Widgets Qt6::Gui Qt6::Test)
    add_test(NAME tst_tray COMMAND tst_tray)

    qt_add_executable(tst_filesearch tests/tst_filesearch.cpp)
    target_link_libraries(tst_filesearch PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_filesearch COMMAND tst_filesearch)

    qt_add_executable(tst_scan tests/tst_scan.cpp)
    target_link_libraries(tst_scan PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_scan COMMAND tst_scan)

    qt_add_executable(tst_index tests/tst_index.cpp)
    target_link_libraries(tst_index PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_index COMMAND tst_index)

    qt_add_executable(tst_history tests/tst_history.cpp)
    target_link_libraries(tst_history PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_history COMMAND tst_history)

    qt_add_executable(tst_icons tests/tst_icons.cpp)
    target_link_libraries(tst_icons PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_icons COMMAND tst_icons)

    qt_add_executable(tst_iconcache tests/tst_iconcache.cpp)
    target_link_libraries(tst_iconcache PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_iconcache COMMAND tst_iconcache)

    qt_add_executable(tst_settings tests/tst_settings.cpp)
    target_link_libraries(tst_settings PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_settings COMMAND tst_settings)

    qt_add_executable(tst_autostart tests/tst_autostart.cpp)
    target_link_libraries(tst_autostart PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_autostart COMMAND tst_autostart)

    qt_add_executable(tst_curation tests/tst_curation.cpp)
    target_link_libraries(tst_curation PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_curation COMMAND tst_curation)

    qt_add_executable(tst_favorites tests/tst_favorites.cpp)
    target_link_libraries(tst_favorites PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_favorites COMMAND tst_favorites)

    qt_add_executable(tst_updater tests/tst_updater.cpp)
    target_link_libraries(tst_updater PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
    add_test(NAME tst_updater COMMAND tst_updater)

    # Qt test binaries need the Qt DLLs (Qt6Testd.dll etc.) at run time; ctest
    # does not inherit build.ps1's PATH. Prepend Qt's bin dir (derived from
    # Qt6::Core's location — no hardcoded path) for every test so plain
    # `ctest --test-dir build/dev` works from any shell.
    set_tests_properties(
        tst_shell tst_hotkey tst_launcher tst_capture tst_singleinstance
        tst_matcher tst_model
        tst_enum tst_catalog tst_launch tst_tray tst_filesearch
        tst_scan tst_index
        tst_history tst_icons tst_iconcache tst_settings tst_autostart
        tst_curation tst_favorites tst_updater tst_command
        PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()
