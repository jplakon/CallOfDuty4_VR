#ifndef KISAK_SP
#error This file is for SinglePlayer only
#endif

#include "cg_draw.h"
#include "vr/vr_openxr.h"
#include <stringed/stringed_hooks.h>
#include "cg_main.h"
#include "cg_servercmds.h"
#include "cg_newdraw.h"
#include <gfx_d3d/r_cinematic.h>
#include <ui/ui.h>
#include "cg_view.h"
#include <gfx_d3d/r_reflection_probe.h>
#include <gfx_d3d/r_rendercmds.h>
#include <chrono>
#include <cstddef>
#include <initializer_list>

// KISAK_VR_DEDICATED_SCOPE_CAMERA_V2
#ifdef KISAK_SP
void __cdecl
CG_AddDeferredPhysicalSniperViewWeaponToScene(
    int32_t localClientNum);
#endif

CenterPrint s_centerPrint[1];
ScreenBlur s_screenBlur[1];
ScreenFade s_screenFade[1];

// KISAK_SP_JAVELIN_HUD_DIAGNOSTICS
// KISAK_SP_JAVELIN_MENU_DIAGNOSTICS_DRAW
void VR_JavelinMenuDiagnosticSetActive(bool active);

static uint32_t s_vrJavelinHudDiagnosticSequence = 0;

static bool VR_JavelinHudDiagnosticActive()
{
    return
        VR_VerboseDiagnosticsEnabled() &&
        BG_GetViewmodelWeaponIndex(
               &cgArray[0].predictedPlayerState) == 7;
}

static void VR_JavelinHudDiagnostic(const char *stage)
{
    if (!VR_JavelinHudDiagnosticActive())
        return;

    Com_Printf(
        0,
        "[VR][JAVELIN][HUD] %u: %s.\n",
        ++s_vrJavelinHudDiagnosticSequence,
        stage);
}

void __cdecl TRACK_cg_draw()
{
    track_static_alloc_internal(s_centerPrint, 1028, "s_centerPrint", 9);
    track_static_alloc_internal(s_screenBlur, 28, "s_screenBlur", 9);
    track_static_alloc_internal(s_screenFade, 16, "s_screenFade", 9);
}

void __cdecl CG_CenterPrint(int localClientNum, const char *str)
{
    CenterPrint *v3; // r30
    const char *v4; // r3

    v3 = &s_centerPrint[localClientNum];
    v4 = SEH_LocalizeTextMessage(str, "Center Print", LOCMSG_SAFE);
    I_strncpyz(s_centerPrint[localClientNum].text, v4, 1024);
    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    v3->time = cgArray[0].time;
}

void __cdecl CG_DrawCenterString(
    int localClientNum,
    const rectDef_s *rect,
    Font_s *font,
    double fontscale,
    float *color,
    int textStyle)
{
    CenterPrint *centerPrint;
    int time;
    float *fadeColor;
    float x;
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    centerPrint = &s_centerPrint[localClientNum];
    time = centerPrint->time;
    if (time && !cg_paused->current.integer)
    {
        fadeColor = CG_FadeColor(
            cgameGlob->time,
            time,
            (int)(cg_centertime->current.value * 1000.0f),
            100);
        if (fadeColor)
        {
            Vec4Mul(color, fadeColor, color);
            x = rect->x - (double)SnapFloatToInt(UI_TextWidth(centerPrint->text, 0, font, fontscale) * 0.5f);
            UI_DrawText(
                &scrPlaceView[localClientNum],
                centerPrint->text,
                0x7FFFFFFF,
                font,
                x,
                rect->y,
                rect->horzAlign,
                rect->vertAlign,
                fontscale,
                color,
                textStyle);
        }
        else
        {
            centerPrint->time = 0;
        }
    }
}

int __cdecl CG_DrawFriendlyFire(const cg_s *cgameGlob)
{
    const playerState_s *p_predictedPlayerState; // r31
    double fWeaponPosFrac; // fp30
    int weapFlags; // r11
    unsigned int ViewmodelWeaponIndex; // r3
    Material *v9; // r4
    const float *v10; // r3
    float crossX; // [sp+60h] [-40h] BYREF
    float crossY; // [sp+64h] [-3Ch] BYREF
    float width; // [sp+68h] [-38h] BYREF
    float height; // [sp+6Ch] [-34h] BYREF

    p_predictedPlayerState = &cgameGlob->predictedPlayerState;
    fWeaponPosFrac = cgameGlob->predictedPlayerState.fWeaponPosFrac;
    if ((cgameGlob->predictedPlayerState.eFlags & 0x20000) != 0)
        return 0;
    weapFlags = cgameGlob->predictedPlayerState.weapFlags;
    if ((weapFlags & 8) == 0)
        return 0;
    if ((weapFlags & 0x200) != 0)
        return 0;
    if (cg_paused->current.integer)
        return 0;
    ViewmodelWeaponIndex = BG_GetViewmodelWeaponIndex(&cgameGlob->predictedPlayerState);
    if (BG_GetWeaponDef(ViewmodelWeaponIndex)->overlayMaterial)
    {
        if (p_predictedPlayerState->fWeaponPosFrac > 0.0)
            return 0;
    }
    CG_CalcCrosshairPosition(cgameGlob, &crossX, &crossY);
    crossX = crossX * (float)fWeaponPosFrac;
    crossY = crossY * (float)fWeaponPosFrac;
    height = 40.0f;
    width = 40.0f;
    ScrPlace_ApplyRect(&scrPlaceFull, &crossX, &crossY, &width, &height, 2, 2);

    crossX = -((width * 0.5f) - crossX);
    crossY = -((height * 0.5f) - crossY);

    CL_DrawStretchPicPhysical(crossX, crossY, width, height, 0.0, 0.0, 1.0, 1.0, 0, cgMedia.friendlyFireMaterial);

    return 1;
}

static int lastTime;
void __cdecl CG_DrawFlashFade(int localClientNum)
{
    ScreenFade *fade;
    float alpha;
    float alphaCurrent;
    float a;
    int height;
    int width;
    float aspect;
    float color[4];

    iassert(localClientNum == 0);

    fade = &s_screenFade[localClientNum];
    if (fade->startTime + fade->duration >= cgArray[0].time)
    {
        if (fade->alphaCurrent == fade->alpha)
            goto LABEL_12;

        int now = Sys_Milliseconds();
        int deltaMS = now - lastTime;
        lastTime = now;
        if ((unsigned int)(deltaMS - 1) > 498u)
            goto LABEL_12;

        alphaCurrent = fade->alphaCurrent;
        alpha = fade->alpha;
        const float fadeStep = (float)deltaMS / (float)fade->duration;
        if (alphaCurrent <= alpha)
        {
            fade->alphaCurrent = fadeStep + alphaCurrent;
            if (fade->alphaCurrent <= alpha)
                goto LABEL_12;
        }
        else
        {
            fade->alphaCurrent = alphaCurrent - fadeStep;
            if (fade->alphaCurrent >= alpha)
                goto LABEL_12;
        }
    }
    else
    {
        alpha = fade->alpha;
    }

    fade->alphaCurrent = alpha;
LABEL_12:
    a = fade->alphaCurrent;
    if (a > 0.0f)
    {
        color[0] = 0.0f;
        color[1] = 0.0f;
        color[2] = 0.0f;
        color[3] = a;
        CL_GetScreenDimensions(&width, &height, &aspect);
        UI_FillRectPhysical(0.0, 0.0, width, height, color);
    }
}

// local variable allocation has failed, the output may be wrong!
//int __cdecl CG_CheckPlayerMovement(
//    __int64 newCmd,
//    __int64 a2,
//    __int64 a3,
//    __int64 a4,
//    __int64 a5,
//    __int64 a6,
//    __int64 a7,
//    __int64 a8,
//    __int64 a9,
//    __int64 a10,
//    __int64 a11,
//    __int64 a12,
//    __int64 a13,
//    __int64 a14,
//    int a15,
//    int a16,
//    int a17,
//    __int16 a18)
//{
//    __int64 *v18; // r11
//    int v19; // r7
//    int v20; // r9
//    int result; // r3
//
//    v18 = &a7;
//    a11 = a3;
//    a14 = a4;
//    LODWORD(a4) = &a15;
//    LODWORD(a3) = (char *)&a8 + 4;
//    a7 = newCmd;
//    a8 = *(__int64 *)((char *)&a2 + 4);
//    a9 = a2;
//    a10 = *(__int64 *)((char *)&a3 + 4);
//    a12 = *(__int64 *)((char *)&a4 + 4);
//    do
//    {
//        v19 = *(unsigned __int8 *)a4;
//        v20 = *(unsigned __int8 *)v18 - v19;
//        if (*(unsigned __int8 *)v18 != v19)
//            break;
//        v18 = (__int64 *)((char *)v18 + 1);
//        LODWORD(a4) = a4 + 1;
//    } while (v18 != (__int64 *)((char *)&a8 + 4));
//    if (v20)
//        return 1;
//    if (HIBYTE(a18))
//        return 1;
//    result = 0;
//    if ((_BYTE)a18)
//        return 1;
//    return result;
//}

int __cdecl CG_CheckPlayerMovement(usercmd_s oldCmd, usercmd_s newCmd)
{
    if (memcmp(oldCmd.angles, newCmd.angles, 20))
        return 1;

    return newCmd.forwardmove || newCmd.rightmove;
}

int __cdecl CG_CheckPlayerStanceChange(int localClientNum, __int16 newButtons, __int16 changedButtons)
{
    if ((changedButtons & 0x1300) != 0)
    {
        CG_MenuShowNotify(localClientNum, 3);
        return 1;
    }
    else
    {
        if ((newButtons & 0x1300) != 0)
            CG_MenuShowNotify(localClientNum, 3);
        return 0;
    }
}

int __cdecl CG_CheckPlayerTryReload(int localClientNum, char buttons)
{
    int result; // r3

    if ((buttons & 0x30) == 0)
        return 0;
    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    if ((cgArray[0].predictedPlayerState.pm_flags & 4) != 0)
        return 0;
    result = 1;
    if ((cgArray[0].predictedPlayerState.eFlags & 0x300) != 0)
        return 0;
    return result;
}

int __cdecl CG_CheckPlayerFireNonTurret(int localClientNum, char buttons)
{
    int result; // r3

    if ((buttons & 1) == 0)
        return 0;
    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    result = 1;
    if ((cgArray[0].predictedPlayerState.eFlags & 0x300) != 0)
        return 0;
    return result;
}

int __cdecl CG_CheckPlayerWeaponUsage(int localClientNum, char buttons)
{
    int result; // r3

    if (CG_CheckPlayerFireNonTurret(localClientNum, buttons)
        || (result = CG_CheckPlayerTryReload(localClientNum, buttons)) != 0)
    {
        CG_MenuShowNotify(localClientNum, 1);
        return 1;
    }
    return result;
}

int __cdecl CG_CheckPlayerOffHandUsage(int localClientNum, __int16 buttons)
{
    if ((buttons & 0xC000) == 0)
        return 0;
    CG_MenuShowNotify(localClientNum, 4);
    return 1;
}

unsigned int __cdecl CG_CheckPlayerMiscInput(int buttons)
{
    return buttons & 0xFFFFECFF;
}

void __cdecl CG_CheckForPlayerInput(int localClientNum)
{
    usercmd_s v1; // [esp-40h] [ebp-9Ch] BYREF
    usercmd_s v2; // [esp-20h] [ebp-7Ch] BYREF
    usercmd_s oldCmd; // [esp+8h] [ebp-54h] BYREF
    int32_t oldCmdIndex; // [esp+28h] [ebp-34h]
    usercmd_s newCmd; // [esp+2Ch] [ebp-30h] BYREF
    int32_t newInput; // [esp+50h] [ebp-Ch]
    int32_t changedButtons; // [esp+54h] [ebp-8h]
    int32_t newCmdIndex; // [esp+58h] [ebp-4h]

    newCmdIndex = CL_GetCurrentCmdNumber(localClientNum);
    if (newCmdIndex > 1)
    {
        oldCmdIndex = newCmdIndex - 1;
        CL_GetUserCmd(localClientNum, newCmdIndex - 1, &oldCmd);
        CL_GetUserCmd(localClientNum, newCmdIndex, &newCmd);
        changedButtons = newCmd.buttons ^ oldCmd.buttons;
        memcpy(&v2, &newCmd, sizeof(v2));
        memcpy(&v1, &oldCmd, sizeof(v1));
        newInput = CG_CheckPlayerMovement(v1, v2);
        if (CG_CheckPlayerWeaponUsage(localClientNum, newCmd.buttons))
            newInput = 1;
        if (CG_CheckPlayerOffHandUsage(localClientNum, newCmd.buttons))
            newInput = 1;
        if (CG_CheckPlayerStanceChange(localClientNum, newCmd.buttons, changedButtons))
            newInput = 1;
        if (!newInput)
            newInput = CG_CheckPlayerMiscInput(changedButtons) != 0;
        if (newInput)
            CG_MenuShowNotify(localClientNum, 2);
    }
#if 0
    int CurrentCmdNumber; // r3
    int v3; // r30
    int buttons; // r30
    int changedButtons; // r28
    int newInput; // r29
    _BYTE v7[48]; // [sp+58h] [-D8h] BYREF
    usercmd_s v8; // [sp+90h] [-A0h] BYREF
    usercmd_s weaponMdlName; // [sp+D0h] [-60h] BYREF

    CurrentCmdNumber = CL_GetCurrentCmdNumber(localClientNum);
    v3 = CurrentCmdNumber;
    if (CurrentCmdNumber > 1)
    {
        CL_GetUserCmd(localClientNum, CurrentCmdNumber - 1, &v8);
        CL_GetUserCmd(localClientNum, v3, &weaponMdlName);
        buttons = weaponMdlName.buttons;
        changedButtons = weaponMdlName.buttons ^ v8.buttons;
        memcpy(v7, weaponMdlName.angles, sizeof(v7));
        newInput = CG_CheckPlayerMovement(*(usercmd_s *)v8.angles[0], *(usercmd_s *)v8.angles[2]);
        if (CG_CheckPlayerWeaponUsage(localClientNum, buttons))
            newInput = 1;
        if ((weaponMdlName.buttons & 0xC000) != 0)
        {
            CG_MenuShowNotify(localClientNum, 4);
            newInput = 1;
        }
        if ((changedButtons & 0x1300) != 0)
        {
            CG_MenuShowNotify(localClientNum, 3);
            CG_MenuShowNotify(localClientNum, 2);
        }
        else
        {
            if ((weaponMdlName.buttons & 0x1300) != 0)
                CG_MenuShowNotify(localClientNum, 3);
            if (!newInput)
                newInput = CG_CheckPlayerMiscInput(changedButtons) != 0;
            if (newInput)
                CG_MenuShowNotify(localClientNum, 2);
        }
    }
#endif
}

void __cdecl CG_CheckHudHealthDisplay(int localClientNum)
{
    __int64 v2; // r11

    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    if (CG_CalcPlayerHealth(&cgArray[0].nextSnap->ps) >= (double)hud_health_startpulse_injured->current.value)
    {
        if (hud_fade_healthbar->current.value != 0.0 && cgArray[0].healthFadeTime)
        {
            int elapsed = cgArray[0].time - cgArray[0].healthFadeTime;
            if ((float)elapsed > hud_fade_healthbar->current.value * 1000.0f)
            {
                Menus_HideByName(&cgDC, "Health");
                cgArray[0].healthFadeTime = 0;
            }
        }
    }
    else
    {
        CG_MenuShowNotify(localClientNum, 0);
    }
}

void __cdecl CG_CheckHudAmmoDisplay(int localClientNum)
{
    __int64 v2; // r11

    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    if (CG_CheckPlayerForLowAmmo(cgArray) || CG_CheckPlayerForLowClip(cgArray))
        CG_MenuShowNotify(localClientNum, 1);
    if (hud_fade_ammodisplay->current.value != 0.0 && cgArray[0].ammoFadeTime)
    {
        int elapsed = cgArray[0].time - cgArray[0].ammoFadeTime;
        if ((float)elapsed > hud_fade_ammodisplay->current.value * 1000.0f)
        {
            Menus_HideByName(&cgDC, "weaponinfo");
            Menus_HideByName(&cgDC, "weaponinfo_lowdef");
            cgArray[0].ammoFadeTime = 0;
        }
    }
}

void __cdecl CG_CheckHudCompassDisplay(int localClientNum)
{
    const dvar_s *v1; // r11
    __int64 v2; // r9

    v1 = hud_fade_compass;
    if (hud_fade_compass->current.value != 0.0)
    {
        if (localClientNum)
        {
            MyAssertHandler(
                "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
                910,
                0,
                "%s\n\t(localClientNum) = %i",
                "(localClientNum == 0)",
                localClientNum);
            v1 = hud_fade_compass;
        }
        if (cgArray[0].compassFadeTime)
        {
            int elapsed = cgArray[0].time - cgArray[0].compassFadeTime;
            if ((float)elapsed > v1->current.value * 1000.0f)
            {
                cgArray[0].compassFadeTime = 0;
                Menus_HideByName(&cgDC, "Compass");
            }
        }
    }
}

void __cdecl CG_CheckHudStanceDisplay(int localClientNum)
{
    int eFlags; // r11
    __int64 v3; // r11

    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    eFlags = cgArray[0].nextSnap->ps.eFlags;
    if ((eFlags & 8) != 0 && (eFlags & 0x100) != 0 || (eFlags & 4) != 0 && (eFlags & 0x200) != 0)
        CG_MenuShowNotify(localClientNum, 3);
    if (hud_fade_stance->current.value != 0.0 && cgArray[0].stanceFadeTime)
    {
        int elapsed = cgArray[0].time - cgArray[0].stanceFadeTime;
        if ((float)elapsed > hud_fade_stance->current.value * 1000.0f)
        {
            Menus_HideByName(&cgDC, "stance");
            cgArray[0].stanceFadeTime = 0;
        }
    }
}

void __cdecl CG_CheckHudSprintDisplay(int localClientNum)
{
    playerState_s *p_ps; // r30
    __int64 v3; // r11
    int v4; // [sp+50h] [-30h]

    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    p_ps = &cgArray[0].nextSnap->ps;
    if (cgArray[0].nextSnap->ps.pm_type != 5)
    {
        v4 = (int)(float)(BG_GetWeaponDef(cgArray[0].nextSnap->ps.weapon)->sprintDurationScale
            * (float)(player_sprintTime->current.value * (float)1000.0));
        if (PM_GetSprintLeft(p_ps, cgArray[0].time) < v4)
            CG_MenuShowNotify(localClientNum, 6);
    }
    if (p_ps->pm_type != 5
        && cgArray[0].predictedPlayerState.sprintState.lastSprintStart > cgArray[0].predictedPlayerState.sprintState.lastSprintEnd)
    {
        CG_MenuShowNotify(localClientNum, 6);
    }
    if (hud_fade_sprint->current.value != 0.0 && cgArray[0].sprintFadeTime)
    {
        int elapsed = cgArray[0].time - cgArray[0].sprintFadeTime;
        // KISAKTODO: original compared against hud_fade_stance, not hud_fade_sprint — verify
        if ((float)elapsed > hud_fade_stance->current.value * 1000.0f)
        {
            Menus_HideByName(&cgDC, "sprintMeter");
            cgArray[0].sprintFadeTime = 0;
        }
    }
}

void __cdecl CG_CheckHudOffHandDisplay(int localClientNum)
{
    const dvar_s *v1; // r11
    __int64 v2; // r9

    v1 = hud_fade_offhand;
    if (hud_fade_offhand->current.value != 0.0)
    {
        if (localClientNum)
        {
            MyAssertHandler(
                "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
                910,
                0,
                "%s\n\t(localClientNum) = %i",
                "(localClientNum == 0)",
                localClientNum);
            v1 = hud_fade_offhand;
        }
        if (cgArray[0].offhandFadeTime)
        {
            int elapsed = cgArray[0].time - cgArray[0].offhandFadeTime;
            if ((float)elapsed > v1->current.value * 1000.0f)
            {
                cgArray[0].offhandFadeTime = 0;
                Menus_HideByName(&cgDC, "offhandinfo");
            }
        }
    }
}

void __cdecl CG_CheckHudObjectiveDisplay(int localClientNum)
{
    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    if (cgArray[0].showScores)
    {
        Menus_ShowByName(&cgDC, "objectiveinfo");
    }
    else if (cgArray[0].time - cgArray[0].scoreFadeTime > 300)
    {
        Menus_HideByName(&cgDC, "objectiveinfo");
    }
}

void __cdecl CG_CheckTimedMenus(int localClientNum)
{
    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    if (cgArray[0].voiceTime && cgArray[0].time - cgArray[0].voiceTime > 2500)
    {
        Menus_CloseByName(&cgDC, "voiceMenu");
        cgArray[0].voiceTime = 0;
    }
    CG_CheckForPlayerInput(localClientNum);
    CG_CheckHudHealthDisplay(localClientNum);
    CG_CheckHudAmmoDisplay(localClientNum);
    CG_CheckHudCompassDisplay(localClientNum);
    CG_CheckHudStanceDisplay(localClientNum);
    CG_CheckHudOffHandDisplay(localClientNum);
    CG_CheckHudObjectiveDisplay(localClientNum);
    CG_CheckHudSprintDisplay(localClientNum);
}

void __cdecl CG_Blur(
    int localClientNum,
    int time,
    double endBlur,
    BlurTime timeType,
    BlurTime priority,
    BlurPriority a6)
{
    double blurRadius; // fp1
    ScreenBlur *v12; // r31
    int v13; // r3

    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    if (time < 0)
        MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_draw.cpp", 592, 0, "%s", "time >= 0");
    if (endBlur < 0.0)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_draw.cpp",
            593,
            0,
            "%s\n\t(endBlur) = %g",
            HIDWORD(endBlur),
            LODWORD(endBlur));
    blurRadius = cgArray[0].refdef.blurRadius;
    if (cgArray[0].refdef.blurRadius < 0.0)
    {
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_draw.cpp",
            594,
            0,
            "%s\n\t(cgameGlob->refdef.blurRadius) = %g",
            "(cgameGlob->refdef.blurRadius >= 0)",
            blurRadius);
        blurRadius = cgArray[0].refdef.blurRadius;
    }
    v12 = &s_screenBlur[localClientNum];
    if (a6 >= v12->priority)
    {
        if (priority == BLUR_TIME_ABSOLUTE)
        {
            v13 = Sys_Milliseconds();
            blurRadius = cgArray[0].refdef.blurRadius;
        }
        else
        {
            v13 = cgArray[0].time;
        }
        v12->start = blurRadius;
        v12->end = endBlur;
        v12->timeStart = v13;
        v12->time = priority;
        v12->priority = a6;
        v12->timeEnd = v13 + time;
    }
}

void __cdecl CG_ClearBlur(int localClientNum)
{
    ScreenBlur *v1; // r11

    v1 = &s_screenBlur[localClientNum];
    v1->start = 0.0;
    v1->timeStart = 0;
    v1->end = 0.0;
    v1->timeEnd = 0;
    v1->radius = 0.0;
    v1->priority = BLUR_PRIORITY_NONE;
}

float __cdecl CG_GetBlurRadius(int localClientNum)
{
    double radius; // fp1

    radius = s_screenBlur[localClientNum].radius;
    return *((float *)&radius + 1);
}

void __cdecl CG_ScreenBlur(int localClientNum)
{
    ScreenBlur *scrBlur;
    int time;
    int timeStart;
    int timeEnd;
    float t;
    float end;
    float blur;
    cg_s *cgameGlob;

    cgameGlob = CG_GetLocalClientGlobals(localClientNum);
    scrBlur = &s_screenBlur[localClientNum];
    if (scrBlur->time == BLUR_TIME_ABSOLUTE)
        time = Sys_Milliseconds();
    else
        time = cgameGlob->time;
    timeStart = scrBlur->timeStart;
    if (timeStart)
    {
        iassert(scrBlur->timeStart <= time);
        iassert(scrBlur->timeStart <= scrBlur->timeEnd);
        timeEnd = scrBlur->timeEnd;
        if (time >= timeEnd)
        {
            end = scrBlur->end;
            blur = end;
            scrBlur->timeStart = 0;
            scrBlur->timeEnd = 0;
            scrBlur->priority = BLUR_PRIORITY_NONE;
        }
        else
        {
            iassert(scrBlur->timeEnd - scrBlur->timeStart > 0);
            t = (float)(time - scrBlur->timeStart) / (float)(scrBlur->timeEnd - scrBlur->timeStart);
            iassert(t >= 0.0f && t <= 1.0f);
            end = scrBlur->end;
            blur = ((1.0f - t) * scrBlur->start) + (scrBlur->end * t);
        }

        iassert(blur >= I_fmin(scrBlur->start, scrBlur->end) && blur <= I_fmax(scrBlur->start, scrBlur->end));

        scrBlur->radius = blur;
    }
}

void __cdecl CG_Fade(int localClientNum, int r, int g, int b, int a, int startTime, int duration)
{
    ScreenFade *v6; // r31

    v6 = &s_screenFade[localClientNum];
    v6->startTime = startTime;
    v6->duration = duration;
    v6->alpha = (float)a * 0.0039215689f;

    iassert(localClientNum == 0);
    //if (localClientNum)
    //    MyAssertHandler(
    //        "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
    //        910,
    //        0,
    //        "%s\n\t(localClientNum) = %i",
    //        "(localClientNum == 0)",
    //        localClientNum);
    if (v6->duration + v6->startTime <= cgArray[0].time)
        v6->alphaCurrent = v6->alpha;
}

void CG_DrawMiniConsole()
{
    if (cg_minicon->current.enabled)
        Con_DrawMiniConsole(0, 2, 4, 1.0);
}

void CG_DrawErrorMessages()
{
    // KISAK_SP_VR_CONSOLE_ERROR_OVERLAY_SUPPRESSION_V100
    // The stock error message window is useful on a flat development screen,
    // but in VR it is enlarged and duplicated into the shared HUD command
    // list.  Suppress only that presentation layer after VR initializes;
    // Com_Printf, console.log, the full console, and fatal dialogs remain.
    if (VR_IsInitialized())
    {
        static bool loggedVrErrorOverlaySuppression = false;

        if (!loggedVrErrorOverlaySuppression)
        {
            Com_Printf(
                0,
                "[VR][HUD][V100] Suppressed the in-headset console "
                "error overlay; messages remain in the console and "
                "console.log.\n");
            loggedVrErrorOverlaySuppression = true;
        }
        return;
    }

    Con_DrawErrors(0, 2, 300, 1.0);
}

void __cdecl CG_DrawFadeInCinematic(int localClientNum)
{
    char v2; // r31

    v2 = 0;
    if (R_Cinematic_IsNextReady())
    {
        R_Cinematic_StartNextPlayback();
        v2 = 1;
    }
    if (R_Cinematic_IsFinished())
        R_Cinematic_StopPlayback();
    if (R_Cinematic_IsStarted() || R_Cinematic_IsPending())
        v2 = 1;
    if (v2)
    {
        if (cg_cinematicFullscreen->current.enabled)
            R_Cinematic_DrawStretchPic_Letterboxed();
    }
}

void __cdecl CG_DrawFriendOverlay(int localClientNum)
{
    cg_s *LocalClientGlobals; // r30
    const char *ConfigString; // r3
    const char *friendName; // r25
    int v6; // r4
    const dvar_s *textColor; // r11
    const dvar_s *outlineColor; // r10
    const ScreenPlacement *place; // r31
    Font_s *FontHandle; // r27
    int v11; // r7
    double v12; // fp8
    double v13; // fp7
    double v14; // fp6
    double v15; // fp5
    double v16; // fp4
    const char *v17; // r3
    const char *v18; // r28
    float color[4];
    float glowColor[4];

    if (!cg_paused->current.integer && !CG_Flashbanged(localClientNum))
    {
        LocalClientGlobals = CG_GetLocalClientGlobals(localClientNum);
        ConfigString = CL_GetConfigString(localClientNum, CS_FRIEND_OVERLAY);
        if (ConfigString)
        {
            if (*ConfigString && I_stricmp(ConfigString, "none") && !hud_missionFailed->current.enabled)
            {
                friendName = SEH_LocalizeTextMessage(ConfigString, "Friend Name", LOCMSG_SAFE);
                v6 = !friendlyNameFontObjective->current.enabled ? 0 : 6;
                if ((LocalClientGlobals->predictedPlayerState.weapFlags & 0x10) != 0)
                {
                    textColor = hostileNameFontColor;
                    outlineColor = hostileNameFontGlowColor;
                }
                else
                {
                    textColor = friendlyNameFontColor;
                    outlineColor = friendlyNameFontGlowColor;
                }
                color[0] = textColor->current.value; // name color (friendly or hostile)
                color[1] = textColor->current.vector[1];
                color[2] = textColor->current.vector[2];
                color[3] = textColor->current.vector[3];
                glowColor[0] = outlineColor->current.value; // name glow color
                glowColor[1] = outlineColor->current.vector[1];
                glowColor[2] = outlineColor->current.vector[2];
                glowColor[3] = outlineColor->current.vector[3];
                place = &scrPlaceView[localClientNum];
                FontHandle = UI_GetFontHandle(place, v6, friendlyNameFontSize->current.value);
                UI_DrawTextWithGlow(
                    place,
                    friendName,
                    0x7FFFFFFF,
                    FontHandle,
                    25.0,      // x
                    -2.0,      // y
                    2,         // horzAlign
                    2,         // vertAlign
                    friendlyNameFontSize->current.value, // scale
                    color,     // name color
                    3,         // style
                    glowColor, // glow color
                    false,     // subtitle
                    false);    // cinematic
                v17 = CL_GetConfigString(localClientNum, CS_FRIEND_OVERLAY_LAST);
                v18 = v17;
                if (v17 && *v17)
                {
                    if (I_stricmp(v17, "none"))
                    {
                        color[0] = 1.0f; // last-friend line drawn white @ 0.7 alpha
                        color[1] = 1.0f;
                        color[2] = 1.0f;
                        color[3] = 0.7f;
                        UI_DrawTextWithGlow(
                            place,
                            UI_SafeTranslateString(v18),
                            0x7FFFFFFF,
                            FontHandle,
                            25.0,      // x
                            20.0,      // y
                            2,         // horzAlign
                            2,         // vertAlign
                            friendlyNameFontSize->current.value, // scale
                            color,     // {1,1,1,0.7}
                            3,         // style
                            glowColor, // same glow color as the name
                            false,     // subtitle
                            false);    // cinematic
                    }
                }
            }
        }
    }
}

void __cdecl CG_DrawPaused(int localClientNum)
{
    if (cg_paused->current.integer)
    {
        if (cg_drawpaused->current.enabled)
            CG_HudMenuShowAllTimed(localClientNum);
    }
}

void __cdecl CG_AlterTimescale(int localClientNum, int time, double startScale, double endScale)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    cgameGlob->timeScaleTimeStart = Sys_Milliseconds();
    cgameGlob->timeScaleTimeEnd = cgArray[0].timeScaleTimeStart + time;
    cgameGlob->timeScaleStart = startScale;
    cgameGlob->timeScaleEnd = endScale;
}

void __cdecl CG_UpdateTimeScale(int localClientNum)
{
    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    int currentTime = Sys_Milliseconds();
    int startTime = cgameGlob->timeScaleTimeStart;

    if (!startTime || currentTime <= startTime)
        return;

    int endTime = cgameGlob->timeScaleTimeEnd;
    if (currentTime >= endTime)
    {
        cgameGlob->timeScaleTimeStart = 0;
        cgameGlob->timeScaleTimeEnd = 0;
        Com_SetTimeScale(cgameGlob->timeScaleEnd);
        return;
    }

    int duration = endTime - startTime;
    iassert(duration > 0);
    int elapsed = currentTime - startTime;
    float t = (float)elapsed / (float)duration;
    Com_SetTimeScale((1.0f - t) * cgameGlob->timeScaleStart + t * cgameGlob->timeScaleEnd);
}

const char strButtons[17] =
{
  '\x01',
  '\x02',
  '\x03',
  '\x04',
  '\x05',
  '\x06',
  '\x0E',
  '\x0F',
  '\x10',
  '\x11',
  '\x12',
  '\x13',
  '\x14',
  '\x15',
  '\x16',
  '\x17',
  '\0'
};

void DrawFontTest(int localClientNum)
{
    const ScreenPlacement *v1; // r31
    Font_s *FontHandle; // r25

    char txt[512]; // [sp+70h] [-250h] BYREF

    const float MY_X = -25.0f;

    v1 = &scrPlaceView[localClientNum];
    FontHandle = UI_GetFontHandle(v1, 1, 0.40000001);
    Com_sprintf(
        txt,
        512,
        "%s: %s All those moments will be lost in time, like tears in rain.",
        FontHandle->fontName,
        strButtons);

    int horzAlign = 0;
    int vertAlign = 0;

    UI_FilterStringForButtonAnimation(txt, 0x200u);
    UI_DrawText(v1, txt, 0x7FFFFFFF, FontHandle, MY_X, 10.0f, horzAlign, vertAlign, 0.4f, colorWhite, 1);
    Font_s *v5 = UI_GetFontHandle(v1, 2, 0.4f);
    Com_sprintf(txt, 512, "%s: %s All those moments will be lost in time, like tears in rain.", v5->fontName, strButtons);
    UI_FilterStringForButtonAnimation(txt, 0x200u);
    UI_DrawText(v1, txt, 0x7FFFFFFF, v5, MY_X, 35.0f, horzAlign, vertAlign, 0.4f, colorWhite, 1);
    Font_s *v8 = UI_GetFontHandle(v1, 3, 0.4f);
    Com_sprintf(txt, 512, "%s: %s All those moments will be lost in time, like tears in rain.", v8->fontName, strButtons);
    UI_FilterStringForButtonAnimation(txt, 0x200u);
    UI_DrawText(v1, txt, 0x7FFFFFFF, v8, MY_X, 60.0f, horzAlign, vertAlign, 0.4f, colorWhite, 1);
    Font_s *v11 = UI_GetFontHandle(v1, 5, 0.40000001);
    Com_sprintf(txt, 512, "%s: %s All those moments will be lost in time, like tears in rain.", v11->fontName, strButtons);
    UI_FilterStringForButtonAnimation(txt, 0x200u);
    UI_DrawText(v1, txt, 0x7FFFFFFF, v11, MY_X, 85.0f, horzAlign, vertAlign, 0.4f, colorWhite, 1);
    Font_s *v14 = UI_GetFontHandle(v1, 6, 0.4f);
    Com_sprintf(txt, 512, "%s: %s All those moments will be lost in time, like tears in rain.", v14->fontName, strButtons);
    UI_FilterStringForButtonAnimation(txt, 0x200u);
    UI_DrawText(v1, txt, 0x7FFFFFFF, v14, MY_X, 110.0f, horzAlign, vertAlign, 0.4f, colorWhite, 1);
}

static const char *WeaponStateNames_8[27] =
{
  "WEAPON_READY",
  "WEAPON_RAISING",
  "WEAPON_RAISING_ALTSWITCH",
  "WEAPON_DROPPING",
  "WEAPON_DROPPING_QUICK",
  "WEAPON_FIRING",
  "WEAPON_RECHAMBERING",
  "WEAPON_RELOADING",
  "WEAPON_RELOADING_INTERUPT",
  "WEAPON_RELOAD_START",
  "WEAPON_RELOAD_START_INTERUPT",
  "WEAPON_RELOAD_END",
  "WEAPON_MELEE_INIT",
  "WEAPON_MELEE_FIRE",
  "WEAPON_MELEE_END",
  "WEAPON_OFFHAND_INIT",
  "WEAPON_OFFHAND_PREPARE",
  "WEAPON_OFFHAND_HOLD",
  "WEAPON_OFFHAND_START",
  "WEAPON_OFFHAND",
  "WEAPON_OFFHAND_END",
  "WEAPON_DETONATING",
  "WEAPON_SPRINT_RAISE",
  "WEAPON_SPRINT_LOOP",
  "WEAPON_SPRINT_DROP",
  "WEAPON_NIGHTVISION_WEAR",
  "WEAPON_NIGHTVISION_REMOVE"
};

void VR_DrawHudEditorOutline(
    const ScreenPlacement* const placement,
    const float left,
    const float top,
    const float width,
    const float height,
    const float thickness,
    const float color[4])
{
    UI_FillRect(placement, left, top, width, thickness, 0, 0, color);
    UI_FillRect(
        placement,
        left,
        top + height - thickness,
        width,
        thickness,
        0,
        0,
        color);
    UI_FillRect(placement, left, top, thickness, height, 0, 0, color);
    UI_FillRect(
        placement,
        left + width - thickness,
        top,
        thickness,
        height,
        0,
        0,
        color);
}

void VR_DrawHudEditorOverlay(const int localClientNum)
{
    kisak::vr::hud::EditorSnapshot editor;
    if (!VR_GetHudEditorSnapshot(&editor))
    {
        return;
    }

    const ScreenPlacement* const placement =
        &scrPlaceView[localClientNum];
    Font_s* const font =
        UI_GetFontHandle(placement, 6, 0.30f);
    const float backdrop[4] = {0.01f, 0.02f, 0.04f, 0.38f};
    const float grid[4] = {0.35f, 0.55f, 0.72f, 0.30f};
    const float safeColor[4] = {0.20f, 0.82f, 1.00f, 0.90f};
    const float selectedColor[4] = {1.00f, 0.84f, 0.20f, 1.00f};
    const float white[4] = {1.00f, 1.00f, 1.00f, 1.00f};
    const float muted[4] = {0.76f, 0.82f, 0.90f, 0.95f};
    const float saveColor[4] = {0.10f, 0.62f, 0.35f, 0.92f};
    const float cancelColor[4] = {0.58f, 0.18f, 0.20f, 0.92f};
    const float controlBackdrop[4] = {0.01f, 0.02f, 0.04f, 0.78f};
    const float elementColors[kisak::vr::hud::kElementCount][4] = {
        {0.18f, 0.72f, 0.42f, 0.62f},
        {0.90f, 0.58f, 0.15f, 0.62f},
        {0.25f, 0.55f, 0.94f, 0.62f},
        {0.72f, 0.36f, 0.92f, 0.62f},
        {0.88f, 0.88f, 0.90f, 0.55f},
    };

    UI_FillRect(
        placement,
        0.0f,
        0.0f,
        kisak::vr::hud::kCanvasWidth,
        kisak::vr::hud::kCanvasHeight,
        0,
        0,
        backdrop);

    for (const float x : {64.0f, 320.0f, 576.0f})
    {
        UI_FillRect(placement, x, 0.0f, 1.0f, 480.0f, 0, 0, grid);
    }
    for (const float y : {48.0f, 240.0f, 432.0f})
    {
        UI_FillRect(placement, 0.0f, y, 640.0f, 1.0f, 0, 0, grid);
    }

    const kisak::vr::hud::Point safeMinimum =
        kisak::vr::hud::SafeAreaMinimum(editor.layout);
    const kisak::vr::hud::Point safeMaximum =
        kisak::vr::hud::SafeAreaMaximum(editor.layout);
    VR_DrawHudEditorOutline(
        placement,
        safeMinimum.x,
        safeMinimum.y,
        safeMaximum.x - safeMinimum.x,
        safeMaximum.y - safeMinimum.y,
        1.5f,
        safeColor);

    for (std::size_t index = 0u;
         index < kisak::vr::hud::kElementCount;
         ++index)
    {
        const auto element =
            static_cast<kisak::vr::hud::Element>(index);
        const kisak::vr::hud::Point center =
            kisak::vr::hud::ElementCenter(editor.layout, element);
        const kisak::vr::hud::Size size =
            kisak::vr::hud::ElementSize(editor.layout, element);
        const float left = center.x - size.width * 0.5f;
        const float top = center.y - size.height * 0.5f;
        float fill[4] = {
            elementColors[index][0],
            elementColors[index][1],
            elementColors[index][2],
            elementColors[index][3],
        };
        if (element == kisak::vr::hud::Element::Compass &&
            !editor.layout.compassEnabled)
        {
            fill[3] = 0.22f;
        }
        UI_FillRect(
            placement,
            left,
            top,
            size.width,
            size.height,
            0,
            0,
            fill);

        const bool selected = element == editor.selected;
        VR_DrawHudEditorOutline(
            placement,
            left,
            top,
            size.width,
            size.height,
            selected ? 3.0f : 1.0f,
            selected ? selectedColor : white);

        UI_DrawText(
            placement,
            kisak::vr::hud::ElementLabel(element),
            0x7FFFFFFF,
            font,
            left + 7.0f,
            center.y + 5.0f,
            0,
            0,
            0.23f,
            white,
            3);
    }

    // KISAK_SP_VR_HUD_EDITOR_RECOVERY_V69
    // Keep the selected group name on-screen even when its editable rectangle
    // is covered or entirely outside the 640x480 canvas.
    UI_FillRect(
        placement,
        12.0f,
        12.0f,
        340.0f,
        48.0f,
        0,
        0,
        controlBackdrop);
    UI_DrawText(
        placement,
        "SELECTED HUD GROUP",
        0x7FFFFFFF,
        font,
        22.0f,
        31.0f,
        0,
        0,
        0.17f,
        muted,
        3);
    UI_DrawText(
        placement,
        kisak::vr::hud::ElementLabel(editor.selected),
        0x7FFFFFFF,
        font,
        22.0f,
        53.0f,
        0,
        0,
        0.24f,
        selectedColor,
        3);

    UI_FillRect(placement, 366.0f, 18.0f, 124.0f, 40.0f, 0, 0, cancelColor);
    UI_FillRect(placement, 500.0f, 18.0f, 124.0f, 40.0f, 0, 0, saveColor);
    UI_DrawText(
        placement,
        "CANCEL (B)",
        0x7FFFFFFF,
        font,
        384.0f,
        46.0f,
        0,
        0,
        0.25f,
        white,
        3);
    UI_DrawText(
        placement,
        "SAVE (A)",
        0x7FFFFFFF,
        font,
        527.0f,
        46.0f,
        0,
        0,
        0.25f,
        white,
        3);

    const float crosshair[4] = {0.95f, 0.25f, 0.25f, 0.90f};
    UI_FillRect(placement, 312.0f, 239.0f, 16.0f, 2.0f, 0, 0, crosshair);
    UI_FillRect(placement, 319.0f, 232.0f, 2.0f, 16.0f, 0, 0, crosshair);
    UI_DrawText(
        placement,
        "Crosshair locked to optical center",
        0x7FFFFFFF,
        font,
        330.0f,
        246.0f,
        0,
        0,
        0.18f,
        muted,
        3);

    UI_FillRect(
        placement,
        12.0f,
        414.0f,
        616.0f,
        62.0f,
        0,
        0,
        controlBackdrop);
    UI_DrawText(
        placement,
        "Use / Next weapon (Quest X/Y): previous / next   Sprint / Melee (L3/R3): center / reset",
        0x7FFFFFFF,
        font,
        22.0f,
        433.0f,
        0,
        0,
        0.17f,
        white,
        3);
    UI_DrawText(
        placement,
        "Keyboard: Shift+Tab / Tab selects   Home centers   End resets selected only",
        0x7FFFFFFF,
        font,
        22.0f,
        452.0f,
        0,
        0,
        0.17f,
        muted,
        3);
    UI_DrawText(
        placement,
        editor.snapEnabled
            ? "Trigger: drag   Right stick: resize   Hold left grip: free move   A: save   B: cancel"
            : "FREE MOVE (left grip held)   Release grip to restore snap anchors   A: save   B: cancel",
        0x7FFFFFFF,
        font,
        22.0f,
        471.0f,
        0,
        0,
        0.17f,
        muted,
        3);

    if (editor.pointerValid)
    {
        const float pointerColor[4] = {
            1.00f,
            editor.dragging ? 0.35f : 0.95f,
            0.18f,
            1.00f,
        };
        UI_FillRect(
            placement,
            editor.pointer.x - 8.0f,
            editor.pointer.y - 1.0f,
            16.0f,
            2.0f,
            0,
            0,
            pointerColor);
        UI_FillRect(
            placement,
            editor.pointer.x - 1.0f,
            editor.pointer.y - 8.0f,
            2.0f,
            16.0f,
            0,
            0,
            pointerColor);
    }
}


void DrawViewmodelInfo(int localClientNum)
{
    int ViewmodelWeaponIndex; // r31
    Font_s *font; // r24
    weaponInfo_s *weapInfo; // r27
    WeaponDef *weapDef; // r28
    XModel *weaponMdl; // r11
    const char *name; // r31
    double fov; // fp1
    const char *weaponMdlName; // r8
    const char **p_name; // r11
    const char *goggles; // r10
    const char **v12; // r11
    const char *hands; // r9
    char *len; // r11
    int v16; // r7
    const char *rocket; // [sp+8h] [-8B8h]
    const char *knife; // [sp+Ch] [-8B4h]
    char buf[2128]; // [sp+70h] [-850h] BYREF

    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    ViewmodelWeaponIndex = BG_GetViewmodelWeaponIndex(&cgArray[0].predictedPlayerState);
    font = UI_GetFontHandle(&scrPlaceView[localClientNum], 6, 0.25);
    if (ViewmodelWeaponIndex > 0)
    {
        weapInfo = CG_GetLocalClientWeaponInfo(localClientNum, ViewmodelWeaponIndex);
        weapDef = BG_GetWeaponDef(ViewmodelWeaponIndex);
        weaponMdl = weapDef->gunXModel[cgArray[0].predictedPlayerState.weaponmodels[ViewmodelWeaponIndex]];
        if (weaponMdl)
            name = weaponMdl->name;
        else
            name = 0;
        fov = CG_GetViewFov(localClientNum);
        weaponMdlName = "none";

        p_name = &weapInfo->gogglesModel->name;
        if (p_name)
            goggles = *p_name;
        else
            goggles = "none";

        v12 = &weapInfo->handModel->name;
        if (v12)
            hands = *v12;
        else
            hands = "none";

        if (name)
            weaponMdlName = name;
        
        // LWSS add - why this missing? :o
        if (weapInfo->rocketModel)
        {
            rocket = weapInfo->rocketModel->name;
        }
        else
        {
            rocket = "none";
        }
        if (weapInfo->knifeModel)
        {
            knife = weapInfo->knifeModel->name;
        }
        else
        {
            knife = "none";
        }
        // LWSS end

        Com_sprintf(
            buf,
            2048,
            "^6%s\n"
            "^7Weapon: ^2%s^7 - ^5%s\n"
            "^7Hands: ^5%s\n"
            "^7Goggles: ^5%s\n"
            "^7Rocket: ^5%s\n"
            "^7Knife: ^5%s\n"
            "^7ADS: ^5%.2f ^7-^5 %.0f^7fov\n"
            "^7---Anims---\n"
            "^3",
            WeaponStateNames_8[cgArray[0].predictedPlayerState.weaponstate],
            weapDef->szInternalName,
            weaponMdlName,
            hands,
            goggles,
            rocket,
            knife,
            cgArray[0].predictedPlayerState.fWeaponPosFrac,
            fov);
        len = buf;
        while (*len++)
            ;
        DObjDisplayAnimToBuffer(
            weapInfo->viewModelDObj,
            "",
            &buf[len - buf - 1],
            2048 - (len - buf - 1));
        UI_DrawText(&scrPlaceView[localClientNum], buf, 2048, font, 0.0, 20.0, 1, 1, 0.25f, colorWhite, 3);
        //UI_DrawText(&scrPlaceView[localClientNum], buf, 2048, font, 0.0, 20.0, v16, 3, 0.25, (const float *)1, 1);
    }
}

void __cdecl CG_Draw2D(int localClientNum)
{
    snapshot_s *nextSnap; // r29
    int integer; // r11

    cg_s *cgameGlob = CG_GetLocalClientGlobals(localClientNum);

    VR_JavelinHudDiagnostic("CG_Draw2D enter");
    CG_UpdateTimeScale(localClientNum);
    VR_JavelinHudDiagnostic("time scale complete");

    if (cgArray[0].predictedPlayerState.pm_type != 4 && cgArray[0].cubemapShot == CUBEMAPSHOT_NONE)
    {
        nextSnap = cgArray[0].nextSnap;
        if (cg_draw2D->current.enabled)
        {
            integer = debugOverlay->current.integer;
            if (integer == 1)
            {
                DrawViewmodelInfo(localClientNum);
            }
            else if (integer == 2)
            {
                DrawFontTest(localClientNum);
                return;
            }
            CG_DrawNightVisionOverlay(localClientNum);
            VR_JavelinHudDiagnostic("night-vision overlay complete");
            CG_DrawFadeInCinematic(localClientNum);
            VR_JavelinHudDiagnostic("cinematic fade complete");
            CG_DrawFriendOverlay(localClientNum);
            VR_JavelinHudDiagnostic("friend overlay complete");
            CG_ScreenBlur(localClientNum);
            VR_JavelinHudDiagnostic("screen blur complete");
            CG_DrawFlashDamage(cgArray);
            VR_JavelinHudDiagnostic("flash damage complete");
            if (cg_drawHUD->current.enabled && hud_drawHUD->current.enabled)
            {
                CG_DrawDamageDirectionIndicators(localClientNum);
                VR_JavelinHudDiagnostic("damage indicators complete");
                if (nextSnap->ps.pm_type < 5)
                {
                    VR_JavelinHudDiagnostic("before crosshair stage");
                    if (!cg_drawFriendlyFireCrosshair->current.enabled || !(unsigned __int8)CG_DrawFriendlyFire(cgArray))
                        CG_DrawCrosshair(localClientNum);
                    VR_JavelinHudDiagnostic("crosshair stage complete");
                    CG_DrawGrenadeIndicators(localClientNum);
                    VR_JavelinHudDiagnostic("grenade indicators complete");
                    CG_CheckTimedMenus(localClientNum);
                    VR_JavelinHudDiagnostic("timed menus complete");
                }
                VR_JavelinHudDiagnostic("before primary HUD elements");
                CG_Draw2dHudElems(localClientNum, 0);
                VR_JavelinHudDiagnostic("primary HUD elements complete");
            }
            VR_JavelinHudDiagnostic("before menu painting");
            VR_JavelinMenuDiagnosticSetActive(
                VR_JavelinHudDiagnosticActive());
            Menu_PaintAll(&cgDC);
            VR_JavelinMenuDiagnosticSetActive(false);
            VR_JavelinHudDiagnostic("menu painting complete");
            CG_Draw2dHudElems(localClientNum, 1);
            VR_JavelinHudDiagnostic("secondary HUD elements complete");
            CG_DrawPerformanceWarnings();
            VR_JavelinHudDiagnostic("performance warnings complete");
            //Profile_Begin(349);
            CG_DrawDebugOverlays(localClientNum);
            VR_JavelinHudDiagnostic("debug overlays complete");
            //Profile_EndInternal(0);
            CG_DrawUpperRightDebugInfo(localClientNum);
            VR_JavelinHudDiagnostic("upper-right debug info complete");
            if (!cgArray[0].showScores && cg_minicon->current.enabled)
                Con_DrawMiniConsole(0, 2, 4, 1.0);
            CG_DrawErrorMessages();
            VR_JavelinHudDiagnostic("console errors complete");
            CG_DrawFlashFade(localClientNum);
            VR_JavelinHudDiagnostic("flash fade complete");
            if (cg_paused->current.integer && cg_drawpaused->current.enabled)
                CG_HudMenuShowAllTimed(localClientNum);
            VR_JavelinHudDiagnostic("timed HUD visibility complete");
        }
        else
        {
            if (cg_drawHUD->current.enabled && hud_drawHUD->current.enabled)
                CG_Draw2dHudElems(localClientNum, 0);
            CG_DrawFlashFade(localClientNum);
        }

        VR_DrawHudEditorOverlay(localClientNum);
    }
}

void __cdecl CG_DrawActive(int localClientNum)
{
    double zoomSensitivity; // fp1

    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\cod3src\\src\\cgame\\cg_local.h",
            910,
            0,
            "%s\n\t(localClientNum) = %i",
            "(localClientNum == 0)",
            localClientNum);
    zoomSensitivity = cgArray[0].zoomSensitivity;
    if (cgArray[0].shellshock.sensitivity != 0.0)
        zoomSensitivity = (float)(cgArray[0].shellshock.sensitivity * cgArray[0].zoomSensitivity);
    CL_SetFOVSensitivityScale(localClientNum, zoomSensitivity);
    CL_SetUserCmdWeapons(localClientNum, cgArray[0].weaponSelect, cgArray[0].equippedOffHand);
    CL_SetUserCmdAimValues(
        localClientNum,
        cgArray[0].gunPitch,
        cgArray[0].gunYaw,
        cgArray[0].gunXOfs,
        cgArray[0].gunYOfs,
        cgArray[0].gunZOfs);
    CL_SetExtraButtons(localClientNum, cgArray[0].extraButtons);
    cgArray[0].extraButtons = 0;

    float vrScopeAdsFraction = 0.0f;

    const unsigned int vrScopeWeaponIndex =
        BG_GetViewmodelWeaponIndex(
            &cgArray[0].predictedPlayerState);

    WeaponDef* vrScopeWeaponDef =
        BG_GetWeaponDef(vrScopeWeaponIndex);

    const bool vrScopeWeapon =
        vrScopeWeaponDef != nullptr &&
        vrScopeWeaponDef->overlayInterface !=
            WEAPOVERLAYINTERFACE_JAVELIN &&
        (vrScopeWeaponDef->overlayMaterial != nullptr ||
         vrScopeWeaponDef->overlayReticle !=
             WEAPOVERLAYRETICLE_NONE);

    const bool vrScopeActive =
        VR_IsInitialized() &&
        vrScopeWeapon &&
        CG_GetWeapReticleZoom(
            &cgArray[0],
            &vrScopeAdsFraction);

    VR_SetPhysicalSniperScopeState(
        vrScopeActive,
        vrScopeActive ? vrScopeAdsFraction : 0.0f,
        vrScopeWeaponDef != nullptr
            ? vrScopeWeaponDef->fAdsZoomFov
            : 65.0f);

    // KISAK_SP_VR_FIXED_SCOPE_SHARP_VIEW_AND_TRACER_V5
    // Publish the fixed scoped-turret state before choosing the first scene
    // view. CG_DrawCrosshair also publishes it later for the compositor, but
    // that is too late to schedule this frame's dedicated scope camera.
    const unsigned int vrFixedScopeWeaponIndex =
        CG_PlayerTurretWeaponIdx(
            localClientNum);

    WeaponDef* vrFixedScopeWeaponDef =
        BG_GetWeaponDef(
            vrFixedScopeWeaponIndex);

    const bool vrFixedScopeActive =
        VR_IsInitialized() &&
        CG_PlayerUsingScopedTurret(
            localClientNum) &&
        vrFixedScopeWeaponDef != nullptr &&
        vrFixedScopeWeaponDef->overlayInterface ==
            WEAPOVERLAYINTERFACE_TURRETSCOPE;

    VR_SetFixedScopedTurretState(
        vrFixedScopeActive);

    if (!VR_IsInitialized() ||
        cgArray[0].refdef.width < 2)
    {
        CL_RenderScene(&cgArray[0].refdef);
        return;
    }

    int stereoWidth =
        static_cast<int>(
            cgArray[0].refdef.width);

#ifdef KISAK_SP
    const void* deferredClientCommands2D =
        nullptr;

    bool renderedDedicatedScopeCamera =
        false;

    int scopePanelX = 0;
    int scopePanelY = 0;
    int scopePanelSize = 0;

    const bool packedScopeLayout =
        VR_GetPhysicalSniperScopeCaptureLayout(
            static_cast<int>(
                cgArray[0].refdef.width),
            static_cast<int>(
                cgArray[0].refdef.height),
            &stereoWidth,
            &scopePanelX,
            &scopePanelY,
            &scopePanelSize);

    if (packedScopeLayout)
    {
        refdef_s scopeRefdef =
            cgArray[0].refdef;

        if (VR_GetPhysicalSniperScopeRenderView(
                scopeRefdef.vieworg,
                scopeRefdef.viewaxis,
                &scopeRefdef.tanHalfFovX,
                &scopeRefdef.tanHalfFovY) &&
            frontEndDataOut != nullptr &&
            frontEndDataOut->viewInfo != nullptr)
        {
            // KISAK_SP_VR_FIXED_SCOPE_SHARP_VIEW_AND_TRACER_V5
            // Keep mission/cinematic blur in the normal eyes, but do not bake
            // it into the optical source that will be magnified across both
            // eyes.
            if (vrFixedScopeActive)
            {
                scopeRefdef.blurRadius = 0.0f;
                scopeRefdef.dof.nearBlur = 0.0f;
                scopeRefdef.dof.farBlur = 0.0f;
            }

            scopeRefdef.x =
                cgArray[0].refdef.x +
                scopePanelX;

            scopeRefdef.y =
                cgArray[0].refdef.y +
                scopePanelY;

            scopeRefdef.width =
                scopePanelSize;

            scopeRefdef.height =
                scopePanelSize;

            scopeRefdef.useScissorViewport = 0;

            const uint32_t scopeViewInfoIndex =
                frontEndDataOut->viewInfoCount;

            deferredClientCommands2D =
                frontEndDataOut
                    ->viewInfo[scopeViewInfoIndex]
                    .cmds;

            // CL_CGameRendering attaches this frame's 2D list to the next
            // view before cgame runs.  The scope camera is now that first
            // view, so move the list to the left eye instead of painting
            // the HUD into the weapon-free scope source.
            frontEndDataOut
                ->viewInfo[scopeViewInfoIndex]
                .cmds = nullptr;

            CL_RenderScene(
                &scopeRefdef);

            if (frontEndDataOut->viewInfoCount >
                scopeViewInfoIndex)
            {
                renderedDedicatedScopeCamera =
                    true;

                static bool
                    loggedDedicatedScopeCamera = false;

                if (!loggedDedicatedScopeCamera)
                {
                    Com_Printf(
                        0,
                        "[VR] Rendered the first weapon-free "
                        "dedicated sniper-scope camera at %d x %d "
                        "and transferred the 2D HUD to the left eye.\n",
                        scopePanelSize,
                        scopePanelSize);

                    loggedDedicatedScopeCamera =
                        true;
                }
            }
            else
            {
                frontEndDataOut
                    ->viewInfo[scopeViewInfoIndex]
                    .cmds =
                        deferredClientCommands2D;

                deferredClientCommands2D =
                    nullptr;
            }
        }

        CG_AddDeferredPhysicalSniperViewWeaponToScene(
            localClientNum);
    }
#endif

    refdef_s leftEyeRefdef =
        cgArray[0].refdef;

    refdef_s rightEyeRefdef =
        cgArray[0].refdef;

    const int leftEyeWidth =
        stereoWidth / 2;

    const int rightEyeWidth =
        stereoWidth - leftEyeWidth;

    leftEyeRefdef.width =
        leftEyeWidth;

    rightEyeRefdef.x =
        cgArray[0].refdef.x +
        leftEyeWidth;

    rightEyeRefdef.width =
        rightEyeWidth;

    leftEyeRefdef.useScissorViewport = 0;
    rightEyeRefdef.useScissorViewport = 0;

    // KISAK_SP_JAVELIN_VR_USABILITY_FIX
    // The native Javelin is a full-screen magnified optic. Rendering that
    // flat optic from two separated cameras magnifies binocular disparity
    // into an unusable doubled target. Present one stable camera to both eyes
    // only while this optic is raised; normal gameplay remains true stereo.
    const unsigned int vrJavelinWeaponIndex =
        BG_GetViewmodelWeaponIndex(
            &cgArray[0].predictedPlayerState);

    WeaponDef *vrJavelinWeaponDef =
        BG_GetWeaponDef(vrJavelinWeaponIndex);

    const bool vrJavelinAds =
        vrJavelinWeaponDef != nullptr &&
        vrJavelinWeaponDef->overlayInterface ==
            WEAPOVERLAYINTERFACE_JAVELIN &&
        cgArray[0].predictedPlayerState.fWeaponPosFrac > 0.0f;

    if (!vrJavelinAds)
    {
        VR_ApplyStereoEyeOffsetForEye(
            leftEyeRefdef.vieworg,
            leftEyeRefdef.viewaxis,
            0u);

        VR_ApplyStereoEyeOffsetForEye(
            rightEyeRefdef.vieworg,
            rightEyeRefdef.viewaxis,
            1u);
    }
    else
    {
        // KISAK_SP_JAVELIN_CONTROLLER_LOCK_FIX
        // A controller-projected lock square is useful only if the optic's
        // world view looks along the same ray. Keep the head position as the
        // camera origin, but aim the monoscopic Javelin view with the launcher.
        float vrJavelinPitch = 0.0f;
        float vrJavelinYaw = 0.0f;
        bool ignoredVrAttackPressed = false;

        if (VR_GetRightControllerWeaponCommand(
                &vrJavelinPitch,
                &vrJavelinYaw,
                &ignoredVrAttackPressed))
        {
            const float vrJavelinViewAngles[3] = {
                vrJavelinPitch,
                vrJavelinYaw,
                0.0f,
            };

            AnglesToAxis(
                vrJavelinViewAngles,
                leftEyeRefdef.viewaxis);
            AnglesToAxis(
                vrJavelinViewAngles,
                rightEyeRefdef.viewaxis);

            static bool loggedVrJavelinControllerView = false;
            if (!loggedVrJavelinControllerView)
            {
                Com_Printf(
                    0,
                    "[VR][JAVELIN] Monoscopic optic view now follows "
                    "the tracked launcher direction.\n");
                loggedVrJavelinControllerView = true;
            }
        }

        static bool loggedVrJavelinMonoscopicOptic = false;
        if (!loggedVrJavelinMonoscopicOptic)
        {
            Com_Printf(
                0,
                "[VR][JAVELIN] Using a monoscopic full-screen optic "
                "to prevent stereo double vision.\n");
            loggedVrJavelinMonoscopicOptic = true;
        }
    }

    VR_GetStereoEyeFovBounds(
        0u,
        &leftEyeRefdef.tanHalfFovX,
        &leftEyeRefdef.tanHalfFovY);

    // KISAK_SP_JAVELIN_VR_COMPLETION_FIX
    // Keep zero IPD for the raised Javelin, but render the right half with
    // the right eye's own asymmetric projection. This makes the two images
    // converge correctly when OpenXR submits them with per-eye FOV metadata.
    VR_GetStereoEyeFovBounds(
        1u,
        &rightEyeRefdef.tanHalfFovX,
        &rightEyeRefdef.tanHalfFovY);

    const auto vrStereoFrontendStart =
        std::chrono::steady_clock::now();

    // KISAK_SP_VR_CAPTURE_POSE_METADATA_V32
    // The backend may finish this packed stereo frame after one or more newer
    // OpenXR poses have been published. Preserve the exact views used by this
    // GfxBackEndData so the capture consumer never guesses by acquisition time.
    if (frontEndDataOut != nullptr)
    {
        VR_RecordRenderFramePose(
            frontEndDataOut->frameCount);
    }

    const auto vrLeftEyeStart =
        std::chrono::steady_clock::now();

#ifdef KISAK_SP
    if (renderedDedicatedScopeCamera)
    {
        frontEndDataOut
            ->viewInfo[
                frontEndDataOut->viewInfoCount]
            .cmds =
                deferredClientCommands2D;
    }
#endif

    VR_BeginStereoEyeRender(0u);
    CL_RenderScene(&leftEyeRefdef);
    VR_EndStereoEyeRender();

    const auto vrLeftEyeEnd =
        std::chrono::steady_clock::now();

    if (frontEndDataOut != nullptr &&
        frontEndDataOut->viewInfo != nullptr)
    {
        frontEndDataOut
            ->viewInfo[
                frontEndDataOut->viewInfoCount]
            .cmds = nullptr;
    }

    VR_BeginStereoEyeRender(1u);
    CL_RenderScene(&rightEyeRefdef);
    VR_EndStereoEyeRender();

    const auto vrStereoFrontendEnd =
        std::chrono::steady_clock::now();

    const double vrLeftEyeMilliseconds =
        std::chrono::duration<double, std::milli>(
            vrLeftEyeEnd - vrLeftEyeStart).count();

    const double vrRightEyeMilliseconds =
        std::chrono::duration<double, std::milli>(
            vrStereoFrontendEnd - vrLeftEyeEnd).count();

    const double vrStereoFrontendMilliseconds =
        std::chrono::duration<double, std::milli>(
            vrStereoFrontendEnd - vrStereoFrontendStart).count();

    static unsigned int vrStereoTimingSampleCount = 0u;
    static double vrStereoTimingLeftTotal = 0.0;
    static double vrStereoTimingRightTotal = 0.0;
    static double vrStereoTimingTotal = 0.0;
    static double vrStereoTimingMaximum = 0.0;

    ++vrStereoTimingSampleCount;
    vrStereoTimingLeftTotal += vrLeftEyeMilliseconds;
    vrStereoTimingRightTotal += vrRightEyeMilliseconds;
    vrStereoTimingTotal += vrStereoFrontendMilliseconds;

    if (vrStereoFrontendMilliseconds >
        vrStereoTimingMaximum)
    {
        vrStereoTimingMaximum =
            vrStereoFrontendMilliseconds;
    }

    if (vrStereoTimingSampleCount >= 120u)
    {
        const double sampleScale =
            1.0 /
            static_cast<double>(
                vrStereoTimingSampleCount);

        Com_Printf(
            0,
            "[VR] SP stereo frontend timing: "
            "left %.2f ms, right %.2f ms, "
            "total %.2f ms, max %.2f ms.\n",
            vrStereoTimingLeftTotal * sampleScale,
            vrStereoTimingRightTotal * sampleScale,
            vrStereoTimingTotal * sampleScale,
            vrStereoTimingMaximum);

        vrStereoTimingSampleCount = 0u;
        vrStereoTimingLeftTotal = 0.0;
        vrStereoTimingRightTotal = 0.0;
        vrStereoTimingTotal = 0.0;
        vrStereoTimingMaximum = 0.0;
    }

    static bool loggedSpStereoViews = false;

    if (!loggedSpStereoViews)
    {
        Com_Printf(
            0,
            "[VR] Rendered single-player same-frame "
            "stereo views.\n");

        loggedSpStereoViews = true;
    }
}

// attributes: thunk
void __cdecl CG_AddSceneTracerBeams(int localClientNum)
{
    CG_AddLocalEntityTracerBeams(localClientNum);
}

// attributes: thunk
void __cdecl CG_GenerateSceneVerts(int localClientNum)
{
    CG_AddDrawSurfsFor3dHudElems(localClientNum);
}
