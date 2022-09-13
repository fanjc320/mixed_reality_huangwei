/****************************************************************
 * Copyright (c) 2020-2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <android/log.h>
#include <android/looper.h>
#include <android_native_app_glue.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <jni.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include "AppCommon.h"
#include "Geometry.h"
#include "KtxLoader.h"
#include "Shader.h"

#define LOGI(...)                                                              \
    ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGW(...)                                                              \
    ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#define LOGE(...)                                                              \
    ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

#define EGL_SAMPLE_COUNT 4
#define CUBE_COUNT 3 

static int engine_init_xr_swapchains(struct engine *engine);

glm::vec3 CUBE_COLORS[CUBE_COUNT] = {{0.16f, 0.32f, 0.85f},
                                     {1.0f, 0.8f, 0.5f},
                                     {0.80f, 0.57f, 0.84f}};

void CheckGlError(const char *file, int line)
{
    for (GLint error = glGetError(); error; error = glGetError()) {
        char *pError;
        switch (error) {
        case GL_NO_ERROR:
            pError = (char *)"GL_NO_ERROR";
            break;
        case GL_INVALID_ENUM:
            pError = (char *)"GL_INVALID_ENUM";
            break;
        case GL_INVALID_VALUE:
            pError = (char *)"GL_INVALID_VALUE";
            break;
        case GL_INVALID_OPERATION:
            pError = (char *)"GL_INVALID_OPERATION";
            break;
        case GL_OUT_OF_MEMORY:
            pError = (char *)"GL_OUT_OF_MEMORY";
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            pError = (char *)"GL_INVALID_FRAMEBUFFER_OPERATION";
            break;

        default:
            LOGE("glError (0x%x) %s:%d\n", error, file, line);
            return;
        }

        LOGE("glError (%s) %s:%d\n", pError, file, line);
        return;
    }
    return;
}

#define GL(func)                                                               \
    func;                                                                      \
    CheckGlError(__FILE__, __LINE__)

struct Swapchain : public AppCommon::Swapchain {
    std::vector<GLuint> fbos;
    std::vector<GLuint> dbos;
};

struct StereoSwapchain {
    std::vector<Swapchain> eyeSwapchain;
};

/**
 * Shared state for our app.
 */
struct engine : public AppCommon::base_engine {
    // render target width
    uint32_t width;

    // render target height
    uint32_t height;

    // <sample count, stereo swapchain>
    std::unordered_map<uint32_t, StereoSwapchain> swapchainMap;

    // cube geometry
    QtiGL::Geometry cube;

    // cube shader
    QtiGL::Shader *cubeShader;

    // cube texture
    GLuint cubeTexture;

    // cube position
    std::vector<glm::mat4> cubeMatrices;

    // cube colors
    std::vector<glm::vec3> cubeColors;

    // max sample count
    GLint maxSampleCount;

    // current sample count
    GLint currentSampleCount;

    engine()
            : width(0), height(0), cubeShader(nullptr), cubeTexture(0),
              maxSampleCount(4), currentSampleCount(4)
    {
    }
};

/**
 * Initializes OpenXR
 */
static int engine_init_openxr(struct engine *engine)
{
    AppCommon::app_query_layers_and_extensions(engine);

    XrInstanceCreateInfoAndroidKHR instanceCreateInfoAndroidKHR;
    instanceCreateInfoAndroidKHR.type =
            XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR;
    instanceCreateInfoAndroidKHR.next = nullptr;
    instanceCreateInfoAndroidKHR.applicationVM =
            (void *)engine->app->activity->vm;
    instanceCreateInfoAndroidKHR.applicationActivity =
            (void *)engine->app->activity->clazz;

    // TODO: should not hard-code these ideally
    const char *const enabledExtensions[] = {
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
            XR_KHR_COMPOSITION_LAYER_EQUIRECT_EXTENSION_NAME,
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};

    XrInstanceCreateInfo instanceCreateInfo = {
            .type = XR_TYPE_INSTANCE_CREATE_INFO,
            .next = &instanceCreateInfoAndroidKHR,
            .createFlags = 0,
            .enabledExtensionCount =
                    sizeof(enabledExtensions) / sizeof(*enabledExtensions),
            .enabledExtensionNames = enabledExtensions,
            .enabledApiLayerCount = 0,
            .applicationInfo =
                    {
                            .applicationName = "OpenXR MSAA Sample",
                            .engineName = "",
                            .applicationVersion = 1,
                            .engineVersion = 0,
                            .apiVersion = XR_CURRENT_API_VERSION,
                    },
    };
    AppCommon::app_create_instance(&instanceCreateInfo, engine);

    AppCommon::app_get_system_prop(engine);

    AppCommon::app_enum_view_configuration(engine);
    engine->width = engine->state.viewConfigs[0].recommendedImageRectWidth / 4;
    engine->height =
            engine->state.viewConfigs[0].recommendedImageRectHeight / 4;

    // Create XR session
    assert(!engine->state.xrSession);
    XrGraphicsBindingOpenGLESAndroidKHR gfxBinding = {
            .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
            .next = nullptr,
            .display = engine->display,
            .config = engine->config,
            .context = engine->context};
    XrSessionCreateInfo createInfo = {.type = XR_TYPE_SESSION_CREATE_INFO,
                                      .next = &gfxBinding,
                                      .systemId = engine->state.xrSysId};
    AppCommon::app_create_session(&createInfo, engine);

    // initialize space
    XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{
            XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    referenceSpaceCreateInfo.poseInReferenceSpace = idPose;
    referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    AppCommon::app_create_space(&referenceSpaceCreateInfo, engine);

    XrReferenceSpaceCreateInfo viewSpaceInfo{
            XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    viewSpaceInfo.poseInReferenceSpace = idPose;
    viewSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    AppCommon::app_create_space(&viewSpaceInfo, engine);

    return engine_init_xr_swapchains(engine);

}

/**
 * Shutdown OpenXR
 */
static void engine_shutdown_openxr(struct engine *engine)
{
    XrResult result = AppCommon::app_destroy_space(engine->state.xrLocalSpace);
    if (XR_SUCCESS != result) {
        LOGW("Destroy XR local space failed: %d", result);
        assert(0);
    }

    result = AppCommon::app_destroy_space(engine->state.xrViewSpace);
    if (XR_SUCCESS != result) {
        LOGW("Destroy XR view space failed: %d", result);
        assert(0);
    }

    result = AppCommon::app_destroy_session(engine->state.xrSession);
    if (XR_SUCCESS != result) {
        LOGW("Destroy XR session failed: %d", result);
        assert(0);
    }

    result = AppCommon::app_destroy_instance(engine);
    if (XR_SUCCESS != result) {
        LOGW("Destroy XR instance failed: %d", result);
        assert(0);
    }
}

/**
 * Create XR swapchains
 */
static int engine_init_xr_swapchains(struct engine *engine)
{
    AppCommon::app_enum_sc_format(engine);

    // Looking for multisample extension
    PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC
            glFramebufferTexture2DMultisampleEXT = nullptr;
    glFramebufferTexture2DMultisampleEXT =
            (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEIMGPROC)eglGetProcAddress(
                    "glFramebufferTexture2DMultisampleEXT");
    if (!glFramebufferTexture2DMultisampleEXT) {
        LOGW("Couldn't get function pointer to "
             "glFramebufferTexture2DMultisampleEXT()!");
        assert(0);
        return 1;
    }

    PFNGLRENDERBUFFERSTORAGEMULTISAMPLEIMGPROC
            glRenderbufferStorageMultisampleEXT =
                    (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEIMGPROC)
                            eglGetProcAddress(
                                    "glRenderbufferStorageMultisampleEXT");
    if (!glRenderbufferStorageMultisampleEXT) {
        LOGE("Couldn't get function pointer to "
             "glRenderbufferStorageMultisampleEXT()!");
        return false;
    }

    // Create swapchain with different sample count and cached in map
	uint32_t samples = engine->currentSampleCount;
    {
        StereoSwapchain stereoSwapchain;
        stereoSwapchain.eyeSwapchain.resize(engine->state.viewCount);
        std::vector<uint32_t> swapchainLengths;
        swapchainLengths.resize(engine->state.viewCount);
        uint32_t maxSwapchainLength = 0;

        for (uint32_t eye = 0; eye < engine->state.viewCount; ++eye) {
            auto &swapchain = stereoSwapchain.eyeSwapchain[eye];
            XrSwapchainCreateInfo swapchainCreateInfo = {
                    .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
                    .usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                                  XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
                    .createFlags = 0,
                    .format = GL_RGBA8,
                    .sampleCount = samples,
                    .width = engine->width,
                    .height = engine->height,
                    .faceCount = 1,
                    .arraySize = 1,
                    .mipCount = 1,
                    .next = nullptr,
            };

            XrResult result = xrCreateSwapchain(engine->state.xrSession,
                                                &swapchainCreateInfo,
                                                &swapchain.xrSwapchain);
            if (XR_FAILED(result)) {
                LOGW("xrCreateSwapchain failed");
                assert(0);
                return 1;
            }

            result = xrEnumerateSwapchainImages(
                    swapchain.xrSwapchain, 0, &swapchainLengths[eye], nullptr);
            if (XR_FAILED(result)) {
                LOGW("xrEnumerateSwapchainImages failed");
                assert(0);
                return 1;
            }

            if (swapchainLengths[eye] > maxSwapchainLength)
                maxSwapchainLength = swapchainLengths[eye];

            swapchain.xrImages.resize(swapchainLengths[eye],
                                      {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
            swapchain.fbos.resize(swapchainLengths[eye]);
            swapchain.dbos.resize(swapchainLengths[eye]);

            result = xrEnumerateSwapchainImages(
                    swapchain.xrSwapchain, swapchainLengths[eye],
                    &swapchainLengths[eye],
                    (XrSwapchainImageBaseHeader *)&swapchain.xrImages[0]);

            if (XR_SUCCESS != result) {
                LOGW("xrEnumerateSwapchainImages failed");
                assert(0);
                return 1;
            }

            for (uint32_t index = 0; index < swapchainLengths[eye]; ++index) {
                // Create depth buffer
                GL(glGenRenderbuffers(1, &swapchain.dbos[index]));
                GL(glBindRenderbuffer(GL_RENDERBUFFER, swapchain.dbos[index]));
                GL(glRenderbufferStorageMultisampleEXT(
                        GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT16,
                        engine->width, engine->height));
                GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

                // Create MSAA fbo
                GL(glGenFramebuffers(1, &swapchain.fbos[index]));
                GL(glBindFramebuffer(GL_FRAMEBUFFER, swapchain.fbos[index]));
                GL(glFramebufferRenderbuffer(
                        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                        swapchain.dbos[index]));
                LOGI("glFramebufferTexture2DMultisampleEXT index:%d sample: %d",
                     index, samples);
                GL(glFramebufferTexture2DMultisampleEXT(
                        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                        swapchain.xrImages[index].image, 0, samples));

                GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                // check frame buffer status
                if (status != GL_FRAMEBUFFER_COMPLETE) {
                    LOGE("framebuffer is incomplete, status: %d! Error code %d",
                         status, glGetError());
                    assert(0);
                    return 1;
                }
            }
        }

        LOGI("Insert to table, sample :%d", samples);
        engine->swapchainMap[samples] = std::move(stereoSwapchain);
    }

    return 0;
}

/**
 * Destroy XR swapchains
 */
static int engine_destroy_xr_swapchains(struct engine *engine)
{
    for (auto it = engine->swapchainMap.begin();
         it != engine->swapchainMap.end(); ++it) {
        for (auto &swapchain : it->second.eyeSwapchain) {
            GL(glDeleteFramebuffers(swapchain.fbos.size(),
                                    swapchain.fbos.data()));
            GL(glDeleteRenderbuffers(swapchain.dbos.size(),
                                     swapchain.dbos.data()));
            if (XR_FAILED(xrDestroySwapchain(swapchain.xrSwapchain))) {
                LOGW("xrDestroySwapchain failed");
                assert(0);
                return 1;
            }
        }
    }
    engine->swapchainMap.clear();

    return 0;
}

/**
 * Initialize an EGL context for the current display.
 */
static int engine_init_display(struct engine *engine)
{
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLint numConfig;

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(display != EGL_NO_DISPLAY);

    EGLBoolean res = eglInitialize(display, nullptr, nullptr);
    assert(res == EGL_TRUE);

    EGLint const configAttrs[] = {
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            // This is tricky, you can't use glGetIntegerv until egl context was
            // created. Maybe create a temp context and get max sample count
            // first.
            EGL_SAMPLES, EGL_SAMPLE_COUNT, EGL_NONE};

    res = eglChooseConfig(display, configAttrs, &config, 1, &numConfig);
    LOGI("numConfig: %d", numConfig);
    assert(res == EGL_TRUE);

    EGLint const contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3,
                                     EGL_PROTECTED_CONTENT_EXT, false,
                                     EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    assert(context != EGL_NO_CONTEXT);

    res = eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
    assert(res == EGL_TRUE);

    engine->config = config;
    engine->context = context;
    engine->display = display;

    return 0;
}

/**
 * Render scene.
 */
static void engine_draw_frame(struct engine *engine,
                              const uint32_t viewIndex,
                              const uint32_t imgIndex,
                              const XrView &xrView)
{
    assert(engine->display);
    auto &stereoSwapchain = engine->swapchainMap[engine->currentSampleCount];
    auto &swapchain = stereoSwapchain.eyeSwapchain[viewIndex];
    GL(glBindFramebuffer(GL_FRAMEBUFFER, swapchain.fbos[imgIndex]));

    GL(glEnable(GL_SCISSOR_TEST));
    GL(glEnable(GL_DEPTH_TEST));
    GL(glEnable(GL_CULL_FACE));
    GL(glDepthFunc(GL_LESS));
    GL(glDepthMask(GL_TRUE));

    GL(glViewport(0, 0, engine->width, engine->height));
    GL(glScissor(0, 0, engine->width, engine->height));
    GL(glClearColor(0.1f, 0.1f, 0.1f, 0.0f));
    GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    glm::mat4 eyeProjMat, eyeViewMat;

    XrMatrix4x4f result;
    XrMatrix4x4f_CreateProjectionFov(&result, GRAPHICS_OPENGL_ES, xrView.fov,
                                     0.05f, 100.f);
    AppCommon::array2matrix(result, eyeProjMat);
    glm::mat4 rot = glm::mat4_cast(
            glm::fquat(xrView.pose.orientation.w, xrView.pose.orientation.x,
                       xrView.pose.orientation.y, xrView.pose.orientation.z));
    glm::mat4 trans =
            glm::translate(glm::mat4(1.0f), glm::vec3(xrView.pose.position.x,
                                                      xrView.pose.position.y,
                                                      xrView.pose.position.z));
    eyeViewMat = trans * rot;
    eyeViewMat = glm::inverse(eyeViewMat);

    engine->cubeShader->Bind();
    engine->cubeShader->SetUniformMat4("projectionMatrix", eyeProjMat);
    engine->cubeShader->SetUniformMat4("viewMatrix", eyeViewMat);
    engine->cubeShader->SetUniformSampler("srcTex", engine->cubeTexture,
                                          GL_TEXTURE_2D, 0);
    glm::vec3 eyePos =
            glm::vec3(-eyeViewMat[3][0], -eyeViewMat[3][1], -eyeViewMat[3][2]);
    engine->cubeShader->SetUniformVec3("eyePos", eyePos);

    for (size_t i = 0; i < engine->cubeMatrices.size(); ++i) {
        engine->cubeShader->SetUniformMat4("modelMatrix",
                                           engine->cubeMatrices[i]);
        engine->cubeShader->SetUniformVec3("modelColor", engine->cubeColors[i]);
        engine->cube.Submit();
    }

    engine->cubeShader->Unbind();
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

static std::string read_text_file(const std::string &file)
{
    std::stringstream ss;
    std::ifstream ifs(file);
    if (ifs) {
        ss << ifs.rdbuf();
    } else {
        LOGW("Read %s failed", file.c_str());
    }

    return ss.str();
}

static void engine_create_cube(QtiGL::Geometry &geometry, float width)
{
    // Create attributes of the cube
    unsigned int numElementsPerVert = 8;
    int stride = (int)(numElementsPerVert * sizeof(float));

    std::vector<QtiGL::ProgramAttribute> attribs = {
            {QtiGL::kPosition, 3, GL_FLOAT, false, stride, 0},
            {QtiGL::kNormal, 3, GL_FLOAT, false, stride, 3 * sizeof(float)},
            {QtiGL::kTexcoord0, 2, GL_FLOAT, false, stride, 6 * sizeof(float)}};

    float halfWidth = width / 2.0f;

    // Create vertex data of the cube with position and normal data
    float cubeVerts[] = {
            // Front
            halfWidth, halfWidth, halfWidth, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
            -halfWidth, halfWidth, halfWidth, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
            -halfWidth, -halfWidth, halfWidth, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            halfWidth, -halfWidth, halfWidth, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
            // Right
            halfWidth, halfWidth, halfWidth, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            halfWidth, -halfWidth, halfWidth, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            halfWidth, -halfWidth, -halfWidth, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            halfWidth, halfWidth, -halfWidth, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
            // Top
            halfWidth, halfWidth, halfWidth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
            halfWidth, halfWidth, -halfWidth, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
            -halfWidth, halfWidth, -halfWidth, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            -halfWidth, halfWidth, halfWidth, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            // Left
            -halfWidth, halfWidth, halfWidth, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
            -halfWidth, halfWidth, -halfWidth, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            -halfWidth, -halfWidth, -halfWidth, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            -halfWidth, -halfWidth, halfWidth, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            // Bottom
            -halfWidth, -halfWidth, -halfWidth, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
            halfWidth, -halfWidth, -halfWidth, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
            halfWidth, -halfWidth, halfWidth, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,
            -halfWidth, -halfWidth, halfWidth, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,
            // Back
            halfWidth, -halfWidth, -halfWidth, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
            -halfWidth, -halfWidth, -halfWidth, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,
            -halfWidth, halfWidth, -halfWidth, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,
            halfWidth, halfWidth, -halfWidth, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f

    };
    int numCubeVerts = 24;

    // Create index data of the cube
    unsigned int cubeIndices[] = {// Front
                                  0, 1, 2, 2, 3, 0,
                                  // Right
                                  4, 5, 6, 6, 7, 4,
                                  // Top
                                  8, 9, 10, 10, 11, 8,
                                  // Left
                                  12, 13, 14, 14, 15, 12,
                                  // Bottom
                                  16, 17, 18, 18, 19, 16,
                                  // Back
                                  20, 21, 22, 22, 23, 20

    };
    int numCubeIndices = 36;

    geometry.Initialize(attribs.data(), attribs.size(), cubeIndices,
                        numCubeIndices, cubeVerts,
                        numCubeVerts * numElementsPerVert * sizeof(float),
                        numCubeVerts);
}

/**
 * Init resources for rendering scene
 */
static int engine_init_scene_resources(struct engine *engine)
{
    std::string externalDir =
            std::string(engine->app->activity->externalDataPath);

    // load shader
    {
        std::string vsFilePath = externalDir + "/model_v.glsl";
        std::string fsFilePath = externalDir + "/model_f.glsl";
        // load shader sources
        std::string vsSource = read_text_file(vsFilePath);
        if (vsSource.length() <= 0) {
            return 1;
        }

        std::string fsSource = read_text_file(fsFilePath);
        if (fsSource.length() <= 0) {
            return 1;
        }

        engine->cubeShader = new QtiGL::Shader();
        std::vector<const char *> vs = {vsSource.c_str()};
        std::vector<const char *> fs = {fsSource.c_str()};
        if (!engine->cubeShader->Initialize(vs.size(), vs.data(), fs.size(),
                                            fs.data(), vsFilePath.c_str(),
                                            fsFilePath.c_str())) {
            return 1;
        }
    }

    // load texture
    {
        std::string textureFilePath = externalDir + "/white.ktx";
        std::ifstream ifs(textureFilePath);
        if (!ifs) {
            return false;
        }

        std::streampos texBuffSize = ifs.tellg();
        ifs.seekg(0, std::ios::end);
        texBuffSize = ifs.tellg() - texBuffSize;
        ifs.seekg(0);

        std::vector<char> data(texBuffSize);
        ifs.read(data.data(), data.size());
        ifs.close();

        GLenum texTarget;
        QtiGL::KtxTexture texHelper;
        QtiGL::TKTXHeader pOutHeader;
        QtiGL::TKTXErrorCode result = texHelper.LoadKtxFromBuffer(
                data.data(), data.size(), &engine->cubeTexture, &texTarget,
                &pOutHeader, false);
        LOGI("texture width: %d, height: %d", pOutHeader.pixelWidth,
             pOutHeader.pixelHeight);

        if (result != QtiGL::KTX_SUCCESS || 0 == engine->cubeTexture) {
            return 1;
        }
    }

    // cube geometry
    engine_create_cube(engine->cube, 0.3f);

    // Create cube sea around origin
    float xPos = -(CUBE_COUNT / 2);
    float yPos = -(CUBE_COUNT / 2);
    float zPos = -(CUBE_COUNT / 2);

    // rotate 45 degrees along y axis so we can see the edge
    glm::mat4 rotationMat =
            glm::rotate(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    for (int z = 0; z < CUBE_COUNT; ++z) {
        for (int y = 0; y < CUBE_COUNT; ++y) {
            for (int x = 0; x < CUBE_COUNT; ++x) {
                engine->cubeMatrices.push_back(
                        glm::translate(
                                glm::mat4(1.0f),
                                glm::vec3(xPos + x, yPos + y, zPos + z)) *
                        rotationMat);
                engine->cubeColors.push_back(CUBE_COLORS[y]);
            }
        }
    }

    return 0;
}

/**
 * Destroys resources for rendering scene
 */
static void engine_destroy_scene_resources(struct engine *engine)
{
    engine->cube.Destroy();

    engine->cubeShader->Destroy();
    delete engine->cubeShader;
    engine->cubeShader = nullptr;

    glDeleteTextures(1, &engine->cubeTexture);
    engine->cubeTexture = 0;
}

void android_main(struct android_app *state)
{
    struct engine engine;

    state->userData = &engine;
    state->onAppCmd = AppCommon::app_handle_cmd;
    state->onInputEvent = nullptr;
    engine.app = state;

    if (engine_init_display(&engine) != 0) {
        LOGW("Failed to create EGL resources");
        return;
    }

    glGetIntegerv(GL_MAX_SAMPLES, &engine.maxSampleCount);
    LOGI("Max sample count: %d", engine.maxSampleCount);
	if(engine.maxSampleCount < 4)
	{
		engine.currentSampleCount = engine.maxSampleCount;
        LOGW("maxSampleCount < 4. Render quality may be impacted ... ");
	}


    if (engine_init_scene_resources(&engine) != 0) {
        LOGW("Failed to load scene resources!  Exiting");
        return;
    }

    AppCommon::app_wait_window((AppCommon::base_engine *)&engine);
    engine_init_openxr(&engine);

    while (1) {
        // Read all pending events.
        int ident;
        int events;
        struct android_poll_source *source;

        // If not ready, we will block forever waiting for events.
        // If ready, we loop until all events are read, then continue
        // to draw the next frame of animation.
        while ((ident = ALooper_pollAll(engine.ready ? 0 : -1, nullptr, &events,
                                        (void **)&source)) >= 0) {

            // Process this event.
            if (source != nullptr) {
                source->process(state, source);
            }

            AppCommon::app_poll_events(&engine);
            if (!engine.state.Resumed && !engine.has_exit_sess_once) {
                AppCommon::app_req_exit_session(&engine);
                engine.has_exit_sess_once = true;
            }

            // Check if we are exiting.
            if (state->destroyRequested != 0) {
                engine_destroy_xr_swapchains(&engine);
                engine_shutdown_openxr(&engine);
                engine_destroy_scene_resources(&engine);
                AppCommon::app_term_display(&engine);
                return;
            }
        }

        if (!engine.ready) {
            continue;
        }

        XrFrameState frameState = {.type = XR_TYPE_FRAME_STATE,
                                   .next = nullptr};
        XrFrameWaitInfo frameWaitInfo = {.type = XR_TYPE_FRAME_WAIT_INFO,
                                         .next = nullptr};
        XrResult result = xrWaitFrame(engine.state.xrSession, &frameWaitInfo,
                                      &frameState);
        if (XR_FAILED(result)) {
            LOGW("xrWaitFrame failed");
            continue;
        }

        XrFrameBeginInfo frameBeginInfo = {.type = XR_TYPE_FRAME_BEGIN_INFO,
                                           .next = nullptr};
        result = xrBeginFrame(engine.state.xrSession, &frameBeginInfo);

        if (XR_FAILED(result)) {
            LOGW("xrBeginFrame failed");
            continue;
        }

        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCapacityInput = (uint32_t)engine.state.m_views.size();
        uint32_t viewCountOutput;

        XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        viewLocateInfo.viewConfigurationType =
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        viewLocateInfo.displayTime = frameState.predictedDisplayTime;
        viewLocateInfo.space = engine.state.xrLocalSpace;
        result = xrLocateViews(engine.state.xrSession, &viewLocateInfo,
                               &viewState, viewCapacityInput, &viewCountOutput,
                               engine.state.m_views.data());
        if (XR_FAILED(result)) {
            LOGW("xrLocateViews failed");
        }

        XrCompositionLayerProjectionView
                projectionViews[engine.state.viewCount];
        auto &stereoSwapchain = engine.swapchainMap[engine.currentSampleCount];
        for (uint32_t i = 0; i < engine.state.viewCount; ++i) {
            auto &swapchain = stereoSwapchain.eyeSwapchain[i];
            XrSwapchainImageAcquireInfo swapchainImageAcquireInfo = {
                    .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
                    .next = nullptr};
            uint32_t bufferIndex;
            result = xrAcquireSwapchainImage(swapchain.xrSwapchain,
                                             &swapchainImageAcquireInfo,
                                             &bufferIndex);

            if (XR_FAILED(result)) {
                LOGW("xrAcquireSwapchainImage failed");
            }

            XrSwapchainImageWaitInfo swapchainImageWaitInfo = {
                    .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
                    .next = nullptr,
                    .timeout = 1000};
            result = xrWaitSwapchainImage(swapchain.xrSwapchain,
                                          &swapchainImageWaitInfo);

            if (XR_FAILED(result)) {
                LOGW("xrWaitSwapchainImage failed");
            }

            // NOTE: since xrLocateViews is not implemented (neither is
            // tracking) yet... we hard-code some things in the following
            projectionViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projectionViews[i].next = nullptr;
            projectionViews[i].pose = engine.state.m_views[i].pose;
            projectionViews[i].fov = engine.state.m_views[i].fov;
            projectionViews[i].subImage.swapchain = swapchain.xrSwapchain;
            projectionViews[i].subImage.imageArrayIndex = 0;
            projectionViews[i].subImage.imageRect.offset.x = 0;
            projectionViews[i].subImage.imageRect.offset.y = 0;
            projectionViews[i].subImage.imageRect.extent.width = engine.width;
            projectionViews[i].subImage.imageRect.extent.height = engine.height;

            // Draw scene
            engine_draw_frame(&engine, i, bufferIndex, engine.state.m_views[i]);

            XrSwapchainImageReleaseInfo swapchainImageReleaseInfo = {
                    .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
                    .next = nullptr};
            result = xrReleaseSwapchainImage(swapchain.xrSwapchain,
                                             &swapchainImageReleaseInfo);

            if (XR_FAILED(result)) {
                LOGW("xrReleaseSwapchainImage failed");
            }
        }

        glFlush();

        XrCompositionLayerProjection projectionLayer = {
                .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
                .next = nullptr,
                .layerFlags =
                        XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT |
                        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                .space = engine.state.xrLocalSpace,
                .viewCount = engine.state.viewCount,
                .views = projectionViews,
        };

        const XrCompositionLayerBaseHeader *const layers[] = {
                (const XrCompositionLayerBaseHeader *const) & projectionLayer,
        };

        XrFrameEndInfo frameEndInfo = {
                .type = XR_TYPE_FRAME_END_INFO,
                .displayTime = frameState.predictedDisplayTime,
                .layerCount = sizeof(layers) / sizeof(layers[0]),
                .layers = layers,
                .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,
                .next = nullptr};

        result = xrEndFrame(engine.state.xrSession, &frameEndInfo);

        if (XR_FAILED(result)) {
            LOGW("xrEndFrame failed");
        }
    }
}
