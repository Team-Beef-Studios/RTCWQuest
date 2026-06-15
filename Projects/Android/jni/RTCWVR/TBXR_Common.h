/*
===========================================================================
TBXR_Common.h -- Team Beef OpenXR common layer (Android / Quest / Pico).

Android twin of code/vr/windows/TBXR_Common.h.  The engine-facing types and the
TBXR_ and VR_ prototype surface are kept identical to the Windows header so the
engine (cl_scrn.c, VrCommon.c, EFXR_SurfaceView.c) is platform-blind; only the
graphics binding differs: EGL + OpenGL ES 3 + XR_USE_PLATFORM_ANDROID instead of
WGL + desktop GL + XR_USE_PLATFORM_WIN32.

Modeled on JKXR (Projects/Android/jni/OpenJK/JKXR/android/TBXR_Common.cpp).
===========================================================================
*/
#if !defined(tbxr_common_h)
#define tbxr_common_h

#if defined(__ANDROID__)

#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <pthread.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>   // 3.1 for glGetTexLevelParameteriv + GL_TEXTURE_WIDTH/HEIGHT
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h> // GL_EXT_multisampled_render_to_texture proc typedefs

// Select the Android platform + GLES graphics binding before pulling the
// OpenXR platform structs (XrInstanceCreateInfoAndroidKHR,
// XrGraphicsBindingOpenGLESAndroidKHR, XrSwapchainImageOpenGLESKHR, ...).
#ifndef XR_USE_PLATFORM_ANDROID
#define XR_USE_PLATFORM_ANDROID
#endif
#ifndef XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_GRAPHICS_API_OPENGL_ES
#endif

#include <openxr.h>
#include <openxr_platform.h>
#include <openxr_helpers.h>

#endif // __ANDROID__

#ifndef NDEBUG
#define DEBUG 1
#endif

#define LOG_TAG "EFXR"

// During early VR bring-up (before Com_Init) Com_Printf is unavailable, so log
// straight to logcat.  Event-driven sites only (state changes, recenter, XR
// errors) -- not per-frame -- so this does not spam normal play.
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)

enum { ovrMaxLayerCount = 3 };
enum { ovrMaxNumEyes = 2 };

#ifndef RTCW_OPENXR_COMPAT_TYPES
typedef enum xrButton_ {
    xrButton_A = 0x00000001,
    xrButton_B = 0x00000002,
    xrButton_RThumb = 0x00000004,
    xrButton_RShoulder = 0x00000008,
    xrButton_X = 0x00000100,
    xrButton_Y = 0x00000200,
    xrButton_LThumb = 0x00000400,
    xrButton_LShoulder = 0x00000800,
    xrButton_Up = 0x00010000,
    xrButton_Down = 0x00020000,
    xrButton_Left = 0x00040000,
    xrButton_Right = 0x00080000,
    xrButton_Enter = 0x00100000,
    xrButton_Back = 0x00200000,
    xrButton_GripTrigger = 0x04000000,
    xrButton_Trigger = 0x20000000,
    xrButton_Joystick = 0x80000000,
    xrButton_ThumbRest = 0x00000010,
    xrButton_EnumSize = 0x7fffffff
} xrButton;

#define ovrButton_A xrButton_A
#define ovrButton_B xrButton_B
#define ovrButton_X xrButton_X
#define ovrButton_Y xrButton_Y
#define ovrButton_Enter xrButton_Enter
#define ovrButton_Back xrButton_Back
#define ovrButton_GripTrigger xrButton_GripTrigger
#define ovrButton_Trigger xrButton_Trigger
#define ovrButton_Joystick xrButton_Joystick

typedef XrVector2f ovrVector2f;
typedef XrVector3f ovrVector3f;
typedef XrQuaternionf ovrQuatf;
typedef uint64_t ovrDeviceID;

typedef struct {
    XrPosef Pose;
} ovrPoseStatef;

typedef struct {
    ovrPoseStatef HeadPose;
} ovrTracking;

typedef ovrTracking ovrTracking2;

typedef struct {
    XrVector2f LeftJoystick;
} ovrInputStateGamepad;

typedef struct {
    uint32_t Buttons;
    uint32_t Touches;
    float IndexTrigger;
    float GripTrigger;
    XrVector2f Joystick;
} ovrInputStateTrackedRemote;
#else
typedef uint32_t xrButton;
#endif

typedef struct {
    GLboolean Active;
    ovrPoseStatef HeadPose;
    uint32_t Status;
    XrPosef GripPose;
    XrSpaceVelocity Velocity;
} ovrTrackedController;

typedef enum control_scheme {
    RIGHT_HANDED_DEFAULT = 0,
    LEFT_HANDED_DEFAULT = 10,
    WEAPON_ALIGN = 99
} control_scheme_t;

typedef struct {
    float M[4][4];
} ovrMatrix4f;

typedef struct {
    XrSwapchain Handle;
    uint32_t Width;
    uint32_t Height;
} ovrSwapChain;

typedef struct {
    int Width;
    int Height;
    int Multisamples;

    uint32_t TextureSwapChainLength;
    uint32_t TextureSwapChainIndex;
    bool Acquired;
    ovrSwapChain ColorSwapChain;
    // GLES swapchain images (cf. the Windows ...OpenGLKHR variant).
    XrSwapchainImageOpenGLESKHR* ColorSwapChainImage;
    GLuint* DepthBuffers;
    GLuint* FrameBuffers;
} ovrFramebuffer;

typedef struct
{
    ovrFramebuffer  FrameBuffer[ovrMaxNumEyes];
    ovrFramebuffer  NullFrameBuffer; // black projection view when showing the quad layer
} ovrRenderer;

// EGL context (created off-screen; the eye FBOs are the XR swapchain images).
typedef struct {
    EGLint      MajorVersion;
    EGLint      MinorVersion;
    EGLDisplay  Display;
    EGLConfig   Config;
    EGLSurface  TinySurface;
    EGLSurface  MainSurface;
    EGLContext  Context;
} ovrEgl;

#define GL(func) func;

// Forward declaration
XrInstance TBXR_GetXrInstance();

__attribute__((unused)) static void
OXR_CheckErrors(XrInstance instance, XrResult result, const char* function, bool failOnError) {
    if (XR_FAILED(result)) {
        char errorBuffer[XR_MAX_RESULT_STRING_SIZE];
        xrResultToString(instance, result, errorBuffer);
        if (failOnError) {
            ALOGE("OpenXR error: %s: %s\n", function, errorBuffer);
        } else {
            ALOGV("OpenXR error: %s: %s\n", function, errorBuffer);
        }
    }
}

#define OXR(func) OXR_CheckErrors(TBXR_GetXrInstance(), func, #func, true);

typedef struct
{
    bool                Initialised;
    bool                Resumed;
    bool                Focused;
    bool                FrameSetup;
    uint32_t            FinishedEyeMask;

    float               Width;
    float               Height;

    XrInstance Instance;
    XrSession Session;
    XrViewConfigurationProperties ViewportConfig;
    XrViewConfigurationView ViewConfigurationView[ovrMaxNumEyes];
    XrSystemId SystemId;

    XrSpace LocalSpace;
    XrSpace ViewSpace;
    XrSpace StageSpace;

    GLboolean SessionActive;
    XrPosef xfStageFromHead;
    XrView* Views;

    int controllersPresent;
    float currentDisplayRefreshRate;
    float* SupportedDisplayRefreshRates;
    uint32_t RequestedDisplayRefreshRateIndex;
    uint32_t NumSupportedDisplayRefreshRates;
    PFN_xrGetDisplayRefreshRateFB pfnGetDisplayRefreshRate;
    PFN_xrRequestDisplayRefreshRateFB pfnRequestDisplayRefreshRate;

    XrFrameState        FrameState;
    int                 SwapInterval;
    int                 MainThreadTid;
    int                 RenderThreadTid;

    ovrRenderer         Renderer;
    ovrTrackedController TrackedController[2];

    // ---- Android-specific (cf. JKXR ovrApp) -------------------------------
    ovrEgl              Egl;
    ANativeWindow*      NativeWindow;
    bool                NativeWindowChanged;
    char                OpenXRHMD[64];   // "meta" / "pico" (from OPENXR_HMD env)
} ovrApp;

// ---- Android JNI / lifecycle plumbing (cf. JKXR ovrAppThread) --------------
enum
{
    MESSAGE_ON_CREATE,
    MESSAGE_ON_START,
    MESSAGE_ON_RESUME,
    MESSAGE_ON_PAUSE,
    MESSAGE_ON_STOP,
    MESSAGE_ON_DESTROY,
    MESSAGE_ON_SURFACE_CREATED,
    MESSAGE_ON_SURFACE_DESTROYED
};

typedef struct {
    JavaVM*     JavaVm;
    jobject     ActivityObject;
    jclass      ActivityClass;
    pthread_t   Thread;
    ANativeWindow* NativeWindow;
    bool        Resumed;
} ovrAppThread;

extern ovrApp gAppState;
extern ovrAppThread gAppThread;

void ovrTrackedController_Clear(ovrTrackedController* controller);

void GlInitExtensions();   // no-op on Android (GLES3 FBO entry points are core)

void * AppThreadFunction(void * parm );

// Functions implemented by the engine-specific glue (EFXR_SurfaceView.c)
void VR_FrameSetup();
qboolean VR_UseScreenLayer();
float VR_GetScreenLayerDistance();
qboolean VR_GetFovTangents(float *tanLeft, float *tanRight, float *tanUp, float *tanDown);
qboolean VR_GetFovTangentsForEye(int eye, float *tanLeft, float *tanRight, float *tanUp, float *tanDown);
void VR_HandleControllerInput();
void VR_SetHMDOrientation(float pitch, float yaw, float roll );
void VR_SetHMDPosition(float x, float y, float z );
void VR_HapticEvent(const char* event, int position, int flags, int intensity, float angle, float yHeight );
void VR_HapticUpdateEvent(const char* event, int intensity, float angle );
void VR_HapticEndFrame();
void VR_HapticStopEvent(const char* event);
void VR_HapticEnable();
void VR_HapticDisable();
void TBXR_GetHapticState(int hand, float *duration, float *intensity);
void TBXR_ClearHapticState(int hand);

// Engine glue provided by EFXR_SurfaceView.c
void EFXR_GetScreenResolution(int *width, int *height);
void EFXR_SwapWindow();

// Reusable Team Beef OpenXR stuff (in TBXR_Common.c)
double TBXR_GetTimeInMilliSeconds();
int TBXR_GetRefresh();
void TBXR_Recenter();
void TBXR_InitialiseOpenXR();
void TBXR_WaitForSessionActive();
void TBXR_InitRenderer();
void TBXR_LeaveVR();
void TBXR_EnterVR();
void TBXR_GetScreenRes(int *width, int *height);
void TBXR_InitActions( void );
void TBXR_DestroyActions( void );
void TBXR_DestroySessionForReinit( void );
void *TBXR_GetCurrentGLContext( void );
void TBXR_Vibrate(int duration, int channel, float intensity );
void TBXR_ProcessHaptics();
void TBXR_FrameSetup();
void TBXR_updateProjections();
void TBXR_UpdateControllers( );
void TBXR_prepareEyeBuffer(int eye);
void TBXR_finishEyeBuffer(int eye);
void TBXR_submitFrame();

#define VIVE_CONTROLLERS 10
#define INDEX_CONTROLLERS 11
#define PICO_CONTROLLERS 12
#define TOUCH_CONTROLLERS 13

#endif //tbxr_common_h
