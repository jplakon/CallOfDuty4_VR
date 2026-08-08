#include <qcommon/qcommon.h>
#include <universal/com_memory.h>
#include "vr/vr_openxr.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "cg_local.h"
#include "cg_public.h"
#include <script/scr_stringlist.h>

#include <xanim/dobj.h>
#include <DynEntity/DynEntity_client.h>
#include <stringed/stringed_hooks.h>
#include <aim_assist/aim_assist.h>
#include <script/scr_const.h>
#include <xanim/dobj_utils.h>
#include <gfx_d3d/r_scene.h>
#include <gfx_d3d/r_model_skin.h>
#include <sound/snd_public.h>
#include <qcommon/cmd.h>
#include <EffectsCore/fx_system.h>
#include <game/bullet.h>

#ifdef KISAK_MP
#include <cgame_mp/cg_local_mp.h>
#include <server_mp/server_mp.h>
#include <game_mp/g_main_mp.h>
#elif KISAK_SP
#include "cg_servercmds.h"
#include "cg_ents.h"
#include "cg_main.h"
#include "cg_view.h"
#include "cg_pose.h"
#include <game/savememory.h>
#include <xanim/xanim_readwrite.h>
#include <gfx_d3d/r_model.h>
#include "cg_compassfriendlies.h"
#include <client/cl_scrn.h>
#endif

enum {
    NUM_WEAP_ANIMS = 0x20,
    WEAP_ANIM_ADS_UP = 31,
    WEAP_ANIM_ADS_DOWN = 32,
};

const float MYLERP_START = 0.3f;
const float MYLERP_END = 0.1f;

int32_t removeMeWhenMPStopsCrashingInHere;

// KISAK_SP_JAVELIN_DIAGNOSTICS
// Keep this temporary trace deliberately limited to the Bog Javelin index
// reported by CG_SelectWeaponIndex so normal weapons do not flood console.log.
static uint32_t s_vrJavelinDiagnosticSequence = 0;

static void VR_JavelinDiagnostic(
    const char *stage,
    uint32_t weaponNum,
    const void *object = nullptr)
{
    if (!VR_VerboseDiagnosticsEnabled() ||
        weaponNum != 7)
        return;

    Com_Printf(
        0,
        "[VR][JAVELIN] %u: %s (weapon %u, object %p).\n",
        ++s_vrJavelinDiagnosticSequence,
        stage,
        weaponNum,
        object);
}

#ifdef KISAK_SP
// KISAK_SP_VR_MANUAL_MAGAZINE_RELOAD_V1
// World clip models are ordinary one-model DObjs.  Cache one per weapon so
// their backing allocations and pose pointers remain valid through scene
// submission; CG_FreeWeapons releases them before map assets are unloaded.
struct VrManualReloadClipRenderObject
{
    DObj_s object = {};
    XModel* model = nullptr;
    cpose_t ejectedPose = {};
    cpose_t heldPose = {};
    bool created = false;
};

static VrManualReloadClipRenderObject
    s_vrManualReloadClipObjects[128] = {};

// KISAK_SP_VR_MANUAL_GRENADE_THROW_V53
// The stock offhand DObj includes canned arms and is rooted at the firearm
// hand.  Cache a one-model projectile DObj per grenade weapon so the actual
// frag/flash/smoke asset can instead follow the tracked left grip.
struct VrManualGrenadeRenderObject
{
    DObj_s object = {};
    XModel* model = nullptr;
    cpose_t heldPose = {};
    bool created = false;
};

static VrManualGrenadeRenderObject
    s_vrManualGrenadeRenderObjects[128] = {};

// KISAK_SP_VR_TRACKED_HANDS_V27_MAGAZINE_GRIP_POSE
// V27 preserves V26's corrected XR_EXT_palm_pose orientation and adds the
// missing third left-hand render state: neutral tracking, weapon support, or
// magazine grip.  The magazine state reuses the weapon-specific baked closed
// fingers in a dedicated identity-bind DObj.  On pickup it captures the rigid
// magazine-to-wrist transform without moving the hand; while held, the glove
// inherits the exact world pose used to render the magazine, including the
// insertion-assist orientation snap.  The OpenXR reload state machine,
// insertion radius, weapon support grip, right hand, weapon placement, and
// firing remain unchanged.
static constexpr uint8_t
    kVrRuntimeSplitSurfaceZoneHandle = 0xFFu;

static constexpr int
    kVrSplitHandAssetCapacity = 8;

struct VrSplitHandAsset
{
    XModel* sourceModel = nullptr;
    XModel rightOnlyModel = {};
    XModel floatingLeftModel = {};
    XModel supportGripModel = {};
    XModel magazineGripModel = {};
    XSurface* rightOnlySurfaces = nullptr;
    XSurface* floatingLeftSurfaces = nullptr;
    XSurface* supportGripSurfaces = nullptr;
    DObjAnimMat* floatingRootBaseMat = nullptr;
    DObj_s floatingLeftObject = {};
    DObj_s supportGripObject = {};
    DObj_s magazineGripObject = {};
    cpose_t floatingLeftPose = {};
    cpose_t supportGripPose = {};
    cpose_t magazineGripPose = {};
    DObjAnimMat baseLeftWristPose = {};
    float modelAnatomicalGripAxis[3][3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
    float modelPalmAnchor[3] = {};
    int leftWristIndex = -1;
    int supportGripWeaponIndex = -1;
    uint32_t sourceTriangleCount = 0u;
    uint32_t rightTriangleCount = 0u;
    uint32_t leftTriangleCount = 0u;
    uint32_t rebasedVertexCount = 0u;
    uint32_t rigidifiedVertexCount = 0u;
    uint32_t stableIdlePoseFrames = 0u;
    uint32_t lastAttachmentUpdateMilliseconds = 0u;
    uint32_t lastFreeTransformDiagnosticMilliseconds = 0u;
    float attachmentBlend = 0.0f;
    float magazineToWristOrigin[3] = {};
    float magazineToWristAxis[3][3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
    float diagnosticControllerToWristOrigin[3] = {};
    float diagnosticControllerToWristAxis[3][3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
    bool ready = false;
    bool failed = false;
    bool floatingObjectCreated = false;
    bool supportGripObjectCreated = false;
    bool magazineGripObjectCreated = false;
    bool supportGripPoseBaked = false;
    bool supportGripLatched = false;
    bool magazineGripLatched = false;
    bool magazineToWristPoseValid = false;
    bool supportGripBakeFailureLogWritten = false;
    bool magazineGripPoseFailureLogWritten = false;
    bool objectCreationLogWritten = false;
    bool submissionAttemptLogWritten = false;
    bool submittedLogWritten = false;
    bool controllerPoseFailureLogWritten = false;
    bool modelAnatomicalFrameReady = false;
    bool freeTransformDiagnosticBaselineReady = false;
};

static VrSplitHandAsset
    s_vrSplitHandAssets[kVrSplitHandAssetCapacity] = {};

static const dvar_t*
    s_vrLeftHandOffsetForward = nullptr;

static const dvar_t*
    s_vrLeftHandOffsetLeft = nullptr;

static const dvar_t*
    s_vrLeftHandOffsetUp = nullptr;

static const dvar_t*
    s_vrLeftHandPitch = nullptr;

static const dvar_t*
    s_vrLeftHandYaw = nullptr;

static const dvar_t*
    s_vrLeftHandRoll = nullptr;

static const dvar_t*
    s_vrLeftHandGripRadius = nullptr;

static constexpr float
    kVrLeftHandDefaultOffsetForward = 0.00f;

static constexpr float
    kVrLeftHandDefaultOffsetLeft = 0.00f;

static constexpr float
    kVrLeftHandDefaultOffsetUp = 0.00f;

static void VR_RegisterFloatingLeftHandCalibrationDvars()
{
    if (s_vrLeftHandOffsetForward != nullptr)
    {
        return;
    }

    s_vrLeftHandOffsetForward =
        Dvar_RegisterFloat(
            "vr_leftHandModelOffsetForward",
            kVrLeftHandDefaultOffsetForward,
            -8.0f,
            8.0f,
            DVAR_ARCHIVE,
            "Left VR wrist offset along controller forward, in game units/inches");

    s_vrLeftHandOffsetLeft =
        Dvar_RegisterFloat(
            "vr_leftHandModelOffsetLeft",
            kVrLeftHandDefaultOffsetLeft,
            -8.0f,
            8.0f,
            DVAR_ARCHIVE,
            "Left VR wrist offset along controller left, in game units/inches");

    s_vrLeftHandOffsetUp =
        Dvar_RegisterFloat(
            "vr_leftHandModelOffsetUp",
            kVrLeftHandDefaultOffsetUp,
            -8.0f,
            8.0f,
            DVAR_ARCHIVE,
            "Left VR wrist offset along controller up, in game units/inches");

    s_vrLeftHandPitch =
        Dvar_RegisterFloat(
            "vr_leftHandModelPitch",
            0.0f,
            -180.0f,
            180.0f,
            DVAR_ARCHIVE,
            "Left VR wrist pitch adjustment in controller-local degrees");

    s_vrLeftHandYaw =
        Dvar_RegisterFloat(
            "vr_leftHandModelYaw",
            0.0f,
            -180.0f,
            180.0f,
            DVAR_ARCHIVE,
            "Left VR wrist yaw adjustment in controller-local degrees");

    s_vrLeftHandRoll =
        Dvar_RegisterFloat(
            "vr_leftHandModelRoll",
            0.0f,
            -180.0f,
            180.0f,
            DVAR_ARCHIVE,
            "Left VR wrist roll adjustment in controller-local degrees");

    s_vrLeftHandGripRadius =
        Dvar_RegisterFloat(
            "vr_leftHandGripRadius",
            14.0f,
            3.0f,
            24.0f,
            DVAR_ARCHIVE,
            "Maximum left-controller distance from the animated weapon wrist anchor before squeeze can attach the support hand");

    Com_Printf(
        0,
        "[VR][HANDS] Model-aware free left-hand fit is "
        "enabled: offset forward %.2f, left %.2f, up %.2f; "
        "pitch %.1f, yaw %.1f, roll %.1f; weapon-grip radius %.1f.  "
        "The vr_leftHandModel* dvars update live and are archived; V18's "
        "old free-hand fit values are intentionally ignored.\n",
        s_vrLeftHandOffsetForward->current.value,
        s_vrLeftHandOffsetLeft->current.value,
        s_vrLeftHandOffsetUp->current.value,
        s_vrLeftHandPitch->current.value,
        s_vrLeftHandYaw->current.value,
        s_vrLeftHandRoll->current.value,
        s_vrLeftHandGripRadius->current.value);
}

static bool s_vrTrackedHandsSettingRead = false;
static bool s_vrTrackedHandsEnabled = true;

static bool VR_TrackedHandsEnabled()
{
    if (!s_vrTrackedHandsSettingRead)
    {
        const char* requestedSetting =
            std::getenv("KISAK_VR_TRACKED_HANDS");

        if (requestedSetting != nullptr &&
            requestedSetting[0] != '\0')
        {
            if (std::strcmp(requestedSetting, "0") == 0)
            {
                s_vrTrackedHandsEnabled = false;
            }
            else if (std::strcmp(requestedSetting, "1") != 0)
            {
                Com_PrintWarning(
                    0,
                    "[VR][HANDS] Ignoring invalid "
                    "KISAK_VR_TRACKED_HANDS='%s'; using 1. "
                    "Valid values are 0 and 1.\n",
                    requestedSetting);
            }
        }

        s_vrTrackedHandsSettingRead = true;

        Com_Printf(
            0,
            "[VR][HANDS] Runtime-split floating left hand is %s.\n",
            s_vrTrackedHandsEnabled
                ? "enabled"
                : "disabled");
    }

    return s_vrTrackedHandsEnabled;
}

static int VR_GetModelBoneParentIndex(
    const XModel* model,
    const int boneIndex)
{
    if (model == nullptr ||
        boneIndex < 0 ||
        boneIndex >= model->numBones ||
        boneIndex < model->numRootBones ||
        model->parentList == nullptr)
    {
        return -1;
    }

    const int parentOffset =
        model->parentList[
            boneIndex - model->numRootBones];

    if (parentOffset <= 0 ||
        parentOffset > boneIndex)
    {
        return -1;
    }

    return boneIndex - parentOffset;
}

static int VR_FindModelBoneIndex(
    const XModel* model,
    const char* requestedName)
{
    if (model == nullptr ||
        requestedName == nullptr ||
        requestedName[0] == '\0')
    {
        return -1;
    }

    for (int boneIndex = 0;
         boneIndex < model->numBones;
         ++boneIndex)
    {
        const char* boneName =
            SL_ConvertToString(
                model->boneNames[boneIndex]);

        if (boneName != nullptr &&
            I_stricmp(boneName, requestedName) == 0)
        {
            return boneIndex;
        }
    }

    return -1;
}

static bool VR_ModelBoneDescendsFrom(
    const XModel* model,
    int boneIndex,
    const int ancestorIndex)
{
    for (int depth = 0;
         boneIndex >= 0 &&
             depth < DOBJ_MAX_PARTS;
         ++depth)
    {
        if (boneIndex == ancestorIndex)
        {
            return true;
        }

        boneIndex =
            VR_GetModelBoneParentIndex(
                model,
                boneIndex);
    }

    return false;
}

static bool VR_NormalizeModelHandDirection(
    float direction[3])
{
    if (direction == nullptr)
    {
        return false;
    }

    const float lengthSquared =
        direction[0] * direction[0] +
        direction[1] * direction[1] +
        direction[2] * direction[2];

    if (!std::isfinite(lengthSquared) ||
        lengthSquared <= 0.000001f)
    {
        return false;
    }

    const float inverseLength =
        1.0f / std::sqrt(lengthSquared);

    for (int component = 0;
         component < 3;
         ++component)
    {
        direction[component] *=
            inverseLength;
    }

    return true;
}

static void VR_CrossModelHandDirections(
    const float first[3],
    const float second[3],
    float output[3])
{
    output[0] =
        first[1] * second[2] -
        first[2] * second[1];
    output[1] =
        first[2] * second[0] -
        first[0] * second[2];
    output[2] =
        first[0] * second[1] -
        first[1] * second[0];
}

static bool VR_GetModelBoneOriginInWristSpace(
    const XModel* model,
    const int wristIndex,
    const DObjAnimMat& wristPose,
    const char* boneName,
    float wristLocalOrigin[3])
{
    if (model == nullptr ||
        model->baseMat == nullptr ||
        wristLocalOrigin == nullptr)
    {
        return false;
    }

    const int boneIndex =
        VR_FindModelBoneIndex(
            model,
            boneName);

    if (boneIndex < 0 ||
        !VR_ModelBoneDescendsFrom(
            model,
            boneIndex,
            wristIndex))
    {
        return false;
    }

    InvMatrixTransformVectorQuatTrans(
        model->baseMat[boneIndex].trans,
        &wristPose,
        wristLocalOrigin);

    return
        std::isfinite(wristLocalOrigin[0]) &&
        std::isfinite(wristLocalOrigin[1]) &&
        std::isfinite(wristLocalOrigin[2]);
}

static bool VR_BuildModelAnatomicalGripFrame(
    const XModel* model,
    const int wristIndex,
    const DObjAnimMat& wristPose,
    float modelAnatomicalGripAxis[3][3],
    float modelPalmAnchor[3],
    int* contributingFingerBaseCount)
{
    if (model == nullptr ||
        modelAnatomicalGripAxis == nullptr ||
        modelPalmAnchor == nullptr ||
        contributingFingerBaseCount == nullptr)
    {
        return false;
    }

    static const char* const
        kNonThumbFingerBaseNames[] = {
            "j_index_le_0",
            "j_mid_le_0",
            "j_ring_le_0",
            "j_pinky_le_0",
        };

    float fingerBaseCenter[3] = {};
    int fingerBaseCount = 0;

    for (const char* fingerBaseName :
         kNonThumbFingerBaseNames)
    {
        float fingerBaseOrigin[3] = {};

        if (!VR_GetModelBoneOriginInWristSpace(
                model,
                wristIndex,
                wristPose,
                fingerBaseName,
                fingerBaseOrigin))
        {
            continue;
        }

        for (int component = 0;
             component < 3;
             ++component)
        {
            fingerBaseCenter[component] +=
                fingerBaseOrigin[component];
        }

        ++fingerBaseCount;
    }

    float thumbBaseOrigin[3] = {};

    if (fingerBaseCount <= 0 ||
        !VR_GetModelBoneOriginInWristSpace(
            model,
            wristIndex,
            wristPose,
            "j_thumb_le_0",
            thumbBaseOrigin))
    {
        return false;
    }

    for (int component = 0;
         component < 3;
         ++component)
    {
        fingerBaseCenter[component] /=
            static_cast<float>(fingerBaseCount);
    }

    float wristToFingers[3] = {
        fingerBaseCenter[0],
        fingerBaseCenter[1],
        fingerBaseCenter[2],
    };

    if (!VR_NormalizeModelHandDirection(
            wristToFingers))
    {
        return false;
    }

    const float thumbLongitudinalComponent =
        thumbBaseOrigin[0] * wristToFingers[0] +
        thumbBaseOrigin[1] * wristToFingers[1] +
        thumbBaseOrigin[2] * wristToFingers[2];

    float littleToThumb[3] = {
        thumbBaseOrigin[0] -
            thumbLongitudinalComponent *
                wristToFingers[0],
        thumbBaseOrigin[1] -
            thumbLongitudinalComponent *
                wristToFingers[1],
        thumbBaseOrigin[2] -
            thumbLongitudinalComponent *
                wristToFingers[2],
    };

    if (!VR_NormalizeModelHandDirection(
            littleToThumb))
    {
        return false;
    }

    // VR_GetTrackedControllerGripPoseWorld publishes these three anatomical
    // OpenXR directions as its rows: -Z is little-finger-to-thumb, -X is into
    // the left palm, and +Y is wrist-to-fingertips.  V19 accidentally negated
    // the last two rows, producing a 180-degree rotation around -Z.  Build the
    // spec-defined proper orthonormal basis in this model's wrist-local
    // coordinates.
    float intoPalm[3] = {};

    VR_CrossModelHandDirections(
        wristToFingers,
        littleToThumb,
        intoPalm);

    if (!VR_NormalizeModelHandDirection(
            intoPalm))
    {
        return false;
    }

    // Rebuild the lateral row from the other two so tiny authored-skeleton
    // skew cannot leak scale or shear into the quaternion.
    VR_CrossModelHandDirections(
        intoPalm,
        wristToFingers,
        littleToThumb);

    if (!VR_NormalizeModelHandDirection(
            littleToThumb))
    {
        return false;
    }

    for (int component = 0;
         component < 3;
         ++component)
    {
        modelAnatomicalGripAxis[0][component] =
            littleToThumb[component];
        modelAnatomicalGripAxis[1][component] =
            intoPalm[component];
        modelAnatomicalGripAxis[2][component] =
            wristToFingers[component];

        // The OpenXR grip origin represents the palm centroid, while the
        // extracted glove root is j_wrist_le.  Halfway from the wrist to the
        // non-thumb knuckles is a model-derived palm centroid and removes the
        // previous systematic half-palm placement error.
        modelPalmAnchor[component] =
            fingerBaseCenter[component] *
            0.5f;
    }

    *contributingFingerBaseCount =
        fingerBaseCount;

    return true;
}

static void VR_AccumulateSplitHandBoneWeight(
    const XModel* sourceModel,
    const int boneIndex,
    const uint32_t boneWeight,
    const int leftShoulderIndex,
    const int leftWristIndex,
    const int leftWristTwistIndex,
    float* leftArmWeight,
    float* leftHandWeight)
{
    if (sourceModel == nullptr ||
        boneIndex < 0 ||
        boneIndex >= sourceModel->numBones ||
        boneWeight == 0u ||
        leftArmWeight == nullptr ||
        leftHandWeight == nullptr)
    {
        return;
    }

    const float normalizedWeight =
        static_cast<float>(boneWeight) /
        65535.0f;

    if (VR_ModelBoneDescendsFrom(
            sourceModel,
            boneIndex,
            leftShoulderIndex))
    {
        *leftArmWeight += normalizedWeight;
    }

    if (boneIndex == leftWristTwistIndex ||
        VR_ModelBoneDescendsFrom(
            sourceModel,
            boneIndex,
            leftWristIndex))
    {
        *leftHandWeight += normalizedWeight;
    }
}

static bool VR_CalculateSplitHandVertexWeights(
    const XModel* sourceModel,
    const XSurface& sourceSurface,
    const int leftShoulderIndex,
    const int leftWristIndex,
    const int leftWristTwistIndex,
    float* leftArmWeights,
    float* leftHandWeights)
{
    if (sourceModel == nullptr ||
        !sourceSurface.deformed ||
        sourceSurface.vertCount == 0u ||
        sourceSurface.vertInfo.vertsBlend == nullptr ||
        leftArmWeights == nullptr ||
        leftHandWeights == nullptr)
    {
        return false;
    }

    const uint16_t* vertexBlend =
        sourceSurface.vertInfo.vertsBlend;

    int vertexIndex = 0;

    for (int additionalWeightCount = 0;
         additionalWeightCount < 4;
         ++additionalWeightCount)
    {
        const int groupedVertexCount =
            sourceSurface.vertInfo.vertCount[
                additionalWeightCount];

        if (groupedVertexCount < 0)
        {
            return false;
        }

        for (int groupedVertexIndex = 0;
             groupedVertexIndex < groupedVertexCount;
             ++groupedVertexIndex)
        {
            if (vertexIndex >= sourceSurface.vertCount)
            {
                return false;
            }

            uint32_t secondaryWeightTotal = 0u;

            for (int secondaryIndex = 0;
                 secondaryIndex < additionalWeightCount;
                 ++secondaryIndex)
            {
                const uint32_t secondaryWeight =
                    vertexBlend[2 + secondaryIndex * 2];

                secondaryWeightTotal +=
                    secondaryWeight;

                VR_AccumulateSplitHandBoneWeight(
                    sourceModel,
                    vertexBlend[1 + secondaryIndex * 2] >> 6,
                    secondaryWeight,
                    leftShoulderIndex,
                    leftWristIndex,
                    leftWristTwistIndex,
                    &leftArmWeights[vertexIndex],
                    &leftHandWeights[vertexIndex]);
            }

            if (secondaryWeightTotal > 65535u)
            {
                secondaryWeightTotal = 65535u;
            }

            VR_AccumulateSplitHandBoneWeight(
                sourceModel,
                vertexBlend[0] >> 6,
                65535u - secondaryWeightTotal,
                leftShoulderIndex,
                leftWristIndex,
                leftWristTwistIndex,
                &leftArmWeights[vertexIndex],
                &leftHandWeights[vertexIndex]);

            vertexBlend +=
                1 + additionalWeightCount * 2;

            ++vertexIndex;
        }
    }

    return vertexIndex == sourceSurface.vertCount;
}

enum VrSplitHandTriangleSelection : int
{
    VR_SPLIT_HAND_KEEP_RIGHT = 0,
    VR_SPLIT_HAND_KEEP_LEFT_GLOVE = 1,
};

static bool VR_SelectSplitHandTriangle(
    const VrSplitHandTriangleSelection selection,
    const uint16_t triangleIndices[3],
    const uint16_t vertexCount,
    const float* leftArmWeights,
    const float* leftHandWeights)
{
    if (triangleIndices == nullptr ||
        leftArmWeights == nullptr ||
        leftHandWeights == nullptr)
    {
        return false;
    }

    float leftArmWeightSum = 0.0f;
    float leftHandWeightSum = 0.0f;
    float maximumLeftHandWeight = 0.0f;

    for (int cornerIndex = 0;
         cornerIndex < 3;
         ++cornerIndex)
    {
        const uint16_t vertexIndex =
            triangleIndices[cornerIndex];

        if (vertexIndex >= vertexCount)
        {
            return false;
        }

        leftArmWeightSum +=
            leftArmWeights[vertexIndex];

        leftHandWeightSum +=
            leftHandWeights[vertexIndex];

        if (leftHandWeights[vertexIndex] >
            maximumLeftHandWeight)
        {
            maximumLeftHandWeight =
                leftHandWeights[vertexIndex];
        }
    }

    if (selection == VR_SPLIT_HAND_KEEP_RIGHT)
    {
        // Left and right geometry are disconnected islands.  Any meaningful
        // left-shoulder influence therefore identifies a left-side triangle.
        return leftArmWeightSum < 0.50f;
    }

    // Keep the glove plus a small wrist-weighted cuff, but reject the sleeve.
    // The latter carries only the wrist-twist helper's roughly five-percent
    // corrective weight in the stock model.
    return
        leftHandWeightSum >= 0.60f &&
        maximumLeftHandWeight >= 0.35f;
}

static uint16_t* VR_CreateSplitHandIndexList(
    const XSurface& sourceSurface,
    const VrSplitHandTriangleSelection selection,
    const float* leftArmWeights,
    const float* leftHandWeights,
    uint16_t* outputTriangleCount,
    uint32_t* outputVisibleTriangleCount)
{
    if (sourceSurface.triIndices == nullptr ||
        sourceSurface.triCount == 0u ||
        outputTriangleCount == nullptr ||
        outputVisibleTriangleCount == nullptr)
    {
        return nullptr;
    }

    uint32_t visibleTriangleCount = 0u;

    for (uint32_t triangleIndex = 0u;
         triangleIndex < sourceSurface.triCount;
         ++triangleIndex)
    {
        if (VR_SelectSplitHandTriangle(
                selection,
                &sourceSurface.triIndices[
                    triangleIndex * 3u],
                sourceSurface.vertCount,
                leftArmWeights,
                leftHandWeights))
        {
            ++visibleTriangleCount;
        }
    }

    uint32_t storedTriangleCount =
        visibleTriangleCount;

    if (storedTriangleCount < 2u)
    {
        storedTriangleCount = 2u;
    }
    else if ((storedTriangleCount & 1u) != 0u)
    {
        ++storedTriangleCount;
    }

    if (storedTriangleCount > 65535u)
    {
        return nullptr;
    }

    uint16_t* outputIndices =
        reinterpret_cast<uint16_t*>(
            Hunk_Alloc(
                storedTriangleCount *
                    3u *
                    sizeof(uint16_t),
                "VR split hand indices",
                21));

    if (outputIndices == nullptr)
    {
        return nullptr;
    }

    uint32_t outputIndex = 0u;

    for (uint32_t triangleIndex = 0u;
         triangleIndex < sourceSurface.triCount;
         ++triangleIndex)
    {
        const uint16_t* sourceIndices =
            &sourceSurface.triIndices[
                triangleIndex * 3u];

        if (!VR_SelectSplitHandTriangle(
                selection,
                sourceIndices,
                sourceSurface.vertCount,
                leftArmWeights,
                leftHandWeights))
        {
            continue;
        }

        outputIndices[outputIndex++] =
            sourceIndices[0];

        outputIndices[outputIndex++] =
            sourceIndices[1];

        outputIndices[outputIndex++] =
            sourceIndices[2];
    }

    const uint16_t degenerateVertex =
        visibleTriangleCount > 0u
            ? outputIndices[0]
            : sourceSurface.triIndices[0];

    while (outputIndex <
           storedTriangleCount * 3u)
    {
        outputIndices[outputIndex++] =
            degenerateVertex;
    }

    *outputTriangleCount =
        static_cast<uint16_t>(
            storedTriangleCount);

    *outputVisibleTriangleCount =
        visibleTriangleCount;

    return outputIndices;
}

static GfxPackedVertex* VR_CreateWristRootedVertexList(
    const XSurface& sourceSurface,
    const DObjAnimMat& baseWristPose,
    const float baseWristAxis[3][3])
{
    if (sourceSurface.verts0 == nullptr ||
        sourceSurface.vertCount == 0u ||
        baseWristAxis == nullptr)
    {
        return nullptr;
    }

    GfxPackedVertex* wristRootedVertices =
        reinterpret_cast<GfxPackedVertex*>(
            Hunk_Alloc(
                sourceSurface.vertCount *
                    sizeof(GfxPackedVertex),
                "VR wrist-rooted left hand vertices",
                21));

    if (wristRootedVertices == nullptr)
    {
        return nullptr;
    }

    std::memcpy(
        wristRootedVertices,
        sourceSurface.verts0,
        sourceSurface.vertCount *
            sizeof(GfxPackedVertex));

    for (uint32_t vertexIndex = 0u;
         vertexIndex < sourceSurface.vertCount;
         ++vertexIndex)
    {
        const GfxPackedVertex& sourceVertex =
            sourceSurface.verts0[vertexIndex];

        GfxPackedVertex& wristRootedVertex =
            wristRootedVertices[vertexIndex];

        InvMatrixTransformVectorQuatTrans(
            sourceVertex.xyz,
            &baseWristPose,
            wristRootedVertex.xyz);

        float sourceNormal[3] = {};
        float wristRootedNormal[3] = {};

        Vec3UnpackUnitVec(
            sourceVertex.normal,
            sourceNormal);

        MatrixTransposeTransformVector(
            sourceNormal,
            *reinterpret_cast<const mat3x3*>(
                baseWristAxis),
            wristRootedNormal);

        Vec3Normalize(
            wristRootedNormal);

        wristRootedVertex.normal =
            Vec3PackUnitVec(
                wristRootedNormal);

        float sourceTangent[3] = {};
        float wristRootedTangent[3] = {};

        Vec3UnpackUnitVec(
            sourceVertex.tangent,
            sourceTangent);

        MatrixTransposeTransformVector(
            sourceTangent,
            *reinterpret_cast<const mat3x3*>(
                baseWristAxis),
            wristRootedTangent);

        Vec3Normalize(
            wristRootedTangent);

        wristRootedVertex.tangent =
            Vec3PackUnitVec(
                wristRootedTangent);
    }

    return wristRootedVertices;
}

static uint16_t* VR_CreateRigidRootBlendList(
    const XSurface& sourceSurface)
{
    // XSurfaceVertexInfo stores group counts as signed 16-bit values.  Every
    // vertex in the floating copy is placed in the zero-additional-weight
    // group and references root bone zero (byte offset zero).
    if (sourceSurface.vertCount == 0u ||
        sourceSurface.vertCount > 32767u)
    {
        return nullptr;
    }

    uint16_t* rigidRootBlends =
        reinterpret_cast<uint16_t*>(
            Hunk_Alloc(
                sourceSurface.vertCount *
                    sizeof(uint16_t),
                "VR rigid-root left hand weights",
                21));

    if (rigidRootBlends == nullptr)
    {
        return nullptr;
    }

    std::memset(
        rigidRootBlends,
        0,
        sourceSurface.vertCount *
            sizeof(uint16_t));

    return rigidRootBlends;
}

static void VR_MultiplySkelMat(
    const DObjSkelMat& first,
    const DObjSkelMat& second,
    DObjSkelMat* output)
{
    if (output == nullptr)
    {
        return;
    }

    for (int row = 0;
         row < 3;
         ++row)
    {
        for (int column = 0;
             column < 3;
             ++column)
        {
            output->axis[row][column] =
                first.axis[row][0] * second.axis[0][column] +
                first.axis[row][1] * second.axis[1][column] +
                first.axis[row][2] * second.axis[2][column];
        }

        output->axis[row][3] = 0.0f;
    }

    for (int component = 0;
         component < 3;
         ++component)
    {
        output->origin[component] =
            first.origin[0] * second.axis[0][component] +
            first.origin[1] * second.axis[1][component] +
            first.origin[2] * second.axis[2][component] +
            second.origin[component];
    }

    output->origin[3] = 1.0f;
}

static bool VR_BuildSplitHandModels(
    VrSplitHandAsset* asset,
    XModel* sourceModel)
{
    if (asset == nullptr ||
        sourceModel == nullptr ||
        sourceModel->numBones == 0u ||
        sourceModel->numBones > DOBJ_MAX_PARTS ||
        sourceModel->numsurfs == 0u ||
        sourceModel->surfs == nullptr ||
        sourceModel->materialHandles == nullptr ||
        sourceModel->baseMat == nullptr)
    {
        return false;
    }

    const int leftShoulderIndex =
        VR_FindModelBoneIndex(
            sourceModel,
            "j_shoulder_le");

    const int leftWristIndex =
        VR_FindModelBoneIndex(
            sourceModel,
            "j_wrist_le");

    const int leftWristTwistIndex =
        VR_FindModelBoneIndex(
            sourceModel,
            "j_wristtwist_le");

    if (leftShoulderIndex < 0 ||
        leftWristIndex < 0 ||
        leftWristTwistIndex < 0 ||
        !VR_ModelBoneDescendsFrom(
            sourceModel,
            leftWristIndex,
            leftShoulderIndex) ||
        !VR_ModelBoneDescendsFrom(
            sourceModel,
            leftWristTwistIndex,
            leftShoulderIndex))
    {
        Com_PrintWarning(
            0,
            "[VR][HANDS] Stock hand model '%s' lacks the left "
            "shoulder/wrist/wrist-twist hierarchy required for the "
            "runtime floating-hand split.\n",
            XModelGetName(sourceModel));

        return false;
    }

    XSurface* rightOnlySurfaces =
        reinterpret_cast<XSurface*>(
            Hunk_Alloc(
                sourceModel->numsurfs *
                    sizeof(XSurface),
                "VR right-only hand surfaces",
                21));

    XSurface* floatingLeftSurfaces =
        reinterpret_cast<XSurface*>(
            Hunk_Alloc(
                sourceModel->numsurfs *
                    sizeof(XSurface),
                "VR floating left hand surfaces",
                21));

    XSurface* supportGripSurfaces =
        reinterpret_cast<XSurface*>(
            Hunk_Alloc(
                sourceModel->numsurfs *
                    sizeof(XSurface),
                "VR weapon-support hand surfaces",
                21));

    if (rightOnlySurfaces == nullptr ||
        floatingLeftSurfaces == nullptr ||
        supportGripSurfaces == nullptr)
    {
        return false;
    }

    std::memcpy(
        rightOnlySurfaces,
        sourceModel->surfs,
        sourceModel->numsurfs *
            sizeof(XSurface));

    std::memcpy(
        floatingLeftSurfaces,
        sourceModel->surfs,
        sourceModel->numsurfs *
            sizeof(XSurface));

    std::memcpy(
        supportGripSurfaces,
        sourceModel->surfs,
        sourceModel->numsurfs *
            sizeof(XSurface));

    uint32_t sourceTriangleCount = 0u;
    uint32_t rightTriangleCount = 0u;
    uint32_t leftTriangleCount = 0u;
    uint32_t rebasedVertexCount = 0u;

    const DObjAnimMat& baseLeftWristPose =
        sourceModel->baseMat[
            leftWristIndex];

    float baseLeftWristAxis[3][3] = {};

    QuatToAxis(
        baseLeftWristPose.quat,
        *reinterpret_cast<mat3x3*>(
            baseLeftWristAxis));

    int anatomicalFingerBaseCount = 0;

    asset->modelAnatomicalFrameReady =
        VR_BuildModelAnatomicalGripFrame(
            sourceModel,
            leftWristIndex,
            baseLeftWristPose,
            asset->modelAnatomicalGripAxis,
            asset->modelPalmAnchor,
            &anatomicalFingerBaseCount);

    if (!asset->modelAnatomicalFrameReady)
    {
        Com_PrintWarning(
            0,
            "[VR][HANDS] Stock hand model '%s' did not expose usable "
            "left thumb/finger bind origins; free tracking will retain "
            "the direct controller basis with a zero palm anchor.\n",
            XModelGetName(sourceModel));
    }

    bool floatingBoundsInitialized = false;
    float floatingBoundsMins[3] = {};
    float floatingBoundsMaxs[3] = {};
    float floatingRadiusSquared = 0.0f;

    for (int surfaceIndex = 0;
         surfaceIndex < sourceModel->numsurfs;
         ++surfaceIndex)
    {
        const XSurface& sourceSurface =
            sourceModel->surfs[surfaceIndex];

        if (!sourceSurface.deformed ||
            sourceSurface.vertCount == 0u ||
            sourceSurface.triCount == 0u ||
            sourceSurface.triIndices == nullptr ||
            sourceSurface.verts0 == nullptr)
        {
            Com_PrintWarning(
                0,
                "[VR][HANDS] Stock hand model '%s' surface %d is not "
                "a weighted CPU-readable surface; keeping stock arms.\n",
                XModelGetName(sourceModel),
                surfaceIndex);

            return false;
        }

        const uint32_t temporaryWeightBytes =
            sourceSurface.vertCount *
            sizeof(float) *
            2u;

        float* temporaryWeights =
            reinterpret_cast<float*>(
                Z_Malloc(
                    temporaryWeightBytes,
                    "VR split hand weights",
                    21));

        if (temporaryWeights == nullptr)
        {
            return false;
        }

        std::memset(
            temporaryWeights,
            0,
            temporaryWeightBytes);

        float* leftArmWeights =
            temporaryWeights;

        float* leftHandWeights =
            &temporaryWeights[
                sourceSurface.vertCount];

        const bool weightsValid =
            VR_CalculateSplitHandVertexWeights(
                sourceModel,
                sourceSurface,
                leftShoulderIndex,
                leftWristIndex,
                leftWristTwistIndex,
                leftArmWeights,
                leftHandWeights);

        if (!weightsValid)
        {
            Z_Free(
                temporaryWeights,
                21);

            Com_PrintWarning(
                0,
                "[VR][HANDS] Could not decode skin weights for stock "
                "hand model '%s' surface %d; keeping stock arms.\n",
                XModelGetName(sourceModel),
                surfaceIndex);

            return false;
        }

        uint16_t rightStoredTriangleCount = 0u;
        uint16_t leftStoredTriangleCount = 0u;
        uint32_t rightVisibleTriangleCount = 0u;
        uint32_t leftVisibleTriangleCount = 0u;

        uint16_t* rightIndices =
            VR_CreateSplitHandIndexList(
                sourceSurface,
                VR_SPLIT_HAND_KEEP_RIGHT,
                leftArmWeights,
                leftHandWeights,
                &rightStoredTriangleCount,
                &rightVisibleTriangleCount);

        uint16_t* leftIndices =
            VR_CreateSplitHandIndexList(
                sourceSurface,
                VR_SPLIT_HAND_KEEP_LEFT_GLOVE,
                leftArmWeights,
                leftHandWeights,
                &leftStoredTriangleCount,
                &leftVisibleTriangleCount);

        Z_Free(
            temporaryWeights,
            21);

        if (rightIndices == nullptr ||
            leftIndices == nullptr)
        {
            return false;
        }

        GfxPackedVertex* wristRootedVertices =
            VR_CreateWristRootedVertexList(
                sourceSurface,
                baseLeftWristPose,
                baseLeftWristAxis);

        GfxPackedVertex* supportGripVertices =
            reinterpret_cast<GfxPackedVertex*>(
                Hunk_Alloc(
                    sourceSurface.vertCount *
                        sizeof(GfxPackedVertex),
                    "VR weapon-support hand vertices",
                    21));

        uint16_t* rigidRootBlends =
            VR_CreateRigidRootBlendList(
                sourceSurface);

        if (wristRootedVertices == nullptr ||
            supportGripVertices == nullptr ||
            rigidRootBlends == nullptr)
        {
            return false;
        }

        std::memcpy(
            supportGripVertices,
            wristRootedVertices,
            sourceSurface.vertCount *
                sizeof(GfxPackedVertex));

        for (uint32_t visibleIndex = 0u;
             visibleIndex <
                 leftVisibleTriangleCount * 3u;
             ++visibleIndex)
        {
            const uint16_t vertexIndex =
                leftIndices[visibleIndex];

            if (vertexIndex >=
                sourceSurface.vertCount)
            {
                return false;
            }

            const float* vertexOrigin =
                wristRootedVertices[
                    vertexIndex].xyz;

            if (!floatingBoundsInitialized)
            {
                for (int component = 0;
                     component < 3;
                     ++component)
                {
                    floatingBoundsMins[component] =
                        vertexOrigin[component];

                    floatingBoundsMaxs[component] =
                        vertexOrigin[component];
                }

                floatingBoundsInitialized = true;
            }
            else
            {
                for (int component = 0;
                     component < 3;
                     ++component)
                {
                    if (vertexOrigin[component] <
                        floatingBoundsMins[component])
                    {
                        floatingBoundsMins[component] =
                            vertexOrigin[component];
                    }

                    if (vertexOrigin[component] >
                        floatingBoundsMaxs[component])
                    {
                        floatingBoundsMaxs[component] =
                            vertexOrigin[component];
                    }
                }
            }

            const float vertexRadiusSquared =
                vertexOrigin[0] * vertexOrigin[0] +
                vertexOrigin[1] * vertexOrigin[1] +
                vertexOrigin[2] * vertexOrigin[2];

            if (vertexRadiusSquared >
                floatingRadiusSquared)
            {
                floatingRadiusSquared =
                    vertexRadiusSquared;
            }
        }

        XSurface& rightSurface =
            rightOnlySurfaces[surfaceIndex];

        XSurface& leftSurface =
            floatingLeftSurfaces[surfaceIndex];

        XSurface& supportSurface =
            supportGripSurfaces[surfaceIndex];

        rightSurface.zoneHandle =
            kVrRuntimeSplitSurfaceZoneHandle;

        rightSurface.baseTriIndex = 0u;
        rightSurface.triIndices = rightIndices;
        rightSurface.triCount =
            rightStoredTriangleCount;

        leftSurface.zoneHandle =
            kVrRuntimeSplitSurfaceZoneHandle;

        leftSurface.baseTriIndex = 0u;
        leftSurface.triIndices = leftIndices;
        leftSurface.verts0 = wristRootedVertices;
        leftSurface.triCount =
            leftStoredTriangleCount;

        // The copied vertices are already in wrist-local coordinates.  Leaving
        // the stock blend table here would transform them once more through
        // the old sleeve, wrist, and finger bones, which collapsed the visible
        // mesh and displaced most of the glove in V9/V10.  A single root
        // influence makes the entire extracted surface follow only the DObj's
        // controller-driven pose while preserving every vertex-to-vertex
        // distance.  V21 also supplies the neutral model with an identity
        // root bind below; otherwise renderer skinning would still prepend
        // the stock root's inverse bind transform to these wrist-local verts.
        leftSurface.deformed = true;
        leftSurface.vertInfo.vertCount[0] =
            static_cast<int16_t>(
                sourceSurface.vertCount);
        leftSurface.vertInfo.vertCount[1] = 0;
        leftSurface.vertInfo.vertCount[2] = 0;
        leftSurface.vertInfo.vertCount[3] = 0;
        leftSurface.vertInfo.vertsBlend =
            rigidRootBlends;
        leftSurface.vertListCount = 0u;
        leftSurface.vertList = nullptr;
        leftSurface.partBits[0] =
            static_cast<int>(0x80000000u);
        leftSurface.partBits[1] = 0;
        leftSurface.partBits[2] = 0;
        leftSurface.partBits[3] = 0;

        supportSurface = leftSurface;
        supportSurface.verts0 =
            supportGripVertices;

        sourceTriangleCount +=
            sourceSurface.triCount;

        rightTriangleCount +=
            rightVisibleTriangleCount;

        leftTriangleCount +=
            leftVisibleTriangleCount;

        rebasedVertexCount +=
            sourceSurface.vertCount;
    }

    if (rightTriangleCount == 0u ||
        leftTriangleCount == 0u ||
        !floatingBoundsInitialized)
    {
        Com_PrintWarning(
            0,
            "[VR][HANDS] Runtime split found %u right and %u left "
            "triangles in stock hand model '%s'; keeping stock arms.\n",
            rightTriangleCount,
            leftTriangleCount,
            XModelGetName(sourceModel));

        return false;
    }

    DObjAnimMat* floatingRootBaseMat =
        reinterpret_cast<DObjAnimMat*>(
            Hunk_Alloc(
                sourceModel->numBones *
                    sizeof(DObjAnimMat),
                "VR floating hand identity root bind",
                21));

    if (floatingRootBaseMat == nullptr)
    {
        return false;
    }

    std::memcpy(
        floatingRootBaseMat,
        sourceModel->baseMat,
        sourceModel->numBones *
            sizeof(DObjAnimMat));

    const DObjAnimMat sourceRootBasePose =
        floatingRootBaseMat[0];

    floatingRootBaseMat[0].quat[0] = 0.0f;
    floatingRootBaseMat[0].quat[1] = 0.0f;
    floatingRootBaseMat[0].quat[2] = 0.0f;
    floatingRootBaseMat[0].quat[3] = 1.0f;
    floatingRootBaseMat[0].trans[0] = 0.0f;
    floatingRootBaseMat[0].trans[1] = 0.0f;
    floatingRootBaseMat[0].trans[2] = 0.0f;
    floatingRootBaseMat[0].transWeight = 2.0f;

    asset->rightOnlyModel =
        *sourceModel;

    asset->floatingLeftModel =
        *sourceModel;

    asset->supportGripModel =
        *sourceModel;

    asset->magazineGripModel =
        *sourceModel;

    asset->floatingRootBaseMat =
        floatingRootBaseMat;

    // These vertices are already wrist-local and use only bone zero.  The
    // renderer builds each skin matrix as inverse(base pose) * current pose;
    // identity is therefore the only base transform that does not rotate or
    // translate the tracked wrist a second time.  Keep the support-grip model
    // on the stock base pose so its already-correct attached rendering is not
    // altered by this free-state repair.
    asset->floatingLeftModel.baseMat =
        asset->floatingRootBaseMat;

    // The magazine-grip geometry is filled by the same settled-pose bake as
    // the weapon-support copy, but it follows an independently published
    // wrist transform.  Give that third DObj the neutral hand's identity root
    // bind so its captured magazine-to-wrist transform is applied exactly
    // once.  The proven rifle-support model retains its stock bind data.
    asset->magazineGripModel.baseMat =
        asset->floatingRootBaseMat;

    // Keep the registered source name.  DObjBad validates fast-file models by
    // name even though these two model headers and index lists are runtime
    // copies of that already-loaded asset.
    asset->rightOnlyModel.name =
        sourceModel->name;

    asset->floatingLeftModel.name =
        sourceModel->name;

    asset->supportGripModel.name =
        sourceModel->name;

    asset->magazineGripModel.name =
        sourceModel->name;

    asset->rightOnlyModel.surfs =
        rightOnlySurfaces;

    asset->floatingLeftModel.surfs =
        floatingLeftSurfaces;

    asset->supportGripModel.surfs =
        supportGripSurfaces;

    // Both closed-hand models intentionally share the baked vertex buffer.
    // They differ only in root bind and DObj pose publication.
    asset->magazineGripModel.surfs =
        supportGripSurfaces;

    // DObjGetSurfaces obtains its pose mask from the model LOD rather than the
    // individual surface.  Restrict every copied LOD to root bone zero as well
    // so no stale arm/finger animation enters the standalone glove DObj.
    for (int lodIndex = 0;
         lodIndex < 4;
         ++lodIndex)
    {
        asset->floatingLeftModel
            .lodInfo[lodIndex]
            .partBits[0] =
                static_cast<int>(0x80000000u);
        asset->floatingLeftModel
            .lodInfo[lodIndex]
            .partBits[1] = 0;
        asset->floatingLeftModel
            .lodInfo[lodIndex]
            .partBits[2] = 0;
        asset->floatingLeftModel
            .lodInfo[lodIndex]
            .partBits[3] = 0;

        asset->supportGripModel
            .lodInfo[lodIndex]
            .partBits[0] =
                static_cast<int>(0x80000000u);
        asset->supportGripModel
            .lodInfo[lodIndex]
            .partBits[1] = 0;
        asset->supportGripModel
            .lodInfo[lodIndex]
            .partBits[2] = 0;
        asset->supportGripModel
            .lodInfo[lodIndex]
            .partBits[3] = 0;

        asset->magazineGripModel
            .lodInfo[lodIndex]
            .partBits[0] =
                static_cast<int>(0x80000000u);
        asset->magazineGripModel
            .lodInfo[lodIndex]
            .partBits[1] = 0;
        asset->magazineGripModel
            .lodInfo[lodIndex]
            .partBits[2] = 0;
        asset->magazineGripModel
            .lodInfo[lodIndex]
            .partBits[3] = 0;
    }

    constexpr float kFloatingHandBoundsPadding =
        1.0f;

    for (int component = 0;
         component < 3;
         ++component)
    {
        asset->floatingLeftModel.mins[component] =
            floatingBoundsMins[component] -
            kFloatingHandBoundsPadding;

        asset->floatingLeftModel.maxs[component] =
            floatingBoundsMaxs[component] +
            kFloatingHandBoundsPadding;

        asset->supportGripModel.mins[component] =
            asset->floatingLeftModel.mins[component];

        asset->supportGripModel.maxs[component] =
            asset->floatingLeftModel.maxs[component];

        asset->magazineGripModel.mins[component] =
            asset->floatingLeftModel.mins[component];

        asset->magazineGripModel.maxs[component] =
            asset->floatingLeftModel.maxs[component];
    }

    asset->floatingLeftModel.radius =
        std::sqrt(
            floatingRadiusSquared) +
        kFloatingHandBoundsPadding;

    asset->supportGripModel.radius =
        asset->floatingLeftModel.radius;

    asset->magazineGripModel.radius =
        asset->floatingLeftModel.radius;

    asset->rightOnlySurfaces =
        rightOnlySurfaces;

    asset->floatingLeftSurfaces =
        floatingLeftSurfaces;

    asset->supportGripSurfaces =
        supportGripSurfaces;

    asset->leftWristIndex =
        leftWristIndex;

    asset->baseLeftWristPose =
        sourceModel->baseMat[
            leftWristIndex];

    asset->sourceTriangleCount =
        sourceTriangleCount;

    asset->rightTriangleCount =
        rightTriangleCount;

    asset->leftTriangleCount =
        leftTriangleCount;

    asset->rebasedVertexCount =
        rebasedVertexCount;

    asset->rigidifiedVertexCount =
        rebasedVertexCount;

    Com_Printf(
        0,
        "[VR][HANDS] Runtime-split stock hand model '%s': kept %u "
        "right-hand/arm triangles and %u floating left glove/cuff "
        "triangles from %u source triangles.\n",
        XModelGetName(sourceModel),
        rightTriangleCount,
        leftTriangleCount,
        sourceTriangleCount);

    Com_Printf(
        0,
        "[VR][HANDS] Built rigid wrist-rooted stock left glove/cuff: "
        "rebased and root-rigidified %u surface vertices around "
        "j_wrist_le; local bounds "
        "[(%.2f %.2f %.2f) to (%.2f %.2f %.2f)], radius %.2f.\n",
        asset->rigidifiedVertexCount,
        asset->floatingLeftModel.mins[0],
        asset->floatingLeftModel.mins[1],
        asset->floatingLeftModel.mins[2],
        asset->floatingLeftModel.maxs[0],
        asset->floatingLeftModel.maxs[1],
        asset->floatingLeftModel.maxs[2],
        asset->floatingLeftModel.radius);

    Com_Printf(
        0,
        "[VR][HANDS] Neutral floating glove now uses an identity "
        "rigid-root bind (stock root quat %.3f %.3f %.3f %.3f, "
        "trans %.3f %.3f %.3f); wrist-local vertices receive the "
        "tracked root exactly once.\n",
        sourceRootBasePose.quat[0],
        sourceRootBasePose.quat[1],
        sourceRootBasePose.quat[2],
        sourceRootBasePose.quat[3],
        sourceRootBasePose.trans[0],
        sourceRootBasePose.trans[1],
        sourceRootBasePose.trans[2]);

    if (asset->modelAnatomicalFrameReady)
    {
        Com_Printf(
            0,
            "[VR][HANDS] Derived the free-hand wrist frame from "
            "j_wrist_le, j_thumb_le_0, and %d stock finger-base "
            "bone(s); model palm anchor (%.2f %.2f %.2f), corrected "
            "OpenXR rows [-Z little-to-thumb (%.3f %.3f %.3f), "
            "-X into-palm (%.3f %.3f %.3f), +Y wrist-to-fingertips "
            "(%.3f %.3f %.3f)].\n",
            anatomicalFingerBaseCount,
            asset->modelPalmAnchor[0],
            asset->modelPalmAnchor[1],
            asset->modelPalmAnchor[2],
            asset->modelAnatomicalGripAxis[0][0],
            asset->modelAnatomicalGripAxis[0][1],
            asset->modelAnatomicalGripAxis[0][2],
            asset->modelAnatomicalGripAxis[1][0],
            asset->modelAnatomicalGripAxis[1][1],
            asset->modelAnatomicalGripAxis[1][2],
            asset->modelAnatomicalGripAxis[2][0],
            asset->modelAnatomicalGripAxis[2][1],
            asset->modelAnatomicalGripAxis[2][2]);
    }

    return true;
}

static bool VR_BakeRigidSupportGripPose(
    VrSplitHandAsset* asset,
    const int weaponIndex,
    DObj_s* weaponViewModel,
    const cpose_t* weaponPose,
    const playerState_s* playerState)
{
    if (asset == nullptr ||
        !asset->ready ||
        weaponIndex <= 0 ||
        weaponViewModel == nullptr ||
        weaponPose == nullptr ||
        playerState == nullptr)
    {
        return false;
    }

    if (asset->supportGripWeaponIndex !=
        weaponIndex)
    {
        asset->supportGripWeaponIndex =
            weaponIndex;
        asset->supportGripPoseBaked = false;
        asset->stableIdlePoseFrames = 0u;
        asset->supportGripLatched = false;
        asset->magazineGripLatched = false;
        asset->magazineToWristPoseValid = false;
        asset->attachmentBlend = 0.0f;
        asset->supportGripBakeFailureLogWritten = false;
        asset->magazineGripPoseFailureLogWritten = false;
    }

    if (asset->supportGripPoseBaked)
    {
        return true;
    }

    // Do not freeze a raise, fire, melee, or reload frame into the permanent
    // floating glove.  Eight consecutive idle frames are enough for the
    // stock support-hand animation to settle while remaining imperceptible at
    // startup.
    if (playerState->weaponstate != WEAPON_READY)
    {
        asset->stableIdlePoseFrames = 0u;
        return false;
    }

    ++asset->stableIdlePoseFrames;

    constexpr uint32_t kRequiredStableIdlePoseFrames =
        8u;

    if (asset->stableIdlePoseFrames <
        kRequiredStableIdlePoseFrames)
    {
        return false;
    }

    if (asset->sourceModel == nullptr ||
        asset->sourceModel->surfs == nullptr ||
        asset->sourceModel->baseMat == nullptr ||
        asset->supportGripSurfaces == nullptr ||
        asset->leftWristIndex < 0 ||
        asset->sourceModel->numBones == 0u ||
        asset->sourceModel->numBones > DOBJ_MAX_PARTS ||
        weaponViewModel->numBones <
            asset->sourceModel->numBones)
    {
        if (!asset->supportGripBakeFailureLogWritten)
        {
            Com_PrintWarning(
                0,
                "[VR][HANDS] The stock idle support-hand pose "
                "could not be baked because its source model or "
                "weapon skeleton was incomplete.\n");

            asset->supportGripBakeFailureLogWritten =
                true;
        }

        return false;
    }

    int posePartBits[4] = {};

    for (int boneIndex = 0;
         boneIndex < asset->sourceModel->numBones;
         ++boneIndex)
    {
        posePartBits[boneIndex >> 5] |=
            static_cast<int>(
                0x80000000u >>
                (boneIndex & 31));
    }

    DObjAnimMat* posedBones =
        CG_DObjCalcPose(
            weaponPose,
            weaponViewModel,
            posePartBits);

    if (posedBones == nullptr)
    {
        if (!asset->supportGripBakeFailureLogWritten)
        {
            Com_PrintWarning(
                0,
                "[VR][HANDS] The active viewmodel did not expose "
                "its settled idle bone matrices; the rigid support "
                "grip will retry on a later frame.\n");

            asset->supportGripBakeFailureLogWritten =
                true;
        }

        return false;
    }

    alignas(16) DObjSkelMat
        deformationMatrices[DOBJ_MAX_PARTS] = {};

    for (int boneIndex = 0;
         boneIndex < asset->sourceModel->numBones;
         ++boneIndex)
    {
        DObjSkelMat inverseBaseMatrix = {};
        DObjSkelMat posedMatrix = {};

        ConvertQuatToInverseSkelMat(
            &asset->sourceModel->baseMat[boneIndex],
            &inverseBaseMatrix);

        ConvertQuatToSkelMat(
            &posedBones[boneIndex],
            &posedMatrix);

        VR_MultiplySkelMat(
            inverseBaseMatrix,
            posedMatrix,
            &deformationMatrices[boneIndex]);
    }

    const DObjAnimMat& posedWrist =
        posedBones[asset->leftWristIndex];

    float posedWristAxis[3][3] = {};

    QuatToAxis(
        posedWrist.quat,
        *reinterpret_cast<mat3x3*>(
            posedWristAxis));

    bool boundsInitialized = false;
    float boundsMins[3] = {};
    float boundsMaxs[3] = {};
    float radiusSquared = 0.0f;
    uint32_t bakedVertexCount = 0u;

    for (int surfaceIndex = 0;
         surfaceIndex < asset->sourceModel->numsurfs;
         ++surfaceIndex)
    {
        const XSurface& sourceSurface =
            asset->sourceModel->surfs[surfaceIndex];

        XSurface& supportSurface =
            asset->supportGripSurfaces[surfaceIndex];

        if (!sourceSurface.deformed ||
            sourceSurface.verts0 == nullptr ||
            sourceSurface.vertInfo.vertsBlend == nullptr ||
            sourceSurface.vertCount == 0u ||
            supportSurface.verts0 == nullptr ||
            supportSurface.triIndices == nullptr)
        {
            if (!asset->supportGripBakeFailureLogWritten)
            {
                Com_PrintWarning(
                    0,
                    "[VR][HANDS] A stock hand surface lacked the "
                    "CPU skinning data needed to bake the support "
                    "grip; the rigid pose was not replaced.\n");

                asset->supportGripBakeFailureLogWritten =
                    true;
            }

            return false;
        }

        GfxPackedVertex* posedVertices =
            reinterpret_cast<GfxPackedVertex*>(
                Z_Malloc(
                    sourceSurface.vertCount *
                        sizeof(GfxPackedVertex),
                    "VR support-hand pose vertices",
                    21));

        if (posedVertices == nullptr)
        {
            return false;
        }

        R_SkinXSurfaceWeight(
            sourceSurface.verts0,
            &sourceSurface.vertInfo,
            deformationMatrices,
            posedVertices);

        GfxPackedVertex* rigidVertices =
            supportSurface.verts0;

        for (uint32_t vertexIndex = 0u;
             vertexIndex < sourceSurface.vertCount;
             ++vertexIndex)
        {
            const GfxPackedVertex& posedVertex =
                posedVertices[vertexIndex];

            GfxPackedVertex& rigidVertex =
                rigidVertices[vertexIndex];

            rigidVertex = posedVertex;

            InvMatrixTransformVectorQuatTrans(
                posedVertex.xyz,
                &posedWrist,
                rigidVertex.xyz);

            float posedNormal[3] = {};
            float rigidNormal[3] = {};

            Vec3UnpackUnitVec(
                posedVertex.normal,
                posedNormal);

            MatrixTransposeTransformVector(
                posedNormal,
                *reinterpret_cast<const mat3x3*>(
                    posedWristAxis),
                rigidNormal);

            Vec3Normalize(rigidNormal);

            rigidVertex.normal =
                Vec3PackUnitVec(rigidNormal);

            float posedTangent[3] = {};
            float rigidTangent[3] = {};

            Vec3UnpackUnitVec(
                posedVertex.tangent,
                posedTangent);

            MatrixTransposeTransformVector(
                posedTangent,
                *reinterpret_cast<const mat3x3*>(
                    posedWristAxis),
                rigidTangent);

            Vec3Normalize(rigidTangent);

            rigidVertex.tangent =
                Vec3PackUnitVec(rigidTangent);
        }

        Z_Free(
            posedVertices,
            21);

        for (uint32_t index = 0u;
             index <
                 static_cast<uint32_t>(
                     supportSurface.triCount) *
                     3u;
             ++index)
        {
            const uint16_t vertexIndex =
                supportSurface.triIndices[index];

            if (vertexIndex >=
                supportSurface.vertCount)
            {
                return false;
            }

            const float* vertexOrigin =
                rigidVertices[vertexIndex].xyz;

            if (!boundsInitialized)
            {
                for (int component = 0;
                     component < 3;
                     ++component)
                {
                    boundsMins[component] =
                        vertexOrigin[component];

                    boundsMaxs[component] =
                        vertexOrigin[component];
                }

                boundsInitialized = true;
            }
            else
            {
                for (int component = 0;
                     component < 3;
                     ++component)
                {
                    if (vertexOrigin[component] <
                        boundsMins[component])
                    {
                        boundsMins[component] =
                            vertexOrigin[component];
                    }

                    if (vertexOrigin[component] >
                        boundsMaxs[component])
                    {
                        boundsMaxs[component] =
                            vertexOrigin[component];
                    }
                }
            }

            const float vertexRadiusSquared =
                vertexOrigin[0] * vertexOrigin[0] +
                vertexOrigin[1] * vertexOrigin[1] +
                vertexOrigin[2] * vertexOrigin[2];

            if (vertexRadiusSquared > radiusSquared)
            {
                radiusSquared = vertexRadiusSquared;
            }
        }

        bakedVertexCount +=
            sourceSurface.vertCount;
    }

    if (!boundsInitialized)
    {
        return false;
    }

    constexpr float kSupportGripBoundsPadding =
        1.0f;

    for (int component = 0;
         component < 3;
         ++component)
    {
        asset->supportGripModel.mins[component] =
            boundsMins[component] -
            kSupportGripBoundsPadding;

        asset->supportGripModel.maxs[component] =
            boundsMaxs[component] +
            kSupportGripBoundsPadding;

        asset->magazineGripModel.mins[component] =
            asset->supportGripModel.mins[component];

        asset->magazineGripModel.maxs[component] =
            asset->supportGripModel.maxs[component];
    }

    asset->supportGripModel.radius =
        std::sqrt(radiusSquared) +
        kSupportGripBoundsPadding;

    asset->magazineGripModel.radius =
        asset->supportGripModel.radius;

    if (asset->supportGripObjectCreated)
    {
        asset->supportGripObject.radius =
            asset->supportGripModel.radius;
    }

    if (asset->magazineGripObjectCreated)
    {
        asset->magazineGripObject.radius =
            asset->magazineGripModel.radius;
    }

    asset->supportGripPoseBaked = true;
    asset->supportGripBakeFailureLogWritten = false;

    Com_Printf(
        0,
        "[VR][HANDS] Baked the settled stock weapon-support grip "
        "for weapon %d into %u rigid wrist-local vertices; the "
        "attached fingers now use that weapon's own handle pose "
        "without inheriting its arm skeleton.\n",
        weaponIndex,
        bakedVertexCount);

    return true;
}

static VrSplitHandAsset* VR_GetSplitHandAsset(
    XModel* sourceModel)
{
    if (!VR_TrackedHandsEnabled() ||
        sourceModel == nullptr)
    {
        return nullptr;
    }

    for (VrSplitHandAsset& asset :
         s_vrSplitHandAssets)
    {
        if (asset.sourceModel ==
            sourceModel)
        {
            return asset.ready
                ? &asset
                : nullptr;
        }
    }

    for (VrSplitHandAsset& asset :
         s_vrSplitHandAssets)
    {
        if (asset.sourceModel != nullptr)
        {
            continue;
        }

        asset.sourceModel =
            sourceModel;

        asset.ready =
            VR_BuildSplitHandModels(
                &asset,
                sourceModel);

        asset.failed =
            !asset.ready;

        return asset.ready
            ? &asset
            : nullptr;
    }

    static bool loggedAssetCapacityFailure = false;

    if (!loggedAssetCapacityFailure)
    {
        Com_PrintWarning(
            0,
            "[VR][HANDS] Runtime hand-model split cache is full; "
            "additional hand models retain stock arms.\n");

        loggedAssetCapacityFailure = true;
    }

    return nullptr;
}

static VrSplitHandAsset* VR_ResolveSplitHandAssetForWeapon(
    DObj_s* weaponViewModel,
    XModel* sourceHandModel)
{
    XModel* submittedWeaponHandModel = nullptr;

    if (weaponViewModel != nullptr &&
        DObjGetNumModels(weaponViewModel) > 0)
    {
        submittedWeaponHandModel =
            DObjGetModel(
                weaponViewModel,
                0);
    }

    for (VrSplitHandAsset& asset :
         s_vrSplitHandAssets)
    {
        if (!asset.ready)
        {
            continue;
        }

        if (asset.sourceModel == sourceHandModel ||
            &asset.rightOnlyModel == sourceHandModel ||
            &asset.floatingLeftModel == sourceHandModel ||
            &asset.supportGripModel == sourceHandModel ||
            &asset.magazineGripModel == sourceHandModel ||
            submittedWeaponHandModel == asset.sourceModel ||
            submittedWeaponHandModel == &asset.rightOnlyModel ||
            submittedWeaponHandModel == &asset.floatingLeftModel ||
            submittedWeaponHandModel == &asset.supportGripModel ||
            submittedWeaponHandModel == &asset.magazineGripModel)
        {
            return &asset;
        }
    }

    return VR_GetSplitHandAsset(
        sourceHandModel);
}

static XModel* VR_GetRightOnlyWeaponHandModel(
    XModel* sourceModel)
{
    VrSplitHandAsset* asset =
        VR_GetSplitHandAsset(
            sourceModel);

    return asset != nullptr
        ? &asset->rightOnlyModel
        : sourceModel;
}

static bool VR_CreateFloatingLeftHandObjects(
    VrSplitHandAsset* asset)
{
    if (asset == nullptr ||
        !asset->ready)
    {
        return false;
    }

    if (asset->floatingObjectCreated &&
        asset->supportGripObjectCreated &&
        asset->magazineGripObjectCreated)
    {
        return true;
    }

    if (!asset->floatingObjectCreated)
    {
        DObjModel_s freeModelDescription = {};
        freeModelDescription.model =
            &asset->floatingLeftModel;
        freeModelDescription.boneName = 0;
        freeModelDescription.ignoreCollision = false;

        DObjCreate(
            &freeModelDescription,
            1u,
            nullptr,
            &asset->floatingLeftObject,
            0);

        asset->floatingLeftPose.eType =
            ET_GENERAL;

        if (DObjGetNumModels(
                &asset->floatingLeftObject) != 1 ||
            DObjGetModel(
                &asset->floatingLeftObject,
                0) != &asset->floatingLeftModel)
        {
            Com_PrintWarning(
                0,
                "[VR][HANDS] The neutral floating-glove DObj was "
                "created with an unexpected model.\n");

            DObjFree(
                &asset->floatingLeftObject);

            return false;
        }

        asset->floatingObjectCreated = true;
    }

    if (!asset->supportGripObjectCreated)
    {
        DObjModel_s gripModelDescription = {};
        gripModelDescription.model =
            &asset->supportGripModel;
        gripModelDescription.boneName = 0;
        gripModelDescription.ignoreCollision = false;

        DObjCreate(
            &gripModelDescription,
            1u,
            nullptr,
            &asset->supportGripObject,
            0);

        asset->supportGripPose.eType =
            ET_GENERAL;

        if (DObjGetNumModels(
                &asset->supportGripObject) != 1 ||
            DObjGetModel(
                &asset->supportGripObject,
                0) != &asset->supportGripModel)
        {
            Com_PrintWarning(
                0,
                "[VR][HANDS] The weapon-support glove DObj was "
                "created with an unexpected model.\n");

            DObjFree(
                &asset->supportGripObject);

            return false;
        }

        asset->supportGripObjectCreated = true;
    }

    if (!asset->magazineGripObjectCreated)
    {
        DObjModel_s magazineModelDescription = {};
        magazineModelDescription.model =
            &asset->magazineGripModel;
        magazineModelDescription.boneName = 0;
        magazineModelDescription.ignoreCollision = false;

        DObjCreate(
            &magazineModelDescription,
            1u,
            nullptr,
            &asset->magazineGripObject,
            0);

        asset->magazineGripPose.eType =
            ET_GENERAL;

        if (DObjGetNumModels(
                &asset->magazineGripObject) != 1 ||
            DObjGetModel(
                &asset->magazineGripObject,
                0) != &asset->magazineGripModel)
        {
            Com_PrintWarning(
                0,
                "[VR][HANDS] The held-magazine glove DObj was "
                "created with an unexpected model.\n");

            DObjFree(
                &asset->magazineGripObject);

            return false;
        }

        asset->magazineGripObjectCreated = true;
    }

    if (!asset->objectCreationLogWritten)
    {
        Com_Printf(
            0,
            "[VR][HANDS] Created independent neutral, weapon-support, "
            "and held-magazine left-glove DObjs from the extracted "
            "stock geometry.\n");

        asset->objectCreationLogWritten =
            true;
    }

    return true;
}

static bool VR_NormalizeHandQuaternion(
    const float input[4],
    float output[4])
{
    if (input == nullptr ||
        output == nullptr)
    {
        return false;
    }

    const float lengthSquared =
        input[0] * input[0] +
        input[1] * input[1] +
        input[2] * input[2] +
        input[3] * input[3];

    if (!std::isfinite(lengthSquared) ||
        lengthSquared <= 0.000001f)
    {
        return false;
    }

    const float inverseLength =
        1.0f / std::sqrt(lengthSquared);

    for (int component = 0;
         component < 4;
         ++component)
    {
        output[component] =
            input[component] *
            inverseLength;
    }

    return true;
}

static float VR_HandDirectionDot(
    const float first[3],
    const float second[3])
{
    return
        first[0] * second[0] +
        first[1] * second[1] +
        first[2] * second[2];
}

static float VR_HandAxisRigidityError(
    const float axis[3][3])
{
    if (axis == nullptr)
    {
        return 999.0f;
    }

    float maximumError = 0.0f;

    for (int row = 0; row < 3; ++row)
    {
        const float lengthSquared =
            VR_HandDirectionDot(
                axis[row],
                axis[row]);

        if (!std::isfinite(lengthSquared) ||
            lengthSquared <= 0.000001f)
        {
            return 999.0f;
        }

        const float lengthError =
            std::fabs(
                std::sqrt(lengthSquared) -
                1.0f);

        if (lengthError > maximumError)
        {
            maximumError = lengthError;
        }
    }

    for (int firstRow = 0;
         firstRow < 3;
         ++firstRow)
    {
        for (int secondRow = firstRow + 1;
             secondRow < 3;
             ++secondRow)
        {
            const float firstLength =
                std::sqrt(
                    VR_HandDirectionDot(
                        axis[firstRow],
                        axis[firstRow]));

            const float secondLength =
                std::sqrt(
                    VR_HandDirectionDot(
                        axis[secondRow],
                        axis[secondRow]));

            const float normalizedDot =
                std::fabs(
                    VR_HandDirectionDot(
                        axis[firstRow],
                        axis[secondRow]) /
                    (firstLength * secondLength));

            if (normalizedDot > maximumError)
            {
                maximumError = normalizedDot;
            }
        }
    }

    return maximumError;
}

static bool VR_BuildRigidTrackedHandAxis(
    const float input[3][3],
    float output[3][3])
{
    if (input == nullptr ||
        output == nullptr)
    {
        return false;
    }

    // Retain the third row exactly after normalization, remove its component
    // from the first row, then rebuild the middle and first rows with cross
    // products.  Both grip/pose and palm_ext/pose use the same proper
    // [-Z, -X, +Y] row ordering even though their anatomical meanings differ.
    float wristToFingers[3] = {
        input[2][0],
        input[2][1],
        input[2][2],
    };

    if (!VR_NormalizeModelHandDirection(
            wristToFingers))
    {
        return false;
    }

    const float lateralAlongFingers =
        VR_HandDirectionDot(
            input[0],
            wristToFingers);

    float littleToThumb[3] = {
        input[0][0] -
            lateralAlongFingers *
                wristToFingers[0],
        input[0][1] -
            lateralAlongFingers *
                wristToFingers[1],
        input[0][2] -
            lateralAlongFingers *
                wristToFingers[2],
    };

    if (!VR_NormalizeModelHandDirection(
            littleToThumb))
    {
        // Degenerate -Z/+Y input: retain +Y and recover -Z from the raw -X
        // row instead.  A valid right-handed grip has -X cross +Y = -Z.
        VR_CrossModelHandDirections(
            input[1],
            wristToFingers,
            littleToThumb);

        if (!VR_NormalizeModelHandDirection(
                littleToThumb))
        {
            return false;
        }
    }

    float intoPalm[3] = {};

    // For the published row convention, (-Z) cross (-X) = +Y, therefore
    // +Y cross -Z reconstructs -X.
    VR_CrossModelHandDirections(
        wristToFingers,
        littleToThumb,
        intoPalm);

    if (!VR_NormalizeModelHandDirection(
            intoPalm))
    {
        return false;
    }

    // Recompute -Z so all three rows are mutually perpendicular even after
    // the fallback path and floating-point normalization.
    VR_CrossModelHandDirections(
        intoPalm,
        wristToFingers,
        littleToThumb);

    if (!VR_NormalizeModelHandDirection(
            littleToThumb))
    {
        return false;
    }

    for (int component = 0;
         component < 3;
         ++component)
    {
        output[0][component] =
            littleToThumb[component];
        output[1][component] =
            intoPalm[component];
        output[2][component] =
            wristToFingers[component];
    }

    return
        VR_HandAxisRigidityError(output) <=
        0.0001f;
}

static bool VR_BuildLeftHandPoseAxisFromOpenXrQuaternion(
    const float orientationHeadLocalOpenXr[4],
    const float cameraAxis[3][3],
    float worldAxis[3][3])
{
    if (orientationHeadLocalOpenXr == nullptr ||
        cameraAxis == nullptr ||
        worldAxis == nullptr)
    {
        return false;
    }

    float orientation[4] = {};

    if (!VR_NormalizeHandQuaternion(
            orientationHeadLocalOpenXr,
            orientation))
    {
        return false;
    }

    // These are the same canonical OpenXR grip directions drawn by the
    // compositor proxy: red -Z, green -X, and blue +Y.  Rotate them with the
    // original normalized runtime quaternion, convert OpenXR head-local axes
    // to CoD camera-local axes, then place them in the current world camera.
    static const float canonicalDirections[3][3] = {
        {0.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };

    const float quaternionVector[3] = {
        orientation[0],
        orientation[1],
        orientation[2],
    };

    for (int axisRow = 0;
         axisRow < 3;
         ++axisRow)
    {
        float twiceCross[3] = {};

        VR_CrossModelHandDirections(
            quaternionVector,
            canonicalDirections[axisRow],
            twiceCross);

        twiceCross[0] *= 2.0f;
        twiceCross[1] *= 2.0f;
        twiceCross[2] *= 2.0f;

        float secondCross[3] = {};

        VR_CrossModelHandDirections(
            quaternionVector,
            twiceCross,
            secondCross);

        const float rotatedOpenXr[3] = {
            canonicalDirections[axisRow][0] +
                orientation[3] * twiceCross[0] +
                secondCross[0],
            canonicalDirections[axisRow][1] +
                orientation[3] * twiceCross[1] +
                secondCross[1],
            canonicalDirections[axisRow][2] +
                orientation[3] * twiceCross[2] +
                secondCross[2],
        };

        const float cameraLocalCod[3] = {
            -rotatedOpenXr[2],
            -rotatedOpenXr[0],
            rotatedOpenXr[1],
        };

        for (int worldComponent = 0;
             worldComponent < 3;
             ++worldComponent)
        {
            worldAxis[axisRow][worldComponent] =
                cameraLocalCod[0] *
                    cameraAxis[0][worldComponent] +
                cameraLocalCod[1] *
                    cameraAxis[1][worldComponent] +
                cameraLocalCod[2] *
                    cameraAxis[2][worldComponent];
        }
    }

    return
        VR_HandAxisRigidityError(worldAxis) <=
        0.0001f;
}

static bool VR_BuildFreeLeftHandTransform(
    const VrSplitHandAsset* asset,
    const float leftControllerOrigin[3],
    const float leftControllerAxis[3][3],
    const bool palmSurfacePose,
    float wristOrigin[3],
    float wristAxis[3][3],
    float wristQuaternion[4])
{
    if (asset == nullptr ||
        leftControllerOrigin == nullptr ||
        leftControllerAxis == nullptr ||
        wristOrigin == nullptr ||
        wristAxis == nullptr ||
        wristQuaternion == nullptr)
    {
        return false;
    }

    VR_RegisterFloatingLeftHandCalibrationDvars();

    if (s_vrLeftHandOffsetForward == nullptr ||
        s_vrLeftHandOffsetLeft == nullptr ||
        s_vrLeftHandOffsetUp == nullptr ||
        s_vrLeftHandPitch == nullptr ||
        s_vrLeftHandYaw == nullptr ||
        s_vrLeftHandRoll == nullptr ||
        s_vrLeftHandGripRadius == nullptr)
    {
        return false;
    }

    const float controllerLocalOffset[3] = {
        s_vrLeftHandOffsetForward->current.value,
        s_vrLeftHandOffsetLeft->current.value,
        s_vrLeftHandOffsetUp->current.value,
    };

    const float controllerLocalFitAngles[3] = {
        s_vrLeftHandPitch->current.value,
        s_vrLeftHandYaw->current.value,
        s_vrLeftHandRoll->current.value,
    };

    float controllerLocalFitAxis[3][3] = {};

    AnglesToAxis(
        controllerLocalFitAngles,
        *reinterpret_cast<mat3x3*>(
            controllerLocalFitAxis));

    float modelAnatomicalPoseAxis[3][3] = {};

    for (int component = 0;
         component < 3;
         ++component)
    {
        if (palmSurfacePose)
        {
            // XR_EXT_palm_pose / OpenXR 1.1 grip_surface semantics:
            // -Z follows a straightened index finger; +X points away from
            // the left palm, therefore -X points into it; and +Y follows
            // from the right-hand rule.  The stored model rows are
            // little-to-thumb, away-from-palm (historically misnamed
            // "intoPalm"), and wrist-to-fingertips.  V25 left the last two
            // palm-surface rows reversed.  Negate both to remove that exact
            // 180-degree roll while retaining a proper rotation.
            modelAnatomicalPoseAxis[0][component] =
                asset->modelAnatomicalGripAxis[2][component];
            modelAnatomicalPoseAxis[1][component] =
                -asset->modelAnatomicalGripAxis[1][component];
            modelAnatomicalPoseAxis[2][component] =
                asset->modelAnatomicalGripAxis[0][component];
        }
        else
        {
            modelAnatomicalPoseAxis[0][component] =
                asset->modelAnatomicalGripAxis[0][component];
            modelAnatomicalPoseAxis[1][component] =
                asset->modelAnatomicalGripAxis[1][component];
            modelAnatomicalPoseAxis[2][component] =
                asset->modelAnatomicalGripAxis[2][component];
        }
    }

    float rigidModelAnatomicalGripAxis[3][3] = {};

    if (!VR_BuildRigidTrackedHandAxis(
            modelAnatomicalPoseAxis,
            rigidModelAnatomicalGripAxis))
    {
        return false;
    }

    float inverseModelAnatomicalGripAxis[3][3] = {};

    for (int row = 0;
         row < 3;
         ++row)
    {
        for (int column = 0;
             column < 3;
             ++column)
        {
            inverseModelAnatomicalGripAxis[row][column] =
                rigidModelAnatomicalGripAxis[column][row];
        }
    }

    float modelWristAxis[3][3] = {};

    MatrixMultiply(
        *reinterpret_cast<const mat3x3*>(
            inverseModelAnatomicalGripAxis),
        *reinterpret_cast<const mat3x3*>(
            leftControllerAxis),
        *reinterpret_cast<mat3x3*>(
            modelWristAxis));

    // CoD basis vectors are rows.  The model-derived anatomical remap and any
    // fit adjustment are both constant in controller space.  The first is
    // derived from this exact stock glove rather than guessed, so neither its
    // relationship nor the direction of a wrist motion can change at runtime.
    MatrixMultiply(
        *reinterpret_cast<const mat3x3*>(
            controllerLocalFitAxis),
        *reinterpret_cast<const mat3x3*>(
            modelWristAxis),
        *reinterpret_cast<mat3x3*>(
            wristAxis));

    for (int component = 0;
         component < 3;
         ++component)
    {
        const float palmAnchorWorld =
            asset->modelPalmAnchor[0] *
                wristAxis[0][component] +
            asset->modelPalmAnchor[1] *
                wristAxis[1][component] +
            asset->modelPalmAnchor[2] *
                wristAxis[2][component];

        wristOrigin[component] =
            leftControllerOrigin[component] -
            palmAnchorWorld +
            controllerLocalOffset[0] *
                wristAxis[0][component] +
            controllerLocalOffset[1] *
                wristAxis[1][component] +
            controllerLocalOffset[2] *
                wristAxis[2][component];
    }

    float unnormalizedQuaternion[4] = {};

    AxisToQuat(
        wristAxis,
        unnormalizedQuaternion);

    return VR_NormalizeHandQuaternion(
        unnormalizedQuaternion,
        wristQuaternion);
}

static bool VR_BlendHandQuaternion(
    const float freeQuaternion[4],
    const float gripQuaternion[4],
    const float blend,
    float output[4])
{
    if (freeQuaternion == nullptr ||
        gripQuaternion == nullptr ||
        output == nullptr)
    {
        return false;
    }

    float clampedBlend = blend;

    if (clampedBlend < 0.0f)
    {
        clampedBlend = 0.0f;
    }
    else if (clampedBlend > 1.0f)
    {
        clampedBlend = 1.0f;
    }

    const float dot =
        freeQuaternion[0] * gripQuaternion[0] +
        freeQuaternion[1] * gripQuaternion[1] +
        freeQuaternion[2] * gripQuaternion[2] +
        freeQuaternion[3] * gripQuaternion[3];

    const float gripSign =
        dot < 0.0f
            ? -1.0f
            : 1.0f;

    float blended[4] = {};

    for (int component = 0;
         component < 4;
         ++component)
    {
        blended[component] =
            freeQuaternion[component] *
                (1.0f - clampedBlend) +
            gripQuaternion[component] *
                gripSign *
                clampedBlend;
    }

    return VR_NormalizeHandQuaternion(
        blended,
        output);
}

static bool VR_SetRigidHandRootPose(
    DObj_s* handObject,
    cpose_t* handPose,
    const float wristOrigin[3],
    const float wristQuaternion[4])
{
    if (handObject == nullptr ||
        handPose == nullptr ||
        wristOrigin == nullptr ||
        wristQuaternion == nullptr)
    {
        return false;
    }

    handPose->eType = ET_GENERAL;

    for (int component = 0;
         component < 3;
         ++component)
    {
        handPose->origin[component] =
            wristOrigin[component];
        handPose->angles[component] = 0.0f;
    }

    // cpose_t stores only Euler angles.  Build its ordinary identity-root
    // skeleton, then replace root zero with the tracked quaternion before the
    // renderer requests the same-frame pose.  The root-rigid glove surfaces
    // use no other bone, so the tracked rotation never passes through Euler.
    DObjClearSkel(handObject);

    int rootPartBits[4] = {
        static_cast<int>(0x80000000u),
        0,
        0,
        0,
    };

    DObjAnimMat* rootPose =
        CG_DObjCalcPose(
            handPose,
            handObject,
            rootPartBits);

    if (rootPose == nullptr)
    {
        return false;
    }

    const cg_s* cgameGlob =
        CG_GetLocalClientGlobals(0);

    if (cgameGlob == nullptr)
    {
        return false;
    }

    const float viewRelativeOrigin[3] = {
        wristOrigin[0] -
            cgameGlob->refdef.viewOffset[0],
        wristOrigin[1] -
            cgameGlob->refdef.viewOffset[1],
        wristOrigin[2] -
            cgameGlob->refdef.viewOffset[2],
    };

    DObjSetTrans(
        &rootPose[0],
        viewRelativeOrigin);

    for (int component = 0;
         component < 4;
         ++component)
    {
        rootPose[0].quat[component] =
            wristQuaternion[component];
    }

    NormalizeQuatTrans(
        &rootPose[0]);

    return true;
}

static void VR_HandQuaternionToAxis(
    const float quaternion[4],
    float axis[3][3])
{
    if (quaternion == nullptr ||
        axis == nullptr)
    {
        return;
    }

    const float doubledX =
        quaternion[0] + quaternion[0];
    const float doubledY =
        quaternion[1] + quaternion[1];
    const float doubledZ =
        quaternion[2] + quaternion[2];
    const float xx = doubledX * quaternion[0];
    const float xy = doubledX * quaternion[1];
    const float xz = doubledX * quaternion[2];
    const float xw = doubledX * quaternion[3];
    const float yy = doubledY * quaternion[1];
    const float yz = doubledY * quaternion[2];
    const float yw = doubledY * quaternion[3];
    const float zz = doubledZ * quaternion[2];
    const float zw = doubledZ * quaternion[3];

    // Match COD4's row-basis ConvertQuatToMat convention exactly.
    axis[0][0] = 1.0f - (yy + zz);
    axis[0][1] = xy + zw;
    axis[0][2] = xz - yw;
    axis[1][0] = xy - zw;
    axis[1][1] = 1.0f - (xx + zz);
    axis[1][2] = yz + xw;
    axis[2][0] = xz + yw;
    axis[2][1] = yz - xw;
    axis[2][2] = 1.0f - (xx + yy);
}

static float VR_HandAxisDifferenceDegrees(
    const float first[3][3],
    const float second[3][3])
{
    if (first == nullptr ||
        second == nullptr)
    {
        return -1.0f;
    }

    float firstQuaternionRaw[4] = {};
    float secondQuaternionRaw[4] = {};

    AxisToQuat(
        first,
        firstQuaternionRaw);

    AxisToQuat(
        second,
        secondQuaternionRaw);

    float firstQuaternion[4] = {};
    float secondQuaternion[4] = {};

    if (!VR_NormalizeHandQuaternion(
            firstQuaternionRaw,
            firstQuaternion) ||
        !VR_NormalizeHandQuaternion(
            secondQuaternionRaw,
            secondQuaternion))
    {
        return -1.0f;
    }

    float absoluteDot =
        std::fabs(
            firstQuaternion[0] * secondQuaternion[0] +
            firstQuaternion[1] * secondQuaternion[1] +
            firstQuaternion[2] * secondQuaternion[2] +
            firstQuaternion[3] * secondQuaternion[3]);

    if (absoluteDot > 1.0f)
    {
        absoluteDot = 1.0f;
    }

    constexpr float radiansToDegrees =
        57.295779513082320876f;

    return 2.0f *
        std::acos(absoluteDot) *
        radiansToDegrees;
}

static void VR_LogFreeLeftHandTransformDiagnostic(
    VrSplitHandAsset* asset,
    const float controllerOrigin[3],
    const float quaternionControllerAxis[3][3],
    const float controllerAxis[3][3],
    const float wristOrigin[3],
    const float wristAxis[3][3],
    const float wristQuaternion[4],
    const float cameraAxis[3][3],
    DObj_s* handObject,
    const cpose_t* handPose)
{
    if (asset == nullptr ||
        controllerOrigin == nullptr ||
        quaternionControllerAxis == nullptr ||
        controllerAxis == nullptr ||
        wristOrigin == nullptr ||
        wristAxis == nullptr ||
        wristQuaternion == nullptr ||
        cameraAxis == nullptr ||
        handObject == nullptr ||
        handPose == nullptr ||
        !VR_TrackedHandDiagnosticsEnabled())
    {
        return;
    }

    const float controllerToWristWorld[3] = {
        wristOrigin[0] - controllerOrigin[0],
        wristOrigin[1] - controllerOrigin[1],
        wristOrigin[2] - controllerOrigin[2],
    };

    float controllerToWristOrigin[3] = {};
    float controllerToWristAxis[3][3] = {};

    for (int controllerRow = 0;
         controllerRow < 3;
         ++controllerRow)
    {
        for (int worldComponent = 0;
             worldComponent < 3;
             ++worldComponent)
        {
            controllerToWristOrigin[controllerRow] +=
                controllerToWristWorld[worldComponent] *
                controllerAxis[controllerRow][worldComponent];
        }
    }

    for (int wristRow = 0;
         wristRow < 3;
         ++wristRow)
    {
        for (int controllerRow = 0;
             controllerRow < 3;
             ++controllerRow)
        {
            for (int worldComponent = 0;
                 worldComponent < 3;
                 ++worldComponent)
            {
                controllerToWristAxis[wristRow][controllerRow] +=
                    wristAxis[wristRow][worldComponent] *
                    controllerAxis[controllerRow][worldComponent];
            }
        }
    }

    if (!asset->freeTransformDiagnosticBaselineReady)
    {
        std::memcpy(
            asset->diagnosticControllerToWristOrigin,
            controllerToWristOrigin,
            sizeof(
                asset->diagnosticControllerToWristOrigin));

        std::memcpy(
            asset->diagnosticControllerToWristAxis,
            controllerToWristAxis,
            sizeof(
                asset->diagnosticControllerToWristAxis));

        asset->freeTransformDiagnosticBaselineReady =
            true;

        Com_Printf(
            0,
            "[VR][HANDS][DIAG] Baseline controller-to-wrist frame: "
            "pos (%.4f %.4f %.4f), rows "
            "[(%.4f %.4f %.4f) (%.4f %.4f %.4f) "
            "(%.4f %.4f %.4f)].  These values must remain "
            "invariant through wrist and body rotation.\n",
            controllerToWristOrigin[0],
            controllerToWristOrigin[1],
            controllerToWristOrigin[2],
            controllerToWristAxis[0][0],
            controllerToWristAxis[0][1],
            controllerToWristAxis[0][2],
            controllerToWristAxis[1][0],
            controllerToWristAxis[1][1],
            controllerToWristAxis[1][2],
            controllerToWristAxis[2][0],
            controllerToWristAxis[2][1],
            controllerToWristAxis[2][2]);

        Com_Printf(
            0,
            "[VR][HANDS][DIAG] Baseline rigidity error: camera "
            "%.6f; direct quaternion pose %.6f; stored model %.6f; repaired "
            "grip %.6f; final wrist %.6f.  Zero is a rigid "
            "orthonormal frame.\n",
            VR_HandAxisRigidityError(cameraAxis),
            VR_HandAxisRigidityError(
                quaternionControllerAxis),
            VR_HandAxisRigidityError(
                asset->modelAnatomicalGripAxis),
            VR_HandAxisRigidityError(controllerAxis),
            VR_HandAxisRigidityError(wristAxis));
    }

    const float originDrift[3] = {
        controllerToWristOrigin[0] -
            asset->diagnosticControllerToWristOrigin[0],
        controllerToWristOrigin[1] -
            asset->diagnosticControllerToWristOrigin[1],
        controllerToWristOrigin[2] -
            asset->diagnosticControllerToWristOrigin[2],
    };

    const float originDriftLength =
        std::sqrt(
            originDrift[0] * originDrift[0] +
            originDrift[1] * originDrift[1] +
            originDrift[2] * originDrift[2]);

    const float relativeRotationDriftDegrees =
        VR_HandAxisDifferenceDegrees(
            controllerToWristAxis,
            asset->diagnosticControllerToWristAxis);

    float quaternionAxis[3][3] = {};

    VR_HandQuaternionToAxis(
        wristQuaternion,
        quaternionAxis);

    const float quaternionRoundTripDegrees =
        VR_HandAxisDifferenceDegrees(
            quaternionAxis,
            wristAxis);

    float rootRotationDegrees = -1.0f;
    float rootTranslationError = -1.0f;

    DObjAnimMat* publishedRoot =
        DObjGetRotTransArray(
            handObject);

    const cg_s* cgameGlob =
        CG_GetLocalClientGlobals(0);

    if (publishedRoot != nullptr &&
        cgameGlob != nullptr)
    {
        float normalizedRootQuaternion[4] = {};

        if (VR_NormalizeHandQuaternion(
                publishedRoot[0].quat,
                normalizedRootQuaternion))
        {
            float publishedRootAxis[3][3] = {};

            VR_HandQuaternionToAxis(
                normalizedRootQuaternion,
                publishedRootAxis);

            rootRotationDegrees =
                VR_HandAxisDifferenceDegrees(
                    publishedRootAxis,
                    wristAxis);
        }

        const float expectedRootTranslation[3] = {
            wristOrigin[0] -
                cgameGlob->refdef.viewOffset[0],
            wristOrigin[1] -
                cgameGlob->refdef.viewOffset[1],
            wristOrigin[2] -
                cgameGlob->refdef.viewOffset[2],
        };

        const float rootTranslationDelta[3] = {
            publishedRoot[0].trans[0] -
                expectedRootTranslation[0],
            publishedRoot[0].trans[1] -
                expectedRootTranslation[1],
            publishedRoot[0].trans[2] -
                expectedRootTranslation[2],
        };

        rootTranslationError =
            std::sqrt(
                rootTranslationDelta[0] *
                    rootTranslationDelta[0] +
                rootTranslationDelta[1] *
                    rootTranslationDelta[1] +
                rootTranslationDelta[2] *
                    rootTranslationDelta[2]);
    }

    const uint32_t nowMilliseconds =
        static_cast<uint32_t>(
            Sys_Milliseconds());

    if (asset->lastFreeTransformDiagnosticMilliseconds != 0u &&
        nowMilliseconds -
            asset->lastFreeTransformDiagnosticMilliseconds <
            750u)
    {
        return;
    }

    asset->lastFreeTransformDiagnosticMilliseconds =
        nowMilliseconds;

    constexpr float radiansToDegrees =
        57.295779513082320876f;

    const float viewYawDegrees =
        std::atan2(
            cameraAxis[0][1],
            cameraAxis[0][0]) *
        radiansToDegrees;

    const float cameraRigidityError =
        VR_HandAxisRigidityError(cameraAxis);

    const float quaternionGripRigidityError =
        VR_HandAxisRigidityError(
            quaternionControllerAxis);

    Com_Printf(
        0,
        "[VR][HANDS][DIAG] viewYaw %.2f; input rigidity camera "
        "%.6f / direct quaternion pose %.6f; corrected controller-to-wrist "
        "drift %.6f units / %.6f deg; quaternion round-trip "
        "%.6f deg; published root %.6f deg / %.6f units.\n",
        viewYawDegrees,
        cameraRigidityError,
        quaternionGripRigidityError,
        originDriftLength,
        relativeRotationDriftDegrees,
        quaternionRoundTripDegrees,
        rootRotationDegrees,
        rootTranslationError);
}

static void VR_UpdateLeftHandAttachmentBlend(
    VrSplitHandAsset* asset,
    const bool shouldAttach)
{
    if (asset == nullptr)
    {
        return;
    }

    const uint32_t nowMilliseconds =
        static_cast<uint32_t>(
            Sys_Milliseconds());

    uint32_t elapsedMilliseconds = 0u;

    if (asset->lastAttachmentUpdateMilliseconds != 0u)
    {
        elapsedMilliseconds =
            nowMilliseconds -
            asset->lastAttachmentUpdateMilliseconds;

        if (elapsedMilliseconds > 50u)
        {
            elapsedMilliseconds = 50u;
        }
    }

    asset->lastAttachmentUpdateMilliseconds =
        nowMilliseconds;

    constexpr float kAttachmentBlendMilliseconds =
        120.0f;

    const float blendStep =
        static_cast<float>(elapsedMilliseconds) /
        kAttachmentBlendMilliseconds;

    if (shouldAttach)
    {
        asset->attachmentBlend +=
            blendStep;

        if (asset->attachmentBlend > 1.0f)
        {
            asset->attachmentBlend = 1.0f;
        }
    }
    else
    {
        asset->attachmentBlend -=
            blendStep;

        if (asset->attachmentBlend < 0.0f)
        {
            asset->attachmentBlend = 0.0f;
        }
    }
}

static bool VR_BuildHeldMagazineRenderAxis(
    const float heldMagazineAxis[3][3],
    float renderAxis[3][3])
{
    if (heldMagazineAxis == nullptr ||
        renderAxis == nullptr)
    {
        return false;
    }

    // The world magazine DObj receives Euler angles produced from this same
    // source matrix.  Rebuild that exact rendered orientation before latching
    // the glove, rather than attaching to the old scaled/sheared controller
    // matrix that V23 diagnosed.
    float renderAngles[3] = {};

    AxisToAngles(
        *reinterpret_cast<const mat3x3*>(
            heldMagazineAxis),
        renderAngles);

    if (!std::isfinite(renderAngles[0]) ||
        !std::isfinite(renderAngles[1]) ||
        !std::isfinite(renderAngles[2]))
    {
        return false;
    }

    float angleAxis[3][3] = {};

    AnglesToAxis(
        renderAngles,
        *reinterpret_cast<mat3x3*>(
            angleAxis));

    return VR_BuildRigidTrackedHandAxis(
        angleAxis,
        renderAxis);
}

static bool VR_CaptureHeldMagazineToWristTransform(
    VrSplitHandAsset* asset,
    const float heldMagazineOrigin[3],
    const float heldMagazineAxis[3][3],
    const float wristOrigin[3],
    const float wristAxis[3][3])
{
    if (asset == nullptr ||
        heldMagazineOrigin == nullptr ||
        heldMagazineAxis == nullptr ||
        wristOrigin == nullptr ||
        wristAxis == nullptr)
    {
        return false;
    }

    const float magazineToWristWorld[3] = {
        wristOrigin[0] - heldMagazineOrigin[0],
        wristOrigin[1] - heldMagazineOrigin[1],
        wristOrigin[2] - heldMagazineOrigin[2],
    };

    for (int magazineLocalComponent = 0;
         magazineLocalComponent < 3;
         ++magazineLocalComponent)
    {
        asset->magazineToWristOrigin[
            magazineLocalComponent] =
            VR_HandDirectionDot(
                magazineToWristWorld,
                heldMagazineAxis[
                    magazineLocalComponent]);
    }

    float relativeAxis[3][3] = {};

    for (int wristAxisRow = 0;
         wristAxisRow < 3;
         ++wristAxisRow)
    {
        for (int magazineAxisRow = 0;
             magazineAxisRow < 3;
             ++magazineAxisRow)
        {
            relativeAxis[wristAxisRow][magazineAxisRow] =
                VR_HandDirectionDot(
                    wristAxis[wristAxisRow],
                    heldMagazineAxis[magazineAxisRow]);
        }
    }

    if (!VR_BuildRigidTrackedHandAxis(
            relativeAxis,
            asset->magazineToWristAxis))
    {
        asset->magazineToWristPoseValid = false;
        return false;
    }

    asset->magazineToWristPoseValid =
        std::isfinite(
            asset->magazineToWristOrigin[0]) &&
        std::isfinite(
            asset->magazineToWristOrigin[1]) &&
        std::isfinite(
            asset->magazineToWristOrigin[2]);

    return asset->magazineToWristPoseValid;
}

static bool VR_BuildHeldMagazineWristTransform(
    const VrSplitHandAsset* asset,
    const float heldMagazineOrigin[3],
    const float heldMagazineAxis[3][3],
    float wristOrigin[3],
    float wristAxis[3][3],
    float wristQuaternion[4])
{
    if (asset == nullptr ||
        !asset->magazineToWristPoseValid ||
        heldMagazineOrigin == nullptr ||
        heldMagazineAxis == nullptr ||
        wristOrigin == nullptr ||
        wristAxis == nullptr ||
        wristQuaternion == nullptr)
    {
        return false;
    }

    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        wristOrigin[worldComponent] =
            heldMagazineOrigin[worldComponent];

        for (int magazineAxisRow = 0;
             magazineAxisRow < 3;
             ++magazineAxisRow)
        {
            wristOrigin[worldComponent] +=
                asset->magazineToWristOrigin[
                    magazineAxisRow] *
                heldMagazineAxis[
                    magazineAxisRow][worldComponent];
        }
    }

    float reconstructedWristAxis[3][3] = {};

    for (int wristAxisRow = 0;
         wristAxisRow < 3;
         ++wristAxisRow)
    {
        for (int worldComponent = 0;
             worldComponent < 3;
             ++worldComponent)
        {
            for (int magazineAxisRow = 0;
                 magazineAxisRow < 3;
                 ++magazineAxisRow)
            {
                reconstructedWristAxis[
                    wristAxisRow][worldComponent] +=
                    asset->magazineToWristAxis[
                        wristAxisRow][magazineAxisRow] *
                    heldMagazineAxis[
                        magazineAxisRow][worldComponent];
            }
        }
    }

    if (!VR_BuildRigidTrackedHandAxis(
            reconstructedWristAxis,
            wristAxis))
    {
        return false;
    }

    float unnormalizedWristQuaternion[4] = {};

    AxisToQuat(
        wristAxis,
        unnormalizedWristQuaternion);

    return VR_NormalizeHandQuaternion(
        unnormalizedWristQuaternion,
        wristQuaternion);
}

static bool VR_AddFloatingLeftHandToScene(
    const int weaponIndex,
    DObj_s* weaponViewModel,
    XModel* sourceHandModel,
    const cpose_t* weaponPose,
    const playerState_s* playerState,
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    const float lightingOrigin[3],
    const bool drawHeldMagazine,
    const float heldMagazineOrigin[3],
    const float heldMagazineAxis[3][3])
{
    if (!VR_TrackedHandsEnabled() ||
        weaponIndex <= 0 ||
        weaponViewModel == nullptr ||
        sourceHandModel == nullptr ||
        weaponPose == nullptr ||
        playerState == nullptr ||
        cameraOrigin == nullptr ||
        cameraAxis == nullptr ||
        lightingOrigin == nullptr ||
        heldMagazineOrigin == nullptr ||
        heldMagazineAxis == nullptr)
    {
        static bool invalidSubmissionArgumentsLogWritten =
            false;

        if (VR_TrackedHandsEnabled() &&
            !invalidSubmissionArgumentsLogWritten)
        {
            Com_PrintWarning(
                0,
                "[VR][HANDS] The floating-glove render call received "
                "an unavailable weapon, hand model, pose, player "
                "state, camera, or lighting input.\n");

            invalidSubmissionArgumentsLogWritten =
                true;
        }

        return false;
    }

    VrSplitHandAsset* asset =
        VR_ResolveSplitHandAssetForWeapon(
            weaponViewModel,
            sourceHandModel);

    if (asset == nullptr)
    {
        static bool assetResolutionFailureLogWritten =
            false;

        if (!assetResolutionFailureLogWritten)
        {
            Com_PrintWarning(
                0,
                "[VR][HANDS] The extracted floating-glove asset "
                "could not be resolved for the active weapon DObj.\n");

            assetResolutionFailureLogWritten =
                true;
        }

        return false;
    }

    if (!VR_CreateFloatingLeftHandObjects(
            asset))
    {
        Com_PrintWarning(
            0,
            "[VR][HANDS] The extracted floating-glove asset was "
            "resolved, but its standalone DObj could not be created.\n");

        return false;
    }

    float leftControllerOrigin[3] = {};
    float leftHandPoseOrientationHeadLocalOpenXr[4] = {};

    const bool usingPalmSurfacePose =
        VR_GetTrackedLeftControllerPalmQuaternionWorld(
            cameraOrigin,
            cameraAxis,
            leftControllerOrigin,
            leftHandPoseOrientationHeadLocalOpenXr);

    if (!usingPalmSurfacePose &&
        !VR_GetTrackedLeftControllerGripQuaternionWorld(
            cameraOrigin,
            cameraAxis,
            leftControllerOrigin,
            leftHandPoseOrientationHeadLocalOpenXr))
    {
        if (!asset->controllerPoseFailureLogWritten)
        {
            Com_PrintWarning(
                0,
                "[VR][HANDS] Wrist-rooted left glove was built, but "
                "neither the OpenXR palm surface nor grip fallback "
                "was available; the hand was not submitted.\n");

            asset->controllerPoseFailureLogWritten =
                true;
        }

        return false;
    }

    float supportGripControllerOrigin[3] = {
        leftControllerOrigin[0],
        leftControllerOrigin[1],
        leftControllerOrigin[2],
    };
    bool supportGripControllerPoseAvailable =
        true;

    if (usingPalmSurfacePose)
    {
        float ignoredGripOrientation[4] = {};

        supportGripControllerPoseAvailable =
            VR_GetTrackedLeftControllerGripQuaternionWorld(
                cameraOrigin,
                cameraAxis,
                supportGripControllerOrigin,
                ignoredGripOrientation);
    }

    float quaternionLeftControllerAxis[3][3] = {};

    if (!VR_BuildLeftHandPoseAxisFromOpenXrQuaternion(
            leftHandPoseOrientationHeadLocalOpenXr,
            cameraAxis,
            quaternionLeftControllerAxis))
    {
        Com_PrintWarning(
            0,
            "[VR][HANDS] The normalized OpenXR left-hand pose "
            "quaternion could not reconstruct a rigid axis; the hand "
            "was not submitted.\n");

        return false;
    }

    float leftControllerAxis[3][3] = {};

    if (!VR_BuildRigidTrackedHandAxis(
            quaternionLeftControllerAxis,
            leftControllerAxis))
    {
        Com_PrintWarning(
            0,
            "[VR][HANDS] The direct-quaternion left controller frame was "
            "degenerate and could not be rebuilt as a rigid "
            "right-handed grip frame; the hand was not submitted.\n");

        return false;
    }

    float freeWristOrigin[3] = {};
    float freeWristAxis[3][3] = {};
    float freeWristQuaternion[4] = {};

    if (!VR_BuildFreeLeftHandTransform(
            asset,
            leftControllerOrigin,
            leftControllerAxis,
            usingPalmSurfacePose,
            freeWristOrigin,
            freeWristAxis,
            freeWristQuaternion))
    {
        Com_PrintWarning(
            0,
            "[VR][HANDS] The neutral left glove could not build its "
            "model-derived free transform.\n");

        return false;
    }

    // A weapon change resets this asset's grip cache.  Failure here only
    // means the current idle animation has not settled yet; the neutral hand
    // remains visible and controller-tracked while the bake retries.
    const bool supportGripPoseReady =
        VR_BakeRigidSupportGripPose(
            asset,
            weaponIndex,
            weaponViewModel,
            weaponPose,
            playerState);

    float magazineRenderAxis[3][3] = {};
    float magazineWristOrigin[3] = {};
    float magazineWristAxis[3][3] = {};
    float magazineWristQuaternion[4] = {};

    const bool previousMagazineGripLatched =
        asset->magazineGripLatched;

    bool magazineGripPoseAvailable =
        drawHeldMagazine &&
        supportGripPoseReady &&
        VR_BuildHeldMagazineRenderAxis(
            heldMagazineAxis,
            magazineRenderAxis);

    if (magazineGripPoseAvailable &&
        !asset->magazineToWristPoseValid)
    {
        magazineGripPoseAvailable =
            VR_CaptureHeldMagazineToWristTransform(
                asset,
                heldMagazineOrigin,
                magazineRenderAxis,
                freeWristOrigin,
                freeWristAxis);
    }

    if (magazineGripPoseAvailable)
    {
        magazineGripPoseAvailable =
            VR_BuildHeldMagazineWristTransform(
                asset,
                heldMagazineOrigin,
                magazineRenderAxis,
                magazineWristOrigin,
                magazineWristAxis,
                magazineWristQuaternion);
    }

    asset->magazineGripLatched =
        magazineGripPoseAvailable;

    if (!drawHeldMagazine)
    {
        asset->magazineToWristPoseValid = false;
        asset->magazineGripPoseFailureLogWritten = false;
    }
    else if (!asset->magazineGripLatched &&
             !asset->magazineGripPoseFailureLogWritten)
    {
        Com_PrintWarning(
            0,
            "[VR][HANDS] A held magazine was visible, but the closed "
            "glove could not capture its rigid wrist attachment; the "
            "neutral tracked hand remains active.\n");

        asset->magazineGripPoseFailureLogWritten = true;
    }

    if (asset->magazineGripLatched !=
        previousMagazineGripLatched)
    {
        if (asset->magazineGripLatched)
        {
            const float captureOriginDelta[3] = {
                magazineWristOrigin[0] -
                    freeWristOrigin[0],
                magazineWristOrigin[1] -
                    freeWristOrigin[1],
                magazineWristOrigin[2] -
                    freeWristOrigin[2],
            };

            const float captureOriginError =
                std::sqrt(
                    captureOriginDelta[0] *
                        captureOriginDelta[0] +
                    captureOriginDelta[1] *
                        captureOriginDelta[1] +
                    captureOriginDelta[2] *
                        captureOriginDelta[2]);

            Com_Printf(
                0,
                "[VR][HANDS] Left hand gripped weapon %d magazine "
                "with the baked closed-hand mesh; captured the "
                "magazine-to-wrist attachment with %.6f-unit / "
                "%.6f-degree pickup continuity error.\n",
                weaponIndex,
                captureOriginError,
                VR_HandAxisDifferenceDegrees(
                    magazineWristAxis,
                    freeWristAxis));
        }
        else
        {
            Com_Printf(
                0,
                "[VR][HANDS] Left hand released weapon %d magazine "
                "and returned to neutral palm tracking.\n",
                weaponIndex);
        }
    }

    float supportWristOrigin[3] = {};
    float supportWristAxis[3][3] = {};
    float supportWristQuaternion[4] = {};

    const uint32_t leftWristName =
        SL_FindString(
            "j_wrist_le");

    bool supportWristAvailable =
        supportGripPoseReady &&
        supportGripControllerPoseAvailable &&
        leftWristName != 0u &&
        CG_DObjGetWorldTagMatrix(
            weaponPose,
            weaponViewModel,
            leftWristName,
            supportWristAxis,
            supportWristOrigin) != 0;

    if (supportWristAvailable)
    {
        float unnormalizedSupportQuaternion[4] = {};

        AxisToQuat(
            supportWristAxis,
            unnormalizedSupportQuaternion);

        supportWristAvailable =
            VR_NormalizeHandQuaternion(
                unnormalizedSupportQuaternion,
                supportWristQuaternion);
    }

    bool leftSupportGripPressed = false;

    const bool supportGripStateAvailable =
        VR_GetLeftControllerSupportGripPressed(
            &leftSupportGripPressed);

    float controllerToSupportDistance =
        999.0f;

    if (supportWristAvailable)
    {
        const float controllerToSupport[3] = {
            supportGripControllerOrigin[0] -
                supportWristOrigin[0],
            supportGripControllerOrigin[1] -
                supportWristOrigin[1],
            supportGripControllerOrigin[2] -
                supportWristOrigin[2],
        };

        controllerToSupportDistance =
            std::sqrt(
                controllerToSupport[0] *
                    controllerToSupport[0] +
                controllerToSupport[1] *
                    controllerToSupport[1] +
                controllerToSupport[2] *
                    controllerToSupport[2]);
    }

    const bool previousSupportGripLatched =
        asset->supportGripLatched;

    const bool insideGripRadius =
        supportWristAvailable &&
        s_vrLeftHandGripRadius != nullptr &&
        controllerToSupportDistance <=
            s_vrLeftHandGripRadius->current.value;

    asset->supportGripLatched =
        !asset->magazineGripLatched &&
        supportGripStateAvailable &&
        leftSupportGripPressed &&
        supportWristAvailable &&
        (previousSupportGripLatched ||
         insideGripRadius);

    if (asset->supportGripLatched !=
        previousSupportGripLatched)
    {
        if (asset->supportGripLatched)
        {
            Com_Printf(
                0,
                "[VR][HANDS] Left support hand attached to weapon %d "
                "at its animated j_wrist_le anchor (controller "
                "distance %.2f).\n",
                weaponIndex,
                controllerToSupportDistance);
        }
        else
        {
            Com_Printf(
                0,
                "[VR][HANDS] Left support hand released weapon %d "
                "and returned to free controller tracking.\n",
                weaponIndex);
        }
    }

    VR_UpdateLeftHandAttachmentBlend(
        asset,
        asset->supportGripLatched);

    float renderedWristOrigin[3] = {};

    for (int component = 0;
         component < 3;
         ++component)
    {
        const float gripOrigin =
            supportWristAvailable
                ? supportWristOrigin[component]
                : freeWristOrigin[component];

        renderedWristOrigin[component] =
            freeWristOrigin[component] *
                (1.0f - asset->attachmentBlend) +
            gripOrigin *
                asset->attachmentBlend;
    }

    float renderedWristQuaternion[4] = {};

    const float* gripQuaternion =
        supportWristAvailable
            ? supportWristQuaternion
            : freeWristQuaternion;

    if (!VR_BlendHandQuaternion(
            freeWristQuaternion,
            gripQuaternion,
            asset->attachmentBlend,
            renderedWristQuaternion))
    {
        return false;
    }

    if (asset->magazineGripLatched)
    {
        std::memcpy(
            renderedWristOrigin,
            magazineWristOrigin,
            sizeof(renderedWristOrigin));

        std::memcpy(
            renderedWristQuaternion,
            magazineWristQuaternion,
            sizeof(renderedWristQuaternion));
    }

    const bool renderMagazineGripModel =
        asset->magazineGripLatched &&
        supportGripPoseReady;

    const bool renderSupportGripModel =
        !renderMagazineGripModel &&
        supportGripPoseReady &&
        asset->attachmentBlend >= 0.5f;

    DObj_s* renderedHandObject =
        renderMagazineGripModel
            ? &asset->magazineGripObject
            : (renderSupportGripModel
                   ? &asset->supportGripObject
                   : &asset->floatingLeftObject);

    cpose_t* renderedHandPose =
        renderMagazineGripModel
            ? &asset->magazineGripPose
            : (renderSupportGripModel
                   ? &asset->supportGripPose
                   : &asset->floatingLeftPose);

    const bool handPosePublished =
        VR_SetRigidHandRootPose(
            renderedHandObject,
            renderedHandPose,
            renderedWristOrigin,
            renderedWristQuaternion);

    if (!handPosePublished)
    {
        Com_PrintWarning(
            0,
            "[VR][HANDS] The left glove could not publish its neutral, "
            "weapon-support, or held-magazine pose.\n");

        return false;
    }

    if (!renderMagazineGripModel &&
        !renderSupportGripModel &&
        asset->attachmentBlend <= 0.0001f)
    {
        VR_LogFreeLeftHandTransformDiagnostic(
            asset,
            leftControllerOrigin,
            quaternionLeftControllerAxis,
            leftControllerAxis,
            freeWristOrigin,
            freeWristAxis,
            freeWristQuaternion,
            cameraAxis,
            renderedHandObject,
            renderedHandPose);
    }

    if (!asset->submissionAttemptLogWritten)
    {
        const float cameraToHand[3] = {
            leftControllerOrigin[0] - cameraOrigin[0],
            leftControllerOrigin[1] - cameraOrigin[1],
            leftControllerOrigin[2] - cameraOrigin[2],
        };

        const float cameraToHandDistance =
            std::sqrt(
                cameraToHand[0] * cameraToHand[0] +
                cameraToHand[1] * cameraToHand[1] +
                cameraToHand[2] * cameraToHand[2]);

        Com_Printf(
            0,
            "[VR][HANDS] Floating-glove render path reached the "
            "scene submission stage at %.2f game units from the "
            "camera.\n",
            cameraToHandDistance);

        asset->submissionAttemptLogWritten =
            true;
    }

    float mutableLightingOrigin[3] = {
        lightingOrigin[0],
        lightingOrigin[1],
        lightingOrigin[2],
    };

    R_AddDObjToSceneUntracked(
        renderedHandObject,
        renderedHandPose,
        3u,
        mutableLightingOrigin,
        0.0f);

    if (!asset->submittedLogWritten)
    {
        Com_Printf(
            0,
            "[VR][HANDS] V27 keeps V26's corrected palm frame and "
            "adds a rigid held-magazine grip using the baked closed "
            "hand; current free-pose source: %s.\n",
            usingPalmSurfacePose
                ? "palm_ext/pose"
                : "grip/pose fallback");

        asset->submittedLogWritten =
            true;
    }

    return true;
}

static void VR_FreeFloatingLeftHandRenderObjects()
{
    for (VrSplitHandAsset& asset :
         s_vrSplitHandAssets)
    {
        if (asset.floatingObjectCreated)
        {
            DObjFree(
                &asset.floatingLeftObject);
        }

        if (asset.supportGripObjectCreated)
        {
            DObjFree(
                &asset.supportGripObject);
        }

        if (asset.magazineGripObjectCreated)
        {
            DObjFree(
                &asset.magazineGripObject);
        }

        // The split model headers, surfaces, indices, and wrist-rooted vertices
        // are hunk-backed and remain valid until the cgame hunk is cleared
        // after weapon shutdown.
        asset = VrSplitHandAsset{};
    }
}

static void VR_FreeManualReloadClipRenderObjects()
{
    for (VrManualReloadClipRenderObject& clipObject :
         s_vrManualReloadClipObjects)
    {
        if (clipObject.created)
        {
            DObjFree(&clipObject.object);
        }

        clipObject =
            VrManualReloadClipRenderObject{};
    }

    for (VrManualGrenadeRenderObject& grenadeObject :
         s_vrManualGrenadeRenderObjects)
    {
        if (grenadeObject.created)
        {
            DObjFree(&grenadeObject.object);
        }

        grenadeObject =
            VrManualGrenadeRenderObject{};
    }
}

void VR_FreeManualReloadClipRenderObjectsForShutdown()
{
    VR_FreeManualReloadClipRenderObjects();
    VR_FreeFloatingLeftHandRenderObjects();
}

static VrManualReloadClipRenderObject*
VR_GetManualReloadClipRenderObject(
    const uint32_t weaponNum,
    XModel* clipModel)
{
    if (weaponNum >=
            ARRAY_COUNT(
                s_vrManualReloadClipObjects) ||
        clipModel == nullptr)
    {
        return nullptr;
    }

    VrManualReloadClipRenderObject& clipObject =
        s_vrManualReloadClipObjects[weaponNum];

    if (clipObject.created &&
        clipObject.model != clipModel)
    {
        DObjFree(&clipObject.object);

        clipObject =
            VrManualReloadClipRenderObject{};
    }

    if (!clipObject.created)
    {
        DObjModel_s modelDescription = {};
        modelDescription.model = clipModel;
        modelDescription.boneName = 0;
        modelDescription.ignoreCollision = false;

        DObjCreate(
            &modelDescription,
            1u,
            nullptr,
            &clipObject.object,
            0);

        clipObject.model = clipModel;
        clipObject.created = true;

        clipObject.ejectedPose.eType =
            ET_GENERAL;

        clipObject.heldPose.eType =
            ET_GENERAL;

        Com_Printf(
            0,
            "[VR][RELOAD] Prepared world clip model '%s' "
            "for weapon %u.\n",
            XModelGetName(clipModel),
            weaponNum);
    }

    return &clipObject;
}

static void VR_SetManualReloadClipPose(
    cpose_t* pose,
    const float origin[3],
    const float axis[3][3])
{
    if (pose == nullptr ||
        origin == nullptr ||
        axis == nullptr)
    {
        return;
    }

    pose->origin[0] = origin[0];
    pose->origin[1] = origin[1];
    pose->origin[2] = origin[2];

    AxisToAngles(
        *reinterpret_cast<const mat3x3*>(axis),
        pose->angles);
}


static VrManualGrenadeRenderObject*
VR_GetManualGrenadeRenderObject(
    const uint32_t weaponNum,
    XModel* projectileModel)
{
    if (weaponNum >=
            ARRAY_COUNT(
                s_vrManualGrenadeRenderObjects) ||
        projectileModel == nullptr)
    {
        return nullptr;
    }

    VrManualGrenadeRenderObject& grenadeObject =
        s_vrManualGrenadeRenderObjects[weaponNum];

    if (grenadeObject.created &&
        grenadeObject.model != projectileModel)
    {
        DObjFree(&grenadeObject.object);

        grenadeObject =
            VrManualGrenadeRenderObject{};
    }

    if (!grenadeObject.created)
    {
        DObjModel_s modelDescription = {};
        modelDescription.model = projectileModel;
        modelDescription.boneName = 0;
        modelDescription.ignoreCollision = false;

        DObjCreate(
            &modelDescription,
            1u,
            nullptr,
            &grenadeObject.object,
            0);

        grenadeObject.model = projectileModel;
        grenadeObject.created = true;
        grenadeObject.heldPose.eType = ET_GENERAL;

        Com_Printf(
            0,
            "[VR][GRENADE] Prepared tracked left-hand model '%s' "
            "for weapon %u.\n",
            XModelGetName(projectileModel),
            weaponNum);
    }

    return &grenadeObject;
}


static void VR_AddManualGrenadeModelToScene(
    const float lightingOrigin[3])
{
    if (lightingOrigin == nullptr)
    {
        return;
    }

    int grenadeWeaponIndex = 0;
    float heldOrigin[3] = {};
    float heldAxis[3][3] = {};

    if (!VR_GetManualGrenadeRenderState(
            &grenadeWeaponIndex,
            heldOrigin,
            heldAxis) ||
        grenadeWeaponIndex <= 0 ||
        grenadeWeaponIndex >=
            static_cast<int>(BG_GetNumWeapons()))
    {
        return;
    }

    WeaponDef* grenadeWeaponDef =
        BG_GetWeaponDef(
            grenadeWeaponIndex);

    if (grenadeWeaponDef == nullptr ||
        grenadeWeaponDef->projectileModel == nullptr)
    {
        static bool loggedMissingGrenadeModel = false;

        if (!loggedMissingGrenadeModel)
        {
            Com_PrintWarning(
                0,
                "[VR][GRENADE] The held offhand weapon has no "
                "projectile model; physical input remains active.\n");

            loggedMissingGrenadeModel = true;
        }

        return;
    }

    VrManualGrenadeRenderObject* grenadeObject =
        VR_GetManualGrenadeRenderObject(
            static_cast<uint32_t>(
                grenadeWeaponIndex),
            grenadeWeaponDef->projectileModel);

    if (grenadeObject == nullptr)
    {
        return;
    }

    VR_SetManualReloadClipPose(
        &grenadeObject->heldPose,
        heldOrigin,
        heldAxis);

    float mutableLightingOrigin[3] = {
        lightingOrigin[0],
        lightingOrigin[1],
        lightingOrigin[2],
    };

    // The ordinary viewmodel already owns ENTITYNUM_NONE.  Submit this
    // independent first-person object through the untracked path so it can
    // coexist with the firearm and floating glove in both stereo eyes.
    R_AddDObjToSceneUntracked(
        &grenadeObject->object,
        &grenadeObject->heldPose,
        3u,
        mutableLightingOrigin,
        0.0f);
}

static void VR_AddManualReloadClipModelsToScene(
    const uint32_t weaponNum,
    XModel* clipModel,
    const float lightingOrigin[3])
{
    if (clipModel == nullptr ||
        lightingOrigin == nullptr)
    {
        return;
    }

    bool hideLoadedMagazine = false;
    bool drawEjectedMagazine = false;
    bool drawHeldMagazine = false;
    float ejectedOrigin[3] = {};
    float ejectedAxis[3][3] = {};
    float heldOrigin[3] = {};
    float heldAxis[3][3] = {};

    if (!VR_GetManualMagazineReloadRenderState(
            static_cast<int>(weaponNum),
            &hideLoadedMagazine,
            &drawEjectedMagazine,
            ejectedOrigin,
            ejectedAxis,
            &drawHeldMagazine,
            heldOrigin,
            heldAxis) ||
        (!drawEjectedMagazine &&
         !drawHeldMagazine))
    {
        return;
    }

    VrManualReloadClipRenderObject* clipObject =
        VR_GetManualReloadClipRenderObject(
            weaponNum,
            clipModel);

    if (clipObject == nullptr)
    {
        return;
    }

    float mutableLightingOrigin[3] = {
        lightingOrigin[0],
        lightingOrigin[1],
        lightingOrigin[2],
    };

    if (drawEjectedMagazine)
    {
        VR_SetManualReloadClipPose(
            &clipObject->ejectedPose,
            ejectedOrigin,
            ejectedAxis);

        R_AddDObjToScene(
            &clipObject->object,
            &clipObject->ejectedPose,
            ENTITYNUM_WORLD,
            0u,
            mutableLightingOrigin,
            0.0f);
    }

    if (drawHeldMagazine)
    {
        VR_SetManualReloadClipPose(
            &clipObject->heldPose,
            heldOrigin,
            heldAxis);

        R_AddDObjToScene(
            &clipObject->object,
            &clipObject->heldPose,
            ENTITYNUM_NONE,
            0u,
            mutableLightingOrigin,
            0.0f);
    }
}
#endif

int32_t g_animRateOffsets[33] =
{
  -1,
  -1,
  -1,
  -1,
  888,
  -1,
  -1,
  896,
  900,
  904,
  912,
  920,
  928,
  936,
  956,
  932,
  944,
  940,
  952,
  948,
  960,
  964,
  968,
  972,
  976,
  -1,
  980,
  992,
  -1,
  -1,
  -1,
  -1,
  -1
}; // idb

// KISAK_SP_JAVELIN_MENU_DIAGNOSTICS_WEAPON
bool __cdecl CG_JavelinADS(int32_t localClientNum)
{
    int32_t weapIdx; // [esp+4h] [ebp-Ch]
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    weapIdx = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    VR_JavelinDiagnostic(
        "ADS query: weapon index resolved",
        weapIdx,
        cgameGlob);

    if (weapIdx <= 0 || weapIdx >= (int32_t)BG_GetNumWeapons())
        return false;

    if (cgameGlob->predictedPlayerState.fWeaponPosFrac != 1.0f)
        return false;

    WeaponDef *weapDef = BG_GetWeaponDef(weapIdx);
    VR_JavelinDiagnostic(
        "ADS query: weapon definition resolved",
        weapIdx,
        weapDef);

    return weapDef != nullptr &&
        weapDef->overlayInterface == WEAPOVERLAYINTERFACE_JAVELIN;
}

int32_t __cdecl CG_WeaponDObjHandle(int32_t weaponNum)
{
    return weaponNum + MAX_GENTITIES;
}

void __cdecl CG_RegisterWeapon(int32_t localClientNum, uint32_t weaponNum)
{
    uint32_t NumWeapons; // eax
    char *v3; // eax
    int64_t _C; // [esp+Ch] [ebp-34h]
    const char *blendTime; // [esp+14h] [ebp-2Ch]
    weaponInfo_s *weapInfo; // [esp+18h] [ebp-28h]
    uint32_t dobjHandle; // [esp+1Ch] [ebp-24h]
    uint8_t boneIndex; // [esp+23h] [ebp-1Dh] BYREF
    DObj_s *obj; // [esp+24h] [ebp-1Ch]
    int32_t tagIndex; // [esp+28h] [ebp-18h]
    WeaponDef *weapDef; // [esp+2Ch] [ebp-14h]
    DObjModel_s dobjModels[2]; // [esp+30h] [ebp-10h] BYREF

    removeMeWhenMPStopsCrashingInHere = weaponNum;
    VR_JavelinDiagnostic("register: enter", weaponNum);
    if (weaponNum)
    {
        iassert(weaponNum < BG_GetNumWeapons());
        iassert(weaponNum + WEAPON_HINT_OFFSET <= LAST_WEAPON_HINT);
        iassert(localClientNum == 0);

        weapInfo = &cg_weaponsArray[0][weaponNum]; // KISAKTODO :refactor to CG_GetLocalClientWeaponInfo();
        weapDef = BG_GetWeaponDef(weaponNum);
        VR_JavelinDiagnostic("register: weapon definition resolved", weaponNum, weapDef);
        if (!weapInfo->registered)
        {
            VR_JavelinDiagnostic("register: new registration", weaponNum, weapInfo);
            SCR_UpdateLoadScreen();
            VR_JavelinDiagnostic("register: load-screen update complete", weaponNum, weapInfo);
            memset(weapInfo, 0, sizeof(weaponInfo_s));
            weapInfo->registered = 1;
            weapInfo->item = &bg_itemlist[weaponNum];
            weapInfo->iPrevAnim = -1;
            if (weapDef->gunXModel[0] && weapDef->handXModel)
            {
                dobjModels[0].boneName = 0;
                dobjModels[1].boneName = scr_const.tag_weapon;
                dobjModels[0].ignoreCollision = 0;
                dobjModels[1].ignoreCollision = 0;
#ifdef KISAK_SP
                dobjModels[0].model =
                    VR_GetRightOnlyWeaponHandModel(
                        weapDef->handXModel);
#else
                dobjModels[0].model = weapDef->handXModel;
#endif
                dobjModels[1].model = weapDef->gunXModel[0];
                iassert(!weapInfo->tree);
                VR_JavelinDiagnostic("register: before animation-tree creation", weaponNum, weapDef);
                weapInfo->tree = CG_CreateWeaponViewModelXAnim(weapDef);
                VR_JavelinDiagnostic("register: animation-tree creation complete", weaponNum, weapInfo->tree);
                iassert(dobjModels[0].model);
                iassert(dobjModels[1].model);
                dobjHandle = CG_WeaponDObjHandle(weaponNum);
                VR_JavelinDiagnostic("register: before DObj creation", weaponNum, weapInfo->tree);
                obj = Com_ClientDObjCreate(dobjModels, 2u, weapInfo->tree, dobjHandle, localClientNum);
                VR_JavelinDiagnostic("register: DObj creation complete", weaponNum, obj);
                weapInfo->viewModelDObj = obj;
                weapInfo->handModel = weapDef->handXModel;
                weapInfo->weapModelIdx = 0;
                XAnimClearTreeGoalWeights(weapInfo->tree, 0, 0.0);
                XAnimSetGoalWeight(obj, 0, 1.0, 0.0, 1.0, 0, 1u, 0);
                XAnimSetGoalWeight(obj, 1u, 1.0, 0.0, 1.0, 0, 1u, 0);
                if (*weapDef->szXAnims[32])
                {
                    XAnimSetGoalWeight(obj, 0x20u, 1.0, 0.0, 0.0, 0, 1u, 0);
                    XAnimSetTime(weapInfo->tree, 32, 1.0);
                }
                for (tagIndex = 0; tagIndex < 8 && weapDef->hideTags[tagIndex]; ++tagIndex)
                {
                    boneIndex = -2;
                    if (DObjGetBoneIndex(obj, weapDef->hideTags[tagIndex], &boneIndex))
                    {
                        weapInfo->partBits[(int)boneIndex >> 5] |= 0x80000000 >> (boneIndex & 0x1F);
                    }
                    else
                    {
                        Com_PrintError(14, "CG_RegisterWeapon: No such bone tag (%s) for weapon (%s)\n", SL_ConvertToString(weapDef->hideTags[tagIndex]), weapDef->szInternalName);
                    }
                }
                DObjSetHidePartBits(obj, weapInfo->partBits);
                VR_JavelinDiagnostic("register: hide-part setup complete", weaponNum, obj);
                DObjUpdateClientInfo(weapInfo->viewModelDObj, 0.05f, 0);
                VR_JavelinDiagnostic("register: initial DObj update complete", weaponNum, obj);
            }
            // KISAK_SP_JAVELIN_PICKUP_FIX
            // stanceMaterials immediately follows hintMaterials in cgMedia_t.
            // The recovered negative index reached backward into that array,
            // which is undefined C++ and corrupted memory when a newly seen
            // weapon such as the Bog Javelin was registered.
            const uint32_t hintMaterialIndex =
                weaponNum + WEAPON_HINT_OFFSET;

            iassert(
                hintMaterialIndex >= FIRST_WEAPON_HINT &&
                hintMaterialIndex <= LAST_WEAPON_HINT);

            cgMedia.hintMaterials[hintMaterialIndex] =
                weapDef->hudIcon;
            VR_JavelinDiagnostic("register: HUD material stored", weaponNum, weapDef->hudIcon);
            weapInfo->translatedDisplayName = SEH_StringEd_GetString(weapDef->szDisplayName);
            if (!weapInfo->translatedDisplayName)
            {
                if (loc_warnings->current.enabled)
                {
                    if (loc_warningsAsErrors->current.enabled)
                        Com_Error(
                            ERR_LOCALIZATION,
                            "Weapon %s: Could not translate display name \"%s\"",
                            weapDef->szInternalName,
                            weapDef->szDisplayName);
                    else
                        Com_PrintWarning(
                            17,
                            "WARNING: Weapon %s: Could not translate display name \"%s\"\n",
                            weapDef->szInternalName,
                            weapDef->szDisplayName);
                }
                weapInfo->translatedDisplayName = weapDef->szDisplayName;
            }
            weapInfo->translatedModename = SEH_StringEd_GetString(weapDef->szModeName);
            if (!weapInfo->translatedModename)
            {
                if (loc_warnings->current.enabled)
                {
                    if (loc_warningsAsErrors->current.enabled)
                        Com_Error(
                            ERR_LOCALIZATION,
                            "Weapon %s: Could not translate mode name \"%s\"",
                            weapDef->szInternalName,
                            weapDef->szModeName);
                    else
                        Com_PrintWarning(
                            17,
                            "WARNING: Weapon %s: Could not translate mode name \"%s\"\n",
                            weapDef->szInternalName,
                            weapDef->szModeName);
                }
                weapInfo->translatedModename = weapDef->szModeName;
            }
            weapInfo->translatedAIOverlayDescription = SEH_StringEd_GetString(weapDef->szOverlayName);
            if (!weapInfo->translatedAIOverlayDescription)
            {
                if (loc_warnings->current.enabled)
                {
                    if (loc_warningsAsErrors->current.enabled)
                        Com_Error(
                            ERR_LOCALIZATION,
                            "Weapon %s: Could not translate AI overlay description \"%s\"",
                            weapDef->szInternalName,
                            weapDef->szOverlayName);
                    else
                        Com_PrintWarning(
                            17,
                            "WARNING: Weapon %s: Could not translate AI overlay description \"%s\"\n",
                            weapDef->szInternalName,
                            weapDef->szOverlayName);
                }
                weapInfo->translatedAIOverlayDescription = weapDef->szOverlayName;
            }
            VR_JavelinDiagnostic("register: translations complete", weaponNum, weapInfo->viewModelDObj);
        }
        else
        {
            VR_JavelinDiagnostic("register: already registered", weaponNum, weapInfo->viewModelDObj);
        }
        VR_JavelinDiagnostic("register: exit", weaponNum, weapInfo->viewModelDObj);
    }
}

XAnimTree_s *__cdecl CG_CreateWeaponViewModelXAnim(WeaponDef *weapDef)
{
    int32_t v2; // [esp+0h] [ebp-14h]
    int32_t animIndex; // [esp+8h] [ebp-Ch]
    XAnimTree_s *pAnimTree; // [esp+Ch] [ebp-8h]
    XAnim_s *pAnims; // [esp+10h] [ebp-4h]

    iassert(weapDef);
    pAnims = XAnimCreateAnims("VIEWMODEL", 33, Hunk_AllocXAnimClient);
    iassert(pAnims);
    XAnimBlend(pAnims, 0, "root", 1, 32, 0);
    for (animIndex = 1; animIndex < 33; ++animIndex)
    {
        if (*weapDef->szXAnims[animIndex])
            v2 = animIndex;
        else
            v2 = 1;

        BG_CreateXAnim(pAnims, animIndex, (char *)weapDef->szXAnims[v2]);
    }
    pAnimTree = XAnimCreateTree(pAnims, Hunk_AllocXAnimClient);
    iassert(pAnimTree);
    if (!weapDef->szXAnims[1] || !*weapDef->szXAnims[1])
        Com_Error(ERR_DROP, "CG_RegisterWeapon: No idle anim specified for [%s]", weapDef->szDisplayName);
    if (*weapDef->szXAnims[31] && XAnimIsLooped(pAnims, 0x1Fu))
        Com_Error(ERR_DROP, "CG_RegisterWeapon: ADS anim [%s] cannot be looping", weapDef->szXAnims[31]);
    if (*weapDef->szXAnims[32] && XAnimIsLooped(pAnims, 0x20u))
        Com_Error(ERR_DROP, "CG_RegisterWeapon: ADS anim [%s] cannot be looping", weapDef->szXAnims[32]);
    return pAnimTree;
}

void __cdecl CG_UpdateWeaponViewmodels(int32_t localClientNum)
{
    uint32_t weaponIndex; // [esp+8h] [ebp-4h]

    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    for (weaponIndex = 1; weaponIndex < BG_GetNumWeapons(); ++weaponIndex)
    {
        if (cg_weaponsArray[0][weaponIndex].weapModelIdx != cgameGlob->nextSnap->ps.weaponmodels[weaponIndex])
            ChangeViewmodelDobj(
                localClientNum,
                weaponIndex,
                cgameGlob->nextSnap->ps.weaponmodels[weaponIndex],
                cg_weaponsArray[0][weaponIndex].handModel,
                cg_weaponsArray[0][weaponIndex].gogglesModel,
                cg_weaponsArray[0][weaponIndex].rocketModel,
                cg_weaponsArray[0][weaponIndex].knifeModel,
                1);
    }
}

void __cdecl ChangeViewmodelDobj(
    int32_t localClientNum,
    uint32_t weaponNum,
    uint8_t weaponModel,
    XModel *newHands,
    XModel *newGoggles,
    XModel *newRocket,
    XModel *newKnife,
    bool updateClientInfo)
{
    uint32_t NumWeapons; // eax
    weaponInfo_s *weapInfo; // [esp+8h] [ebp-34h]
    uint32_t dobjHandle; // [esp+Ch] [ebp-30h]
    int32_t mdlIdx; // [esp+10h] [ebp-2Ch]
    WeaponDef *weapDef; // [esp+14h] [ebp-28h]
    XAnimTree_s *pAnimTree; // [esp+18h] [ebp-24h]
    DObjModel_s dobjModels[4]; // [esp+1Ch] [ebp-20h] BYREF

    VR_JavelinDiagnostic("change DObj: enter", weaponNum, newHands);
    if (weaponNum)
    {
        bcassert(weaponNum, BG_GetNumWeapons());
        iassert(newHands);
        iassert(localClientNum == 0);
        weapInfo = &cg_weaponsArray[0][weaponNum]; // KISAKTODO: refactor to CG_GetLocalClientWeaponInfo()
        weapDef = BG_GetWeaponDef(weaponNum);
        VR_JavelinDiagnostic("change DObj: weapon definition resolved", weaponNum, weapDef);
        if (weapDef->gunXModel[weaponModel])
        {
            weapInfo->handModel = newHands;
            weapInfo->gogglesModel = newGoggles;
            weapInfo->rocketModel = newRocket;
            weapInfo->knifeModel = newKnife;
            weapInfo->weapModelIdx = weaponModel;
            dobjHandle = CG_WeaponDObjHandle(weaponNum);
            if (weapInfo->viewModelDObj)
            {
                pAnimTree = DObjGetTree(weapInfo->viewModelDObj);
                Com_SafeClientDObjFree(dobjHandle, localClientNum);
            }
            else
            {
                iassert(!weapInfo->tree);
                pAnimTree = CG_CreateWeaponViewModelXAnim(weapDef);
                weapInfo->tree = pAnimTree;
            }
            iassert(pAnimTree);
            dobjModels[0].boneName = 0;
            dobjModels[0].ignoreCollision = 0;
#ifdef KISAK_SP
            dobjModels[0].model =
                VR_GetRightOnlyWeaponHandModel(
                    weapInfo->handModel);
#else
            dobjModels[0].model = weapInfo->handModel;
#endif

            dobjModels[1].boneName = scr_const.tag_weapon;
            dobjModels[1].ignoreCollision = 0;
            dobjModels[1].model = weapDef->gunXModel[weaponModel];
            mdlIdx = 2;
            if (weapInfo->gogglesModel)
            {
                if (overrideNVGModelWithKnife->current.enabled)
                    dobjModels[2].boneName = scr_const.tag_gasmask2;
                else
                    dobjModels[2].boneName = scr_const.tag_gasmask;
                dobjModels[2].model = weapInfo->gogglesModel;
                dobjModels[2].ignoreCollision = 0;
                mdlIdx = 3;
            }
            if (weapInfo->rocketModel)
            {
                dobjModels[mdlIdx].boneName = scr_const.tag_clip;
                dobjModels[mdlIdx].ignoreCollision = 0;
                dobjModels[mdlIdx].model = weapInfo->rocketModel;
                mdlIdx++;
            }
            if (weapInfo->knifeModel)
            {
                dobjModels[mdlIdx].boneName = scr_const.tag_knife_attach;
                dobjModels[mdlIdx].ignoreCollision = 0;
                dobjModels[mdlIdx].model = weapInfo->knifeModel;
                mdlIdx++;
            }
            iassert(mdlIdx <= MYMODELCOUNT);
            VR_JavelinDiagnostic("change DObj: before DObj creation", weaponNum, pAnimTree);
            weapInfo->viewModelDObj = Com_ClientDObjCreate(dobjModels, mdlIdx, pAnimTree, dobjHandle, localClientNum);
            VR_JavelinDiagnostic("change DObj: DObj creation complete", weaponNum, weapInfo->viewModelDObj);
            DObjSetHidePartBits(weapInfo->viewModelDObj, weapInfo->partBits);
            VR_JavelinDiagnostic("change DObj: hide-part setup complete", weaponNum, weapInfo->viewModelDObj);
            if (updateClientInfo)
            {
                DObjUpdateClientInfo(weapInfo->viewModelDObj, 0.05f, 0);
                VR_JavelinDiagnostic("change DObj: client update complete", weaponNum, weapInfo->viewModelDObj);
            }
        }
    }
}

void __cdecl CG_UpdateHandViewmodels(int32_t localClientNum, XModel *handModel)
{
    uint32_t weaponIndex; // [esp+Ch] [ebp-4h]

    iassert(handModel);
    iassert(localClientNum == 0);
    for (weaponIndex = 1; weaponIndex < BG_GetNumWeapons(); ++weaponIndex)
    {
        iassert(localClientNum == 0);
        if (cg_weaponsArray[0][weaponIndex].handModel != handModel)
            ChangeViewmodelDobj(
                localClientNum,
                weaponIndex,
                CG_GetLocalClientGlobals(localClientNum)->predictedPlayerState.weaponmodels[weaponIndex],
                handModel,
                cg_weaponsArray[0][weaponIndex].gogglesModel,
                cg_weaponsArray[0][weaponIndex].rocketModel,
                cg_weaponsArray[0][weaponIndex].knifeModel,
                1);
    }
}

void __cdecl CG_RegisterItemVisuals(int32_t localClientNum, uint32_t weapIdx)
{
    int32_t modelIdx; // [esp+4h] [ebp-4h]

    bcassert(weapIdx, MAX_WEAPONS);

    for (modelIdx = 0; modelIdx < 16; ++modelIdx)
    {
        gitem_s *item = &bg_itemlist[128 * modelIdx + weapIdx];
        iassert(item->giType == IT_WEAPON);
    }

    CG_RegisterWeapon(localClientNum, weapIdx);
}

void __cdecl CG_RegisterItems(int32_t localClientNum)
{
    char v1; // al
    char *v2; // [esp+8h] [ebp-98h]
    const char *ConfigString; // [esp+Ch] [ebp-94h]
    char items[132]; // [esp+10h] [ebp-90h] BYREF
    int32_t i; // [esp+98h] [ebp-8h]
    int32_t digit; // [esp+9Ch] [ebp-4h]

    ConfigString = CL_GetConfigString(localClientNum, CS_ITEMS);
    v2 = items;
    do
    {
        v1 = *ConfigString;
        *v2++ = *ConfigString++;
    } while (v1);
    for (i = 1; i < 128; ++i)
    {
        digit = items[i / 4];
        if (digit > 57)
            digit -= 87;
        else
            digit -= 48;
        if ((digit & (1 << (i & 3))) != 0)
            CG_RegisterItemVisuals(localClientNum, i);
    }
}

void __cdecl CG_HoldBreathInit(cg_s *cgameGlob)
{
    cgameGlob->holdBreathTime = -1;
    cgameGlob->holdBreathInTime = 0;
    cgameGlob->holdBreathDelay = 0;
    cgameGlob->holdBreathFrac = 0.0;
}

void __cdecl CG_UpdateViewModelPose(const DObj_s* obj, int32_t localClientNum)
{
    cg_s *cgameGlob;

    if (obj)
    {
        DObjLock((DObj_s*)obj); // LWSS: backport locks from blops
        DObjClearSkel(obj);
        DObjUnlock((DObj_s *)obj);
    }

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    AxisToAngles((mat3x3 &)cgameGlob->viewModelAxis, cgameGlob->viewModelPose.angles);
    cgameGlob->viewModelPose.origin[0] = cgameGlob->viewModelAxis[3][0];
    cgameGlob->viewModelPose.origin[1] = cgameGlob->viewModelAxis[3][1];
    cgameGlob->viewModelPose.origin[2] = cgameGlob->viewModelAxis[3][2];
}

#ifdef KISAK_MP
bool __cdecl CG_IsPlayerCrouching(clientInfo_t *ci, const centity_s *cent)
{
    return BG_IsCrouchingAnim(ci, cent->nextState.legsAnim);
}

bool __cdecl CG_IsPlayerProne(clientInfo_t *ci, const centity_s *cent)
{
    return BG_IsProneAnim(ci, cent->nextState.legsAnim);
}

bool __cdecl CG_IsPlayerADS(clientInfo_t *ci, const centity_s *cent)
{
    return BG_IsAds(ci, cent->nextState.legsAnim);
}

void __cdecl CG_GuessSpreadForWeapon(
    int32_t localClientNum,
    const centity_s *cent,
    const WeaponDef *weapDef,
    float *minSpread,
    float *maxSpread)
{
    clientInfo_t *ci; // [esp+4h] [ebp-4h]
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    bcassert(cent->nextState.number, MAX_CLIENTS);

    ci = &cgameGlob->bgs.clientinfo[cent->nextState.number];

    if (CG_IsPlayerProne(ci, cent))
    {
        *minSpread = weapDef->fHipSpreadProneMin;
        *maxSpread = weapDef->hipSpreadProneMax;
    }
    else if (CG_IsPlayerCrouching(ci, cent))
    {
        *minSpread = weapDef->fHipSpreadDuckedMin;
        *maxSpread = weapDef->hipSpreadDuckedMax;
    }
    else
    {
        *minSpread = weapDef->fHipSpreadStandMin;
        *maxSpread = weapDef->hipSpreadStandMax;
    }
}

void __cdecl CG_GetPlayerViewOrigin(int32_t localClientNum, const playerState_s *ps, float *origin)
{
    DObj_s *obj; // [esp+0h] [ebp-8h]
    centity_s *turretEnt; // [esp+4h] [ebp-4h]

    if ((ps->eFlags & 0x300) != 0)
    {
        iassert(ps->viewlocked);
        iassert(ps->viewlocked_entNum != ENTITYNUM_NONE);
        turretEnt = CG_GetEntity(localClientNum, ps->viewlocked_entNum);
        obj = Com_GetClientDObj(turretEnt->nextState.number, 0);
        if (!obj)
            Com_Error(ERR_DROP, "CG_GetPlayerViewOrigin: Unable to get DObj for turret entity %i", turretEnt->nextState.number);
        if (!CG_DObjGetWorldTagPos(&turretEnt->pose, obj, scr_const.tag_player, origin))
            Com_Error(ERR_DROP, "CG_GetPlayerViewOrigin: Couldn't find [tag_player] on turret");
    }
    else
    {
        BG_GetPlayerViewOrigin(ps, origin, CG_GetLocalClientGlobals(localClientNum)->time);
    }
}
#endif

#if defined(KISAK_MP) || defined(KISAK_SP)
// Returns the filtered OpenXR right grip origin in CoD world space.
bool VR_GetRightControllerWeaponGripWorld(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float gripWorld[3]);
#endif

#ifdef KISAK_SP
// KISAK_VR_DEDICATED_SCOPE_CAMERA_V2
static bool
    s_vrPhysicalScopeViewWeaponDeferred = false;
#endif

void __cdecl CG_AddPlayerWeapon(
    int32_t localClientNum,
    const GfxScaledPlacement* placement,
    const playerState_s* ps,
    centity_s* cent,
    int32_t bDrawGun)
{
    uint32_t fLeanDist; // [esp+Ch] [ebp-48h]
    bool v7; // [esp+10h] [ebp-44h]
    BOOL v8; // [esp+14h] [ebp-40h]
    bool v9; // [esp+18h] [ebp-3Ch]
    snapshot_s* nextSnap; // [esp+28h] [ebp-2Ch]
    const weaponInfo_s* weapInfo; // [esp+30h] [ebp-24h]
    int32_t weaponNum; // [esp+34h] [ebp-20h]
    float lightingOrigin[3]; // [esp+44h] [ebp-10h] BYREF
    const WeaponDef* weapDef; // [esp+50h] [ebp-4h]

    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);

#ifdef KISAK_SP
    bool deferPhysicalScopeViewWeapon = false;
    bool manualGrenadeViewOverride = false;

    if (ps != nullptr)
    {
        s_vrPhysicalScopeViewWeaponDeferred =
            false;

        manualGrenadeViewOverride =
            bDrawGun &&
            ps->weapon > 0 &&
            VR_IsManualGrenadeViewOverrideActive();

        if (bDrawGun &&
            VR_IsPhysicalSniperScopeAimActive())
        {
            deferPhysicalScopeViewWeapon =
                VR_GetPhysicalSniperScopeCaptureLayout(
                    static_cast<int>(
                        cgameGlob->refdef.width),
                    static_cast<int>(
                        cgameGlob->refdef.height),
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr);

            s_vrPhysicalScopeViewWeaponDeferred =
                deferPhysicalScopeViewWeapon;
        }
    }
#endif

    nextSnap = cgameGlob->nextSnap;
#ifdef KISAK_MP
    v9 = (nextSnap->ps.otherFlags & 6) != 0 && cent->nextState.number == nextSnap->ps.clientNum;
    v8 = v9 && !cgameGlob->renderingThirdPerson;
#endif

    if (ps)
    {
#ifdef KISAK_SP
        // V53 leaves the ordinary firearm in the tracked right hand while a
        // standalone projectile model follows the left hand.  Native offhand
        // simulation still owns ps->offHandIndex and all grenade events.
        weaponNum =
            manualGrenadeViewOverride
                ? ps->weapon
                : BG_GetViewmodelWeaponIndex(ps);
#else
        weaponNum = BG_GetViewmodelWeaponIndex(ps);
#endif
    }
    else
    {
        weaponNum = cent->nextState.weapon;
    }

    // KISAKFIX: IDA CG_AddPlayerWeapon (sub_8215C3B8) gates on BOTH
    // `(eFlags & 0x300) == 0` (not turret) AND `(eFlags & 0x20000) == 0` (not in vehicle).
    // Kisak port missing the vehicle gate so viewmodel renders while driving.
    if (weaponNum > 0
        && (cent->nextState.lerp.eFlags & 0x300) == 0
        && (cent->nextState.lerp.eFlags & 0x20000) == 0)
    {
        iassert(localClientNum == 0);
        weapInfo = &cg_weaponsArray[0][weaponNum];
        weapDef = BG_GetWeaponDef(weaponNum);
        iassert(weapDef);
        VR_JavelinDiagnostic("render: enter", weaponNum, weapInfo->viewModelDObj);
        iassert(weapInfo->viewModelDObj);
        if (ps)
        {
            UnitQuatToAxis(placement->base.quat, (mat3x3&)cgameGlob->viewModelAxis);
            cgameGlob->viewModelAxis[3][0] = placement->base.origin[0];
            cgameGlob->viewModelAxis[3][1] = placement->base.origin[1];
            cgameGlob->viewModelAxis[3][2] = placement->base.origin[2];
            CG_UpdateViewModelPose(weapInfo->viewModelDObj, localClientNum);
            VR_JavelinDiagnostic("render: initial pose update complete", weaponNum, weapInfo->viewModelDObj);

#if defined(KISAK_MP) || defined(KISAK_SP)
#if defined(KISAK_MP)
            if (bDrawGun &&
                !cgameGlob->renderingThirdPerson)
#else
            if (bDrawGun)
#endif
            {
                float vrTrackedGripWorld[3] = {};
                float vrViewmodelGripTagWorld[3] = {};

                const char* configuredTagName =
                    "tag_weapon_right";

#if defined(KISAK_MP)
                if (cg_weaponrightbone != nullptr &&
                    cg_weaponrightbone->current.string != nullptr &&
                    cg_weaponrightbone->current.string[0] != '\0')
                {
                    configuredTagName =
                        cg_weaponrightbone->current.string;
                }
#else
                static bool loggedSpGripTagFallback = false;

                if (!loggedSpGripTagFallback)
                {
                    Com_Printf(
                        0,
                        "[VR] SP uses the default viewmodel "
                        "grip tag fallback.\n");

                    loggedSpGripTagFallback = true;
                }
#endif

                const char* candidateTagNames[] = {
                    configuredTagName,
                    "tag_weapon",
                    "tag_inhand",
                    "tag_origin",
                };

                const char* selectedTagName = nullptr;
                bool foundGripTag = false;

                if (VR_GetRightControllerWeaponGripWorld(
                        cgameGlob->refdef.vieworg,
                        cgameGlob->refdef.viewaxis,
                        vrTrackedGripWorld))
                {
                    for (const char* candidateTagName :
                         candidateTagNames)
                    {
                        if (candidateTagName == nullptr ||
                            candidateTagName[0] == '\0')
                        {
                            continue;
                        }

                        const uint32_t candidateTag =
                            SL_FindString(
                                candidateTagName);

                        if (candidateTag == 0)
                        {
                            continue;
                        }

                        if (CG_DObjGetWorldTagPos(
                                &cgameGlob->viewModelPose,
                                weapInfo->viewModelDObj,
                                candidateTag,
                                vrViewmodelGripTagWorld))
                        {
                            selectedTagName =
                                candidateTagName;

                            foundGripTag = true;
                            break;
                        }
                    }
                }

                if (foundGripTag)
                {
                    const float vrGripCorrection[3] = {
                        vrTrackedGripWorld[0] -
                            vrViewmodelGripTagWorld[0],
                        vrTrackedGripWorld[1] -
                            vrViewmodelGripTagWorld[1],
                        vrTrackedGripWorld[2] -
                            vrViewmodelGripTagWorld[2],
                    };

                    cgameGlob->viewModelAxis[3][0] +=
                        vrGripCorrection[0];

                    cgameGlob->viewModelAxis[3][1] +=
                        vrGripCorrection[1];

                    cgameGlob->viewModelAxis[3][2] +=
                        vrGripCorrection[2];

                    CG_UpdateViewModelPose(
                        weapInfo->viewModelDObj,
                        localClientNum);

                    static bool loggedVrGripTagAlignment = false;

                    if (!loggedVrGripTagAlignment)
                    {
                        Com_Printf(
                            0,
                            "[VR] Aligned viewmodel grip tag '%s' "
                            "to the tracked right grip pose.\n",
                            selectedTagName);

                        loggedVrGripTagAlignment = true;
                    }
                }
                else
                {
                    static bool loggedVrGripTagAlignmentFailure = false;

                    if (!loggedVrGripTagAlignmentFailure)
                    {
                        Com_PrintWarning(
                            0,
                            "[VR] No usable viewmodel grip tag found; "
                            "tried configured, tag_weapon, "
                            "tag_inhand, and tag_origin.\n");

                        loggedVrGripTagAlignmentFailure = true;
                    }
                }
            }

#if defined(KISAK_MP)
            if (bDrawGun &&
                !cgameGlob->renderingThirdPerson)
#else
            if (bDrawGun)
#endif
            {
                const int32_t vrWeaponDobjNumber =
                    CG_WeaponDObjHandle(weaponNum);

                uint8_t vrMuzzleBoneIndex = 0;
                orientation_t vrMuzzleOrientation = {};

                if (CG_GetBoneIndex(
                        localClientNum,
                        vrWeaponDobjNumber,
                        scr_const.tag_flash,
                        &vrMuzzleBoneIndex) &&
                    FX_GetBoneOrientation(
                        localClientNum,
                        vrWeaponDobjNumber,
                        vrMuzzleBoneIndex,
                        &vrMuzzleOrientation))
                {
                    VR_PublishRightControllerWeaponMuzzleWorld(
                        vrMuzzleOrientation.origin);

                    static bool loggedVrMuzzlePublication = false;

                    if (!loggedVrMuzzlePublication)
                    {
                        Com_Printf(
                            0,
                            "[VR] Published transformed tag_flash "
                            "for physical bullet origins.\n");

                        loggedVrMuzzlePublication = true;
                    }
                }

#ifdef KISAK_SP
                if (VR_IsPhysicalSniperScopeAimActive())
                {
                    const char* scopeTagCandidates[] = {
                        "tag_scope_rear",
                        "tag_scope_rear_lid_animate",
                        "tag_scope",
                        "tag_scope_animate",
                        "tag_sights",
                        "tag_sight",
                        "tag_reticle",
                    };

                    float vrScopeOriginWorld[3] = {};
                    const char* selectedScopeAnchor = nullptr;

                    for (const char* scopeTagName :
                         scopeTagCandidates)
                    {
                        const uint32_t scopeTag =
                            SL_FindString(
                                scopeTagName);

                        if (scopeTag != 0 &&
                            CG_DObjGetWorldTagPos(
                                &cgameGlob->viewModelPose,
                                weapInfo->viewModelDObj,
                                scopeTag,
                                vrScopeOriginWorld))
                        {
                            selectedScopeAnchor =
                                scopeTagName;

                            break;
                        }
                    }

                    if (selectedScopeAnchor == nullptr)
                    {
                        float vrTrackedGripWorld[3] = {};

                        if (VR_GetRightControllerWeaponGripWorld(
                                cgameGlob->refdef.vieworg,
                                cgameGlob->refdef.viewaxis,
                                vrTrackedGripWorld))
                        {
                            constexpr float
                                kScopeFallbackForwardGameUnits =
                                    0.20f *
                                    39.37007874015748f;

                            constexpr float
                                kScopeFallbackUpGameUnits =
                                    0.055f *
                                    39.37007874015748f;

                            for (int worldComponent = 0;
                                 worldComponent < 3;
                                 ++worldComponent)
                            {
                                vrScopeOriginWorld[worldComponent] =
                                    vrTrackedGripWorld[worldComponent] +
                                    cgameGlob->viewModelAxis[
                                        0][worldComponent] *
                                        kScopeFallbackForwardGameUnits +
                                    cgameGlob->viewModelAxis[
                                        2][worldComponent] *
                                        kScopeFallbackUpGameUnits;
                            }

                            selectedScopeAnchor =
                                "grip-relative fallback";
                        }
                    }

                    if (selectedScopeAnchor != nullptr)
                    {
                        VR_PublishPhysicalSniperScopePoseWorld(
                            vrScopeOriginWorld,
                            cgameGlob->viewModelAxis,
                            cgameGlob->refdef.vieworg,
                            cgameGlob->refdef.viewaxis);

                        static bool
                            loggedVrScopeAnchorPublication =
                                false;

                        if (!loggedVrScopeAnchorPublication)
                        {
                            Com_Printf(
                                0,
                                "[VR] Attached the physical sniper scope "
                                "to viewmodel anchor '%s'.\n",
                                selectedScopeAnchor);

                            loggedVrScopeAnchorPublication =
                                true;
                        }
                    }
                    else
                    {
                        static bool
                            loggedVrScopeAnchorFailure =
                                false;

                        if (!loggedVrScopeAnchorFailure)
                        {
                            Com_PrintWarning(
                                0,
                                "[VR] Could not publish a physical "
                                "sniper-scope anchor.\n");

                            loggedVrScopeAnchorFailure =
                                true;
                        }
                    }
                }
#endif
            }
#endif

#ifdef KISAK_SP
            if (bDrawGun)
            {
                const bool detachableMagazineClass =
                    weapDef->weapClass == WEAPCLASS_RIFLE ||
                    weapDef->weapClass == WEAPCLASS_SMG ||
                    weapDef->weapClass == WEAPCLASS_PISTOL;

                const bool detachableMagazineWeapon =
                    weapDef->worldClipModel != nullptr &&
                    weapDef->weapType == WEAPTYPE_BULLET &&
                    detachableMagazineClass &&
                    !weapDef->bSegmentedReload &&
                    !weapDef->bBoltAction;

                const uint32_t magazineTagCandidates[] = {
                    scr_const.tag_clip,
                    SL_FindString("tag_mag"),
                    SL_FindString("tag_magazine"),
                    SL_FindString("tag_clip_animate"),
                };

                float magazineWellOrigin[3] = {};
                float magazineWellAxis[3][3] = {
                    {1.0f, 0.0f, 0.0f},
                    {0.0f, 1.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f},
                };

                uint8_t magazineBoneIndex = 0u;
                bool foundMagazineWell = false;

                if (detachableMagazineWeapon)
                {
                    for (const uint32_t magazineTag :
                         magazineTagCandidates)
                    {
                        if (magazineTag == 0u)
                        {
                            continue;
                        }

                        uint8_t candidateBoneIndex = 0u;

                        if (DObjGetBoneIndex(
                                weapInfo->viewModelDObj,
                                magazineTag,
                                &candidateBoneIndex) &&
                            CG_DObjGetWorldTagMatrix(
                                &cgameGlob->viewModelPose,
                                weapInfo->viewModelDObj,
                                magazineTag,
                                magazineWellAxis,
                                magazineWellOrigin))
                        {
                            magazineBoneIndex =
                                candidateBoneIndex;

                            foundMagazineWell = true;
                            break;
                        }
                    }
                }

                const int clipIndex =
                    BG_ClipForWeapon(weaponNum);

                const int ammoIndex =
                    BG_AmmoForWeapon(weaponNum);

                const bool canManualReload =
                    foundMagazineWell &&
                    ps->weaponstate == WEAPON_READY &&
                    ps->ammoclip[clipIndex] <
                        weapDef->iClipSize &&
                    ps->ammo[ammoIndex] > 0;

                VR_UpdateManualMagazineReload(
                    static_cast<int>(weaponNum),
                    detachableMagazineWeapon &&
                        foundMagazineWell,
                    canManualReload,
                    magazineWellOrigin,
                    magazineWellAxis,
                    cgameGlob->refdef.vieworg,
                    cgameGlob->refdef.viewaxis);

                bool hideLoadedMagazine = false;
                bool drawEjectedMagazine = false;
                bool drawHeldMagazine = false;
                float ejectedOrigin[3] = {};
                float ejectedAxis[3][3] = {};
                float heldOrigin[3] = {};
                float heldAxis[3][3] = {};

                VR_GetManualMagazineReloadRenderState(
                    static_cast<int>(weaponNum),
                    &hideLoadedMagazine,
                    &drawEjectedMagazine,
                    ejectedOrigin,
                    ejectedAxis,
                    &drawHeldMagazine,
                    heldOrigin,
                    heldAxis);

                const float floatingHandLightingOrigin[3] = {
                    ps->origin[0],
                    ps->origin[1],
                    ps->origin[2] +
                        ps->viewHeightCurrent,
                };

                // Update reload ownership first so the glove and magazine
                // consume the same-frame held pose.  This avoids a one-frame
                // controller lag and lets the glove inherit the insertion
                // orientation snap without separating from the magazine.
                const bool floatingLeftHandRendered =
                    VR_AddFloatingLeftHandToScene(
                        weaponNum,
                        weapInfo->viewModelDObj,
                        weapInfo->handModel,
                        &cgameGlob->viewModelPose,
                        ps,
                        cgameGlob->refdef.vieworg,
                        cgameGlob->refdef.viewaxis,
                        floatingHandLightingOrigin,
                        drawHeldMagazine,
                        heldOrigin,
                        heldAxis);

                uint32_t manualHidePartBits[4] = {
                    weapInfo->partBits[0],
                    weapInfo->partBits[1],
                    weapInfo->partBits[2],
                    weapInfo->partBits[3],
                };

                // V2 retargets the original combined hand mesh in place.
                (void)floatingLeftHandRendered;

                if (hideLoadedMagazine &&
                    foundMagazineWell)
                {
                    manualHidePartBits[
                        magazineBoneIndex >> 5] |=
                        0x80000000u >>
                        (magazineBoneIndex & 0x1F);
                }

                DObjSetHidePartBits(
                    weapInfo->viewModelDObj,
                    manualHidePartBits);

                float manualReloadLightingOrigin[3] = {
                    ps->origin[0],
                    ps->origin[1],
                    ps->origin[2] +
                        ps->viewHeightCurrent,
                };

                VR_AddManualReloadClipModelsToScene(
                    weaponNum,
                    weapDef->worldClipModel,
                    manualReloadLightingOrigin);

                VR_AddManualGrenadeModelToScene(
                    manualReloadLightingOrigin);

                static bool loggedManualReloadSupport = false;

                if (detachableMagazineWeapon &&
                    foundMagazineWell &&
                    !loggedManualReloadSupport)
                {
                    Com_Printf(
                        0,
                        "[VR][RELOAD] Weapon %u supports physical "
                        "magazine ejection, hip draw, and insertion.\n",
                        weaponNum);

                    loggedManualReloadSupport = true;
                }
            }
#endif

            VR_JavelinDiagnostic("render: VR grip and muzzle stages complete", weaponNum, weapInfo->viewModelDObj);
            if (bDrawGun
#ifdef KISAK_SP
                && !deferPhysicalScopeViewWeapon
#endif
                )
            {
                lightingOrigin[0] = ps->origin[0];
                lightingOrigin[1] = ps->origin[1];
                lightingOrigin[2] = ps->origin[2];
                lightingOrigin[2] = lightingOrigin[2] + ps->viewHeightCurrent;
                AddLeanToPosition(lightingOrigin, ps->viewangles[1], ps->leanf, 16.0, 20.0);
                VR_JavelinDiagnostic("render: before scene submission", weaponNum, weapInfo->viewModelDObj);
                R_AddDObjToScene(weapInfo->viewModelDObj, &cgameGlob->viewModelPose, ENTITYNUM_NONE, 3u, lightingOrigin, 0.0);
                VR_JavelinDiagnostic("render: scene submission complete", weaponNum, weapInfo->viewModelDObj);
                v7 = CG_LookingThroughNightVision(localClientNum) && weapDef->laserSightDuringNightvision;
                if (cg_laserForceOn->current.enabled || v7)
                    CG_Laser_Add(
                        cent,
                        weapInfo->viewModelDObj,
                        &cgameGlob->viewModelPose,
                        cgameGlob->refdef.viewOffset,
                        LASER_OWNER_PLAYER);
                cgameGlob->refdef.dof.viewModelStart = (weapDef->adsDofStart - ps->dofViewmodelStart) * ps->fWeaponPosFrac + ps->dofViewmodelStart;
                cgameGlob->refdef.dof.viewModelEnd = (weapDef->adsDofEnd - ps->dofViewmodelEnd) * ps->fWeaponPosFrac + ps->dofViewmodelEnd;
            }
#ifdef KISAK_SP
            else if (deferPhysicalScopeViewWeapon)
            {
                static bool
                    loggedDeferredScopeViewWeapon = false;

                if (!loggedDeferredScopeViewWeapon)
                {
                    Com_Printf(
                        0,
                        "[VR] Deferred the scoped rifle until after "
                        "the weapon-free scope-camera render.\n");

                    loggedDeferredScopeViewWeapon =
                        true;
                }
            }
#endif
            HoldBreathUpdate(localClientNum);
        }
        if (cent->bMuzzleFlash 
#ifdef KISAK_MP
            && (!v8 || ps)
#endif
            )
        {
            cent->bMuzzleFlash = 0;
            if (bDrawGun)
            {
                if (ps)
                {
                    fLeanDist = scr_const.tag_flash;
                    WeaponFlash(localClientNum, CG_WeaponDObjHandle(weaponNum), weaponNum, 1, fLeanDist);
                }
                else
                {
                    WeaponFlash(localClientNum, cent->nextState.number, weaponNum, 0, scr_const.tag_flash);
                }
            }
        }
    }
}

#ifdef KISAK_SP
void __cdecl
CG_AddDeferredPhysicalSniperViewWeaponToScene(
    const int32_t localClientNum)
{
    if (!s_vrPhysicalScopeViewWeaponDeferred)
    {
        return;
    }

    s_vrPhysicalScopeViewWeaponDeferred =
        false;

    cg_s* cgameGlob =
        CG_GetLocalClientGlobals(
            localClientNum);

    playerState_s* ps =
        &cgameGlob->predictedPlayerState;

    const int32_t weaponNum =
        BG_GetViewmodelWeaponIndex(ps);

    if (weaponNum <= 0 ||
        (cgameGlob->predictedPlayerEntity
             .nextState.lerp.eFlags &
         0x20300) != 0)
    {
        return;
    }

    const weaponInfo_s* weapInfo =
        &cg_weaponsArray[0][weaponNum];

    if (weapInfo->viewModelDObj == nullptr)
    {
        return;
    }

    float lightingOrigin[3] = {
        ps->origin[0],
        ps->origin[1],
        ps->origin[2] +
            ps->viewHeightCurrent,
    };

    AddLeanToPosition(
        lightingOrigin,
        ps->viewangles[1],
        ps->leanf,
        16.0f,
        20.0f);

    R_AddDObjToScene(
        weapInfo->viewModelDObj,
        &cgameGlob->viewModelPose,
        ENTITYNUM_NONE,
        3u,
        lightingOrigin,
        0.0f);

    const WeaponDef* weapDef =
        BG_GetWeaponDef(weaponNum);

    if (weapDef != nullptr)
    {
        const bool nightVisionLaser =
            CG_LookingThroughNightVision(
                localClientNum) &&
            weapDef->laserSightDuringNightvision;

        if (cg_laserForceOn->current.enabled ||
            nightVisionLaser)
        {
            CG_Laser_Add(
                &cgameGlob->predictedPlayerEntity,
                weapInfo->viewModelDObj,
                &cgameGlob->viewModelPose,
                cgameGlob->refdef.viewOffset,
                LASER_OWNER_PLAYER);
        }

        cgameGlob->refdef.dof.viewModelStart =
            (weapDef->adsDofStart -
             ps->dofViewmodelStart) *
                ps->fWeaponPosFrac +
            ps->dofViewmodelStart;

        cgameGlob->refdef.dof.viewModelEnd =
            (weapDef->adsDofEnd -
             ps->dofViewmodelEnd) *
                ps->fWeaponPosFrac +
            ps->dofViewmodelEnd;
    }

    static bool loggedDeferredScopeViewWeaponAdded =
        false;

    if (!loggedDeferredScopeViewWeaponAdded)
    {
        Com_Printf(
            0,
            "[VR] Added the scoped rifle after the dedicated "
            "scope-camera view; the main eyes retain the weapon.\n");

        loggedDeferredScopeViewWeaponAdded =
            true;
    }
}
#endif

void __cdecl WeaponFlash(
    int32_t localClientNum,
    uint32_t dobjHandle,
    uint32_t weaponNum,
    int32_t bViewFlash,
    uint32_t flashTag)
{
    const FxEffectDef *viewFlashEffect; // [esp+0h] [ebp-Ch]
    WeaponDef *weapDef; // [esp+8h] [ebp-4h]

    weapDef = BG_GetWeaponDef(weaponNum);
    if (bViewFlash)
        viewFlashEffect = weapDef->viewFlashEffect;
    else
        viewFlashEffect = weapDef->worldFlashEffect;
    if (viewFlashEffect)
        CG_PlayBoltedEffect(localClientNum, viewFlashEffect, dobjHandle, flashTag);
}

void __cdecl HoldBreathUpdate(int32_t localClientNum)
{
    float deltaTime; // [esp+10h] [ebp-10h]
    int32_t playbackId; // [esp+14h] [ebp-Ch]
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    if (cgameGlob->holdBreathDelay > 0)
        cgameGlob->holdBreathDelay -= cgameGlob->frametime;
    if ((cgameGlob->predictedPlayerState.weapFlags & 4) != 0)
    {
        deltaTime = (double)cgameGlob->frametime * EQUAL_EPSILON;
        cgameGlob->holdBreathFrac = DiffTrack(
            1.0,
            cgameGlob->holdBreathFrac,
            player_breath_snd_lerp->current.value,
            deltaTime);
        if (cgameGlob->holdBreathTime >= 0)
        {
            if (cgameGlob->holdBreathTime > cgameGlob->holdBreathInTime)
                CG_PlayClientSoundAlias(localClientNum, cgMedia.playerHeartBeatSound);
        }
        else
        {
            cgameGlob->holdBreathTime = 0;
            if (cgameGlob->holdBreathDelay > 0)
            {
                cgameGlob->holdBreathInTime = 0;
            }
            else
            {
                playbackId = CG_PlayClientSoundAlias(localClientNum, cgMedia.playerBreathInSound);
                SND_GetKnownLength(playbackId, &cgameGlob->holdBreathInTime);
                cgameGlob->holdBreathDelay = (int)(player_breath_snd_delay->current.value * 1000.0);
            }
        }
        cgameGlob->holdBreathTime += cgameGlob->frametime;
    }
    else
    {
        if (cgameGlob->holdBreathTime >= 0)
        {
            cgameGlob->holdBreathTime += cgameGlob->frametime;
            if (cgameGlob->holdBreathTime <= (int)(player_breath_hold_time->current.value * 1000.0))
            {
                if (cgameGlob->holdBreathDelay <= 0)
                {
                    CG_PlayClientSoundAlias(localClientNum, cgMedia.playerBreathOutSound);
                    cgameGlob->holdBreathDelay = (int)(player_breath_snd_delay->current.value * 1000.0);
                }
            }
            else
            {
                CG_PlayClientSoundAlias(localClientNum, cgMedia.playerBreathGaspSound);
            }
        }
        cgameGlob->holdBreathTime = -1;
        cgameGlob->holdBreathInTime = 0;
        cgameGlob->holdBreathFrac = 0.0;
    }
    HoldBreathSoundLerp(localClientNum, cgameGlob->holdBreathFrac);
}

void __cdecl HoldBreathSoundLerp(int32_t localClientNum, float lerp)
{
    int32_t channelIndex; // [esp+0h] [ebp-10Ch]
    float channelVolumes[64]; // [esp+Ch] [ebp-100h] BYREF
    cgs_t *cgs;

    if (lerp == 0.0)
    {
        SND_DeactivateChannelVolumes(1, 0);
    }
    else
    {
        cgs = CG_GetLocalClientStaticGlobals(localClientNum);
        for (channelIndex = 0; channelIndex < SND_GetEntChannelCount(); ++channelIndex)
            channelVolumes[channelIndex] = (cgs->holdBreathParams.sound.channelvolume[channelIndex] - 1.0) * lerp + 1.0;
        SND_SetChannelVolumes(1, channelVolumes, 0);
    }
}

void __cdecl CG_UpdateViewWeaponAnim(int32_t localClientNum)
{
    float dtime; // [esp+8h] [ebp-14h]
    weaponInfo_s* weapInfo; // [esp+Ch] [ebp-10h]
    int32_t weaponIndex; // [esp+14h] [ebp-8h]
    playerState_s* ps; // [esp+18h] [ebp-4h]
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    ps = &cgameGlob->predictedPlayerState;
    if (cgameGlob->predictedPlayerState.pm_type < PM_DEAD)
    {
        weaponIndex = BG_GetViewmodelWeaponIndex(ps);
        if (weaponIndex > 0)
        {
            VR_JavelinDiagnostic("animation: frame begin", weaponIndex);
            CG_RegisterWeapon(localClientNum, weaponIndex);
            VR_JavelinDiagnostic("animation: registration complete", weaponIndex);
            iassert(localClientNum == 0);
            weapInfo = &cg_weaponsArray[0][weaponIndex];
            iassert(weapInfo->viewModelDObj);
            VR_JavelinDiagnostic("animation: before attachment update", weaponIndex, weapInfo->viewModelDObj);
            UpdateViewmodelAttachments(localClientNum, weaponIndex, ps->weaponmodels[weaponIndex], weapInfo);
            VR_JavelinDiagnostic("animation: attachment update complete", weaponIndex, weapInfo->viewModelDObj);
            WeaponRunXModelAnims(localClientNum, ps, weapInfo);
            VR_JavelinDiagnostic("animation: model animations complete", weaponIndex, weapInfo->viewModelDObj);
            dtime = (float)cgameGlob->frametime * EQUAL_EPSILON;
            DObjUpdateClientInfo(weapInfo->viewModelDObj, dtime, 1);
            VR_JavelinDiagnostic("animation: DObj client update complete", weaponIndex, weapInfo->viewModelDObj);
            ProcessWeaponNoteTracks(localClientNum, ps);
            VR_JavelinDiagnostic("animation: note tracks complete", weaponIndex, weapInfo->viewModelDObj);
        }
    }
    else
    {
        ResetWeaponAnimTrees(localClientNum, ps);
    }
}

void __cdecl WeaponRunXModelAnims(int32_t localClientNum, const playerState_s* ps, weaponInfo_s* weapInfo)
{
    BOOL v3; // [esp+14h] [ebp-34h]
    BOOL v4; // [esp+18h] [ebp-30h]
    BOOL v5; // [esp+1Ch] [ebp-2Ch]
    DObj_s* obj; // [esp+24h] [ebp-24h]
    float transitionTime; // [esp+28h] [ebp-20h]
    int32_t weaponIndex; // [esp+38h] [ebp-10h]
    int32_t i; // [esp+3Ch] [ebp-Ch]
    XAnimTree_s* pAnimTree; // [esp+40h] [ebp-8h]
    const WeaponDef* weapDef; // [esp+44h] [ebp-4h]

    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    obj = weapInfo->viewModelDObj;
    iassert(obj);
    pAnimTree = DObjGetTree(obj);
    weaponIndex = BG_GetViewmodelWeaponIndex(ps);
    iassert(weaponIndex > WP_NONE);
    weapDef = BG_GetWeaponDef(weaponIndex);
    if (weapDef->aimDownSight)
    {
        v5 = ps->weaponstate == 7 && ps->weaponTime - weapDef->iPositionReloadTransTime > 0;
        v4 = (ps->pm_flags & PMF_SIGHT_AIMING) != 0 && (ps->weapFlags & 2) == 0;
        v3 = !v5 && v4;
        PlayADSAnim(ps->fWeaponPosFrac, weaponIndex, obj, 32 - v3);
    }
    else if (*weapDef->szXAnims[32])
    {
        PlayADSAnim(0.0, weaponIndex, obj, 32);
    }
    if (ps->weapAnim != weapInfo->iPrevAnim || weaponIndex != cgameGlob->prevViewmodelWeapon)
    {
        transitionTime = 0.0;
        switch (ps->weapAnim & 0xFFFFFDFF)
        {
        case 0u:
        case 1u:
            i = 1;
            break;
        case 2u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 3, transitionTime);
            goto LABEL_64;
        case 3u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 5, transitionTime);
            goto LABEL_64;
        case 4u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 6, transitionTime);
            goto LABEL_64;
        case 5u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 28, transitionTime);
            goto LABEL_64;
        case 6u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 29, transitionTime);
            goto LABEL_64;
        case 7u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 30, transitionTime);
            goto LABEL_64;
        case 8u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 7, transitionTime);
            goto LABEL_64;
        case 9u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 8, transitionTime);
            goto LABEL_64;
        case 0xAu:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 15, transitionTime);
            goto LABEL_64;
        case 0xBu:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 13, transitionTime);
            goto LABEL_64;
        case 0xCu:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 14, transitionTime);
            goto LABEL_64;
        case 0xDu:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 9, transitionTime);
            goto LABEL_64;
        case 0xEu:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 10, transitionTime);
            goto LABEL_64;
        case 0xFu:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 11, transitionTime);
            goto LABEL_64;
        case 0x10u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 12, transitionTime);
            goto LABEL_64;
        case 0x11u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 17, transitionTime);
            goto LABEL_64;
        case 0x12u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 16, transitionTime);
            goto LABEL_64;
        case 0x13u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 19, transitionTime);
            goto LABEL_64;
        case 0x14u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 18, transitionTime);
            goto LABEL_64;
        case 0x15u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 21, transitionTime);
            goto LABEL_64;
        case 0x16u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 20, transitionTime);
            goto LABEL_64;
        case 0x17u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 22, transitionTime);
            goto LABEL_64;
        case 0x18u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 23, transitionTime);
            goto LABEL_64;
        case 0x19u:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 24, transitionTime);
            goto LABEL_64;
        case 0x1Au:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 4, transitionTime);
            goto LABEL_64;
        case 0x1Bu:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 25, transitionTime);
            goto LABEL_64;
        case 0x1Cu:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 26, transitionTime);
            goto LABEL_64;
        case 0x1Du:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 27, transitionTime);
            goto LABEL_64;
        default:
            StartWeaponAnim(localClientNum, weaponIndex, obj, 1, transitionTime);
            Com_Printf(19, "WeaponRunXModelAnims: Unknown weapon animation %i\n", ps->weapAnim & 0xFFFFFDFF);
        LABEL_64:
            weapInfo->iPrevAnim = ps->weapAnim;
            CG_GetLocalClientGlobals(localClientNum)->prevViewmodelWeapon = weaponIndex;
            return;
        }
        while (1)
        {
            if (i >= 31)
            {
                if (ps->ammoclip[BG_ClipForWeapon(weaponIndex)])
                    StartWeaponAnim(localClientNum, weaponIndex, obj, 1, transitionTime);
                else
                    StartWeaponAnim(localClientNum, weaponIndex, obj, 2, transitionTime);
                goto LABEL_64;
            }
            if (!XAnimHasFinished(pAnimTree, i))
            {
                weapInfo->iPrevAnim = -1;
                if ((ps->weapAnim & 1) == 0)
                    return;
                transitionTime = 0.5;
            }
            ++i;
        }
    }
}
void __cdecl StartWeaponAnim(
    int32_t localClientNum,
    uint32_t weaponNum,
    DObj_s* obj,
    int32_t animIndex,
    float transitionTime)
{
    float rate; // [esp+20h] [ebp-1Ch]
    XAnim_s* anims; // [esp+2Ch] [ebp-10h]
    WeaponDef* weapDef; // [esp+34h] [ebp-8h]
    const cg_s *cgameGlob;

    iassert((animIndex > WEAP_ANIM_VIEWMODEL_START) && (animIndex < WEAP_ANIM_VIEWMODEL_END));

    anims = XAnimGetAnims(cg_weaponsArray[0][weaponNum].tree);
    iassert(anims);
    weapDef = BG_GetWeaponDef(weaponNum);
    rate = GetWeaponAnimRate(weapDef, anims, animIndex);
    
#ifdef KISAK_MP
    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    if ((cgameGlob->predictedPlayerState.weaponstate == 7
        || cgameGlob->predictedPlayerState.weaponstate == 9
        || cgameGlob->predictedPlayerState.weaponstate == 11
        || cgameGlob->predictedPlayerState.weaponstate == 10
        || cgameGlob->predictedPlayerState.weaponstate == 8)
        && (cgameGlob->predictedPlayerState.perks & 4) != 0)
    {
        if (perk_weapReloadMultiplier->current.value == 0.0)
            rate = 1000.0;
        else
            rate = rate / perk_weapReloadMultiplier->current.value;
    }
    else if (cgameGlob->predictedPlayerState.weaponstate == 6 && (cgameGlob->predictedPlayerState.perks & 8) != 0)
    {
        if (perk_weapRateMultiplier->current.value == 0.0)
            rate = 1000.0;
        else
            rate = rate / perk_weapRateMultiplier->current.value;
    }
#endif

    for (int i = 1; i < 31; ++i)
    {
        if (animIndex == i)
            XAnimSetGoalWeight(obj, i, 1.0, transitionTime, rate, 0, 1u, 1);
        else
            XAnimSetGoalWeight(obj, i, 0.0, transitionTime, 1.0, 0, 0, 0);
    }
}

double __cdecl GetWeaponAnimRate(WeaponDef *weapDef, XAnim_s *anims, uint32_t animIndex)
{
    int32_t offset; // [esp+8h] [ebp-8h]
    int32_t time; // [esp+Ch] [ebp-4h]

    iassert(weapDef);
    iassert(anims);
    bcassert(animIndex, NUM_WEAP_ANIMS);
    offset = g_animRateOffsets[animIndex];
    if (offset < 0)
        return 1.0;
    time = *(int32_t *)((char *)&weapDef->szInternalName + offset);
    iassert(time >= 0);
    if (!time)
        return 0.0;
    return (float)((double)XAnimGetLengthMsec(anims, animIndex) / (double)time);
}

void __cdecl PlayADSAnim(float weaponPosFrac, int32_t weaponNum, DObj_s *obj, int32_t animIndex)
{
    XAnimTree_s *Tree; // eax
    XAnimTree_s *v5; // eax
    float time; // [esp+18h] [ebp-4h]

    iassert((animIndex == WEAP_ANIM_ADS_UP) || (animIndex == WEAP_ANIM_ADS_DOWN));
    if (animIndex == 31)
    {
        XAnimSetGoalWeight(obj, 0x1Fu, 1.0f, 0.1f, 0.0f, 0, 1u, 0);
        XAnimSetGoalWeight(obj, 0x20u, 0.0f, 0.1f, 0.0f, 0, 0, 0);
    }
    else
    {
        XAnimSetGoalWeight(obj, 0x1Fu, 0.0f, 0.1f, 0.0f, 0, 0, 0);
        XAnimSetGoalWeight(obj, 0x20u, 1.0f, 0.1f, 0.0f, 0, 1u, 0);
    }
    Tree = DObjGetTree(obj);
    XAnimSetTime(Tree, 0x1Fu, weaponPosFrac);
    time = 1.0 - weaponPosFrac;
    v5 = DObjGetTree(obj);
    XAnimSetTime(v5, 0x20u, time);
}

void __cdecl ResetWeaponAnimTrees(int32_t localClientNum, const playerState_s *ps)
{
    DObj_s *obj; // [esp+1Ch] [ebp-Ch]
    XAnimTree_s *animTree; // [esp+20h] [ebp-8h]
    uint32_t weapIndex; // [esp+24h] [ebp-4h]

    for (weapIndex = 1; weapIndex < BG_GetNumWeapons(); ++weapIndex)
    {
        iassert(localClientNum == 0);
        obj = cg_weaponsArray[0][weapIndex].viewModelDObj;
        if (obj)
        {
            animTree = DObjGetTree(obj);
            iassert(animTree);
            XAnimClearTreeGoalWeights(animTree, 0, 0.0f);
            XAnimSetGoalWeight(obj, 0, 1.0f, 0.0f, 1.0f, 0, 1u, 0);
            if (ps->ammoclip[BG_ClipForWeapon(weapIndex)])
                StartWeaponAnim(localClientNum, weapIndex, obj, 1, 0.0f);
            else
                StartWeaponAnim(localClientNum, weapIndex, obj, 2, 0.0f);
        }
    }
}

#ifdef KISAK_SP
bool __cdecl CG_NVGViewModelShouldBeAttached(int32_t localClientNum)
{
    cg_s *cgameGlob;
    playerState_s *ps;
    WeaponDef *weapDef;
    int timeLeft;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    ps = &cgameGlob->predictedPlayerState;
    // BG_GetViewmodelWeaponIndex resolves offHandIndex vs weapon via (weapFlags & 2),
    // matching the inline logic in CoD3SP.exe CG_NVGViewModelShouldBeAttached.
    weapDef = BG_GetWeaponDef(BG_GetViewmodelWeaponIndex(ps));

    if (ps->weaponstate == WEAPON_NIGHTVISION_WEAR)
    {
        timeLeft = weapDef->nightVisionWearTime - ps->weaponTime;
        if (timeLeft > 100 && timeLeft <= weapDef->nightVisionWearTimeFadeOutEnd)
            return true;
    }
    else if (ps->weaponstate == WEAPON_NIGHTVISION_REMOVE
        && weapDef->nightVisionRemoveTime - ps->weaponTime >= weapDef->nightVisionRemoveTimePowerDown)
    {
        return true;
    }
    return false;
}
#endif

char __cdecl UpdateViewmodelAttachments(
    int32_t localClientNum,
    uint32_t weaponNum,
    uint8_t weaponModel,
    weaponInfo_s *weapInfo)
{
    XModel *newKnife; // [esp+0h] [ebp-14h]
    WeaponDef *weapDef; // [esp+8h] [ebp-Ch]
    XModel *newRocket; // [esp+10h] [ebp-4h]
	XModel *newGoggles;
    weaponInfo_s *weapInfoa; // [esp+28h] [ebp+14h]

    iassert(weapInfo);
    iassert(localClientNum == 0);
    weapInfoa = &cg_weaponsArray[0][weaponNum];
    newRocket = 0;
    newKnife = 0;
	newGoggles = 0;
    weapDef = BG_GetWeaponDef(weaponNum);
    VR_JavelinDiagnostic("attachments: enter", weaponNum, weapInfoa->viewModelDObj);

#ifdef KISAK_SP
    if (CG_NVGViewModelShouldBeAttached(localClientNum))
    {
        if (overrideNVGModelWithKnife->current.enabled)
            newGoggles = weapDef->knifeModel;
        else
            newGoggles = cgMedia.nightVisionGoggles;
    }
#endif

    if (ViewmodelRocketShouldBeAttached(localClientNum, weapDef))
        newRocket = weapDef->rocketModel;
    if (ViewmodelKnifeShouldBeAttached(localClientNum, weapDef))
        newKnife = weapDef->knifeModel;
    if (newGoggles == weapInfoa->gogglesModel && newRocket == weapInfoa->rocketModel && newKnife == weapInfoa->knifeModel)
    {
        VR_JavelinDiagnostic("attachments: unchanged", weaponNum, weapInfoa->viewModelDObj);
        return 0;
    }
    VR_JavelinDiagnostic("attachments: before DObj change", weaponNum, weapInfoa->viewModelDObj);
    ChangeViewmodelDobj(localClientNum, weaponNum, weaponModel, weapInfoa->handModel, newGoggles, newRocket, newKnife, 0);
    VR_JavelinDiagnostic("attachments: DObj change complete", weaponNum, weapInfoa->viewModelDObj);
    return 1;
}

bool __cdecl ViewmodelRocketShouldBeAttached(int32_t localClientNum, WeaponDef* weapDef)
{
    cg_s *cgameGlob;

    iassert(weapDef);

    if (!weapDef->rocketModel)
        return 0;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    if (cgameGlob->predictedPlayerState.ammoclip[BG_ClipForWeapon(cgameGlob->predictedPlayerState.weapon)])
        return 1;

    return (cgameGlob->predictedPlayerState.weaponstate == 7
        || cgameGlob->predictedPlayerState.weaponstate == 9
        || cgameGlob->predictedPlayerState.weaponstate == 11
        || cgameGlob->predictedPlayerState.weaponstate == 10
        || cgameGlob->predictedPlayerState.weaponstate == 8)
        && weapDef->iReloadTime - cgameGlob->predictedPlayerState.weaponTime > weapDef->reloadShowRocketTime;
}

bool __cdecl ViewmodelKnifeShouldBeAttached(int32_t localClientNum, WeaponDef* weapDef)
{
    uint32_t anim; // [esp+4h] [ebp-8h]

    iassert(weapDef);

    if (!weapDef->knifeModel)
        return 0;

    anim = CG_GetLocalClientGlobals(localClientNum)->predictedPlayerState.weapAnim & 0xFFFFFDFF;
    return anim == 8 || anim == 9;
}

void __cdecl ProcessWeaponNoteTracks(int32_t localClientNum, const playerState_s *predictedPlayerState)
{
    uint32_t NumWeapons; // eax
    int32_t noteListSize; // [esp+0h] [ebp-14h]
    XAnimNotify_s *noteList; // [esp+4h] [ebp-10h] BYREF
    int32_t weapIndex; // [esp+8h] [ebp-Ch]
    int32_t i; // [esp+Ch] [ebp-8h]
    WeaponDef *weapDef; // [esp+10h] [ebp-4h]

    weapIndex = BG_GetViewmodelWeaponIndex(predictedPlayerState);
    if (weapIndex)
    {
        bcassert(weapIndex, BG_GetNumWeapons());
        weapDef = BG_GetWeaponDef(weapIndex);
        noteListSize = DObjGetClientNotifyList(&noteList);
        for (i = 0; i < noteListSize; ++i)
        {
            if (I_stricmp(noteList[i].name, "end"))
            {
                if (I_stricmp(noteList[i].name, "NVG_on_powerup"))
                {
                    if (!I_stricmp(noteList[i].name, "NVG_off_powerdown"))
                        CG_PlayClientSoundAlias(localClientNum, cgMedia.nightVisionOff);
                }
                else
                {
                    CG_PlayClientSoundAlias(localClientNum, cgMedia.nightVisionOn);
                }
                PlayNoteMappedSoundAliases(localClientNum, noteList[i].name, weapDef);
            }
        }
    }
}

void __cdecl PlayNoteMappedSoundAliases(int32_t localClientNum, const char *noteName, const WeaponDef *weapDef)
{
    const char *soundName; // [esp+0h] [ebp-Ch]
    int32_t mapIdx; // [esp+4h] [ebp-8h]
    uint32_t noteNameSL; // [esp+8h] [ebp-4h]

    if (weapDef->notetrackSoundMapKeys[0])
    {
        noteNameSL = SL_FindLowercaseString(noteName);
        if (noteNameSL)
        {
            for (mapIdx = 0; mapIdx < 16 && weapDef->notetrackSoundMapKeys[mapIdx]; ++mapIdx)
            {
                if (weapDef->notetrackSoundMapValues[mapIdx] && weapDef->notetrackSoundMapKeys[mapIdx] == noteNameSL)
                {
                    soundName = SL_ConvertToString(weapDef->notetrackSoundMapValues[mapIdx]);
                    if (soundName)
                        CG_PlayClientSoundAliasByName(localClientNum, soundName);
                }
            }
        }
    }
}

#ifdef KISAK_SP
static void CalculateWeaponPosition_BobOffset(cg_s *cgameGlob)
{
    playerState_s *ps = &cgameGlob->predictedPlayerState;
    unsigned int weapIndex = BG_GetViewmodelWeaponIndex(ps);
    WeaponDef *weapDef = BG_GetWeaponDef(weapIndex);

    float speed = cgameGlob->xyspeed * cg_bobWeaponAmplitude->current.value;
    float cycle = cgameGlob->fBobCycle + cg_bobWeaponLag->current.value * 3.1415927f + 6.2831855f;

    cgameGlob->vAngOfs[0] = -CG_GetVerticalBobFactor(ps, cycle, speed, cg_bobWeaponMax->current.value);
    cgameGlob->vAngOfs[1] = -CG_GetHorizontalBobFactor(ps, cycle, speed, cg_bobWeaponMax->current.value);
    float rollBob = CG_GetHorizontalBobFactor(
        ps,
        cycle - 0.47123894f,
        cg_bobWeaponRollAmplitude->current.value * speed,
        cg_bobWeaponMax->current.value);
    cgameGlob->vAngOfs[2] = (rollBob <= 0.0f) ? rollBob : 0.0f;

    if (ps->fWeaponPosFrac != 0.0f)
    {
        float adsScale = 1.0f - (1.0f - weapDef->fAdsBobFactor) * ps->fWeaponPosFrac;
        cgameGlob->vAngOfs[0] = cgameGlob->vAngOfs[0] * adsScale;
        cgameGlob->vAngOfs[1] = cgameGlob->vAngOfs[1] * adsScale;
        cgameGlob->vAngOfs[2] = cgameGlob->vAngOfs[2] * adsScale;
    }
}

static void CalculateWeaponPosition_BasePosition_angles(cg_s *cgameGlob, float *angles)
{
    playerState_s *ps = &cgameGlob->predictedPlayerState;
    unsigned int weapIndex = BG_GetViewmodelWeaponIndex(ps);
    WeaponDef *weapDef = BG_GetWeaponDef(weapIndex);
    int eFlags = cgameGlob->predictedPlayerEntity.nextState.lerp.eFlags;
    float minSpeed;
    float targetAng[3];

    if ((eFlags & 8) != 0)
        minSpeed = weapDef->fProneRotMinSpeed;
    else if ((eFlags & 4) != 0)
        minSpeed = weapDef->fDuckedRotMinSpeed;
    else
        minSpeed = weapDef->fStandRotMinSpeed;
    minSpeed = minSpeed + cg_gun_rot_minspeed->current.value;

    if (cgameGlob->xyspeed <= minSpeed
        || ps->weaponstate == 7
        || ps->weaponstate == 25
        || ps->weaponstate == 26)
    {
        targetAng[0] = 0.0f;
        targetAng[1] = 0.0f;
        targetAng[2] = 0.0f;
    }
    else
    {
        float scale = (cgameGlob->xyspeed - minSpeed) / ((float)ps->speed - minSpeed);
        if (scale < 0.0f)
            scale = 0.0f;
        if (scale > 1.0f)
            scale = 1.0f;
        const float *rot;
        if ((eFlags & 8) != 0)
            rot = weapDef->vProneRot;
        else if ((eFlags & 4) != 0)
            rot = weapDef->vDuckedRot;
        else
            rot = weapDef->vStandRot;
        targetAng[0] = cg_gun_rot_p->current.value * scale + rot[0] * scale;
        targetAng[1] = cg_gun_rot_y->current.value * scale + rot[1] * scale;
        targetAng[2] = cg_gun_rot_r->current.value * scale + rot[2] * scale;
    }
    if (ps->fWeaponPosFrac != 0.0f)
    {
        float invFrac = 1.0f - ps->fWeaponPosFrac;
        targetAng[0] = invFrac * targetAng[0];
        targetAng[1] = invFrac * targetAng[1];
        targetAng[2] = invFrac * targetAng[2];
    }
    for (int i = 0; i < 3; ++i)
    {
        float last = cgameGlob->playerEntity.vLastMoveAng[i];
        if (last == targetAng[i])
            continue;
        float rate;
        if (ps->viewHeightCurrent == 11.0f)
            rate = weapDef->fPosProneRotRate + cg_gun_rot_rate->current.value;
        else
            rate = weapDef->fPosRotRate + cg_gun_rot_rate->current.value;
        float delta = rate * (targetAng[i] - last) * (float)cgameGlob->frametime * 0.001f;
        if (last >= targetAng[i])
        {
            if (delta > (float)cgameGlob->frametime * -0.0001f)
                delta = (float)cgameGlob->frametime * -0.0001f;
            cgameGlob->playerEntity.vLastMoveAng[i] = last + delta;
            if (cgameGlob->playerEntity.vLastMoveAng[i] < targetAng[i])
                cgameGlob->playerEntity.vLastMoveAng[i] = targetAng[i];
        }
        else
        {
            if (delta < (float)cgameGlob->frametime * 0.0001f)
                delta = (float)cgameGlob->frametime * 0.0001f;
            cgameGlob->playerEntity.vLastMoveAng[i] = last + delta;
            if (cgameGlob->playerEntity.vLastMoveAng[i] > targetAng[i])
                cgameGlob->playerEntity.vLastMoveAng[i] = targetAng[i];
        }
    }
    if (ps->fWeaponPosFrac == 0.0f)
    {
        Vec3Add(angles, cgameGlob->playerEntity.vLastMoveAng, angles);
    }
    else if (ps->fWeaponPosFrac < 0.5f)
    {
        float blend = 1.0f - (ps->fWeaponPosFrac * 2.0f);
        Vec3Mad(angles, blend, cgameGlob->playerEntity.vLastMoveAng, angles);
    }
}

static void CalculateWeaponPosition_BaseAngles(cg_s *cgameGlob, float *angles)
{
    unsigned int weapIndex = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    WeaponDef *weapDef = BG_GetWeaponDef(weapIndex);
    float vAng[3] = { 0.0f, 0.0f, 0.0f };

    if (BG_IsAimDownSightWeapon(weapIndex))
    {
        float frac = cgameGlob->predictedPlayerState.fWeaponPosFrac;
        if (frac == 1.0f)
            cgameGlob->playerEntity.bPositionToADS = 0;
        else if (frac == 0.0f)
            cgameGlob->playerEntity.bPositionToADS = 1;
        cgameGlob->playerEntity.fLastWeaponPosFrac = frac;
        vAng[0] = weapDef->fAdsAimPitch * frac;
    }
    CalculateWeaponPosition_BasePosition_angles(cgameGlob, vAng);
    Vec3Copy(vAng, cgameGlob->playerEntity.vPositionLastAng);
    Vec3Add(angles, vAng, angles);
}

static void CalculateWeaponPosition_IdleAngles(cg_s *cgameGlob, float *angles)
{
    playerEntity_t *pe = &cgameGlob->playerEntity;
    unsigned int weapIndex = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    WeaponDef *weapDef = BG_GetWeaponDef(weapIndex);
    float fIdleAmount;
    float fIdleSpeed;

    if (BG_IsAimDownSightWeapon(weapIndex))
    {
        fIdleAmount = (weapDef->fAdsIdleAmount - weapDef->fHipIdleAmount)
            * cgameGlob->predictedPlayerState.fWeaponPosFrac
            + weapDef->fHipIdleAmount;
        fIdleSpeed = ((float)weapDef->adsIdleSpeed - (float)weapDef->hipIdleSpeed)
            * cgameGlob->predictedPlayerState.fWeaponPosFrac
            + (float)weapDef->hipIdleSpeed;
    }
    else if (weapDef->fHipIdleAmount == 0.0f)
    {
        fIdleSpeed = 1.0f;
        fIdleAmount = 80.0f;
    }
    else
    {
        fIdleSpeed = weapDef->hipIdleSpeed;
        fIdleAmount = weapDef->fHipIdleAmount;
    }

    int eFlags = cgameGlob->predictedPlayerEntity.nextState.lerp.eFlags;
    float fIdleFactor;
    if ((eFlags & 8) != 0)
        fIdleFactor = weapDef->fIdleProneFactor;
    else if ((eFlags & 4) != 0)
        fIdleFactor = weapDef->fIdleCrouchFactor;
    else
        fIdleFactor = 1.0f;

    if ((!weapDef->overlayReticle || cgameGlob->predictedPlayerState.fWeaponPosFrac == 0.0f)
        && fIdleFactor != pe->fLastIdleFactor)
    {
        if (fIdleFactor <= pe->fLastIdleFactor)
        {
            pe->fLastIdleFactor = pe->fLastIdleFactor - (float)cgameGlob->frametime * 0.00050000002f;
            if (pe->fLastIdleFactor < fIdleFactor)
                pe->fLastIdleFactor = fIdleFactor;
        }
        else
        {
            pe->fLastIdleFactor = pe->fLastIdleFactor + (float)cgameGlob->frametime * 0.00050000002f;
            if (pe->fLastIdleFactor > fIdleFactor)
                pe->fLastIdleFactor = fIdleFactor;
        }
    }

    float amount = pe->fLastIdleFactor * fIdleAmount;
    if (weapDef->overlayReticle)
        amount = (1.0f - cgameGlob->predictedPlayerState.fWeaponPosFrac) * amount;

    cgameGlob->weapIdleTime += (int)((float)cgameGlob->frametime * fIdleSpeed);
    angles[2] = (float)sin((float)cgameGlob->weapIdleTime * 0.00050000002f) * amount * 0.0099999998f + angles[2];
    angles[1] = (float)sin((float)cgameGlob->weapIdleTime * 0.00069999998f) * amount * 0.0099999998f + angles[1];
    angles[0] = (float)sin((float)cgameGlob->weapIdleTime * 0.001f) * amount * 0.0099999998f + angles[0];
}

static void CalculateWeaponPosition_BobAngles(const cg_s *cgameGlob, float *angles)
{
    unsigned int weapIndex = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    if (BG_GetWeaponDef(weapIndex)->overlayReticle)
    {
        Vec3Mad(angles, 1.0f - cgameGlob->predictedPlayerState.fWeaponPosFrac, cgameGlob->vAngOfs, angles);
    }
    else
    {
        Vec3Add(cgameGlob->vAngOfs, angles, angles);
    }
}

static void CalculateWeaponPosition_GunRecoil(cg_s *cgameGlob, float *angles)
{
    unsigned int weapIndex = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    WeaponDef *weapDef = BG_GetWeaponDef(weapIndex);
    float frac = cgameGlob->predictedPlayerState.fWeaponPosFrac;

    if (!BG_IsAimDownSightWeapon(weapIndex))
        return;

    float fGunKickAccel = (weapDef->fAdsGunKickAccel - weapDef->fHipGunKickAccel) * frac + weapDef->fHipGunKickAccel;
    float fGunKickSpeedMax = (weapDef->fAdsGunKickSpeedMax - weapDef->fHipGunKickSpeedMax) * frac + weapDef->fHipGunKickSpeedMax;
    float fGunKickSpeedDecay = (weapDef->fAdsGunKickSpeedDecay - weapDef->fHipGunKickSpeedDecay) * frac + weapDef->fHipGunKickSpeedDecay;
    float fGunKickStaticDecay = (weapDef->fAdsGunKickStaticDecay - weapDef->fHipGunKickStaticDecay) * frac + weapDef->fHipGunKickStaticDecay;

    for (int iTimeRemaining = cgameGlob->frametime; iTimeRemaining > 0; iTimeRemaining -= 5)
    {
        int iTimeStep = (iTimeRemaining <= 5) ? iTimeRemaining : 5;
        float fTimeStep = (float)iTimeStep * 0.001f;
        int bPitchDone = BG_CalculateWeaponPosition_GunRecoil_SingleAngle(
            &cgameGlob->vGunOffset[0],
            &cgameGlob->vGunSpeed[0],
            fTimeStep,
            weapDef->fGunMaxPitch,
            fGunKickAccel,
            fGunKickSpeedMax,
            fGunKickSpeedDecay,
            fGunKickStaticDecay);
        if (BG_CalculateWeaponPosition_GunRecoil_SingleAngle(
                &cgameGlob->vGunOffset[1],
                &cgameGlob->vGunSpeed[1],
                fTimeStep,
                weapDef->fGunMaxYaw,
                fGunKickAccel,
                fGunKickSpeedMax,
                fGunKickSpeedDecay,
                fGunKickStaticDecay)
            && bPitchDone)
        {
            break;
        }
    }
    angles[0] = cgameGlob->vGunOffset[0] + angles[0];
    angles[1] = cgameGlob->vGunOffset[1] + angles[1];
    angles[2] = cgameGlob->vGunOffset[2] + angles[2];
}

static void CalculateWeaponPosition_SwayAngles(const cg_s *cgameGlob, float *angles)
{
    angles[0] = AngleDelta(angles[0], cgameGlob->swayAngles[0]);
    angles[1] = AngleDelta(angles[1], cgameGlob->swayAngles[1]);
}

static void CalculateWeaponPosition_SaveOffsetAngles(cg_s *cgameGlob, float (*axis)[3])
{
    unsigned int weapIndex = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    float fZoom;
    float outAngles[3];

    AxisToAngles((const mat3x3&)*axis, outAngles);
    if (!BG_IsAimDownSightWeapon(weapIndex)
        || cgameGlob->predictedPlayerState.fWeaponPosFrac == 0.0f
        || CG_GetWeapReticleZoom(cgameGlob, &fZoom))
    {
        cgameGlob->gunPitch = cgameGlob->refdefViewAngles[0];
        cgameGlob->gunYaw = cgameGlob->refdefViewAngles[1];
    }
    else
    {
        cgameGlob->gunPitch = AngleNormalize360(outAngles[0]);
        cgameGlob->gunYaw = AngleNormalize360(outAngles[1]);
    }
}

static void CalculateWeaponAxis(cg_s *cgameGlob, float (*axis)[3])
{
    unsigned int weapIndex = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    WeaponDef *weapDef = BG_GetWeaponDef(weapIndex);
    float angles[3];
    float localAxis[3][3];

    Vec3Clear(angles);

    if (cgameGlob->predictedPlayerState.leanf != 0.0f)
        angles[2] -= GetLeanFraction(cgameGlob->predictedPlayerState.leanf) * 2.0f;

    CalculateWeaponPosition_BaseAngles(cgameGlob, angles);
    CalculateWeaponPosition_IdleAngles(cgameGlob, angles);
    CalculateWeaponPosition_BobAngles(cgameGlob, angles);

    if (!BG_IsAimDownSightWeapon(weapIndex))
    {
        angles[0] = angles[0] - cgameGlob->kickAngles[0];
        angles[1] = angles[1] - cgameGlob->kickAngles[1];
        angles[2] = angles[2] - cgameGlob->kickAngles[2];
    }

    if (cgameGlob->damageTime)
    {
        float kickScale = (cgameGlob->predictedPlayerState.fWeaponPosFrac + 1.0f) * 0.5f;
        float kickAmount = kickScale;
        if (cgameGlob->predictedPlayerState.fWeaponPosFrac != 0.0f && weapDef->overlayReticle)
            kickAmount = -(cgameGlob->predictedPlayerState.fWeaponPosFrac * 0.75f - 1.0f) * kickScale;
        float delta = (float)(cgameGlob->time - cgameGlob->damageTime);
        float riseTime = kickScale * 100.0f;
        float v11;
        if (delta < riseTime)
        {
            v11 = GetLeanFraction(delta / riseTime) * kickAmount;
            goto applyDamageKick;
        }
        if (1.0f - (delta - riseTime) / (kickScale * 400.0f) > 0.0f)
        {
            v11 = (1.0f - GetLeanFraction((delta - riseTime) / (kickScale * 400.0f))) * kickAmount;
        applyDamageKick:
            float roll = cgameGlob->v_dmg_roll * v11;
            angles[0] = cgameGlob->v_dmg_pitch * v11 * 0.5f + angles[0];
            angles[1] = angles[1] - roll;
            angles[2] = roll * 0.5f + angles[2];
        }
    }

    CalculateWeaponPosition_GunRecoil(cgameGlob, angles);
    CalculateWeaponPosition_SwayAngles(cgameGlob, angles);
    AnglesToAxis(angles, localAxis);
    MatrixMultiply((const mat3x3&)*localAxis, (const mat3x3&)*cgameGlob->viewModelAxis, (mat3x3&)*axis);
    CalculateWeaponPosition_SaveOffsetAngles(cgameGlob, axis);
}
#endif // KISAK_SP


// KISAKTODO: would like to have this function more like blops, it's cleaner
void __cdecl CG_AddViewWeapon(int32_t localClientNum)
{
    double v1; // st7
    double v2; // st7
    int32_t v3; // [esp+Ch] [ebp-12Ch]
    float* vGunSpeed; // [esp+10h] [ebp-128h]
    float* vGunOffset; // [esp+14h] [ebp-124h]
    float* vLastMoveAng; // [esp+18h] [ebp-120h]
    playerEntity_t* pe; // [esp+2Ch] [ebp-10Ch]
    weaponState_t ws; // [esp+30h] [ebp-108h] BYREF
    cg_s* cgameGlob; // [esp+88h] [ebp-B0h]
    int32_t drawgun; // [esp+8Ch] [ebp-ACh]
    float vAxis2[3][3]; // [esp+90h] [ebp-A8h] BYREF
    float angles[3]; // [esp+B4h] [ebp-84h] BYREF
    GfxScaledPlacement placement; // [esp+C0h] [ebp-78h] BYREF
    int32_t weaponIndex; // [esp+E0h] [ebp-58h]
    float fZoom; // [esp+E4h] [ebp-54h] BYREF
    playerState_s* ps; // [esp+E8h] [ebp-50h]
    float axis[3][3]; // [esp+ECh] [ebp-4Ch] BYREF
    WeaponDef* weapDef; // [esp+110h] [ebp-28h]
    float vAxis[3][3]; // [esp+114h] [ebp-24h] BYREF

    drawgun = 1;
    iassert(localClientNum == 0);
    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    ps = &cgameGlob->predictedPlayerState;
    cgameGlob->refdef.dof.viewModelStart = 0.0f;
    cgameGlob->refdef.dof.viewModelEnd = 0.0f;
#ifdef KISAK_MP
    if (ps->pm_type != PM_SPECTATOR && ps->pm_type != PM_INTERMISSION && !cgameGlob->renderingThirdPerson)
#elif KISAK_SP
    if (ps->pm_type != PM_UFO && ps->pm_type != PM_NOCLIP && ps->pm_type != PM_DEAD)
#endif
    {
        bool hideGunForReticleZoom =
            CG_GetWeapReticleZoom(
                cgameGlob,
                &fZoom);

#ifdef KISAK_SP
        if (hideGunForReticleZoom &&
            VR_IsPhysicalSniperScopeAimActive())
        {
            hideGunForReticleZoom = false;

            static bool loggedVrScopedRifleVisible = false;

            if (!loggedVrScopedRifleVisible)
            {
                Com_Printf(
                    0,
                    "[VR] Keeping the rendered sniper rifle visible "
                    "behind the physical scope.\n");

                loggedVrScopedRifleVisible = true;
            }
        }
#endif

        if (cgameGlob->cubemapShot ||
            !cg_drawGun->current.enabled ||
            hideGunForReticleZoom)
        {
            drawgun = 0;
        }
#ifdef KISAK_SP
        if (cgameGlob->hideViewModel)
            drawgun = 0;
        if ((ps->eFlags & 0x300) == 0 && (ps->eFlags & 0x20000) == 0)
#else
        if ((ps->eFlags & 0x300) == 0)
#endif
        {
            weaponIndex = BG_GetViewmodelWeaponIndex(ps);
            if (weaponIndex <= 0)
            {
                cgameGlob->gunPitch = cgameGlob->refdefViewAngles[0];
                cgameGlob->gunYaw = cgameGlob->refdefViewAngles[1];
                cgameGlob->gunXOfs = 0.0f;
                cgameGlob->gunYOfs = 0.0f;
                cgameGlob->gunZOfs = 0.0f;
            }
            else
            {
#ifdef KISAK_SP
                CalculateWeaponPosition_BobOffset(cgameGlob);
                CalculateWeaponPosition_Sway(cgameGlob);
                CalculateWeaponPosition(cgameGlob, placement.base.origin);
                Vec3Mad(placement.base.origin, cg_gun_x->current.value, cgameGlob->viewModelAxis[0], placement.base.origin);
                Vec3Mad(placement.base.origin, cg_gun_y->current.value, cgameGlob->viewModelAxis[1], placement.base.origin);
                Vec3Mad(placement.base.origin, cg_gun_z->current.value, cgameGlob->viewModelAxis[2], placement.base.origin);
                CalculateWeaponAxis(cgameGlob, axis);
                AxisToQuat(axis, placement.base.quat);

                float vrAbsoluteWeaponAxis[3][3] = {};

                memcpy(
                    vrAbsoluteWeaponAxis,
                    cgameGlob->refdef.viewaxis,
                    sizeof(vrAbsoluteWeaponAxis));

                if (VR_ApplyRightControllerToWeaponPlacement(
                        cgameGlob->refdef.vieworg,
                        cgameGlob->refdef.viewaxis,
                        placement.base.origin,
                        vrAbsoluteWeaponAxis))
                {
                    memcpy(
                        axis,
                        vrAbsoluteWeaponAxis,
                        sizeof(axis));

                    AxisToQuat(
                        axis,
                        placement.base.quat);

                    static bool
                        loggedSpTrackedWeaponPlacement =
                            false;

                    if (!loggedSpTrackedWeaponPlacement)
                    {
                        Com_Printf(
                            0,
                            "[VR] Applied tracked weapon placement "
                            "to the single-player viewmodel.\n");

                        Com_Printf(
                            0,
                            "[VR] SP uses the HMD-updated "
                            "canonical weapon basis.\n");

                        loggedSpTrackedWeaponPlacement =
                            true;
                    }
                }

                placement.scale = 1.0f;
                CG_AddPlayerWeapon(localClientNum, &placement, ps, &cgameGlob->predictedPlayerEntity, drawgun);
#else
                CalculateWeaponPosition_Sway(cgameGlob);
                CalculateWeaponPostion_PositionToADS(cgameGlob, ps);
                CalculateWeaponPosition(cgameGlob, placement.base.origin);
                Vec3Mad(placement.base.origin, cg_gun_x->current.value, cgameGlob->viewModelAxis[0], placement.base.origin);
                Vec3Mad(placement.base.origin, cg_gun_y->current.value, cgameGlob->viewModelAxis[1], placement.base.origin);
                Vec3Mad(placement.base.origin, cg_gun_z->current.value, cgameGlob->viewModelAxis[2], placement.base.origin);

                pe = &cgameGlob->playerEntity;
                ws.ps = ps;
                ws.xyspeed = cgameGlob->xyspeed;
                ws.frametime = (float)cgameGlob->frametime * EQUAL_EPSILON;
                ws.vLastMoveAng[0] = cgameGlob->playerEntity.vLastMoveAng[0];
                ws.vLastMoveAng[1] = cgameGlob->playerEntity.vLastMoveAng[1];
                ws.vLastMoveAng[2] = cgameGlob->playerEntity.vLastMoveAng[2];
                ws.fLastIdleFactor = cgameGlob->playerEntity.fLastIdleFactor;

#ifdef KISAK_MP
                ws.time = cgameGlob->time - ps->deltaTime;

                if (cgameGlob->damageTime)
                    v3 = cgameGlob->damageTime - ps->deltaTime;
                else
                    v3 = 0;
                ws.damageTime = v3;
#endif

                ws.v_dmg_pitch = cgameGlob->v_dmg_pitch;
                ws.v_dmg_roll = cgameGlob->v_dmg_roll;
                ws.vGunOffset[0] = cgameGlob->vGunOffset[0];
                ws.vGunOffset[1] = cgameGlob->vGunOffset[1];
                ws.vGunOffset[2] = cgameGlob->vGunOffset[2];
                ws.vGunSpeed[0] = cgameGlob->vGunSpeed[0];
                ws.vGunSpeed[1] = cgameGlob->vGunSpeed[1];
                ws.vGunSpeed[2] = cgameGlob->vGunSpeed[2];
                ws.swayAngles[0] = cgameGlob->swayAngles[0];
                ws.swayAngles[1] = cgameGlob->swayAngles[1];
                ws.swayAngles[2] = cgameGlob->swayAngles[2];
                ws.weapIdleTime = &cgameGlob->weapIdleTime;
                BG_CalculateWeaponAngles(&ws, angles);
                AnglesToAxis(angles, vAxis);
                AnglesToAxis(ps->viewangles, vAxis2);
                MatrixMultiply(vAxis, vAxis2, axis);

                AxisToQuat(axis, placement.base.quat);

                if (VR_ApplyRightControllerToWeaponPlacement(
                        cgameGlob->refdef.vieworg,
                        cgameGlob->refdef.viewaxis,
                        placement.base.origin,
                        axis))
                {
                    AxisToQuat(
                        axis,
                        placement.base.quat);
                }

                weapDef = BG_GetWeaponDef(weaponIndex);
                if (!BG_IsAimDownSightWeapon(ps->weapon) || ps->fWeaponPosFrac == 0.0 || weapDef->overlayReticle)
                {
                    cgameGlob->gunPitch = cgameGlob->refdefViewAngles[0];
                    cgameGlob->gunYaw = cgameGlob->refdefViewAngles[1];
                }
                else
                {
                    UnitQuatToAngles(placement.base.quat, angles);
                    cgameGlob->gunPitch = AngleNormalize360(angles[0]);
                    cgameGlob->gunYaw = AngleNormalize360(angles[1]);
                }
                vLastMoveAng = cgameGlob->playerEntity.vLastMoveAng;
                cgameGlob->playerEntity.vLastMoveAng[0] = ws.vLastMoveAng[0];
                vLastMoveAng[1] = ws.vLastMoveAng[1];
                vLastMoveAng[2] = ws.vLastMoveAng[2];
                pe->fLastIdleFactor = ws.fLastIdleFactor;
                vGunOffset = cgameGlob->vGunOffset;
                cgameGlob->vGunOffset[0] = ws.vGunOffset[0];
                vGunOffset[1] = ws.vGunOffset[1];
                vGunOffset[2] = ws.vGunOffset[2];
                vGunSpeed = cgameGlob->vGunSpeed;
                cgameGlob->vGunSpeed[0] = ws.vGunSpeed[0];
                vGunSpeed[1] = ws.vGunSpeed[1];
                vGunSpeed[2] = ws.vGunSpeed[2];

                placement.scale = 1.0f;
                CG_AddPlayerWeapon(localClientNum, &placement, ps, &cgameGlob->predictedPlayerEntity, drawgun);
#endif
            }
        }
    }
}

void __cdecl CalculateWeaponPosition_Sway(cg_s *cgameGlob)
{
    float ssScale; // [esp+8h] [ebp-14h]
    float ssScalea; // [esp+8h] [ebp-14h]
    int32_t ssDT; // [esp+Ch] [ebp-10h]
    int32_t weapIndex; // [esp+10h] [ebp-Ch]
    WeaponDef *weapDef; // [esp+14h] [ebp-8h]
    float ssSwayScale; // [esp+18h] [ebp-4h]

    weapIndex = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    weapDef = BG_GetWeaponDef(weapIndex);
    ssDT = cgameGlob->shellshock.duration + cgameGlob->shellshock.startTime - cgameGlob->time;
    if (ssDT <= 0)
    {
        ssSwayScale = 1.0f;
    }
    else
    {
        ssScale = 1.0f;
        iassert(cgameGlob->shellshock.parms);

        if (ssDT < cgameGlob->shellshock.parms->view.fadeTime)
            ssScale = (float)ssDT / (float)cgameGlob->shellshock.parms->view.fadeTime;

        ssScalea = (3.0f - ssScale * 2.0f) * ssScale * ssScale;
        ssSwayScale = (weapDef->swayShellShockScale - 1.0f) * ssScalea + 1.0f;
    }
    BG_CalculateWeaponPosition_Sway(
        &cgameGlob->predictedPlayerState,
        cgameGlob->swayViewAngles,
        cgameGlob->swayOffset,
        cgameGlob->swayAngles,
        ssSwayScale,
        cgameGlob->frametime);
}

void __cdecl CalculateWeaponPosition(cg_s *cgameGlob, float *origin)
{
    int32_t delta; // [esp+10h] [ebp-24h]
    float fLean; // [esp+14h] [ebp-20h]
    float right[3]; // [esp+18h] [ebp-1Ch] BYREF
    float fDist; // [esp+24h] [ebp-10h]
    float tempAngles[3]; // [esp+28h] [ebp-Ch] BYREF

    Vec3Clear(origin);

    if (cgameGlob->predictedPlayerState.leanf != 0.0f && cgameGlob->predictedPlayerState.fWeaponPosFrac < 1.0f)
    {
        Vec3Clear(tempAngles);
        fLean = GetLeanFraction(cgameGlob->predictedPlayerState.leanf);
        tempAngles[2] = 0.0f - (fLean + fLean);
        fDist = (1.0f - cgameGlob->predictedPlayerState.fWeaponPosFrac) * fLean * 1.6f;
        AngleVectors(tempAngles, 0, right, 0);
        Vec3Mad(origin, fDist, right, origin);
    }
    CalculateWeaponPosition_BasePosition(cgameGlob, origin);
#ifdef KISAK_SP
    {
        float vOfs[3];
        float bobAxis[3][3];

        Vec3Copy(origin, vOfs);
        AnglesToAxis(cgameGlob->vAngOfs, bobAxis);
        MatrixTransformVector(vOfs, (const mat3x3&)*bobAxis, origin);
    }
#endif
    CalculateWeaponPosition_SwayMovement(cgameGlob, origin);
    CalculateWeaponPosition_ToWorldPosition(cgameGlob, origin);
    delta = cgameGlob->time - cgameGlob->landTime;
    if (delta >= 150)
    {
        if (delta < 450)
            origin[2] = cgameGlob->landChange * 0.25f * (float)(450 - delta) / 300.0f + origin[2];
    }
    else
    {
        origin[2] = cgameGlob->landChange * 0.25f * (float)delta / 150.0f + origin[2];
    }
    CalculateWeaponPosition_SaveOffsetMovement(cgameGlob, origin);
}

void __cdecl CalculateWeaponPosition_SwayMovement(const cg_s *cgameGlob, float *origin)
{
    origin[1] = origin[1] - cgameGlob->swayOffset[1];
    origin[2] = origin[2] + cgameGlob->swayOffset[2];
}

void __cdecl CalculateWeaponPosition_BasePosition(cg_s *cgameGlob, float *origin)
{
    float vGunOfs[3] = { 0.0f, 0.0f, 0.0f };

    CalculateWeaponPosition_BasePosition_movement(cgameGlob, vGunOfs);
    cgameGlob->playerEntity.vPositionLastOrg[0] = vGunOfs[0];
    cgameGlob->playerEntity.vPositionLastOrg[1] = vGunOfs[1];
    cgameGlob->playerEntity.vPositionLastOrg[2] = vGunOfs[2];
    Vec3Add(origin, vGunOfs, origin);
}

void __cdecl CalculateWeaponPosition_BasePosition_movement(cg_s *cgameGlob, float *origin)
{
    double v2; // st7
    double v3; // st6
    float scale; // [esp+Ch] [ebp-4Ch]
    float v5; // [esp+10h] [ebp-48h]
    bool v6; // [esp+14h] [ebp-44h]
    float lerp; // [esp+1Ch] [ebp-3Ch]
    float lerpa; // [esp+1Ch] [ebp-3Ch]
    playerEntity_t *pe; // [esp+20h] [ebp-38h]
    float delta; // [esp+24h] [ebp-34h]
    float deltaa; // [esp+24h] [ebp-34h]
    float frac; // [esp+28h] [ebp-30h]
    bool crouched; // [esp+33h] [ebp-25h]
    bool prone; // [esp+35h] [ebp-23h]
    bool moving; // [esp+37h] [ebp-21h]
    float targetPos[3]; // [esp+38h] [ebp-20h] BYREF
    int32_t weapIndex; // [esp+44h] [ebp-14h]
    float minSpeed; // [esp+48h] [ebp-10h]
    int32_t i; // [esp+4Ch] [ebp-Ch]
    playerState_s *ps; // [esp+50h] [ebp-8h]
    WeaponDef *weapDef; // [esp+54h] [ebp-4h]

    ps = &cgameGlob->predictedPlayerState;
    pe = &cgameGlob->playerEntity;
    weapIndex = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    weapDef = BG_GetWeaponDef(weapIndex);
    if ((cgameGlob->predictedPlayerEntity.nextState.lerp.eFlags & 8) != 0)
    {
        minSpeed = weapDef->fProneMoveMinSpeed + cg_gun_move_minspeed->current.value;
    }
    else
    {
        if ((cgameGlob->predictedPlayerEntity.nextState.lerp.eFlags & 4) != 0)
            v2 = weapDef->fDuckedMoveMinSpeed + cg_gun_move_minspeed->current.value;
        else
            v2 = weapDef->fStandMoveMinSpeed + cg_gun_move_minspeed->current.value;
        minSpeed = v2;
    }
    moving = minSpeed < (float)cgameGlob->xyspeed;
    v6 = ps->weaponstate == 25 || ps->weaponstate == 26;
    prone = ps->viewHeightTarget == 11;
    crouched = ps->viewHeightTarget == 40;
    if (minSpeed >= (float)cgameGlob->xyspeed || ps->weaponstate == 7 || v6)
    {
        targetPos[0] = 0.0f;
        targetPos[1] = 0.0f;
        targetPos[2] = 0.0f;
    }
    else
    {
        frac = (cgameGlob->xyspeed - minSpeed) / ((double)ps->speed - minSpeed);
        iassert(frac > 0);
        v5 = frac - 1.0f;
        if (v5 < 0.0f)
            scale = frac;
        else
            scale = 1.0f;
        if ((cgameGlob->predictedPlayerEntity.nextState.lerp.eFlags & 8) != 0)
        {
            Vec3Scale(weapDef->vProneMove, scale, targetPos);
        }
        else if ((cgameGlob->predictedPlayerEntity.nextState.lerp.eFlags & 4) != 0)
        {
            Vec3Scale(weapDef->vDuckedMove, scale, targetPos);
        }
        else
        {
            Vec3Scale(weapDef->vStandMove, scale, targetPos);
        }
        targetPos[0] = cg_gun_move_f->current.value * scale + targetPos[0];
        targetPos[1] = cg_gun_move_r->current.value * scale + targetPos[1];
        targetPos[2] = cg_gun_move_u->current.value * scale + targetPos[2];
    }
    if ((!moving || !v6 || !prone) && (crouched || prone))
    {
        lerp = 1.0;
        if (v6)
        {
            if (ps->weaponstate == 25)
            {
                lerpa = (float)ps->weaponTime / (float)weapDef->nightVisionWearTime;
            }
            else
            {
                iassert(ps->weaponstate == WEAPON_NIGHTVISION_REMOVE);
                lerpa = (float)ps->weaponTime / (float)weapDef->nightVisionWearTime;
            }
            if (MYLERP_END <= (float)lerpa)
            {
                if (MYLERP_START <= (float)lerpa)
                    lerp = 0.0f;
                else
                    lerp = 1.0f - (lerpa - MYLERP_END) / (MYLERP_START - MYLERP_END);
            }
            else
            {
                lerp = 1.0f;
            }
        }
        if (ps->viewHeightTarget == 40)
        {
            Vec3Mad(targetPos, lerp, weapDef->vDuckedOfs, targetPos);
            targetPos[0] = targetPos[0] + cg_gun_ofs_f->current.value;
            targetPos[1] = targetPos[1] + cg_gun_ofs_r->current.value;
            targetPos[2] = targetPos[2] + cg_gun_ofs_u->current.value;
        }
        else if (ps->viewHeightTarget == 11)
        {
            Vec3Mad(targetPos, lerp, weapDef->vProneOfs, targetPos);
            targetPos[0] = targetPos[0] + cg_gun_ofs_f->current.value;
            targetPos[1] = targetPos[1] + cg_gun_ofs_r->current.value;
            targetPos[2] = targetPos[2] + cg_gun_ofs_u->current.value;
        }
    }
    for (i = 0; i < 3; ++i)
    {
        if (targetPos[i] != pe->vLastMoveOrg[i])
        {
            if (ps->viewHeightCurrent == 11.0)
                v3 = weapDef->fPosProneMoveRate + cg_gun_move_rate->current.value;
            else
                v3 = weapDef->fPosMoveRate + cg_gun_move_rate->current.value;
            delta = (float)cgameGlob->frametime * EQUAL_EPSILON * (targetPos[i] - pe->vLastMoveOrg[i]) * v3;
            if (targetPos[i] <= (float)pe->vLastMoveOrg[i])
            {
                if (delta > (float)cgameGlob->frametime * EQUAL_EPSILON * -0.1000000014901161f)
                    delta = (float)cgameGlob->frametime * EQUAL_EPSILON * -0.1000000014901161f;
                pe->vLastMoveOrg[i] = pe->vLastMoveOrg[i] + delta;
                if (targetPos[i] > (float)pe->vLastMoveOrg[i])
                    pe->vLastMoveOrg[i] = targetPos[i];
            }
            else
            {
                if (delta < (float)cgameGlob->frametime * EQUAL_EPSILON * 0.1000000014901161f)
                    delta = (float)cgameGlob->frametime * EQUAL_EPSILON * 0.1000000014901161f;
                pe->vLastMoveOrg[i] = pe->vLastMoveOrg[i] + delta;
                if (targetPos[i] < (float)pe->vLastMoveOrg[i])
                    pe->vLastMoveOrg[i] = targetPos[i];
            }
        }
    }
    if (ps->fWeaponPosFrac == 0.0f)
    {
        Vec3Add(origin, cgameGlob->playerEntity.vLastMoveOrg, origin);
    }
    else if (ps->fWeaponPosFrac < 0.5f)
    {
        deltaa = 1.0f - (ps->fWeaponPosFrac + ps->fWeaponPosFrac);
        Vec3Mad(origin, deltaa, cgameGlob->playerEntity.vLastMoveOrg, origin);
    }
}

void __cdecl CalculateWeaponPosition_ToWorldPosition(const cg_s *cgameGlob, float *origin)
{
    float vOffset[3]; // [esp+0h] [ebp-Ch] BYREF

    vOffset[0] = *origin;
    vOffset[1] = origin[1];
    vOffset[2] = origin[2];
    MatrixTransformVector43(vOffset, cgameGlob->viewModelAxis, origin);
}

void __cdecl CalculateWeaponPosition_SaveOffsetMovement(cg_s *cgameGlob, float *origin)
{
    float fPosLerp; // [esp+0h] [ebp-8h]
    int32_t weapIndex; // [esp+4h] [ebp-4h]

    weapIndex = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    if (!BG_IsAimDownSightWeapon(weapIndex)
        || (fPosLerp = cgameGlob->predictedPlayerState.fWeaponPosFrac, fPosLerp == 0.0f))
    {
        cgameGlob->gunXOfs = 0.0f;
        cgameGlob->gunYOfs = 0.0f;
        cgameGlob->gunZOfs = 0.0f;
    }
    else
    {
        cgameGlob->gunXOfs = (*origin - cgameGlob->refdef.vieworg[0]) * fPosLerp;
        cgameGlob->gunYOfs = (origin[1] - cgameGlob->refdef.vieworg[1]) * fPosLerp;
        cgameGlob->gunZOfs = (origin[2] - cgameGlob->refdef.vieworg[2]) * fPosLerp;
    }
}

void __cdecl CalculateWeaponPostion_PositionToADS(cg_s *cgameGlob, playerState_s *ps)
{
    int32_t weapIndex; // [esp+4h] [ebp-4h]

    weapIndex = BG_GetViewmodelWeaponIndex(ps);
    if (BG_IsAimDownSightWeapon(weapIndex))
    {
        if (ps->fWeaponPosFrac == 1.0f)
        {
            cgameGlob->playerEntity.bPositionToADS = 0;
        }
        else if (ps->fWeaponPosFrac == 0.0f)
        {
            cgameGlob->playerEntity.bPositionToADS = 1;
        }
        cgameGlob->playerEntity.fLastWeaponPosFrac = ps->fWeaponPosFrac;
    }
}

void __cdecl CG_NextWeapon_f()
{
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(0);

    if (cgameGlob->nextSnap)
    {
        if (WeaponCycleAllowed(cgameGlob))
        {
            cgameGlob->weaponSelectTime = cgameGlob->time;
            CG_MenuShowNotify(0, 1);
            CycleWeapPrimary(0, 1, 0);
        }
    }
}

bool __cdecl WeaponCycleAllowed(cg_s *cgameGlob)
{
    // KISAKFIX: kisak port had `return (cgameGlob->nextSnap->ps.otherFlags & 4) != 0;`
    // — wrong field and wrong bit. CoD3SP IDA (sub_8215CA90) actually returns
    // `((~predictedPlayerState.eFlags >> 17) & 1)` — i.e. allow cycling unless eFlags
    // bit 17 (0x20000) is set. Also was missing the cg_paused gate.
    iassert(cgameGlob);
    if ((cgameGlob->predictedPlayerState.pm_flags & (PMF_RESPAWNED | PMF_FROZEN)) != 0)
        return 0;
    if ((cgameGlob->predictedPlayerState.weapFlags & 0x80) != 0)
        return 0;
    if (cgameGlob->time - cgameGlob->weaponSelectTime < cg_weaponCycleDelay->current.integer)
        return 0;
    if (cg_paused->current.integer)
        return 0;
    return (cgameGlob->predictedPlayerState.eFlags & 0x20000) == 0;
}

void __cdecl CG_PrevWeapon_f()
{
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(0);

    if (cgameGlob->nextSnap)
    {
        if (WeaponCycleAllowed(cgameGlob))
        {
            cgameGlob->weaponSelectTime = cgameGlob->time;
            CycleWeapPrimary(0, 0, 0);
        }
    }
}

void __cdecl CG_OutOfAmmoChange(int32_t localClientNum)
{
    uint32_t bitNum; // [esp+0h] [ebp-14h]
    const WeaponDef *weapDef; // [esp+10h] [ebp-4h]
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    if (cgameGlob->nextSnap && cgameGlob->predictedPlayerState.pm_type < PM_DEAD)
    {
        if (!cgameGlob->predictedPlayerState.weapon && cgameGlob->weaponLatestPrimaryIdx)
        {
            bitNum = cgameGlob->weaponLatestPrimaryIdx;

            if (Com_BitCheckAssert(cgameGlob->predictedPlayerState.weapons, bitNum, 16))
            {
                CG_SelectWeaponIndex(localClientNum, cgameGlob->weaponLatestPrimaryIdx);
                return;
            }
        }
        weapDef = BG_GetWeaponDef(cgameGlob->predictedPlayerState.weapon);
        if (!weapDef->cancelAutoHolsterWhenEmpty)
        {
            if (weapDef->inventoryType == WEAPINVENTORY_ALTMODE)
            {
                if (!VerifyPlayerAltModeWeapon(localClientNum, weapDef))
                    return;
                if (BG_WeaponAmmo(&cgameGlob->predictedPlayerState, weapDef->altWeaponIndex))
                    goto LABEL_17;
            }
            if (!CycleWeapPrimary(localClientNum, 1, 1) && weapDef->inventoryType == WEAPINVENTORY_ALTMODE)
                LABEL_17:
            CG_SelectWeaponIndex(localClientNum, weapDef->altWeaponIndex);
        }
    }
}

char __cdecl VerifyPlayerAltModeWeapon(int32_t localClientNum, const WeaponDef *weapDef)
{
    iassert(weapDef);
    iassert(weapDef->inventoryType == WEAPINVENTORY_ALTMODE);

    if (Com_BitCheckAssert(CG_GetLocalClientGlobals(localClientNum)->predictedPlayerState.weapons, weapDef->altWeaponIndex, 16))
        return 1;

    Com_PrintError(
        14,
        "Player is holding alt-mode weapon \"%s\", but does not posses it's original, \"%s\".\n",
        weapDef->szInternalName,
        weapDef->szAltWeaponName);

    CG_SelectWeaponIndex(localClientNum, 0);

    return 0;
}

char __cdecl CycleWeapPrimary(int32_t localClientNum, int32_t cycleForward, int32_t bIgnoreEmpty)
{
    uint32_t weaponSelect; // [esp+0h] [ebp-24h]
    uint32_t bitNum; // [esp+4h] [ebp-20h]
    uint32_t highestWeapIndex; // [esp+8h] [ebp-1Ch]
    int32_t startIndex; // [esp+10h] [ebp-14h]
    int32_t weaponIndex; // [esp+18h] [ebp-Ch]
    WeaponDef *weapDef; // [esp+20h] [ebp-4h]
    WeaponDef *weapDefa; // [esp+20h] [ebp-4h]
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    if (!cgameGlob->nextSnap)
        return 0;

    // KISAKFIX: kisak port added a bogus `if ((nextSnap->ps.otherFlags & 4) == 0) return 0;`
    // here — IDA CycleWeapPrimary (sub_8215E838) has NO such gate. Same wrong-bit pattern as
    // the recent WeaponCycleAllowed fix. Without removing it, weapnext/scroll never cycle.
    if (cgameGlob->predictedPlayerState.pm_type >= PM_DEAD)
        return 0;

    weaponIndex = cgameGlob->weaponSelect;
    weapDef = BG_GetWeaponDef(weaponIndex);
    if (BG_GetNumWeapons() < 2)
        return 0;
    if (weapDef->inventoryType == WEAPINVENTORY_ALTMODE)
    {
        if (VerifyPlayerAltModeWeapon(localClientNum, weapDef))
        {
            CG_SelectWeaponIndex(localClientNum, weapDef->altWeaponIndex);
            return 1;
        }
        else
        {
            return 0;
        }
    }
    else
    {
        if (weapDef->inventoryType == WEAPINVENTORY_PRIMARY || !cgameGlob->weaponLatestPrimaryIdx)
            goto LABEL_21;
        bitNum = cgameGlob->weaponLatestPrimaryIdx;

        if (Com_BitCheckAssert(cgameGlob->predictedPlayerState.weapons, bitNum, 16))
        {
            CG_SelectWeaponIndex(localClientNum, cgameGlob->weaponLatestPrimaryIdx);
            return 1;
        }
        else
        {
        LABEL_21:
            highestWeapIndex = BG_GetNumWeapons() - 1;
            if (!weaponIndex)
                weaponIndex = 1;
            startIndex = weaponIndex;
            while (1)
            {
                weaponIndex = (highestWeapIndex + weaponIndex + 2 * (cycleForward != 0) - 1 - 1) % highestWeapIndex + 1;
                if (weaponIndex == startIndex)
                    break;
                if (Com_BitCheckAssert(cgameGlob->predictedPlayerState.weapons, weaponIndex, 16)
                    && (!bIgnoreEmpty || BG_WeaponAmmo(&cgameGlob->predictedPlayerState, weaponIndex)))
                {
                    weapDefa = BG_GetWeaponDef(weaponIndex);
                    if (weapDefa->inventoryType != WEAPINVENTORY_ALTMODE
                        && weapDefa->inventoryType != WEAPINVENTORY_ITEM
                        && weapDefa->inventoryType != WEAPINVENTORY_OFFHAND)
                    {
                        CG_SelectWeaponIndex(localClientNum, weaponIndex);
                        return 1;
                    }
                }
            }
            weaponSelect = cgameGlob->weaponSelect;
            if (!Com_BitCheckAssert(cgameGlob->predictedPlayerState.weapons, weaponSelect, 16))
                CG_SelectWeaponIndex(localClientNum, 0);
            return 0;
        }
    }
}

uint32_t __cdecl CG_AltWeaponToggleIndex(int32_t localClientNum, const cg_s *cgameGlob)
{
    const playerState_s *ps; // [esp+0h] [ebp-Ch]
    WeaponDef *weapDef; // [esp+4h] [ebp-8h]
    int32_t newPrimaryIdx; // [esp+8h] [ebp-4h]

    iassert(cgameGlob);
    ps = &cgameGlob->predictedPlayerState;
    weapDef = BG_GetWeaponDef(cgameGlob->weaponSelect);
    if (weapDef->altWeaponIndex)
        return weapDef->altWeaponIndex;
    newPrimaryIdx = NextWeapInCycle(localClientNum, ps, WEAPINVENTORY_PRIMARY, cgameGlob->weaponSelect, 1, 1, 1);
    if (newPrimaryIdx)
        return BG_GetWeaponDef(newPrimaryIdx)->altWeaponIndex;
    newPrimaryIdx = NextWeapInCycle(localClientNum, ps, WEAPINVENTORY_PRIMARY, cgameGlob->weaponSelect, 1, 0, 1);
    if (newPrimaryIdx)
        return BG_GetWeaponDef(newPrimaryIdx)->altWeaponIndex;
    else
        return 0;
}

int32_t __cdecl NextWeapInCycle(
    int32_t localClientNum,
    const playerState_s *ps,
    weapInventoryType_t type,
    uint32_t startWeaponIndex,
    bool cycleForward,
    bool skipEmpties,
    bool skipHaveNoAlts)
{
    uint32_t highestWeapIndex; // [esp+0h] [ebp-14h]
    int32_t weaponIndex; // [esp+Ch] [ebp-8h]
    WeaponDef *weapDef; // [esp+10h] [ebp-4h]

    iassert(ps);
    if (BG_GetNumWeapons() < 2)
        return 0;
    highestWeapIndex = BG_GetNumWeapons() - 1;
    if (!startWeaponIndex)
        startWeaponIndex = 1;
    weaponIndex = startWeaponIndex;
    while (1)
    {
        weaponIndex = (highestWeapIndex + weaponIndex + (cycleForward ? 1 : -1) - 1) % highestWeapIndex + 1;
        if (weaponIndex == startWeaponIndex)
            break;
        weapDef = BG_GetWeaponDef(weaponIndex);
        if (weapDef->inventoryType == type)
        {
            iassert(ps);
            if (Com_BitCheckAssert(ps->weapons, weaponIndex, 16)
                && (!skipEmpties || BG_WeaponAmmo(ps, weaponIndex))
                && (!skipHaveNoAlts || weapDef->altWeaponIndex))
            {
                return weaponIndex;
            }
        }
    }
    return 0;
}

void __cdecl CG_ActionSlotDown_f()
{
    ActionSlotType v0; // [esp+0h] [ebp-24h]
    uint32_t bitNum; // [esp+4h] [ebp-20h]
    int32_t slot; // [esp+10h] [ebp-14h] BYREF
    uint32_t weapon; // [esp+14h] [ebp-10h]
    int32_t localClientNum; // [esp+18h] [ebp-Ch]
    bool didSomething; // [esp+1Fh] [ebp-5h]
    playerState_s *ps; // [esp+20h] [ebp-4h]
    cg_s *cgameGlob;

    localClientNum = 0;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    if (ActionSlotUsageAllowed(cgameGlob) && cgameGlob->time - cgameGlob->lastActionSlotTime >= 250 && ActionParms(&slot))
    {
        ps = &cgameGlob->predictedPlayerState;
        didSomething = 0;
        v0 = cgameGlob->predictedPlayerState.actionSlotType[slot];
        switch (v0)
        {
        case ACTIONSLOTTYPE_SPECIFYWEAPON:
            weapon = ps->actionSlotParam[slot].specifyWeapon.index;
            if (weapon == cgameGlob->weaponSelect)
            {
                if (weapon == cgameGlob->weaponLatestPrimaryIdx)
                    goto LABEL_15;
                bitNum = cgameGlob->weaponLatestPrimaryIdx;
                iassert(ps);
                if (!Com_BitCheckAssert(ps->weapons, bitNum, 16))
                {
                LABEL_15:
                    didSomething = CycleWeapPrimary(localClientNum, 1, 0);
                }
                else
                {
                    CG_SelectWeaponIndex(localClientNum, cgameGlob->weaponLatestPrimaryIdx);
                    didSomething = 1;
                }
            }
            else
            {
                iassert(ps);
                if (Com_BitCheckAssert(ps->weapons, weapon, 16) && ps->weapon != weapon)
                {
                    didSomething = 1;
                    CG_SelectWeaponIndex(localClientNum, weapon);
                }
            }
            break;
        case ACTIONSLOTTYPE_ALTWEAPONTOGGLE:
            didSomething = ToggleWeaponAltMode(localClientNum);
            break;
        case ACTIONSLOTTYPE_NIGHTVISION:
            cgameGlob->extraButtons |= 0x40000u;
            didSomething = 1;
            break;
        }
        cgameGlob->ammoFadeTime = cgameGlob->time;
        if (didSomething)
        {
            cgameGlob->lastActionSlotTime = cgameGlob->time;
            if ((ps->eFlags & 0x300) != 0 && ps->actionSlotType[slot] == ACTIONSLOTTYPE_SPECIFYWEAPON)
                cgameGlob->extraButtons |= 0x20u;
        }
    }
}

char __cdecl ToggleWeaponAltMode(int32_t localClientNum)
{
    uint32_t weapIdx; // [esp+4h] [ebp-8h]
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    if ((cgameGlob->predictedPlayerState.weaponstate == 1
        || cgameGlob->predictedPlayerState.weaponstate == 2
        || cgameGlob->predictedPlayerState.weaponstate == 3
        || cgameGlob->predictedPlayerState.weaponstate == 4)
        && cgameGlob->predictedPlayerState.weaponstate != 1)
    {
        return 0;
    }

    weapIdx = CG_AltWeaponToggleIndex(localClientNum, cgameGlob);
    if (!weapIdx)
        return 0;

    CG_SelectWeaponIndex(localClientNum, weapIdx);
    return 1;
}

bool __cdecl ActionSlotUsageAllowed(cg_s *cgameGlob)
{
    // KISAKFIX: same wrong-field bug as WeaponCycleAllowed/CycleWeapPrimary. IDA
    // ActionSlotUsageAllowed (sub_8215CDB8) gates on `(eFlags & 0x20000) == 0` (in-vehicle
    // check) and also tests `cg_paused`. Kisak port had `nextSnap->ps.otherFlags & 4` (wrong
    // field + wrong bit) and was missing the paused gate.
    iassert(cgameGlob);
    if (!cgameGlob->nextSnap)
        return 0;
    if (cgameGlob->predictedPlayerState.weaponstate == 3 || cgameGlob->predictedPlayerState.weaponstate == 4)
        return 0;
    if ((cgameGlob->predictedPlayerState.pm_flags & (PMF_RESPAWNED | PMF_FROZEN)) != 0)
        return 0;
    if ((cgameGlob->predictedPlayerState.weapFlags & 0x80) != 0)
        return 0;
    if (cg_paused->current.integer)
        return 0;
    return (cgameGlob->predictedPlayerState.eFlags & 0x20000) == 0;
}

char __cdecl ActionParms(int32_t *slotResult)
{
    const char *v2; // eax
    int32_t slot; // [esp+0h] [ebp-4h]

    if (Cmd_Argc() >= 2)
    {
        v2 = Cmd_Argv(1);
        slot = atoi(v2);
        if (slot >= 1 && slot <= 4)
        {
            *slotResult = slot - 1;
            return 1;
        }
        else
        {
            Com_Printf(0, "+/-actionslot; number given is out of range.  Was %i, expected 1 thru %i.\n", slot, 4);
            return 0;
        }
    }
    else
    {
        Com_Printf(0, "USAGE: +/-actionslot <number>\n");
        return 0;
    }
}

void __cdecl CG_ActionSlotUp_f()
{
    int32_t slot[2]; // [esp+Ch] [ebp-8h] BYREF
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(0);

    slot[1] = 0;
    if (ActionSlotUsageAllowed(cgameGlob))
        ActionParms(slot);
}

void __cdecl CG_EjectWeaponBrass(int32_t localClientNum, const entityState_s *ent, int32_t event)
{
    uint32_t number; // [esp+0h] [ebp-28h]
    const FxEffectDef *viewShellEjectEffect; // [esp+4h] [ebp-24h]
    const FxEffectDef *viewLastShotEjectEffect; // [esp+8h] [ebp-20h]
    bool v6; // [esp+Ch] [ebp-1Ch]
    snapshot_s *nextSnap; // [esp+10h] [ebp-18h]
    const FxEffectDef *fxDef; // [esp+20h] [ebp-8h]
    const WeaponDef *weaponDef; // [esp+24h] [ebp-4h]

    if (cg_brass->current.enabled && ent->eType < ET_EVENTS && ent->weapon)
    {
        if (ent->weapon < BG_GetNumWeapons())
        {
            nextSnap = CG_GetLocalClientGlobals(localClientNum)->nextSnap;
            // KISAKFIX: kisak `(otherFlags & 6) != 0 && ...` is the MP "is local/spectated player"
            // idiom; SP IDA only tests `ent->number == ps.clientNum`. Determines view-attached
            // vs world brass FX — wrong test meant view brass never spawned.
            v6 = ent->number == nextSnap->ps.clientNum;
            weaponDef = BG_GetWeaponDef(ent->weapon);
            if (weaponDef->viewLastShotEjectEffect && weaponDef->worldLastShotEjectEffect && event == 27)
            {
                if (v6)
                    viewLastShotEjectEffect = weaponDef->viewLastShotEjectEffect;
                else
                    viewLastShotEjectEffect = weaponDef->worldLastShotEjectEffect;
                fxDef = viewLastShotEjectEffect;
            }
            else
            {
                if (v6)
                    viewShellEjectEffect = weaponDef->viewShellEjectEffect;
                else
                    viewShellEjectEffect = weaponDef->worldShellEjectEffect;
                fxDef = viewShellEjectEffect;
            }
            if (fxDef)
            {
                if (v6)
                    number = CG_WeaponDObjHandle(ent->weapon);
                else
                    number = ent->number;
                CG_PlayBoltedEffect(localClientNum, fxDef, number, scr_const.tag_brass);
            }
        }
        else
        {
            Com_Error(ERR_DROP, "CG_EjectWeaponBrass: ent->weapon >= BG_GetNumWeapons()");
        }
    }
}

// OpenXR recoil pulse for confirmed local firearm events.
bool VR_ApplyRightControllerWeaponHaptic(
    float amplitude,
    float durationSeconds);

void __cdecl CG_FireWeapon(
    int32_t localClientNum,
    centity_s *cent,
    int32_t event,
    uint16_t tagName,
    uint32_t weapon,
    const playerState_s *ps)
{
    snapshot_s *nextSnap; // [esp+Ch] [ebp-3Ch]
    const weaponInfo_s *weapInfo; // [esp+14h] [ebp-34h]
    snd_alias_list_t *firesound; // [esp+18h] [ebp-30h]
    DObj_s *obj; // [esp+1Ch] [ebp-2Ch]
    int32_t playbackId; // [esp+20h] [ebp-28h]
    float origin[3]; // [esp+24h] [ebp-24h] BYREF
    int32_t msec; // [esp+30h] [ebp-18h] BYREF
    int32_t isPlayer; // [esp+34h] [ebp-14h]
    cg_s *cgameGlob; // [esp+38h] [ebp-10h]
    const WeaponDef *weaponDef; // [esp+3Ch] [ebp-Ch]
    const entityState_s *p_nextState; // [esp+40h] [ebp-8h]
    int32_t playerUsingTurret; // [esp+44h] [ebp-4h]

    p_nextState = &cent->nextState;
    if (!weapon)
        weapon = p_nextState->weapon;
    if (weapon)
    {
        if (weapon < BG_GetNumWeapons())
        {
            iassert(localClientNum == 0);
            weapInfo = CG_GetLocalClientWeaponInfo(0, weapon);
            weaponDef = BG_GetWeaponDef(weapon);
            iassert(weaponDef);
            cent->bMuzzleFlash = 1;
            iassert(localClientNum == 0);
            cgameGlob = CG_GetLocalClientGlobals(localClientNum);;
            nextSnap = cgameGlob->nextSnap;

#ifdef KISAK_MP
            isPlayer = (nextSnap->ps.otherFlags & 6) != 0 && p_nextState->number == nextSnap->ps.clientNum;

            if (sv_clientSideBullets->current.enabled)
                DrawBulletImpacts(localClientNum, cent, weaponDef, tagName, ps);
#elif KISAK_SP
            isPlayer = cgameGlob->nextSnap->ps.clientNum == p_nextState->number;
#endif

            if (isPlayer)
            {
#if defined(KISAK_MP) || defined(KISAK_SP)
                if (weaponDef->weapType != WEAPTYPE_GRENADE)
                {
                    VR_ApplyRightControllerWeaponHaptic(
                        0.78f,
                        0.050f);
                }
#endif

                CG_UpdateViewModelPose(weapInfo->viewModelDObj, localClientNum);
                BG_WeaponFireRecoil(&cgameGlob->predictedPlayerState, cgameGlob->vGunSpeed, cgameGlob->kickAVel);
            }

            playerUsingTurret = 0;
            if (p_nextState->eType == ET_MG42 && (ps->eFlags & 0x300) != 0 && ps->viewlocked_entNum == p_nextState->number)
            {
                playerUsingTurret = 1;
                isPlayer = 1;
            }

            if (p_nextState->eType == ET_MG42)
                WeaponFlash(localClientNum, p_nextState->number, weapon, playerUsingTurret, tagName);

#ifdef KISAK_MP
            if (p_nextState->eType == ET_HELICOPTER)
            {
                WeaponFlash(localClientNum, p_nextState->number, weapon, 0, tagName);
                CG_EjectWeaponBrass(localClientNum, p_nextState, event);
                Veh_IncTurretBarrelRoll(localClientNum, p_nextState->number, heli_barrelRotation->current.value);
            }
#endif

            firesound = weaponDef->fireSound;
            if (isPlayer && weaponDef->fireSoundPlayer)
                firesound = weaponDef->fireSoundPlayer;

            if (event == EV_FIRE_WEAPON_LASTSHOT)
            {
                if (isPlayer && weaponDef->fireLastSoundPlayer)
                {
                    firesound = weaponDef->fireLastSoundPlayer;
                }
                else if (weaponDef->fireLastSound)
                {
                    firesound = weaponDef->fireLastSound;
                }
            }
            if (firesound)
            {
                if (isPlayer)
                {
                    if (!weapInfo->viewModelDObj || !CG_DObjGetWorldTagPos(&cgameGlob->viewModelPose, weapInfo->viewModelDObj, tagName, origin))
                    {
                        BG_EvaluateTrajectory(&p_nextState->lerp.pos, cgameGlob->time, origin);
                    }
                }
                else
                {
                    obj = Com_GetClientDObj(p_nextState->number, localClientNum);
                    if (!obj || !CG_DObjGetWorldTagPos(&cent->pose, obj, tagName, origin))
                        BG_EvaluateTrajectory(&p_nextState->lerp.pos, cgameGlob->time, origin);
                }
                playbackId = CG_PlaySoundAlias(localClientNum, p_nextState->number, origin, firesound);
#ifdef KISAK_MP
                if (cent->nextState.eType == ET_PLAYER && !weaponDef->silenced && weaponDef->weapType != WEAPTYPE_GRENADE)
#elif KISAK_SP
                if (cent->nextState.eType == ET_ACTOR && !weaponDef->silenced && weaponDef->weapType != WEAPTYPE_GRENADE)
#endif
                {
                    SND_GetKnownLength(playbackId, &msec);
                    CG_CompassAddWeaponPingInfo(localClientNum, cent, origin, msec);
                }
            }
            if (!BG_GetWeaponDef(weapon)->bBoltAction)
                CG_EjectWeaponBrass(localClientNum, p_nextState, event);
#ifdef KISAK_MP
            if (isPlayer)
                TakeClipOnlyWeaponIfEmpty(localClientNum, &cgameGlob->predictedPlayerState);
#elif KISAK_SP
            //if (playerUsingTurret || v19 || p_nextState->eType == 1)
            //{
            //    fireRumble = weaponDef->fireRumble;
            //    if (fireRumble)
            //    {
            //        if (*fireRumble)
            //            CG_PlayRumbleOnClient(localClientNum, fireRumble);
            //    }
            //}
#endif
        }
        else
        {
            Com_Error(ERR_DROP, "CG_FireWeapon: weapon >= BG_GetNumWeapons()");
        }
    }
}

#ifdef KISAK_MP
// OpenXR weapon-command snapshot used by local client bullet effects.
bool VR_GetRightControllerWeaponCommand(
    float* gunPitch,
    float* gunYaw,
    bool* attackPressed);

void __cdecl DrawBulletImpacts(
    int32_t localClientNum,
    const centity_s *ent,
    const WeaponDef *weaponDef,
    uint16_t boneName,
    const playerState_s *ps)
{
    double v5; // st7
    float v6; // [esp+20h] [ebp-138h]
    float v7; // [esp+24h] [ebp-134h]
    float v8; // [esp+28h] [ebp-130h]
    float v10; // [esp+40h] [ebp-118h]
    snapshot_s *nextSnap; // [esp+48h] [ebp-110h]
    float velocity[3]; // [esp+4Ch] [ebp-10Ch] BYREF
    uint8_t boneIndex; // [esp+5Bh] [ebp-FDh] BYREF
    int32_t weaponNum; // [esp+5Ch] [ebp-FCh]
    int32_t ads; // [esp+60h] [ebp-F8h]
    int32_t shotCount; // [esp+64h] [ebp-F4h]
    float origin[3]; // [esp+68h] [ebp-F0h] BYREF
    float range; // [esp+74h] [ebp-E4h]
    float dist; // [esp+78h] [ebp-E0h]
    cg_s *cgameGlob; // [esp+7Ch] [ebp-DCh]
    int32_t shot; // [esp+80h] [ebp-D8h]
    int32_t dobjNumber; // [esp+84h] [ebp-D4h]
    orientation_t orient; // [esp+88h] [ebp-D0h] BYREF
    bool drawTracers; // [esp+BBh] [ebp-9Dh]
    float minSpread; // [esp+BCh] [ebp-9Ch] BYREF
    float maxSpread; // [esp+C0h] [ebp-98h] BYREF
    float viewang[3]; // [esp+C4h] [ebp-94h] BYREF
    float tracerStart[3]; // [esp+D0h] [ebp-88h] BYREF
    clientInfo_t *ci; // [esp+DCh] [ebp-7Ch]
    BulletFireParams dst; // [esp+E0h] [ebp-78h] BYREF
    float aimSpreadScale; // [esp+120h] [ebp-38h]
    float aimSpreadAmount; // [esp+124h] [ebp-34h]
    orientation_t gunOrient; // [esp+128h] [ebp-30h] BYREF

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    nextSnap = cgameGlob->nextSnap;
    // KISAKFIX: MP `(otherFlags & 6) != 0 && ...` → SP IDA just `number == clientNum`.
    if (ent->nextState.number == nextSnap->ps.clientNum)
    {
        weaponNum = BG_GetViewmodelWeaponIndex(ps);
        dobjNumber = CG_WeaponDObjHandle(weaponNum);
        aimSpreadScale = cgameGlob->lastFrame.aimSpreadScale / 255.0;
        ads = ps->fWeaponPosFrac == 1.0;
        BG_GetSpreadForWeapon(ps, weaponDef, &minSpread, &maxSpread);
        CG_GetPlayerViewOrigin(localClientNum, ps, origin);
        viewang[0] = cgameGlob->gunPitch;
        viewang[1] = cgameGlob->gunYaw;
        viewang[2] = 0.0;
        boneIndex = 0;
        if (!CG_GetBoneIndex(localClientNum, dobjNumber, boneName, &boneIndex))
            return;
        if (cgameGlob->renderingThirdPerson)
        {
            ci = &cgameGlob->bgs.clientinfo[ent->nextState.number];
            tracerStart[0] = origin[0];
            tracerStart[1] = origin[1];
            tracerStart[2] = origin[2];
            AngleVectors(ci->playerAngles, orient.axis[0], orient.axis[1], orient.axis[2]);
            Vec3Mad(tracerStart, 30.0, orient.axis[0], tracerStart);
        }
        else
        {
            if (!FX_GetBoneOrientation(localClientNum, dobjNumber, boneIndex, &gunOrient))
                return;
            // Preserve the original tracer start until a valid VR aim
            // snapshot confirms this is the local tracked weapon path.
            tracerStart[0] = gunOrient.origin[0];
            tracerStart[1] = gunOrient.origin[1];
            tracerStart[2] = gunOrient.origin[2];

            float vrGunPitch = 0.0f;
            float vrGunYaw = 0.0f;
            bool ignoredVrAttackPressed = false;

            if (VR_GetRightControllerWeaponCommand(
                    &vrGunPitch,
                    &vrGunYaw,
                    &ignoredVrAttackPressed))
            {
                viewang[0] = vrGunPitch;
                viewang[1] = vrGunYaw;
                viewang[2] = 0.0f;

                trace_t vrMuzzleObstruction = {};

                CG_LocationalTrace(
                    &vrMuzzleObstruction,
                    origin,
                    gunOrient.origin,
                    ent->nextState.number,
                    0x2806831);

                const bool vrMuzzlePathClear =
                    !vrMuzzleObstruction.startsolid &&
                    vrMuzzleObstruction.fraction >= 1.0f;

                if (vrMuzzlePathClear)
                {
                    origin[0] = gunOrient.origin[0];
                    origin[1] = gunOrient.origin[1];
                    origin[2] = gunOrient.origin[2];

                    static bool loggedClientVrPhysicalMuzzle = false;

                    if (!loggedClientVrPhysicalMuzzle)
                    {
                        Com_Printf(
                            0,
                            "[VR] Client bullets now originate "
                            "at tracked tag_flash.\n");

                        loggedClientVrPhysicalMuzzle = true;
                    }
                }
                else
                {
                    static bool loggedClientVrMuzzleBlocked = false;

                    if (!loggedClientVrMuzzleBlocked)
                    {
                        Com_Printf(
                            0,
                            "[VR] Client physical muzzle was blocked; "
                            "suppressing the local bullet trace.\n");

                        loggedClientVrMuzzleBlocked = true;
                    }

                    // Do not draw a tracer, impact, or local bullet path from
                    // the player's eyes. That fallback could still penetrate
                    // a thin wall through CoD4's normal penetration system.
                    return;
                }

                tracerStart[0] = origin[0];
                tracerStart[1] = origin[1];
                tracerStart[2] = origin[2];

                static bool loggedClientVrBulletAim = false;

                if (!loggedClientVrBulletAim)
                {
                    Com_Printf(
                        0,
                        "[VR] Applied right-controller aim to "
                        "client-side bullet construction.\n");

                    loggedClientVrBulletAim = true;
                }
            }

            AngleVectors(
                viewang,
                orient.axis[0],
                orient.axis[1],
                orient.axis[2]);
        }
        drawTracers = cg_firstPersonTracerChance->current.value * 32768.0 > (double)rand();
        goto LABEL_33;
    }
    if (ent->nextState.eType != ET_PLAYER)
    {
        if (ent->nextState.eType == ET_MG42)
        {
            minSpread = weaponDef->playerSpread;
            maxSpread = weaponDef->playerSpread;
            aimSpreadScale = 1.0;
            ads = 0;
            dobjNumber = ent->nextState.number;
            boneIndex = 0;
            if (!CG_GetBoneIndex(localClientNum, dobjNumber, boneName, &boneIndex)
                || !FX_GetBoneOrientation(localClientNum, dobjNumber, boneIndex, &orient))
            {
                return;
            }
        }
        else
        {
            if (ent->nextState.eType != ET_VEHICLE && ent->nextState.eType != ET_HELICOPTER)
            {
                Com_PrintError(14, "Unknown eType %i in CG_DrawBulletImpacts()\n", ent->nextState.eType);
                return;
            }
            minSpread = weaponDef->fAdsSpread;
            maxSpread = weaponDef->fAdsSpread;
            ads = 1;
            aimSpreadScale = 0.0;
            dobjNumber = ent->nextState.number;
            boneIndex = 0;
            if (!CG_GetBoneIndex(localClientNum, dobjNumber, boneName, &boneIndex)
                || !FX_GetBoneOrientation(localClientNum, dobjNumber, boneIndex, &orient))
            {
                return;
            }
        }
        origin[0] = orient.origin[0];
        origin[1] = orient.origin[1];
        origin[2] = orient.origin[2];
        tracerStart[0] = orient.origin[0];
        tracerStart[1] = orient.origin[1];
        tracerStart[2] = orient.origin[2];
        drawTracers = 0;
        goto LABEL_33;
    }
    CG_GuessSpreadForWeapon(localClientNum, ent, weaponDef, &minSpread, &maxSpread);
    ci = &cgameGlob->bgs.clientinfo[ent->nextState.number];
    ads = CG_IsPlayerADS(ci, ent);
    Vec3Sub(ent->nextState.lerp.pos.trBase, ent->currentState.pos.trBase, velocity);
    if (cgameGlob->nextSnap->serverTime != cgameGlob->snap->serverTime)
    {
        v8 = 1000.0 / (double)(cgameGlob->nextSnap->serverTime - cgameGlob->snap->serverTime);
        Vec3Scale(velocity, v8, velocity);
    }
    v10 = Vec3Length(velocity);
    v7 = v10 - 255.0;
    if (v7 < 0.0)
        v6 = v10;
    else
        v6 = 255.0;
    dist = v6;
    aimSpreadScale = v6 / 255.0;
    dobjNumber = ent->nextState.number;
    boneIndex = 0;
    if (CG_GetBoneIndex(localClientNum, dobjNumber, boneName, &boneIndex)
        && FX_GetBoneOrientation(localClientNum, dobjNumber, boneIndex, &orient))
    {
        origin[0] = orient.origin[0];
        origin[1] = orient.origin[1];
        origin[2] = orient.origin[2];
        tracerStart[0] = orient.origin[0];
        tracerStart[1] = orient.origin[1];
        tracerStart[2] = orient.origin[2];
        drawTracers = 0;
        AngleVectors(ci->playerAngles, orient.axis[0], orient.axis[1], orient.axis[2]);
    LABEL_33:
        if (ads)
            v5 = (maxSpread - weaponDef->fAdsSpread) * aimSpreadScale + weaponDef->fAdsSpread;
        else
            v5 = (maxSpread - minSpread) * aimSpreadScale + minSpread;
        aimSpreadAmount = v5;
        memset((uint8_t *)&dst, 0, sizeof(dst));
        dst.weaponEntIndex = ENTITYNUM_WORLD;
        dst.ignoreEntIndex = ent->nextState.number;
        dst.damageMultiplier = 1.0;
        dst.methodOfDeath = (weaponDef->bRifleBullet != 0) + 1;
        if (weaponDef->weapClass == WEAPCLASS_SPREAD)
        {
            shotCount = weaponDef->shotCount;
            range = weaponDef->fMinDamageRange;
        }
        else
        {
            shotCount = 1;
            range = 8192.0;
        }
        dst.origStart[0] = origin[0];
        dst.origStart[1] = origin[1];
        dst.origStart[2] = origin[2];
        for (shot = 0; shot < shotCount; ++shot)
        {
            dst.dir[0] = orient.axis[0][0];
            dst.dir[1] = orient.axis[0][1];
            dst.dir[2] = orient.axis[0][2];
            dst.start[0] = origin[0];
            dst.start[1] = origin[1];
            dst.start[2] = origin[2];
            CG_BulletEndpos(
                shot + ps->commandTime,
                aimSpreadAmount,
                dst.start,
                dst.end,
                dst.dir,
                orient.axis[0],
                orient.axis[1],
                orient.axis[2],
                range);
            dst.damageMultiplier = 1.0;
            FireBulletPenetrate(localClientNum, &dst, weaponDef, ent, tracerStart, drawTracers);
        }
    }
}

void __cdecl FireBulletPenetrate(
    int32_t localClientNum,
    BulletFireParams *bp,
    const WeaponDef *weapDef,
    const centity_s *attacker,
    float *tracerStart,
    bool drawTracer)
{
    float v6; // [esp+Ch] [ebp-1F0h]
    float v7; // [esp+10h] [ebp-1ECh]
    float v8; // [esp+14h] [ebp-1E8h]
    float v9; // [esp+18h] [ebp-1E4h]
    float v10; // [esp+1Ch] [ebp-1E0h]
    float v11; // [esp+20h] [ebp-1DCh]
    double value; // [esp+24h] [ebp-1D8h]
    float v13; // [esp+2Ch] [ebp-1D0h]
    bool v14; // [esp+30h] [ebp-1CCh]
    __int16 v15; // [esp+34h] [ebp-1C8h]
    int32_t v16; // [esp+38h] [ebp-1C4h]
    uint32_t v17; // [esp+3Ch] [ebp-1C0h]
    uint32_t v18; // [esp+40h] [ebp-1BCh]
    int32_t v19; // [esp+44h] [ebp-1B8h]
    float v20; // [esp+50h] [ebp-1ACh]
    float v21[3]; // [esp+54h] [ebp-1A8h] BYREF
    int32_t v22; // [esp+60h] [ebp-19Ch]
    int32_t v23; // [esp+64h] [ebp-198h]
    int32_t v24; // [esp+68h] [ebp-194h]
    int32_t v25; // [esp+6Ch] [ebp-190h]
    int32_t v26; // [esp+70h] [ebp-18Ch]
    int32_t contents; // [esp+78h] [ebp-184h]
    int32_t v28; // [esp+7Ch] [ebp-180h]
    int32_t v29; // [esp+80h] [ebp-17Ch]
    int32_t targetEntityNum; // [esp+84h] [ebp-178h]
    int32_t number; // [esp+88h] [ebp-174h]
    float v32; // [esp+90h] [ebp-16Ch]
    float v33; // [esp+94h] [ebp-168h]
    float v34[3]; // [esp+98h] [ebp-164h] BYREF
    float SurfacePenetrationDepth; // [esp+A4h] [ebp-158h]
    float v36; // [esp+A8h] [ebp-154h]
    int32_t v37; // [esp+ACh] [ebp-150h]
    float v[4]; // [esp+B4h] [ebp-148h] BYREF
    float diff[3]; // [esp+C4h] [ebp-138h] BYREF
    int32_t perks; // [esp+D8h] [ebp-124h]
    int32_t hitContents; // [esp+DCh] [ebp-120h]
    int32_t damage; // [esp+E0h] [ebp-11Ch]
    int32_t surfType; // [esp+E4h] [ebp-118h]
    int32_t entityNum; // [esp+E8h] [ebp-114h]
    int32_t sourceEntityNum; // [esp+ECh] [ebp-110h]
    BulletTraceResults revBr; // [esp+F4h] [ebp-108h] BYREF
    float lastHitPos[3]; // [esp+140h] [ebp-BCh] BYREF
    float depth; // [esp+14Ch] [ebp-B0h]
    int32_t weapType; // [esp+150h] [ebp-ACh]
    int32_t penetrateIndex; // [esp+154h] [ebp-A8h]
    uint16_t traceHitEntityId; // [esp+158h] [ebp-A4h]
    cg_s *cgameGlob; // [esp+15Ch] [ebp-A0h]
    bool allSolid; // [esp+163h] [ebp-99h]
    BulletFireParams revBp; // [esp+164h] [ebp-98h] BYREF
    int32_t weaponIndex; // [esp+1A4h] [ebp-58h]
    bool revTraceHit; // [esp+1ABh] [ebp-51h]
    BulletTraceResults br; // [esp+1ACh] [ebp-50h] BYREF
    float maxDepth; // [esp+1F4h] [ebp-8h]
    bool traceHit; // [esp+1FBh] [ebp-1h]

    iassert(bp);
    iassert(weapDef);
    iassert(attacker);

    weaponIndex = BG_GetWeaponIndex(weapDef);
    weapType = weapDef->weapType;
    if (weapType)
        drawTracer = 0;

    traceHit = BulletTrace(localClientNum, bp, weapDef, attacker, &br, 0);
    if (traceHit)
    {
        traceHitEntityId = Trace_GetEntityHitId(&br.trace);

        if (drawTracer)
            CG_SpawnTracer(localClientNum, tracerStart, br.hitPos);

        if (!weapType)
        {
            DynEntCl_EntityImpactEvent(&br.trace, localClientNum, attacker->nextState.number, bp->start, br.hitPos, 0);
            DynEntCl_DynEntImpactEvent(localClientNum, attacker->nextState.number, bp->start, br.hitPos, weapDef->damage, 0);
            hitContents = br.trace.contents;
            damage = weapDef->damage;
            surfType = (br.trace.surfaceFlags & 0x1F00000) >> 20;
            entityNum = traceHitEntityId;
            sourceEntityNum = attacker->nextState.number;

            if (!sv_clientSideBullets->current.enabled || !IsEntityAPlayer(localClientNum, entityNum))
                CG_BulletHitEvent(
                    localClientNum,
                    sourceEntityNum,
                    entityNum,
                    weaponIndex,
                    bp->start,
                    br.hitPos,
                    br.trace.normal,
                    surfType,
                    41,
                    0,
                    damage,
                    hitContents);
        }
        if (weapDef->penetrateType && !br.trace.startsolid)
        {
            cgameGlob = CG_GetLocalClientGlobals(localClientNum);

            for (penetrateIndex = 0; penetrateIndex < 5; ++penetrateIndex)
            {
                maxDepth = BG_GetSurfacePenetrationDepth(weapDef, br.depthSurfaceType);
                if (attacker->nextState.eType == ET_PLAYER)
                {
                    perks = cgameGlob->bgs.clientinfo[attacker->nextState.clientNum].perks;
                    if ((perks & 0x20) != 0)
                        maxDepth = maxDepth * perk_bulletPenetrationMultiplier->current.value;
                }
                if (maxDepth <= 0.0)
                    break;
                lastHitPos[0] = br.hitPos[0];
                lastHitPos[1] = br.hitPos[1];
                lastHitPos[2] = br.hitPos[2];
                if (!BG_AdvanceTrace(bp, &br, 0.13500001f))
                    break;
                traceHit = BulletTrace(localClientNum, bp, weapDef, attacker, &br, br.depthSurfaceType);
                revBp = *bp; // Com_Memcpy((char *)&revBp, (char *)bp, 64);

                revBp.dir[0] = -bp->dir[0];
                revBp.dir[1] = -bp->dir[1];
                revBp.dir[2] = -bp->dir[2];

                revBp.start[0] = bp->end[0];
                revBp.start[1] = bp->end[1];
                revBp.start[2] = bp->end[2];

                Vec3Mad(lastHitPos, 0.0099999998f, revBp.dir, revBp.end);
                revBr = br; // Com_Memcpy((char *)&revBr, (char *)&br, 68);
                revBr.trace.normal[0] = -revBr.trace.normal[0];
                revBr.trace.normal[1] = -revBr.trace.normal[1];
                revBr.trace.normal[2] = -revBr.trace.normal[2];

                if (traceHit)
                    BG_AdvanceTrace(&revBp, &revBr, 0.0099999998f);

                revTraceHit = BulletTrace(localClientNum, &revBp, weapDef, attacker, &revBr, revBr.depthSurfaceType);
                allSolid = revTraceHit && revBr.trace.allsolid || br.trace.startsolid && revBr.trace.startsolid;

                if (revTraceHit || allSolid)
                {
                    traceHitEntityId = Trace_GetEntityHitId(&revBr.trace);
                    if (allSolid)
                    {
                        Vec3Sub(revBp.end, revBp.start, diff);
                        v13 = Vec3Length(diff);
                    }
                    else
                    {
                        Vec3Sub(lastHitPos, revBr.hitPos, v);
                        v13 = Vec3Length(v);
                    }
                    depth = v13;
                    if (v13 < 1.0f)
                        depth = 1.0f;

                    if (revTraceHit)
                    {
                        if (attacker->nextState.eType == ET_PLAYER
                            && (v37 = cgameGlob->bgs.clientinfo[attacker->nextState.clientNum].perks, (v37 & 0x20) != 0))
                        {
                            value = perk_bulletPenetrationMultiplier->current.value;
                            v36 = BG_GetSurfacePenetrationDepth(weapDef, revBr.depthSurfaceType) * value;
                            v11 = v36 - maxDepth;
                            v10 = v11 < 0.0 ? v36 : maxDepth;
                            maxDepth = v10;
                        }
                        else
                        {
                            SurfacePenetrationDepth = BG_GetSurfacePenetrationDepth(weapDef, revBr.depthSurfaceType);
                            v9 = SurfacePenetrationDepth - maxDepth;
                            v8 = v9 < 0.0f ? SurfacePenetrationDepth : maxDepth;
                            maxDepth = v8;
                        }
                        if (maxDepth <= 0.0f)
                            return;
                    }
                    bp->damageMultiplier = bp->damageMultiplier - depth / maxDepth;
                    if (bp->damageMultiplier <= 0.0f)
                        return;
                    if (!allSolid && !weapType)
                    {
                        Vec3Sub(revBr.hitPos, br.hitPos, v34);
                        v33 = Vec3LengthSq(v34);
                        v32 = bullet_penetrationMinFxDist->current.value;
                        v7 = v32 * v32;
                        if (v7 < (float)v33)
                        {
                            if (!traceHit || (br.trace.surfaceFlags & 4) == 0)
                            {
                                contents = revBr.trace.contents;
                                v28 = weapDef->damage;
                                v29 = (revBr.trace.surfaceFlags & 0x1F00000) >> 20;
                                targetEntityNum = traceHitEntityId;
                                number = attacker->nextState.number;
                                if (!sv_clientSideBullets->current.enabled || !IsEntityAPlayer(localClientNum, targetEntityNum))
                                    CG_BulletHitEvent(
                                        localClientNum,
                                        number,
                                        targetEntityNum,
                                        weaponIndex,
                                        revBp.start,
                                        revBr.hitPos,
                                        bp->dir,
                                        v29,
                                        41,
                                        4u,
                                        v28,
                                        contents);
                            }
                            if (traceHit)
                            {
                                v22 = br.trace.contents;
                                v23 = weapDef->damage;
                                v24 = (br.trace.surfaceFlags & 0x1F00000) >> 20;
                                v25 = traceHitEntityId;
                                v26 = attacker->nextState.number;
                                if (!sv_clientSideBullets->current.enabled || !IsEntityAPlayer(localClientNum, v25))
                                    CG_BulletHitEvent(
                                        localClientNum,
                                        v26,
                                        v25,
                                        weaponIndex,
                                        bp->start,
                                        br.hitPos,
                                        br.trace.normal,
                                        v24,
                                        41,
                                        0,
                                        v23,
                                        v22);
                                DynEntCl_DynEntImpactEvent(
                                    localClientNum,
                                    attacker->nextState.number,
                                    bp->start,
                                    br.hitPos,
                                    weapDef->damage,
                                    0);
                            }
                        }
                    }
                }
                else if (traceHit && !br.trace.allsolid)
                {
                    Vec3Sub(lastHitPos, br.hitPos, v21);
                    v20 = Vec3LengthSq(v21);
                    v6 = bullet_penetrationMinFxDist->current.value * bullet_penetrationMinFxDist->current.value;
                    if (v6 < (float)v20)
                    {
                        traceHitEntityId = Trace_GetEntityHitId(&br.trace);
                        if (!weapType)
                        {
                            v15 = br.trace.contents;
                            v16 = weapDef->damage;
                            v17 = (br.trace.surfaceFlags & 0x1F00000) >> 20;
                            v18 = traceHitEntityId;
                            v19 = attacker->nextState.number;
                            if (!sv_clientSideBullets->current.enabled || !IsEntityAPlayer(localClientNum, traceHitEntityId))
                                CG_BulletHitEvent(
                                    localClientNum,
                                    v19,
                                    v18,
                                    weaponIndex,
                                    bp->start,
                                    br.hitPos,
                                    br.trace.normal,
                                    v17,
                                    41,
                                    0,
                                    v16,
                                    v15);
                            DynEntCl_DynEntImpactEvent(
                                localClientNum,
                                attacker->nextState.number,
                                bp->start,
                                br.hitPos,
                                weapDef->damage,
                                0);
                        }
                    }
                }
                if (!traceHit)
                    return;
            }
        }
    }
    else if (drawTracer)
    {
        CG_SpawnTracer(localClientNum, tracerStart, bp->end);
    }
}


char __cdecl BulletTrace(
    int32_t localClientNum,
    const BulletFireParams *bp,
    const WeaponDef *weapDef,
    const centity_s *attacker,
    BulletTraceResults *br,
    uint32_t lastSurfaceType)
{
    centity_s *Entity; // [esp+Ch] [ebp-10h]
    uint16_t hitEntId; // [esp+18h] [ebp-4h]

    iassert(bp);
    iassert(weapDef);
    iassert(attacker);
    iassert(br);
    bcassert(lastSurfaceType, SURF_TYPECOUNT);
    Com_Memset((uint32_t *)br, 0, 68);
    CG_LocationalTrace(&br->trace, (float*)bp->start, (float*)bp->end, bp->ignoreEntIndex, 0x2806831);
    if (br->trace.hitType == TRACE_HITTYPE_NONE)
        return 0;
    hitEntId = Trace_GetEntityHitId(&br->trace);
    if (hitEntId == ENTITYNUM_WORLD)
        Entity = 0;
    else
        Entity = CG_GetEntity(localClientNum, hitEntId);
    Vec3Lerp(bp->start, bp->end, br->trace.fraction, br->hitPos);
    if (Entity)
    {
        if ((Entity->nextState.eType == ET_PLAYER || Entity->nextState.eType == ET_PLAYER_CORPSE) && !br->trace.surfaceFlags)
            br->trace.surfaceFlags = 0x700000;
        br->ignoreHitEnt = ShouldIgnoreHitEntity(attacker->nextState.number, hitEntId);
    }
    br->depthSurfaceType = (br->trace.surfaceFlags & 0x1F00000) >> 20;
    if ((br->trace.surfaceFlags & 0x100) != 0)
    {
        br->depthSurfaceType = 0;
    }
    else if (!br->depthSurfaceType)
    {
        if (lastSurfaceType)
            br->depthSurfaceType = lastSurfaceType;
    }
    return 1;
}

#endif

bool __cdecl ShouldIgnoreHitEntity(int32_t attackerNum, int32_t hitEntNum)
{
    return hitEntNum == attackerNum;
}

bool __cdecl IsEntityAPlayer(int32_t localClientNum, uint32_t entityNum)
{
    centity_s *cent; // [esp+4h] [ebp-4h]

    cent = CG_GetEntity(localClientNum, entityNum);
    iassert(cent);
    if (!cent)
        return 0;
    return cent->nextState.eType == ET_PLAYER;
}

void __cdecl CG_BulletEndpos(
    int32_t randSeed,
    float spread,
    const float *start,
    float *end,
    float *dir,
    const float *forwardDir,
    const float *rightDir,
    const float *upDir,
    float maxRange)
{
    float v9; // [esp+Ch] [ebp-54h]
    float v10; // [esp+10h] [ebp-50h]
    float right; // [esp+54h] [ebp-Ch] BYREF
    float aimOffset; // [esp+58h] [ebp-8h]
    float up; // [esp+5Ch] [ebp-4h] BYREF

    iassert(!IS_NAN(spread));
    iassert(end);
    v10 = spread * 0.01745329238474369;
    v9 = tan(v10);
    aimOffset = v9 * maxRange;
    iassert(!IS_NAN(aimOffset));
    RandomBulletDir(randSeed, &right, &up);
    right = right * aimOffset;
    up = up * aimOffset;
    iassert(!IS_NAN(right));
    iassert(!IS_NAN(up));
    Vec3Mad(start, maxRange, forwardDir, end);
    iassert(!IS_NAN((end)[0]) && !IS_NAN((end)[1]) && !IS_NAN((end)[2]));
    Vec3Mad(end, right, rightDir, end);
    Vec3Mad(end, up, upDir, end);
    iassert(!IS_NAN((end)[0]) && !IS_NAN((end)[1]) && !IS_NAN((end)[2]));
    if (dir)
    {
        Vec3Sub(end, start, dir);
        Vec3Normalize(dir);
        iassert(!IS_NAN((dir)[0]) && !IS_NAN((dir)[1]) && !IS_NAN((dir)[2]));
    }
}

void __cdecl RandomBulletDir(int32_t randSeed, float *x, float *y)
{
    float v3; // [esp+8h] [ebp-14h]
    float sinT; // [esp+Ch] [ebp-10h]
    float theta; // [esp+10h] [ebp-Ch]
    float r; // [esp+14h] [ebp-8h]
    float cosT; // [esp+18h] [ebp-4h]

    iassert(x);
    iassert(y);
    theta = G_GoodRandomFloat(&randSeed) * 360.0;
    r = G_GoodRandomFloat(&randSeed);
    v3 = theta * 0.01745329238474369;
    cosT = cos(v3);
    sinT = sin(v3);
    *x = r * cosT;
    *y = r * sinT;
}

void __cdecl TakeClipOnlyWeaponIfEmpty(int32_t localClientNum, playerState_s *ps)
{
    if (BG_WeaponIsClipOnly(ps->weapon)
        && !ps->ammoclip[BG_ClipForWeapon(ps->weapon)]
        && !ps->ammo[BG_AmmoForWeapon(ps->weapon)]
            && !BG_GetWeaponDef(ps->weapon)->hasDetonator)
    {
        BG_TakePlayerWeapon(ps, ps->weapon, 0);
        CG_OutOfAmmoChange(localClientNum);
    }
}

void __cdecl CG_SpawnTracer(int32_t localClientNum, const float *pstart, const float *pend)
{
    int32_t v3; // [esp+8h] [ebp-60h]
    float *trBase; // [esp+24h] [ebp-44h]
    float dir[3]; // [esp+34h] [ebp-34h] BYREF
    float dist; // [esp+40h] [ebp-28h]
    const cg_s *cgameGlob; // [esp+44h] [ebp-24h]
    float start[3]; // [esp+48h] [ebp-20h] BYREF
    float end[3]; // [esp+54h] [ebp-14h] BYREF
    int32_t startTime; // [esp+60h] [ebp-8h]
    localEntity_s *le; // [esp+64h] [ebp-4h]

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    start[0] = pstart[0];
    start[1] = pstart[1];
    start[2] = pstart[2];

    end[0] = pend[0];
    end[1] = pend[1];
    end[2] = pend[2];

    Vec3Sub(end, start, dir);
    dist = Vec3Normalize(dir);
    le = CG_AllocLocalEntity(localClientNum);
    le->leType = LE_MOVING_TRACER;
    le->tracerClipDist = dist;
    if (cgameGlob->frametime)
        v3 = rand() % cgameGlob->frametime / 2;
    else
        v3 = 0;
    startTime = cgameGlob->time - v3;
    le->endTime = startTime + (int)(dist * 1000.0 / cg_tracerSpeed->current.value);
    le->pos.trType = TR_LINEAR;
    le->pos.trTime = startTime;
    trBase = le->pos.trBase;
    le->pos.trBase[0] = start[0];
    trBase[1] = start[1];
    trBase[2] = start[2];
    Vec3Scale(dir, cg_tracerSpeed->current.value, le->pos.trDelta);
    iassert(!IS_NAN((le->pos.trBase)[0]) && !IS_NAN((le->pos.trBase)[1]) && !IS_NAN((le->pos.trBase)[2]));
    iassert(!IS_NAN((le->pos.trDelta)[0]) && !IS_NAN((le->pos.trDelta)[1]) && !IS_NAN((le->pos.trDelta)[2]));
}

void __cdecl CG_DrawTracer(const float *start, const float *finish, const refdef_s *refdef)
{
    float v3; // [esp+0h] [ebp-54h]
    float diff[3]; // [esp+8h] [ebp-4Ch] BYREF
    float finishWidth; // [esp+14h] [ebp-40h] BYREF
    FxBeam beam; // [esp+18h] [ebp-3Ch] BYREF
    float startWidth; // [esp+50h] [ebp-4h] BYREF

    startWidth = cg_tracerWidth->current.value;
    finishWidth = cg_tracerWidth->current.value;
    ScaleTracer(start, finish, refdef->vieworg, &startWidth, &finishWidth);
    beam.begin[0] = *start;
    beam.begin[1] = start[1];
    beam.begin[2] = start[2];
    beam.end[0] = *finish;
    beam.end[1] = finish[1];
    beam.end[2] = finish[2];
    beam.beginColor.packed = -1;
    beam.endColor.packed = -1;
    beam.beginRadius = startWidth;
    beam.endRadius = finishWidth;
    beam.material = cgMedia.tracerMaterial;
    Vec3Sub(finish, start, diff);
    v3 = Vec3Length(diff);
    beam.segmentCount = (int)(v3 * 8.0 / cg_tracerScrewDist->current.value);
    beam.wiggleDist = cg_tracerScrewRadius->current.value;
    FX_Beam_Add(&beam);
}

void __cdecl ScaleTracer(
    const float *start,
    const float *finish,
    const float *viewOrg,
    float *startWidth,
    float *finishWidth)
{
    float v[4]; // [esp+28h] [ebp-30h] BYREF
    float diff[3]; // [esp+38h] [ebp-20h] BYREF
    float startDist; // [esp+44h] [ebp-14h]
    float finishDist; // [esp+48h] [ebp-10h]
    float tracerScale; // [esp+4Ch] [ebp-Ch]
    float tracerScaleDistRange; // [esp+50h] [ebp-8h]
    float tracerScaleMinDist; // [esp+54h] [ebp-4h]

    iassert(startWidth);
    iassert(finishWidth);
    iassert(cg_tracerScale);
    iassert(cg_tracerScaleMinDist);
    iassert(cg_tracerScaleDistRange);
    tracerScale = cg_tracerScale->current.value;
    tracerScaleMinDist = cg_tracerScaleMinDist->current.value;
    tracerScaleDistRange = cg_tracerScaleDistRange->current.value;
    if (tracerScale != 1.0)
    {
        Vec3Sub(viewOrg, start, diff);
        startDist = Vec3Length(diff);
        Vec3Sub(viewOrg, finish, v);
        finishDist = Vec3Length(v);
        if (tracerScaleMinDist != 0.0)
        {
            startDist = startDist - tracerScaleMinDist;
            finishDist = finishDist - tracerScaleMinDist;
        }
        if (startDist > 0.0)
            *startWidth = CalcTracerFinalScale(tracerScaleDistRange, startDist, tracerScale) * *startWidth;
        if (finishDist > 0.0)
            *finishWidth = CalcTracerFinalScale(tracerScaleDistRange, finishDist, tracerScale) * *finishWidth;
    }
}

double __cdecl CalcTracerFinalScale(float tracerScaleDistRange, float dist, float tracerScale)
{
    float v4; // [esp+0h] [ebp-Ch]
    float lerp; // [esp+8h] [ebp-4h]

    if (tracerScaleDistRange <= 0.0)
    {
        return tracerScale;
    }
    else
    {
        lerp = dist / tracerScaleDistRange;
        v4 = tracerScale * lerp;
        iassert(tracerScale > 1.0);
        // MyAssertHandler("c:\\trees\\cod3\\src\\universal\\com_math.h", 533, 0, "%s", "min < max");
        if (v4 >= 1.0)
        {
            if (tracerScale >= (double)v4)
                return (float)(tracerScale * lerp);
            else
                return tracerScale;
        }
        else
        {
            return (float)1.0;
        }
    }
}

cg_s *__cdecl CG_GetLocalClientGlobalsForEnt(int32_t localClientNum, int32_t entityNum)
{
    snapshot_s *nextSnap; // [esp+4h] [ebp-Ch]
    int32_t clientIndex; // [esp+Ch] [ebp-4h]
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    for (clientIndex = 0; clientIndex < 1; ++clientIndex)
    {
        if (CL_IsLocalClientActive(clientIndex))
        {
            nextSnap = cgameGlob->nextSnap;

            // KISAKFIX: MP `(otherFlags & 6) != 0 && ...` → SP IDA just `entityNum == clientNum`.
            if (entityNum == nextSnap->ps.clientNum)
                return cgameGlob;
        }
    }

    return 0;
}

void __cdecl CG_GetViewDirection(int32_t localClientNum, int32_t entityNum, float *forward, float *right, float *up)
{
    const cg_s *cgameGlob = CG_GetLocalClientGlobalsForEnt(localClientNum, entityNum);
    if (cgameGlob)
    {
        BG_GetPlayerViewDirection(&cgameGlob->predictedPlayerState, forward, right, up);
    }
    else
    {
#ifdef KISAK_MP
        const clientInfo_t *ci;
        uint32_t clientNum = CG_GetEntity(localClientNum, entityNum)->nextState.clientNum;

        bcassert(clientNum, MAX_CLIENTS);
        ci = &CG_GetLocalClientGlobals(localClientNum)->bgs.clientinfo[clientNum];

        iassert(ci->infoValid);
        AngleVectors(ci->playerAngles, forward, right, up);
#elif KISAK_SP
        AngleVectors(CG_GetEntity(localClientNum, entityNum)->pose.angles, forward, right, up);
#endif
    }
}

void __cdecl CG_CalcEyePoint(int32_t localClientNum, int32_t entityNum, float *eyePos)
{
    const cg_s *cgameGlob; // [esp+8h] [ebp-8h]
    centity_s *cent; // [esp+Ch] [ebp-4h]

    cgameGlob = CG_GetLocalClientGlobalsForEnt(localClientNum, entityNum);
    if (cgameGlob)
    {
        *eyePos = cgameGlob->refdef.vieworg[0];
        eyePos[1] = cgameGlob->refdef.vieworg[1];
        eyePos[2] = cgameGlob->refdef.vieworg[2];
    }
    else
    {
        cent = CG_GetEntity(localClientNum, entityNum);
        *eyePos = cent->nextState.lerp.pos.trBase[0];
        eyePos[1] = cent->nextState.lerp.pos.trBase[1];
        eyePos[2] = cent->nextState.lerp.pos.trBase[2];
        if (entityNum < 64)
        {
            if ((cent->nextState.lerp.eFlags & 8) != 0)
            {
                eyePos[2] = eyePos[2] + 11.0;
            }
            else if ((cent->nextState.lerp.eFlags & 4) != 0)
            {
                eyePos[2] = eyePos[2] + 40.0;
            }
            else
            {
                eyePos[2] = eyePos[2] + 60.0;
            }
        }
    }
}

void __cdecl CG_RandomEffectAxis(const float *forward, float *left, float *up)
{
    float scale; // [esp+0h] [ebp-38h]
    float degrees; // [esp+Ch] [ebp-2Ch]
    float v5; // [esp+1Ch] [ebp-1Ch]
    float v6; // [esp+20h] [ebp-18h]
    float v7; // [esp+24h] [ebp-14h]
    float dot; // [esp+28h] [ebp-10h]
    float point[3]; // [esp+2Ch] [ebp-Ch] BYREF

    iassert(forward);
    iassert(left);
    iassert(up);
    v5 = -forward[2];
    v6 = *forward;
    v7 = -forward[1];
    point[0] = v5;
    point[1] = v6;
    point[2] = v7;
    dot = Vec3Dot(point, forward);
    scale = -dot;
    Vec3Mad(point, scale, forward, point);
    degrees = random() * 360.0;
    RotatePointAroundVector(left, forward, point, degrees);
    Vec3Normalize(left);
    Vec3Cross(forward, left, up);
}

void __cdecl CG_ImpactEffectForWeapon(
    uint32_t weaponIndex,
    uint32_t surfType,
    char impactFlags,
    const FxEffectDef **outFx,
    snd_alias_list_t **outSnd)
{
    snd_alias_list_t *v5; // [esp+0h] [ebp-28h]
    snd_alias_list_t *v6; // [esp+4h] [ebp-24h]
    snd_alias_list_t *v7; // [esp+8h] [ebp-20h]
    snd_alias_list_t *v8; // [esp+Ch] [ebp-1Ch]
    snd_alias_list_t *v9; // [esp+10h] [ebp-18h]
    int32_t fleshType; // [esp+18h] [ebp-10h]
    int32_t fxType; // [esp+20h] [ebp-8h]
    const WeaponDef *weaponDef; // [esp+24h] [ebp-4h]

    weaponDef = BG_GetWeaponDef(weaponIndex);
    iassert(weaponDef);
    bcassert(surfType, SURF_TYPECOUNT);
    fxType = -1;
    *outSnd = 0;
    switch (weaponDef->impactType)
    {
    case IMPACT_TYPE_BULLET_SMALL:
        fxType = (impactFlags & 4) != 0;
        if ((impactFlags & 4) != 0)
            v9 = cgMedia.bulletExitSmallSound[surfType];
        else
            v9 = cgMedia.bulletHitSmallSound[surfType];
        *outSnd = v9;
        break;
    case IMPACT_TYPE_BULLET_LARGE:
        fxType = ((impactFlags & 4) != 0) + 2;
        if ((impactFlags & 4) != 0)
            v8 = cgMedia.bulletExitLargeSound[surfType];
        else
            v8 = cgMedia.bulletHitLargeSound[surfType];
        *outSnd = v8;
        break;
    case IMPACT_TYPE_BULLET_AP:
        fxType = ((impactFlags & 4) != 0) + 6;
        if ((impactFlags & 4) != 0)
            v7 = cgMedia.bulletExitAPSound[surfType];
        else
            v7 = cgMedia.bulletHitAPSound[surfType];
        *outSnd = v7;
        break;
    case IMPACT_TYPE_SHOTGUN:
        fxType = ((impactFlags & 4) != 0) + 4;
        if ((impactFlags & 4) != 0)
            v6 = cgMedia.shotgunExitSound[surfType];
        else
            v6 = cgMedia.shotgunHitSound[surfType];
        *outSnd = v6;
        break;
    case IMPACT_TYPE_GRENADE_BOUNCE:
        fxType = 8;
        if (weaponDef->bounceSound)
            v5 = weaponDef->bounceSound[surfType];
        else
            v5 = 0;
        *outSnd = v5;
        break;
    case IMPACT_TYPE_GRENADE_EXPLODE:
        fxType = 9;
        *outSnd = cgMedia.grenadeExplodeSound[surfType];
        break;
    case IMPACT_TYPE_ROCKET_EXPLODE:
        fxType = 10;
        *outSnd = cgMedia.rocketExplodeSound[surfType];
        break;
    case IMPACT_TYPE_PROJECTILE_DUD:
        fxType = 11;
        *outSnd = weaponDef->projDudSound;
        break;
    default:
        break;
    }
    if (fxType >= 0)
    {
        if (surfType == 7)
        {
            if ((impactFlags & 2) != 0)
                fleshType = (impactFlags & 1) != 0 ? 3 : 1;
            else
                fleshType = (impactFlags & 1) != 0 ? 2 : 0;
            *outFx = cgMedia.fx->table[fxType].flesh[fleshType];
        }
        else
        {
            *outFx = cgMedia.fx->table[fxType].nonflesh[surfType];
        }
    }
    else
    {
        *outFx = 0;
    }
}

void __cdecl CG_BulletHitEvent(
    int32_t localClientNum,
    int32_t sourceEntityNum,
    uint32_t targetEntityNum,
    uint32_t weaponIndex,
    float *startPos,
    float *position,
    const float *normal,
    uint32_t surfType,
    int32_t event,
    uint8_t eventParam,
    int32_t damage,
    __int16 hitContents)
{
#ifdef KISAK_MP
    char hasMuzzlePoint; // [esp+3h] [ebp-29h]
    float muzzle[3]; // [esp+10h] [ebp-1Ch] BYREF
    float exitDir[3]; // [esp+20h] [ebp-Ch] BYREF

    if (sv_clientSideBullets->current.enabled && IsEntityAPlayer(localClientNum, targetEntityNum))
    {
        iassert(!(eventParam & IMPACTEFFECT_EXIT));

        if (CalcMuzzlePoint(localClientNum, sourceEntityNum, muzzle, scr_const.tag_flash))
        {
            Vec3Sub(position, muzzle, exitDir);
            Vec3Normalize(exitDir);
            hasMuzzlePoint = 1;
        }
        else
        {
            hasMuzzlePoint = 0;
        }
        if (hasMuzzlePoint)
            CG_BulletHitEvent_Internal(
                localClientNum,
                sourceEntityNum,
                targetEntityNum,
                weaponIndex,
                startPos,
                position,
                exitDir,
                surfType,
                event,
                eventParam | 4,
                damage,
                hitContents);
    }
#endif
    CG_BulletHitEvent_Internal(
        localClientNum,
        sourceEntityNum,
        targetEntityNum,
        weaponIndex,
        startPos,
        position,
        normal,
        surfType,
        event,
        eventParam,
        damage,
        hitContents);
}

int32_t __cdecl CalcMuzzlePoint(int32_t localClientNum, int32_t entityNum, float *muzzle, uint32_t flashTag)
{
    double v6; // st7
    DObj_s *obj; // [esp+8h] [ebp-Ch]
    const cg_s *cgameGlob; // [esp+Ch] [ebp-8h]
    centity_s *cent; // [esp+10h] [ebp-4h]

    cgameGlob = CG_GetLocalClientGlobalsForEnt(localClientNum, entityNum);
    if (cgameGlob)
    {
        *muzzle = cgameGlob->refdef.vieworg[0];
        muzzle[1] = cgameGlob->refdef.vieworg[1];
        muzzle[2] = cgameGlob->refdef.vieworg[2];
        return 1;
    }
    else
    {
        cent = CG_GetEntity(localClientNum, entityNum);
        obj = Com_GetClientDObj(cent->nextState.number, localClientNum);
        if (obj)
        {
            if (CG_DObjGetWorldTagPos(&cent->pose, obj, flashTag, muzzle))
            {
                return 1;
            }
            else
            {
                *muzzle = cent->nextState.lerp.pos.trBase[0];
                muzzle[1] = cent->nextState.lerp.pos.trBase[1];
                muzzle[2] = cent->nextState.lerp.pos.trBase[2];
                if (entityNum < 64)
                {
                    Com_DPrintf(17, "No %s in CalcMuzzlePoint on entity %d.\n", SL_ConvertToString(flashTag), entityNum);
                    if ((cent->nextState.lerp.eFlags & 8) != 0)
                    {
                        muzzle[2] = muzzle[2] + 11.0;
                    }
                    else
                    {
                        if ((cent->nextState.lerp.eFlags & 4) != 0)
                            v6 = muzzle[2] + 40.0;
                        else
                            v6 = muzzle[2] + 60.0;
                        muzzle[2] = v6;
                    }
                }
                return 1;
            }
        }
        else
        {
            return 0;
        }
    }
}

void __cdecl CG_BulletHitEvent_Internal(
    int32_t localClientNum,
    int32_t sourceEntityNum,
    uint32_t targetEntityNum,
    uint32_t weaponIndex,
    float *startPos,
    float *position,
    const float *normal,
    uint32_t surfType,
    int32_t event,
    uint8_t eventParam,
    int32_t damage,
    __int16 hitContents)
{
    snapshot_s *nextSnap; // [esp+4h] [ebp-3Ch]
    snd_alias_list_t *hitSound; // [esp+Ch] [ebp-34h] BYREF
    cg_s *cgameGlob; // [esp+10h] [ebp-30h]
    int32_t time; // [esp+14h] [ebp-2Ch]
    const FxEffectDef *fx; // [esp+18h] [ebp-28h] BYREF
    float axis[3][3]; // [esp+1Ch] [ebp-24h] BYREF

    iassert(sourceEntityNum >= 0);
    iassert(sourceEntityNum != ENTITYNUM_NONE);
    iassert(position);
    iassert(normal);
    iassert(surfType >= 0 && surfType < SURF_TYPECOUNT);
    iassert(damage >= 0);
    iassert(localClientNum == 0);

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    CG_GetEntity(localClientNum, sourceEntityNum);
    fx = 0;
    hitSound = 0;

    if ((hitContents & 0x800) == 0)
        CG_ImpactEffectForWeapon(weaponIndex, surfType, eventParam, &fx, &hitSound);

    if (fx && !cg_blood->current.enabled && surfType == 7)
        fx = cgMedia.fxNoBloodFleshHit;

    if (hitSound)
        CG_PlaySoundAlias(localClientNum, ENTITYNUM_WORLD, position, hitSound);

    iassert(localClientNum == 0);

    time = cgameGlob->time;
    if (fx && (normal[0] != 0.0 || normal[1] != 0.0 || normal[2] != 0.0))
    {
        axis[0][0] = normal[0];
        axis[0][1] = normal[1];
        axis[0][2] = normal[2];
        CG_RandomEffectAxis(axis[0], axis[1], axis[2]);
        if (cg_marks_ents_player_only->current.enabled)
        {
            nextSnap = cgameGlob->nextSnap;
            // KISAKFIX: MP `(otherFlags & 6) == 0 || ...` → SP IDA just `sourceEntityNum != clientNum`.
            if (sourceEntityNum != nextSnap->ps.clientNum)
                targetEntityNum = ENTITYNUM_NONE;
        }
        FX_PlayOrientedEffectWithMarkEntity(localClientNum, fx, cgameGlob->time, position, axis, targetEntityNum);
    }
    BulletTrajectoryEffects(
        localClientNum,
        sourceEntityNum,
        startPos,
        position,
        surfType,
        scr_const.tag_flash,
        eventParam,
        damage);
}

void __cdecl BulletTrajectoryEffects(
    int32_t localClientNum,
    int32_t sourceEntityNum,
    float *startPos,
    float *position,
    int32_t surfType,
    uint32_t flashTag,
    uint8_t impactFlags,
    int32_t damage)
{
    float muzzle[3]; // [esp+0h] [ebp-Ch] BYREF

    iassert(sourceEntityNum >= 0);
    iassert(damage >= 0);

    if (CalcMuzzlePoint(localClientNum, sourceEntityNum, muzzle, flashTag))
    {
        if (ShouldSpawnTracer(localClientNum, sourceEntityNum))
            CG_SpawnTracer(localClientNum, muzzle, position);
        WhizbySound(localClientNum, muzzle, position);
        if ((impactFlags & 4) == 0
#ifdef KISAK_MP
            && !sv_clientSideBullets->current.enabled
#endif
            )
        {
            DynEntCl_DynEntImpactEvent(localClientNum, sourceEntityNum, startPos, position, damage, 0);
        }
    }
}

void __cdecl WhizbySound(int32_t localClientNum, const float *start, const float *end)
{
    float viewDelta[3]; // [esp+10h] [ebp-4Ch] BYREF
    float delta[3]; // [esp+1Ch] [ebp-40h] BYREF
    float dir[3]; // [esp+28h] [ebp-34h] BYREF
    float projPos[3]; // [esp+34h] [ebp-28h] BYREF
    float viewRadius; // [esp+40h] [ebp-1Ch]
    float dist; // [esp+44h] [ebp-18h]
    float viewDist; // [esp+48h] [ebp-14h]
    float minDist; // [esp+4Ch] [ebp-10h]
    float soundRadius; // [esp+50h] [ebp-Ch]
    const float *viewOrg; // [esp+54h] [ebp-8h]
    float maxDist; // [esp+58h] [ebp-4h]

    minDist = 64.0f;
    maxDist = -64.0f;
    soundRadius = 140.0f;
    viewOrg = CG_GetLocalClientGlobals(localClientNum)->refdef.vieworg;
    Vec3Sub(end, start, delta);
    Vec3NormalizeTo(delta, dir);
    dist = Vec3Dot(delta, dir);
    Vec3Sub(viewOrg, start, viewDelta);
    viewDist = Vec3Dot(viewDelta, dir);
    if (minDist <= (double)viewDist && dist >= viewDist - maxDist)
    {
        Vec3Mad(start, viewDist, dir, projPos);
        Vec3Sub(projPos, viewOrg, delta);
        viewRadius = Vec3Length(delta);
        if (soundRadius >= (double)viewRadius)
        {
            Vec3Mad(projPos, -16.0f, dir, projPos);
            CG_PlaySoundAlias(localClientNum, ENTITYNUM_WORLD, projPos, cgMedia.bulletWhizby);
        }
    }
}

bool __cdecl ShouldSpawnTracer(int32_t localClientNum, int32_t sourceEntityNum)
{
    snapshot_s *nextSnap; // [esp+8h] [ebp-8h]
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    nextSnap = cgameGlob->nextSnap;

    // KISAK_SP_VR_FIXED_SCOPE_SHARP_VIEW_AND_TRACER_V5
    // CoD4 deliberately hides tracers from its flat-screen scoped turret.
    // In VR that makes a valid hitscan shot look like sound-only firing, so
    // force one visible trajectory whenever its impact event reaches cgame.
    const bool vrFixedScopedTurretShot =
        VR_IsInitialized() &&
        CG_PlayerUsingScopedTurret(
            localClientNum) &&
        cgameGlob->predictedPlayerState.viewlocked_entNum ==
            sourceEntityNum;

    if (vrFixedScopedTurretShot)
    {
        static bool loggedVrFixedScopeTracer = false;

        if (!loggedVrFixedScopeTracer)
        {
            Com_Printf(
                0,
                "[VR][FIXED SCOPE] Enabled a visible tracer for "
                "the fixed rifle's hitscan impact event.\n");

            loggedVrFixedScopeTracer = true;
        }

        return 1;
    }

    if (cg_tracerChance->current.value <= 0.0)
        return 0;

    // KISAKFIX: MP `(otherFlags & 6) != 0 && ...` → SP IDA just `sourceEntityNum == clientNum`.
    if (sourceEntityNum == nextSnap->ps.clientNum)
        return 0;

    if (CG_PlayerUsingScopedTurret(localClientNum)
        && cgameGlob->predictedPlayerState.viewlocked_entNum == sourceEntityNum)
    {
        return 0;
    }

    return cg_tracerChance->current.value * 32768.0 > (double)rand();
}

void __cdecl CG_BulletHitClientEvent(
    int32_t localClientNum,
    int32_t sourceEntityNum,
    float *startPos,
    float *position,
    uint32_t surfType,
    int32_t event,
    int32_t damage)
{
    const char *v7; // eax

    iassert(sourceEntityNum >= 0);
    iassert(sourceEntityNum != ENTITYNUM_NONE);
    iassert(position);
    iassert(surfType >= 0 && surfType < SURF_TYPECOUNT);
    iassert(damage >= 0);

    if (event == EV_BULLET_HIT_CLIENT_SMALL)
    {
        CG_PlaySoundAlias(localClientNum, ENTITYNUM_WORLD, position, cgMedia.bulletHitSmallSound[surfType]);
    LABEL_19:
        BulletTrajectoryEffects(
            localClientNum,
            sourceEntityNum,
            startPos,
            position,
            surfType,
            scr_const.tag_flash,
            0,
            damage);
        return;
    }
    if (event == EV_BULLET_HIT_CLIENT_LARGE)
    {
        CG_PlaySoundAlias(localClientNum, ENTITYNUM_WORLD, position, cgMedia.bulletHitLargeSound[surfType]);
        goto LABEL_19;
    }
    if (!alwaysfails)
    {
        v7 = va("CG_BulletHitClientEvent: Unknown event [%d]\n", event);
        MyAssertHandler(".\\cgame\\cg_weapons.cpp", 4573, 0, v7);
    }
}

void __cdecl CG_MeleeBloodEvent(int32_t localClientNum, const centity_s *cent)
{
    int32_t weapon; // [esp+4h] [ebp-18h]
    snapshot_s *nextSnap; // [esp+8h] [ebp-14h]
    uint32_t dobjHandle; // [esp+10h] [ebp-Ch]

    iassert(cent);

    nextSnap = CG_GetLocalClientGlobals(localClientNum)->nextSnap;
    // KISAKFIX: MP `(otherFlags & 6) != 0 && ...` → SP IDA just `number == clientNum`.
    bool isPlayer = cent->nextState.number == nextSnap->ps.clientNum;

    iassert(isPlayer);

    dobjHandle = CG_WeaponDObjHandle(cent->nextState.weapon);
    weapon = cent->nextState.weapon;
    iassert(localClientNum == 0);
    if (cg_weaponsArray[0][weapon].knifeModel)
    {
        if (cg_blood->current.enabled)
            CG_PlayBoltedEffect(localClientNum, cgMedia.fxKnifeBlood, dobjHandle, scr_const.tag_knife_fx);
        else
            CG_PlayBoltedEffect(localClientNum, cgMedia.fxKnifeNoBlood, dobjHandle, scr_const.tag_knife_fx);
    }
}

void __cdecl CG_SetupWeaponDef(int32_t localClientNum)
{
#ifdef KISAK_MP
    char v1; // [esp+3h] [ebp-2225h]
    _BYTE *v2; // [esp+8h] [ebp-2220h]
    const char *v3; // [esp+Ch] [ebp-221Ch]
    _DWORD dst[129]; // [esp+10h] [ebp-2218h] BYREF
    const char *ConfigString; // [esp+214h] [ebp-2014h]
    int32_t iNumFiles; // [esp+218h] [ebp-2010h]
    _BYTE *v7; // [esp+21Ch] [ebp-200Ch]
    _BYTE v8[8196]; // [esp+220h] [ebp-2008h] BYREF

    memset((uint8_t *)dst, 0, 0x1FCu);
    iNumFiles = 0;
    ConfigString = CL_GetConfigString(localClientNum, CS_WEAPONFILES);
    v3 = ConfigString;
    v2 = v8;
    do
    {
        v1 = *v3;
        *v2++ = *v3++;
    } while (v1);
    v7 = v8;
    dst[iNumFiles++] = (_DWORD)v8;
    while (*v7)
    {
        if (*v7 == 32)
        {
            *v7++ = 0;
            if (*v7 && *v7 != 32)
            {
                if (iNumFiles >= 127)
                    break;
                dst[iNumFiles++] = (_DWORD)v7;
            }
        }
        else
        {
            ++v7;
        }
    }
    ParseWeaponDefFiles((const char **)dst, iNumFiles);
#elif KISAK_SP
    iassert(bg_lastParsedWeaponIndex > 0);
#endif
}

void __cdecl ParseWeaponDefFiles(const char **ppszFiles, int32_t iNumFiles)
{
    const char *name; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]

    for (i = 0; i < iNumFiles; ++i)
    {
        name = ppszFiles[i];
        if (BG_GetWeaponIndexForName(name, 0) != i + 1)
            Com_Error(ERR_DROP, "Weapon index mismatch for '%s'", name);
    }
}

uint32_t __cdecl ValidLatestPrimaryWeapIdx(uint32_t weaponIndex)
{
    WeaponDef *weapDef; // [esp+0h] [ebp-4h]
    uint32_t weaponIndexa; // [esp+Ch] [ebp+8h]

    if (!weaponIndex)
        return 0;
    weapDef = BG_GetWeaponDef(weaponIndex);
    if (weapDef->inventoryType == WEAPINVENTORY_PRIMARY)
        return weaponIndex;
    weaponIndexa = weapDef->altWeaponIndex;
    if (BG_GetWeaponDef(weaponIndexa)->inventoryType)
        return 0;
    else
        return weaponIndexa;
}

void __cdecl CG_SelectWeaponIndex(int32_t localClientNum, uint32_t weaponIndex)
{
    BOOL v2; // [esp+0h] [ebp-10h]
    uint32_t validLatest; // [esp+Ch] [ebp-4h]
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    Com_Printf(15, "CG_SelectWeaponIndex: localClientNum=%d weaponIndex=%d prevSelect=%d\n",
        localClientNum, weaponIndex, cgameGlob->weaponSelect);
    cgameGlob->weaponSelectTime = cgameGlob->time;
    if (cgameGlob->weaponSelect != weaponIndex)
    {
        v2 = weaponIndex && weaponIndex == BG_GetWeaponDef(cgameGlob->weaponSelect)->altWeaponIndex;
        validLatest = ValidLatestPrimaryWeapIdx(weaponIndex);
        if (validLatest)
            cgameGlob->weaponLatestPrimaryIdx = validLatest;
        cgameGlob->weaponSelect = weaponIndex;
        CG_MenuShowNotify(localClientNum, 1);
        if (!v2)
            CL_SetADS(localClientNum, 0);
    }
}

char __cdecl CG_ScopeIsOverlayed(int32_t localClientNum)
{
    float zoom; // [esp+8h] [ebp-4h] BYREF

    iassert(localClientNum == 0);
    if (clientUIActives[0].connectionState < CA_ACTIVE)
        return 0;

    if (CG_PlayerUsingScopedTurret(localClientNum))
        return 1;

    return CG_GetWeapReticleZoom(CG_GetLocalClientGlobals(localClientNum), &zoom);
}

int32_t __cdecl CG_PlayerTurretWeaponIdx(int32_t localClientNum)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    if ((cgameGlob->predictedPlayerState.eFlags & 0x300) == 0)
        return 0;

    if (cgameGlob->predictedPlayerState.viewlocked == PLAYERVIEWLOCK_NONE)
        return 0;

    iassert(cgameGlob->predictedPlayerState.viewlocked_entNum != ENTITYNUM_NONE);

    return CG_GetEntity(localClientNum, cgameGlob->predictedPlayerState.viewlocked_entNum)->nextState.weapon;
}

bool __cdecl CG_PlayerUsingScopedTurret(int32_t localClientNum)
{
    int32_t weapIdxTurret; // [esp+4h] [ebp-4h]

    weapIdxTurret = CG_PlayerTurretWeaponIdx(localClientNum);
    return weapIdxTurret && BG_GetWeaponDef(weapIdxTurret)->overlayMaterial != 0;
}

void CG_DisplayViewmodelAnim(int localClientNum)
{
    int ViewmodelWeaponIndex; // r4
    weaponInfo_s *LocalClientWeaponInfo; // r3

    iassert(localClientNum == 0);
    ViewmodelWeaponIndex = BG_GetViewmodelWeaponIndex(&cgArray[0].predictedPlayerState);
    if (ViewmodelWeaponIndex > 0)
    {
        LocalClientWeaponInfo = CG_GetLocalClientWeaponInfo(localClientNum, ViewmodelWeaponIndex);
        DObjDisplayAnim(LocalClientWeaponInfo->viewModelDObj, "");
    }
}

#ifdef KISAK_SP
void CG_SaveViewModelAnimTrees(SaveGame *save)
{
    MemoryFile *MemoryFile; // r27
    uint32_t v3; // r31
    weaponInfo_s *v4; // r30
    uint32_t NumWeapons; // [sp+50h] [-40h] BYREF

    iassert(save);
    NumWeapons = BG_GetNumWeapons();
    SaveMemory_SaveWrite(&NumWeapons, 4, save);
    MemoryFile = SaveMemory_GetMemoryFile(save);
    v3 = 1;
    if (NumWeapons > 1)
    {
        v4 = &cg_weaponsArray[0][1];
        do
        {
            bcassert(v3, ARRAY_COUNT(cg_weaponsArray[0])); // 0x80
            if (v4->hasAnimTree)
            {
                if (!v4->viewModelDObj)
                    Com_Error(ERR_DROP, "CG_SaveViewModelAnimTrees: viewmodel has no dobj");
                XAnimSaveAnimTree(v4->viewModelDObj, MemoryFile);
            }
            ++v3;
            ++v4;
        } while (v3 < NumWeapons);
    }
}

void CG_LoadViewModelAnimTrees(SaveGame *save, const playerState_s *ps)
{
    uint32_t NumWeapons; // r3
    int viewmodelIndex; // r11
    const char *ConfigString; // r3
    const char *v7; // r31
    XModel *v8; // r22
    MemoryFile *MemoryFile; // r23
    uint32_t v10; // r29
    weaponInfo_s *v11; // r31
    WeaponDef *WeaponDef; // r30
    XAnimTree_s *Tree; // r30
    uint32_t numWeapons; // [sp+50h] [-70h] BYREF

    iassert(save);
    iassert(ps);
    SaveMemory_LoadRead(&numWeapons, 4, save);
    iassert(numWeapons <= BG_GetNumWeapons());
    viewmodelIndex = ps->viewmodelIndex;
    if (viewmodelIndex <= 0)
    {
        v8 = 0;
    }
    else
    {
        ConfigString = CL_GetConfigString(0, viewmodelIndex + CS_MODELS);
        v7 = ConfigString;
        iassert(ConfigString && *ConfigString); // KISAK_AI: handModelName -> ConfigString
        // "handModelName && handModelName[0]"
        v8 = R_RegisterModel(v7);
    }
    MemoryFile = SaveMemory_GetMemoryFile(save);
    v10 = 1;
    if (numWeapons > 1)
    {
        v11 = &cg_weaponsArray[0][1];
        do
        {
            if (v11->hasAnimTree)
            {
                CG_RegisterWeapon(0, v10);
                if (!v11->viewModelDObj)
                {
                    if (v8)
                        ChangeViewmodelDobj(
                            0,
                            v10,
                            ps->weaponmodels[v10],
                            v8,
                            v11->gogglesModel,
                            v11->rocketModel,
                            v11->knifeModel,
                            1);
                    if (!v11->viewModelDObj)
                    {
                        WeaponDef = BG_GetWeaponDef(v10);
                        iassert(WeaponDef);
                        Com_Error(ERR_DROP, "CG_LoadViewModelAnimTrees: viewmodel '%s' has no dobj", WeaponDef->szInternalName);
                    }
                }
                Tree = DObjGetTree(v11->viewModelDObj);
                iassert(Tree); // KISAK_AI: animTree -> Tree
                // "animTree"
                XAnimClearTree(Tree);
                XAnimLoadAnimTree(v11->viewModelDObj, MemoryFile);
            }
            ++v10;
            ++v11;
        } while (v10 < numWeapons);
    }
}

void CG_ArchiveWeaponInfo(MemoryFile *memFile)
{
    BOOL IsWriting; // r22
    float numWeapons; // r30
    uint32_t NumWeapons; // r3
    int v5; // r28
    int *p_hasAnimTree; // r30
    const DObj_s *v7; // r3
    float v8[24]; // [sp+50h] [-60h] BYREF

    iassert(memFile);
    IsWriting = MemFile_IsWriting(memFile);
    if (IsWriting)
    {
        numWeapons = COERCE_FLOAT(BG_GetNumWeapons());
        v8[0] = numWeapons;
        MemFile_WriteData(memFile, 4, v8);
    }
    else
    {
        MemFile_ReadData(memFile, 4, (unsigned char*)v8);
        numWeapons = v8[0];
        iassert(LODWORD(numWeapons) <= BG_GetNumWeapons());
    }
    if (LODWORD(numWeapons) > 1)
    {
        v5 = LODWORD(numWeapons) - 1;
        p_hasAnimTree = &cg_weaponsArray[0][1].hasAnimTree;
        do
        {
            if (IsWriting)
            {
                v7 = (const DObj_s *)*(p_hasAnimTree - 11);
                *p_hasAnimTree = v7 && DObjGetTree(v7);
            }
            iassert(memFile);
            iassert(memFile->archiveProc);
            memFile->archiveProc(memFile, 4, (byte*)p_hasAnimTree);
            iassert(memFile->archiveProc);
            memFile->archiveProc(memFile, 4, (byte *)p_hasAnimTree - 1);
            --v5;
            p_hasAnimTree += 18;
        } while (v5);
    }
    iassert(memFile);
    iassert(memFile->archiveProc);
    memFile->archiveProc(memFile, 4, (byte*)&cgArray[0].prevViewmodelWeapon);
    iassert(memFile->archiveProc);
    memFile->archiveProc(memFile, 4, (byte *)&cgArray[0].weaponSelect);
    iassert(memFile->archiveProc);
    memFile->archiveProc(memFile, 4, (byte *)&cgArray[0].weaponSelectTime);
    iassert(memFile->archiveProc);
    memFile->archiveProc(memFile, 4, (byte *)&cgArray[0].equippedOffHand);
    iassert(memFile->archiveProc);
    memFile->archiveProc(memFile, 96, (byte *)cgArray[0].viewDamage);
    iassert(memFile->archiveProc);
    memFile->archiveProc(memFile, 4, (byte *)&cgArray[0].holdBreathTime);
    iassert(memFile->archiveProc);
    memFile->archiveProc(memFile, 4, (byte *)&cgArray[0].holdBreathInTime);
    iassert(memFile->archiveProc);
    memFile->archiveProc(memFile, 4, (byte *)&cgArray[0].holdBreathDelay);
    iassert(memFile->archiveProc);
    memFile->archiveProc(memFile, 4, (byte *)&cgArray[0].holdBreathFrac);
    v8[0] = cgArray[0].holdBreathFrac;
    iassert(!IS_NAN(cgArray[0].holdBreathFrac)); // KISAK_AI: *value -> cgArray[0].holdBreathFrac
    // "!IS_NAN(*value)"
}

#endif // KISAK_SP
