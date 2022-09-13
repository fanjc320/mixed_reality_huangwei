cmake_minimum_required(VERSION 3.4.1)

#add_definitions(-DXR_USE_PLATFORM_ANDROID -DXR_USE_GRAPHICS_API_OPENGL_ES)

set(QXR_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../)
set(QXR_EXTERNAL_DIR ${QXR_ROOT_PATH}/Samples/MixedReality/External/)

set(XR_LOADER_DISTRIBUTION_DIR ${QXR_EXTERNAL_DIR}/)
find_package(loader REQUIRED CONFIG)

# build native_app_glue as a static lib
set(${CMAKE_C_FLAGS}, "${CMAKE_C_FLAGS}")
add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

# now build app's shared lib
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

# Export ANativeActivity_onCreate(),
# Refer to: https://github.com/android-ndk/ndk/issues/381.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

# common-log
set(COMMON_LOG_SOURCE_DIR ${QXR_EXTERNAL_DIR}/Common/Log/)
add_library(qxr-common-log STATIC ${COMMON_LOG_SOURCE_DIR}/LogUtils.c)
target_include_directories(qxr-common-log PUBLIC ${COMMON_LOG_SOURCE_DIR})

# thirdparty-glm module
set(GLM_SOURCE_DIR ${QXR_EXTERNAL_DIR}/ThirdParty/glm/)
add_library(qxr-thirdparty-glm INTERFACE)
target_include_directories(qxr-thirdparty-glm INTERFACE ${GLM_SOURCE_DIR})

# thirdparty-tinyobj module
set(THIRDPARTY_TINYOBJ_SOURCE_DIR
        ${QXR_EXTERNAL_DIR}/ThirdParty/tinyobjloader/)
set(THIRDPARTY_TINYOBJ_SOURCE_FILES
        ${THIRDPARTY_TINYOBJ_SOURCE_DIR}/tiny_obj_loader.cc)
add_library(qxr-thirdparty-tinyobj STATIC ${THIRDPARTY_TINYOBJ_SOURCE_FILES})
target_include_directories(qxr-thirdparty-tinyobj
        INTERFACE ${THIRDPARTY_TINYOBJ_SOURCE_DIR})

# common-data-structures
set(COMMON_DATA_STRUCTURES_SOURCE_DIR
        ${QXR_EXTERNAL_DIR}/Common/DataStructures/cpp)
add_library(qxr-common-data-structures INTERFACE)
target_include_directories(qxr-common-data-structures
        INTERFACE ${COMMON_DATA_STRUCTURES_SOURCE_DIR})

# common-gl
set(COMMON_GL_SOURCE_DIR ${QXR_EXTERNAL_DIR}/Common/GL/cpp)
file(GLOB COMMON_GL_SOURCE_FILES ${COMMON_GL_SOURCE_DIR}/*.cpp)
add_library(qxr-common-gl STATIC ${COMMON_GL_SOURCE_FILES})
target_include_directories(qxr-common-gl PUBLIC ${COMMON_GL_SOURCE_DIR}/)
target_link_libraries(qxr-common-gl PRIVATE
        log
        qxr-common-log
        qxr-thirdparty-glm
        qxr-thirdparty-tinyobj
        qxr-common-data-structures)

# app-common module
set(APPCOMMON_SOURCE_DIR ${QXR_ROOT_PATH}/Samples/MixedReality/External/AppCommon/cpp)
add_library(qxr-app-common STATIC ${APPCOMMON_SOURCE_DIR}/AppCommon.cpp)
target_include_directories(qxr-app-common PUBLIC
        ${APPCOMMON_SOURCE_DIR}/)
target_link_libraries(qxr-app-common PRIVATE
        loader::openxr_loader
        EGL
        native_app_glue
        qxr-common-log
        qxr-thirdparty-glm
        )

add_library(mixedreality SHARED main.cpp)

set(LINKED_LIBRARIES
        mixedreality	
        android
        native_app_glue
        EGL
        GLESv3
        log
        loader::openxr_loader
        qxr-app-common
        qxr-common-gl
        qxr-common-data-structures
        qxr-thirdparty-tinyobj
        qxr-thirdparty-glm)        

target_link_libraries(${LINKED_LIBRARIES})
