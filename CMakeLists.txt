cmake_minimum_required(VERSION 3.21)
project(PapyrusTaskScheduler VERSION 0.1.0 LANGUAGES CXX)

# Fixed deploy target: every build lands here (enable this mod in MO2).
set(DEPLOY_FOLDER "C:/skyrimmods/mo2/mods/PapyrusTaskScheduler")

find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
    SOURCES
        src/plugin.cpp
        src/Scheduler.cpp
        src/UI.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE PCH.h)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_FOLDER}/SKSE/Plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.dll"
        "${DEPLOY_FOLDER}/SKSE/Plugins/${PROJECT_NAME}.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_FOLDER}/Scripts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_FOLDER}/Scripts/Source"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/PapyrusTaskScheduler.pex"
        "${DEPLOY_FOLDER}/Scripts/PapyrusTaskScheduler.pex"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/PapyrusTaskScheduler.psc"
        "${DEPLOY_FOLDER}/Scripts/Source/PapyrusTaskScheduler.psc"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/ExampleTaskConsumer.pex"
        "${DEPLOY_FOLDER}/Scripts/ExampleTaskConsumer.pex"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/ExampleTaskConsumer.psc"
        "${DEPLOY_FOLDER}/Scripts/Source/ExampleTaskConsumer.psc"
)
