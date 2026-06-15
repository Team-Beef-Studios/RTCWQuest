/*
===========================================================================
OpenXrInput.c -- OpenXR action set, bindings, and per-frame controller state
reading for the ioEF Elite Force VR port.

Ported/adapted from RealRTCWXR (code/RealRTCWXR/RealRTCWXR/OpenXrInput.c).
Trimmed to the FOCUSED ioEF input scope: thumbsticks, triggers, grip/squeeze,
the face buttons (A/B right, X/Y left), and thumbstick-click -- enough for
movement, turning, and shoot / jump / use / crouch.  RTCW-only actions
(trackpad force, battery, thumbrest, separate touch booleans we don't read,
the Vive trackpad gameplay mapping) are dropped.

This file implements:
  TBXR_InitActions()       -- create the action set, suggest bindings for the
                              common interaction profiles, attach to session.
  TBXR_UpdateControllers() -- locate the controller poses + xrSyncActions, then
                              fill the left/right ovrInputStateTrackedRemote
                              globals (Buttons bitmask / IndexTrigger /
                              GripTrigger / Joystick) read by VrInputDefault.

Engine-side only; no DLL / vr_client_info_t contract change.
===========================================================================
*/

#include "VrCommon.h"
#include "TBXR_Common.h"
#include "VrCvars.h"

extern ovrApp gAppState;

/* The controller-state globals filled here (defined in VrInputCommon.c). */
extern ovrInputStateTrackedRemote leftTrackedRemoteState_new;
extern ovrInputStateTrackedRemote rightTrackedRemoteState_new;
extern ovrTracking leftRemoteTracking_new;
extern ovrTracking rightRemoteTracking_new;

static XrResult CheckXrResult(XrResult res, const char* originator) {
    if (XR_FAILED(res)) {
        Com_Printf("OpenXR input error: %s\n", originator);
    }
    return res;
}

#define CHECK_XRCMD(cmd) CheckXrResult(cmd, #cmd);

#define SIDE_LEFT 0
#define SIDE_RIGHT 1
#define SIDE_COUNT 2

static XrActionSet actionSet = NULL;

static XrAction gripAction;          /* grip pose  */
static XrAction aimAction;           /* aim pose   */
static XrAction vibrateAction;       /* haptics    */

static XrAction triggerAction;       /* trigger float (both hands)     */
static XrAction triggerTouchAction;
static XrAction squeezeAction;       /* grip/squeeze float (both)      */
static XrAction squeezeClickAction;  /* grip click (Vive/simple)       */

static XrAction thumbstickAction;        /* vector2 axis (both hands)  */
static XrAction thumbstickClickAction;   /* stick click (both)         */
static XrAction thumbstickTouchAction;

static XrAction AAction;   /* right A */
static XrAction BAction;   /* right B */
static XrAction XAction;   /* left X  */
static XrAction YAction;   /* left Y  */
static XrAction backAction; /* menu/system button */

static XrSpace aimSpace[SIDE_COUNT];
static XrSpace handSpace[SIDE_COUNT];
static XrPath handSubactionPath[SIDE_COUNT];


static XrActionSuggestedBinding ActionSuggestedBinding(XrAction action, XrPath path) {
    XrActionSuggestedBinding asb;
    asb.action = action;
    asb.binding = path;
    return asb;
}

static XrActionStateBoolean GetActionStateBoolean(XrAction action, int hand) {
    XrActionStateGetInfo getInfo = {0};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;
    getInfo.next = NULL;
    if (hand >= 0)
        getInfo.subactionPath = handSubactionPath[hand];

    XrActionStateBoolean state = {0};
    state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    state.next = NULL;
    xrGetActionStateBoolean(gAppState.Session, &getInfo, &state);
    return state;
}

static XrActionStateFloat GetActionStateFloat(XrAction action, int hand) {
    XrActionStateGetInfo getInfo = {0};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;
    getInfo.next = NULL;
    if (hand >= 0)
        getInfo.subactionPath = handSubactionPath[hand];

    XrActionStateFloat state = {0};
    state.type = XR_TYPE_ACTION_STATE_FLOAT;
    state.next = NULL;
    xrGetActionStateFloat(gAppState.Session, &getInfo, &state);
    return state;
}

static XrActionStateVector2f GetActionStateVector2(XrAction action, int hand) {
    XrActionStateGetInfo getInfo = {0};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action = action;
    getInfo.next = NULL;
    if (hand >= 0)
        getInfo.subactionPath = handSubactionPath[hand];

    XrActionStateVector2f state = {0};
    state.type = XR_TYPE_ACTION_STATE_VECTOR2F;
    state.next = NULL;
    xrGetActionStateVector2f(gAppState.Session, &getInfo, &state);
    return state;
}

static void CreateAction(
        XrActionSet set,
        XrActionType type,
        const char* actionName,
        const char* localizedName,
        int countSubactionPaths,
        XrPath* subactionPaths,
        XrAction* action) {
    XrActionCreateInfo aci = {0};
    aci.type = XR_TYPE_ACTION_CREATE_INFO;
    aci.next = NULL;
    aci.actionType = type;
    if (countSubactionPaths > 0) {
        aci.countSubactionPaths = countSubactionPaths;
        aci.subactionPaths = subactionPaths;
    }
    strcpy(aci.actionName, actionName);
    strcpy(aci.localizedActionName, localizedName ? localizedName : actionName);
    *action = XR_NULL_HANDLE;
    OXR(xrCreateAction(set, &aci, action));
}

// Destroy the action set (and, with it, its child actions).  Called when the
// session is being torn down for a GL-context-change rebuild so TBXR_InitActions
// can create a fresh action set + re-attach it to the new session.
void TBXR_DestroyActions( void )
{
    if (actionSet != NULL) {
        xrDestroyActionSet(actionSet);
        actionSet = NULL;
    }
}

void TBXR_InitActions( void )
{
    if (gAppState.Session == XR_NULL_HANDLE) {
        return;
    }

    // Create the action set.
    {
        XrActionSetCreateInfo actionSetInfo = {0};
        actionSetInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
        strcpy(actionSetInfo.actionSetName, "gameplay");
        strcpy(actionSetInfo.localizedActionSetName, "Gameplay");
        actionSetInfo.priority = 0;
        actionSetInfo.next = NULL;
        CHECK_XRCMD(xrCreateActionSet(gAppState.Instance, &actionSetInfo, &actionSet));
    }

    // Subaction paths for the two hands.
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left", &handSubactionPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right", &handSubactionPath[SIDE_RIGHT]));

    // Create actions (focused subset).
    CreateAction(actionSet, XR_ACTION_TYPE_POSE_INPUT,       "grip_pose",       "Grip Pose",      SIDE_COUNT, handSubactionPath, &gripAction);
    CreateAction(actionSet, XR_ACTION_TYPE_POSE_INPUT,       "aim_pose",        "Aim Pose",       SIDE_COUNT, handSubactionPath, &aimAction);
    CreateAction(actionSet, XR_ACTION_TYPE_VIBRATION_OUTPUT, "vibrate_hand",    "Vibrate Hand",   SIDE_COUNT, handSubactionPath, &vibrateAction);

    CreateAction(actionSet, XR_ACTION_TYPE_FLOAT_INPUT,      "trigger",         "Trigger",        SIDE_COUNT, handSubactionPath, &triggerAction);
    CreateAction(actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,    "triggertouch",    "Trigger Touch",  SIDE_COUNT, handSubactionPath, &triggerTouchAction);
    CreateAction(actionSet, XR_ACTION_TYPE_FLOAT_INPUT,      "gripvalue",       "Grip Value",     SIDE_COUNT, handSubactionPath, &squeezeAction);
    CreateAction(actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,    "squeezed",        "Gripped",        SIDE_COUNT, handSubactionPath, &squeezeClickAction);

    CreateAction(actionSet, XR_ACTION_TYPE_VECTOR2F_INPUT,   "thumbstick",      "Thumbstick",     SIDE_COUNT, handSubactionPath, &thumbstickAction);
    CreateAction(actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,    "thumbstickclick", "Thumbstick Click", SIDE_COUNT, handSubactionPath, &thumbstickClickAction);
    CreateAction(actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,    "thumbsticktouch", "Thumbstick Touch", SIDE_COUNT, handSubactionPath, &thumbstickTouchAction);

    CreateAction(actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,    "akey",            "A Key",          SIDE_COUNT, handSubactionPath, &AAction);
    CreateAction(actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,    "bkey",            "B Key",          SIDE_COUNT, handSubactionPath, &BAction);
    CreateAction(actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,    "xkey",            "X Key",          SIDE_COUNT, handSubactionPath, &XAction);
    CreateAction(actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,    "ykey",            "Y Key",          SIDE_COUNT, handSubactionPath, &YAction);
    CreateAction(actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,    "backkey",         "Menu Key",       SIDE_COUNT, handSubactionPath, &backAction);

    // ---- Input source paths shared across profiles ----
    XrPath posePath[SIDE_COUNT];
    XrPath aimPath[SIDE_COUNT];
    XrPath hapticPath[SIDE_COUNT];
    XrPath menuClickPath[SIDE_COUNT];
    XrPath selectClickPath[SIDE_COUNT];

    XrPath squeezeValuePath[SIDE_COUNT];
    XrPath squeezeClickPath[SIDE_COUNT];

    XrPath triggerValuePath[SIDE_COUNT];
    XrPath triggerTouchPath[SIDE_COUNT];

    XrPath thumbstickPosPath[SIDE_COUNT];
    XrPath thumbstickClickPath[SIDE_COUNT];
    XrPath thumbstickTouchPath[SIDE_COUNT];

    XrPath AClickPath[SIDE_COUNT];
    XrPath BClickPath[SIDE_COUNT];
    XrPath XClickPath[SIDE_COUNT];
    XrPath YClickPath[SIDE_COUNT];

    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/grip/pose",  &posePath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/grip/pose", &posePath[SIDE_RIGHT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/aim/pose",   &aimPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/aim/pose",  &aimPath[SIDE_RIGHT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/output/haptic",    &hapticPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/output/haptic",   &hapticPath[SIDE_RIGHT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/menu/click",   &menuClickPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/menu/click",  &menuClickPath[SIDE_RIGHT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/select/click",  &selectClickPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/select/click", &selectClickPath[SIDE_RIGHT]));

    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/squeeze/value",  &squeezeValuePath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/squeeze/value", &squeezeValuePath[SIDE_RIGHT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/squeeze/click",  &squeezeClickPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/squeeze/click", &squeezeClickPath[SIDE_RIGHT]));

    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/trigger/value",  &triggerValuePath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/trigger/value", &triggerValuePath[SIDE_RIGHT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/trigger/touch",  &triggerTouchPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/trigger/touch", &triggerTouchPath[SIDE_RIGHT]));

    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/thumbstick",        &thumbstickPosPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/thumbstick",       &thumbstickPosPath[SIDE_RIGHT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/thumbstick/click",  &thumbstickClickPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/thumbstick/click", &thumbstickClickPath[SIDE_RIGHT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/thumbstick/touch",  &thumbstickTouchPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/thumbstick/touch", &thumbstickTouchPath[SIDE_RIGHT]));

    // A/B on the right, X/Y on the left.  (Valve Index has A/B on both hands.)
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/a/click", &AClickPath[SIDE_RIGHT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/right/input/b/click", &BClickPath[SIDE_RIGHT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/a/click",  &AClickPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/b/click",  &BClickPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/x/click",  &XClickPath[SIDE_LEFT]));
    CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/user/hand/left/input/y/click",  &YClickPath[SIDE_LEFT]));

    XrResult result;

    // ---- Oculus Touch ----
    {
        XrPath profile;
        CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/interaction_profiles/oculus/touch_controller", &profile));

        XrActionSuggestedBinding bindings[64];
        int n = 0;
        bindings[n++] = ActionSuggestedBinding(XAction, XClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(YAction, YClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(AAction, AClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(BAction, BClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(backAction, menuClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(squeezeAction, squeezeValuePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(squeezeAction, squeezeValuePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(triggerAction, triggerValuePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(triggerAction, triggerValuePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(triggerTouchAction, triggerTouchPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(triggerTouchAction, triggerTouchPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickAction, thumbstickPosPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickAction, thumbstickPosPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickClickAction, thumbstickClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickClickAction, thumbstickClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickTouchAction, thumbstickTouchPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickTouchAction, thumbstickTouchPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(aimAction, aimPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(aimAction, aimPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(gripAction, posePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(gripAction, posePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(vibrateAction, hapticPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(vibrateAction, hapticPath[SIDE_RIGHT]);

        XrInteractionProfileSuggestedBinding sb = {0};
        sb.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        sb.interactionProfile = profile;
        sb.suggestedBindings = bindings;
        sb.countSuggestedBindings = n;
        sb.next = NULL;
        result = xrSuggestInteractionProfileBindings(gAppState.Instance, &sb);
    }

    // ---- Valve Index ----
    {
        XrPath profile;
        CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/interaction_profiles/valve/index_controller", &profile));

        XrActionSuggestedBinding bindings[64];
        int n = 0;
        // Index has A/B on both hands; map the LEFT a/b to X/Y for parity.
        bindings[n++] = ActionSuggestedBinding(AAction, AClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(BAction, BClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(XAction, AClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(YAction, BClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(squeezeAction, squeezeValuePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(squeezeAction, squeezeValuePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(triggerAction, triggerValuePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(triggerAction, triggerValuePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(triggerTouchAction, triggerTouchPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(triggerTouchAction, triggerTouchPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickAction, thumbstickPosPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickAction, thumbstickPosPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickClickAction, thumbstickClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickClickAction, thumbstickClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickTouchAction, thumbstickTouchPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickTouchAction, thumbstickTouchPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(aimAction, aimPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(aimAction, aimPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(gripAction, posePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(gripAction, posePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(vibrateAction, hapticPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(vibrateAction, hapticPath[SIDE_RIGHT]);

        XrInteractionProfileSuggestedBinding sb = {0};
        sb.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        sb.interactionProfile = profile;
        sb.suggestedBindings = bindings;
        sb.countSuggestedBindings = n;
        sb.next = NULL;
        result = xrSuggestInteractionProfileBindings(gAppState.Instance, &sb);
    }

    // ---- HTC Vive wand (no thumbstick / face buttons; trigger + grip + menu) ----
    {
        XrPath profile;
        CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/interaction_profiles/htc/vive_controller", &profile));

        XrActionSuggestedBinding bindings[32];
        int n = 0;
        bindings[n++] = ActionSuggestedBinding(squeezeClickAction, squeezeClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(squeezeClickAction, squeezeClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(backAction, menuClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(backAction, menuClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(triggerAction, triggerValuePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(triggerAction, triggerValuePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(aimAction, aimPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(aimAction, aimPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(gripAction, posePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(gripAction, posePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(vibrateAction, hapticPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(vibrateAction, hapticPath[SIDE_RIGHT]);

        XrInteractionProfileSuggestedBinding sb = {0};
        sb.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        sb.interactionProfile = profile;
        sb.suggestedBindings = bindings;
        sb.countSuggestedBindings = n;
        sb.next = NULL;
        result = xrSuggestInteractionProfileBindings(gAppState.Instance, &sb);
    }

    // ---- ByteDance Pico 4 ----
    {
        XrPath profile;
        CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/interaction_profiles/bytedance/pico4_controller", &profile));

        XrActionSuggestedBinding bindings[64];
        int n = 0;
        bindings[n++] = ActionSuggestedBinding(XAction, XClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(YAction, YClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(AAction, AClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(BAction, BClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(backAction, menuClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(triggerAction, triggerValuePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(triggerAction, triggerValuePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(squeezeAction, squeezeValuePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(squeezeAction, squeezeValuePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(squeezeClickAction, squeezeClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(squeezeClickAction, squeezeClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickAction, thumbstickPosPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickAction, thumbstickPosPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickClickAction, thumbstickClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickClickAction, thumbstickClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickTouchAction, thumbstickTouchPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(thumbstickTouchAction, thumbstickTouchPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(aimAction, aimPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(aimAction, aimPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(gripAction, posePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(gripAction, posePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(vibrateAction, hapticPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(vibrateAction, hapticPath[SIDE_RIGHT]);

        XrInteractionProfileSuggestedBinding sb = {0};
        sb.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        sb.interactionProfile = profile;
        sb.suggestedBindings = bindings;
        sb.countSuggestedBindings = n;
        sb.next = NULL;
        result = xrSuggestInteractionProfileBindings(gAppState.Instance, &sb);
    }

    // ---- Khronos Simple controller (last-resort fallback) ----
    {
        XrPath profile;
        CHECK_XRCMD(xrStringToPath(gAppState.Instance, "/interaction_profiles/khr/simple_controller", &profile));

        XrActionSuggestedBinding bindings[16];
        int n = 0;
        // KHR Simple only exposes select/click + menu/click + grip pose + haptic.
        bindings[n++] = ActionSuggestedBinding(triggerTouchAction, selectClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(triggerTouchAction, selectClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(backAction, menuClickPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(backAction, menuClickPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(aimAction, aimPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(aimAction, aimPath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(gripAction, posePath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(gripAction, posePath[SIDE_RIGHT]);
        bindings[n++] = ActionSuggestedBinding(vibrateAction, hapticPath[SIDE_LEFT]);
        bindings[n++] = ActionSuggestedBinding(vibrateAction, hapticPath[SIDE_RIGHT]);

        XrInteractionProfileSuggestedBinding sb = {0};
        sb.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        sb.interactionProfile = profile;
        sb.suggestedBindings = bindings;
        sb.countSuggestedBindings = n;
        sb.next = NULL;
        result = xrSuggestInteractionProfileBindings(gAppState.Instance, &sb);
    }

    (void)result;

    // Create the per-hand grip + aim action spaces.
    XrActionSpaceCreateInfo actionSpaceInfo = {0};
    actionSpaceInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
    actionSpaceInfo.action = gripAction;
    actionSpaceInfo.poseInActionSpace.orientation.w = 1.f;
    actionSpaceInfo.subactionPath = handSubactionPath[SIDE_LEFT];
    CHECK_XRCMD(xrCreateActionSpace(gAppState.Session, &actionSpaceInfo, &handSpace[SIDE_LEFT]));
    actionSpaceInfo.subactionPath = handSubactionPath[SIDE_RIGHT];
    CHECK_XRCMD(xrCreateActionSpace(gAppState.Session, &actionSpaceInfo, &handSpace[SIDE_RIGHT]));
    actionSpaceInfo.action = aimAction;
    actionSpaceInfo.poseInActionSpace.orientation.w = 1.f;
    actionSpaceInfo.subactionPath = handSubactionPath[SIDE_LEFT];
    CHECK_XRCMD(xrCreateActionSpace(gAppState.Session, &actionSpaceInfo, &aimSpace[SIDE_LEFT]));
    actionSpaceInfo.subactionPath = handSubactionPath[SIDE_RIGHT];
    actionSpaceInfo.next = NULL;
    CHECK_XRCMD(xrCreateActionSpace(gAppState.Session, &actionSpaceInfo, &aimSpace[SIDE_RIGHT]));

    // Attach the action set to the session.
    XrSessionActionSetsAttachInfo attachInfo = {0};
    attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &actionSet;
    attachInfo.next = NULL;
    CHECK_XRCMD(xrAttachSessionActionSets(gAppState.Session, &attachInfo));
}

static void TBXR_SyncActions( void )
{
    if (actionSet)
    {
        XrActiveActionSet activeActionSet = {0};
        activeActionSet.actionSet = actionSet;
        activeActionSet.subactionPath = XR_NULL_PATH;
        XrActionsSyncInfo syncInfo = {0};
        syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
        syncInfo.countActiveActionSets = 1;
        syncInfo.activeActionSets = &activeActionSet;
        syncInfo.next = NULL;
        xrSyncActions(gAppState.Session, &syncInfo);
    }
}

void TBXR_ProcessHaptics(void)
{
    if (gAppState.Session == XR_NULL_HANDLE) {
        return;
    }

    for (int hand = 0; hand < SIDE_COUNT; hand++) {
        float duration = 0.0f;
        float intensity = 0.0f;

        TBXR_GetHapticState(hand, &duration, &intensity);

        if (duration <= 0.0f || intensity <= 0.0f) {
            continue;
        }

        XrHapticVibration vibration = {0};
        vibration.type = XR_TYPE_HAPTIC_VIBRATION;
        vibration.next = NULL;
        vibration.amplitude = intensity;
        vibration.duration = XR_MIN_HAPTIC_DURATION;
        vibration.frequency = XR_FREQUENCY_UNSPECIFIED;

        XrHapticActionInfo info = {0};
        info.type = XR_TYPE_HAPTIC_ACTION_INFO;
        info.next = NULL;
        info.action = vibrateAction;
        info.subactionPath = handSubactionPath[hand];

        xrApplyHapticFeedback(
                gAppState.Session,
                &info,
                (const XrHapticBaseHeader *)&vibration);

        TBXR_ClearHapticState(hand);
    }
}

static void TBXR_CheckControllers(void)
{
    if (!gAppState.controllersPresent)
    {
        XrInteractionProfileState profileState = { XR_TYPE_INTERACTION_PROFILE_STATE };
        if (xrGetCurrentInteractionProfile(gAppState.Session, handSubactionPath[SIDE_RIGHT], &profileState) != XR_SUCCESS)
            return;

        if (profileState.interactionProfile != XR_NULL_PATH)
        {
            uint32_t bufferLength = 0;
            if (xrPathToString(gAppState.Instance, profileState.interactionProfile, 0, &bufferLength, NULL) != XR_SUCCESS)
                return;

            char* pathString = malloc(bufferLength);
            if (xrPathToString(gAppState.Instance, profileState.interactionProfile, bufferLength, &bufferLength, pathString) == XR_SUCCESS)
            {
                Com_Printf("VR controllers found: %s\n", pathString);

                if (strcmp(pathString, "/interaction_profiles/valve/index_controller") == 0)
                    gAppState.controllersPresent = INDEX_CONTROLLERS;
                else if (strcmp(pathString, "/interaction_profiles/htc/vive_controller") == 0)
                    gAppState.controllersPresent = VIVE_CONTROLLERS;
                else if (strcmp(pathString, "/interaction_profiles/oculus/touch_controller") == 0)
                    gAppState.controllersPresent = TOUCH_CONTROLLERS;
                else if (strcmp(pathString, "/interaction_profiles/bytedance/pico4_controller") == 0 ||
                         strcmp(pathString, "/interaction_profiles/bytedance/pico_neo3_controller_bd") == 0)
                    gAppState.controllersPresent = PICO_CONTROLLERS;
                else
                    gAppState.controllersPresent = TOUCH_CONTROLLERS; // emulate Touch
            }
            free(pathString);
        }
    }
}

void TBXR_UpdateControllers( )
{
    if (gAppState.Session == XR_NULL_HANDLE || gAppState.FrameState.predictedDisplayTime == 0) {
        return;
    }

    TBXR_CheckControllers();
    TBXR_SyncActions();

    // Get controller poses (aim + grip) for both hands.
    for (int i = 0; i < 2; i++) {
        XrSpaceVelocity vel = {0};
        vel.type = XR_TYPE_SPACE_VELOCITY;
        XrSpaceLocation loc = {0};
        loc.type = XR_TYPE_SPACE_LOCATION;
        loc.next = &vel;
        xrLocateSpace(aimSpace[i], gAppState.StageSpace, gAppState.FrameState.predictedDisplayTime, &loc);

        gAppState.TrackedController[i].Active = (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        gAppState.TrackedController[i].HeadPose.Pose.Orientation = loc.pose.orientation;
        gAppState.TrackedController[i].HeadPose.Pose.Position = loc.pose.position;
        gAppState.TrackedController[i].HeadPose.LinearVelocity =
                (vel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) ? vel.linearVelocity : (XrVector3f){0.0f, 0.0f, 0.0f};
        gAppState.TrackedController[i].Status = 0;
        if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0) {
            gAppState.TrackedController[i].Status |= VRAPI_TRACKING_STATUS_POSITION_TRACKED;
        }
        if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
            gAppState.TrackedController[i].Status |= VRAPI_TRACKING_STATUS_POSITION_VALID;
        }
        if ((loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0) {
            gAppState.TrackedController[i].Status |= VRAPI_TRACKING_STATUS_ORIENTATION_TRACKED;
        }
        if ((loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
            gAppState.TrackedController[i].Status |= VRAPI_TRACKING_STATUS_ORIENTATION_VALID;
        }
        gAppState.TrackedController[i].Velocity = vel;

        loc.type = XR_TYPE_SPACE_LOCATION;
        loc.next = NULL;
        xrLocateSpace(handSpace[i], gAppState.StageSpace, gAppState.FrameState.predictedDisplayTime, &loc);
        gAppState.TrackedController[i].GripPose = loc.pose;
    }

    leftRemoteTracking_new.HeadPose = gAppState.TrackedController[0].HeadPose;
    rightRemoteTracking_new.HeadPose = gAppState.TrackedController[1].HeadPose;
    leftRemoteTracking_new.Status = gAppState.TrackedController[0].Status;
    rightRemoteTracking_new.Status = gAppState.TrackedController[1].Status;

    // ---- LEFT hand button bitmask ----
    leftTrackedRemoteState_new.Buttons = 0;
    leftTrackedRemoteState_new.Touches = 0;
    if (GetActionStateBoolean(backAction, SIDE_LEFT).currentState)            leftTrackedRemoteState_new.Buttons |= xrButton_Enter;
    if (GetActionStateBoolean(XAction, SIDE_LEFT).currentState)               leftTrackedRemoteState_new.Buttons |= xrButton_X;
    if (GetActionStateBoolean(YAction, SIDE_LEFT).currentState)               leftTrackedRemoteState_new.Buttons |= xrButton_Y;
    if (GetActionStateBoolean(thumbstickClickAction, SIDE_LEFT).currentState) leftTrackedRemoteState_new.Buttons |= (xrButton_LThumb | xrButton_Joystick);

    leftTrackedRemoteState_new.GripTrigger = GetActionStateFloat(squeezeAction, SIDE_LEFT).currentState;
    if (gAppState.controllersPresent == VIVE_CONTROLLERS)
        leftTrackedRemoteState_new.GripTrigger = GetActionStateBoolean(squeezeClickAction, SIDE_LEFT).currentState;
    if (leftTrackedRemoteState_new.GripTrigger > 0.5f)
        leftTrackedRemoteState_new.Buttons |= xrButton_GripTrigger;

    leftTrackedRemoteState_new.IndexTrigger = GetActionStateFloat(triggerAction, SIDE_LEFT).currentState;
    if (leftTrackedRemoteState_new.IndexTrigger > 0.5f)
        leftTrackedRemoteState_new.Buttons |= xrButton_Trigger;

    // ---- RIGHT hand button bitmask ----
    rightTrackedRemoteState_new.Buttons = 0;
    rightTrackedRemoteState_new.Touches = 0;
    if (GetActionStateBoolean(backAction, SIDE_RIGHT).currentState)            rightTrackedRemoteState_new.Buttons |= xrButton_Enter;
    if (GetActionStateBoolean(AAction, SIDE_RIGHT).currentState)               rightTrackedRemoteState_new.Buttons |= xrButton_A;
    if (GetActionStateBoolean(BAction, SIDE_RIGHT).currentState)               rightTrackedRemoteState_new.Buttons |= xrButton_B;
    if (GetActionStateBoolean(thumbstickClickAction, SIDE_RIGHT).currentState) rightTrackedRemoteState_new.Buttons |= (xrButton_RThumb | xrButton_Joystick);

    rightTrackedRemoteState_new.GripTrigger = GetActionStateFloat(squeezeAction, SIDE_RIGHT).currentState;
    if (gAppState.controllersPresent == VIVE_CONTROLLERS)
        rightTrackedRemoteState_new.GripTrigger = GetActionStateBoolean(squeezeClickAction, SIDE_RIGHT).currentState;
    if (rightTrackedRemoteState_new.GripTrigger > 0.5f)
        rightTrackedRemoteState_new.Buttons |= xrButton_GripTrigger;

    rightTrackedRemoteState_new.IndexTrigger = GetActionStateFloat(triggerAction, SIDE_RIGHT).currentState;
    if (rightTrackedRemoteState_new.IndexTrigger > 0.5f)
        rightTrackedRemoteState_new.Buttons |= xrButton_Trigger;

    // ---- Thumbsticks ----
    XrActionStateVector2f js;
    js = GetActionStateVector2(thumbstickAction, SIDE_LEFT);
    leftTrackedRemoteState_new.Joystick.x = js.currentState.x;
    leftTrackedRemoteState_new.Joystick.y = js.currentState.y;
    js = GetActionStateVector2(thumbstickAction, SIDE_RIGHT);
    rightTrackedRemoteState_new.Joystick.x = js.currentState.x;
    rightTrackedRemoteState_new.Joystick.y = js.currentState.y;
}
