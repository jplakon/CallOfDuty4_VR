#include "game_public.h"
#include <server/sv_world.h>
#include <DynEntity/DynEntity_client.h>
#include <script/scr_const.h>
#include <server/sv_game.h>
#include "bullet.h"

#ifdef KISAK_MP
#include <game_mp/g_utils_mp.h>
#include <server_mp/server_mp.h>
#elif KISAK_SP
#include "g_main.h"
#include "g_local.h"
#include "vr/vr_openxr.h"
#endif


#ifdef KISAK_MP
void __cdecl G_AntiLagRewindClientPos(int gameTime, AntilagClientStore *antilagStore)
{
    const char *v2; // eax
    float *currentOrigin; // edx
    float *v4; // eax
    const char *v5; // eax
    float *v6; // edx
    int client; // [esp+34h] [ebp-14h]
    float clientPosition[4]; // [esp+38h] [ebp-10h] BYREF

    if (!antilagStore)
        MyAssertHandler(".\\game\\g_weapon.cpp", 30, 0, "%s", "antilagStore");
    if (g_antilag->current.enabled)
    {
        memset((uint8_t *)antilagStore, 0, sizeof(AntilagClientStore));
        if (gameTime <= 0)
            MyAssertHandler(".\\game\\g_weapon.cpp", 36, 0, "%s", "gameTime > 0");
        if (level.time - gameTime <= 400 && level.time - gameTime > 1000 / sv_fps->current.integer)
        {
            for (client = 0; client < level.maxclients; ++client)
            {
                if (level.clients[client].sess.connected == CON_CONNECTED
                    && level.clients[client].sess.sessionState == SESS_STATE_PLAYING
                    && SV_GetClientPositionAtTime(client, gameTime, clientPosition))
                {
                    if ((LODWORD(clientPosition[0]) & 0x7F800000) == 0x7F800000
                        || (LODWORD(clientPosition[1]) & 0x7F800000) == 0x7F800000
                        || (LODWORD(clientPosition[2]) & 0x7F800000) == 0x7F800000)
                    {
                        v2 = va(
                            "client %i's antilag origin is invalid - (%f, %f, %f)",
                            client,
                            clientPosition[0],
                            clientPosition[1],
                            clientPosition[2]);
                        MyAssertHandler(
                            ".\\game\\g_weapon.cpp",
                            51,
                            0,
                            "%s\n\t%s",
                            "!IS_NAN(clientPosition[0]) && !IS_NAN(clientPosition[1]) && !IS_NAN(clientPosition[2])",
                            v2);
                    }
                    LODWORD(clientPosition[3]) = gameTime;
                    currentOrigin = g_entities[client].r.currentOrigin;
                    v4 = antilagStore->realClientPositions[client];
                    *v4 = *currentOrigin;
                    v4[1] = currentOrigin[1];
                    v4[2] = currentOrigin[2];
                    if ((COERCE_UNSIGNED_INT(*v4) & 0x7F800000) == 0x7F800000
                        || (COERCE_UNSIGNED_INT(antilagStore->realClientPositions[client][1]) & 0x7F800000) == 0x7F800000
                        || (COERCE_UNSIGNED_INT(antilagStore->realClientPositions[client][2]) & 0x7F800000) == 0x7F800000)
                    {
                        v5 = va(
                            "client %i's origin is invalid - (%f, %f, %f)",
                            client,
                            antilagStore->realClientPositions[client][0],
                            antilagStore->realClientPositions[client][1],
                            antilagStore->realClientPositions[client][2]);
                        MyAssertHandler(
                            ".\\game\\g_weapon.cpp",
                            58,
                            0,
                            "%s\n\t%s",
                            "!IS_NAN( antilagStore->realClientPositions[client][0]) && !IS_NAN(antilagStore->realClientPositions[client"
                            "][1]) && !IS_NAN(antilagStore->realClientPositions[client][2])",
                            v5);
                    }
                    SV_UnlinkEntity(&g_entities[client]);
                    v6 = g_entities[client].r.currentOrigin;
                    *v6 = clientPosition[0];
                    v6[1] = clientPosition[1];
                    v6[2] = clientPosition[2];
                    SV_LinkEntity(&g_entities[client]);
                    antilagStore->clientMoved[client] = 1;
                }
            }
        }
    }
}


void __cdecl G_AntiLag_RestoreClientPos(AntilagClientStore *antilagStore)
{
    const char *v1; // eax
    float *v2; // edx
    float *currentOrigin; // eax
    int client; // [esp+24h] [ebp-4h]

    if (!antilagStore)
        MyAssertHandler(".\\game\\g_weapon.cpp", 80, 0, "%s", "antilagStore");
    if (g_antilag->current.enabled)
    {
        for (client = 0; client < level.maxclients; ++client)
        {
            if (antilagStore->clientMoved[client])
            {
                SV_UnlinkEntity(&g_entities[client]);
                if ((COERCE_UNSIGNED_INT(antilagStore->realClientPositions[client][0]) & 0x7F800000) == 0x7F800000
                    || (COERCE_UNSIGNED_INT(antilagStore->realClientPositions[client][1]) & 0x7F800000) == 0x7F800000
                    || (COERCE_UNSIGNED_INT(antilagStore->realClientPositions[client][2]) & 0x7F800000) == 0x7F800000)
                {
                    v1 = va(
                        "client %i's origin is invalid - (%f, %f, %f)",
                        client,
                        antilagStore->realClientPositions[client][0],
                        antilagStore->realClientPositions[client][1],
                        antilagStore->realClientPositions[client][2]);
                    MyAssertHandler(
                        ".\\game\\g_weapon.cpp",
                        92,
                        0,
                        "%s\n\t%s",
                        "!IS_NAN(antilagStore->realClientPositions[client][0]) && !IS_NAN(antilagStore->realClientPositions[client][1"
                        "]) && !IS_NAN(antilagStore->realClientPositions[client][2])",
                        v1);
                }
                v2 = antilagStore->realClientPositions[client];
                currentOrigin = g_entities[client].r.currentOrigin;
                *currentOrigin = *v2;
                currentOrigin[1] = v2[1];
                currentOrigin[2] = v2[2];
                SV_LinkEntity(&g_entities[client]);
            }
        }
    }
}
#endif

gentity_s *__cdecl Weapon_Melee(gentity_s *ent, weaponParms *wp, float range, float width, float height, int gametime)
{
#ifdef KISAK_MP
    AntilagClientStore antilagClients; // [esp+Ch] [ebp-348h] BYREF
    gentity_s *traceEnt; // [esp+350h] [ebp-4h]

    G_AntiLagRewindClientPos(gametime, &antilagClients);
    traceEnt = Weapon_Melee_internal(ent, wp, range, width, height);
    G_AntiLag_RestoreClientPos(&antilagClients);
    return traceEnt;
#elif KISAK_SP
    return Weapon_Melee_internal(ent, wp, range, width, height);
#endif
}

gentity_s *__cdecl Weapon_Melee_internal(gentity_s *ent, weaponParms *wp, float range, float width, float height)
{
    int v6; // eax
    hitLocation_t partGroup; // [esp+4h] [ebp-5Ch]
    uint32_t modelIndex; // [esp+8h] [ebp-58h]
    uint32_t partName; // [esp+Ch] [ebp-54h]
    bool v10; // [esp+14h] [ebp-4Ch]
    int damage; // [esp+18h] [ebp-48h]
    gentity_s *tent; // [esp+1Ch] [ebp-44h]
    float endpos[3]; // [esp+20h] [ebp-40h] BYREF
    trace_t tr; // [esp+2Ch] [ebp-34h] BYREF
    uint16_t hitEntId; // [esp+58h] [ebp-8h]
    gentity_s *traceEnt; // [esp+5Ch] [ebp-4h]

    if (!wp)
        MyAssertHandler(".\\game\\g_weapon.cpp", 190, 0, "%s", "wp");
    if (!wp->weapDef)
        MyAssertHandler(".\\game\\g_weapon.cpp", 191, 0, "%s", "wp->weapDef");
    damage = wp->weapDef->iMeleeDamage;
    if (!Melee_Trace(ent, wp, damage, range, width, height, &tr, endpos))
        return 0;
    hitEntId = Trace_GetEntityHitId(&tr);
    traceEnt = &g_entities[hitEntId];

#ifdef KISAK_SP
    // SP: AI must not melee a same-team sentient.
    if (!ent->client && ent->sentient && traceEnt->sentient
        && ent->sentient->eTeam == traceEnt->sentient->eTeam)
        return 0;
    // SP: rumble the client victim on melee impact.
    if (traceEnt->client && wp->weapDef->meleeImpactRumble && *wp->weapDef->meleeImpactRumble)
    {
        traceEnt->r.svFlags &= ~1u;
        G_AddEvent(traceEnt, 70, G_RumbleIndex(wp->weapDef->meleeImpactRumble));
    }
    if (ent->client && traceEnt->sentient)
        G_AddEvent(ent, EV_MELEE_BLOOD, 0);

    if (traceEnt->client || traceEnt->actor)
        tent = G_TempEntity(endpos, 35);
    else
        tent = G_TempEntity(endpos, 36);
#elif KISAK_MP
    if (ent->client && traceEnt->client)
        G_AddEvent(ent, EV_MELEE_BLOOD, 0);
    if (traceEnt->client)
        tent = G_TempEntity(endpos, 35);
    else
        tent = G_TempEntity(endpos, 36);
#endif

    tent->s.otherEntityNum = traceEnt->s.number;
    v10 = wp->weapDef->knifeModel && ent->client;
    tent->s.eventParm = v10;
    tent->s.weapon = ent->s.weapon;
    tent->s.weaponModel = ent->s.weaponModel;
    if (traceEnt->s.number == ENTITYNUM_WORLD)
        return 0;
    if (!traceEnt->takedamage)
        return 0;
    partName = tr.partName;
    modelIndex = tr.modelIndex;
    partGroup = (hitLocation_t)tr.partGroup;
    v6 = G_rand();
    G_Damage(
        traceEnt,
        ent,
        ent,
        wp->forward,
        endpos,
        damage + v6 % 5,
        0,
        7,
        0xFFFFFFFF,
        partGroup,
        modelIndex,
        partName
#ifdef KISAK_MP
        , 0);
#elif KISAK_SP
        );
#endif
    return traceEnt;
}

char __cdecl Melee_Trace(
    gentity_s *ent,
    weaponParms *wp,
    int damage,
    float range,
    float width,
    float height,
    trace_t *trace,
    float *endPos)
{
    float v9; // [esp+Ch] [ebp-3Ch]
    float v10; // [esp+10h] [ebp-38h]
    float v11; // [esp+14h] [ebp-34h]
    float v12; // [esp+18h] [ebp-30h]
    float v13; // [esp+1Ch] [ebp-2Ch]
    float scale; // [esp+20h] [ebp-28h]
    int v15; // [esp+24h] [ebp-24h]
    float end[3]; // [esp+2Ch] [ebp-1Ch] BYREF
    float start[3]; // [esp+38h] [ebp-10h] BYREF
    int traceIndex; // [esp+44h] [ebp-4h]

    if (width > 0.0 || height > 0.0)
        v15 = 5;
    else
        v15 = 1;
    for (traceIndex = 0; traceIndex < v15; ++traceIndex)
    {
        Vec3Mad(wp->muzzleTrace, range, wp->forward, end);
        scale = width * (float)traceOffsets[traceIndex][0];
        Vec3Mad(end, scale, wp->right, end);
        v13 = height * (float)traceOffsets[traceIndex][1];
        Vec3Mad(end, v13, wp->up, end);
        if (melee_debug->current.enabled)
            G_DebugLineWithDuration(wp->muzzleTrace, end, colorRed, 1, 200);
        G_LocationalTrace(trace, wp->muzzleTrace, end, ent->s.number, 0x2806891, bulletPriorityMap);
        Vec3Lerp(wp->muzzleTrace, end, trace->fraction, endPos);
        if (!traceIndex)
            G_CheckHitTriggerDamage(ent, wp->muzzleTrace, endPos, damage, 7u);
        if ((trace->surfaceFlags & 0x10) == 0 && trace->fraction != 1.0)
        {
            if (melee_debug->current.enabled)
                G_DebugLineWithDuration(wp->muzzleTrace, endPos, colorGreen, 1, 200);
            return 1;
        }
    }
    for (traceIndex = 1; traceIndex + 1 < v15; traceIndex += 2)
    {
        Vec3Mad(wp->muzzleTrace, range, wp->forward, end);
        v12 = width * (float)traceOffsets[traceIndex][0];
        Vec3Mad(end, v12, wp->right, start);
        v11 = height * (float)traceOffsets[traceIndex][1];
        Vec3Mad(start, v11, wp->up, start);
        v10 = width * (float)traceOffsets[traceIndex + 1][0];
        Vec3Mad(end, v10, wp->right, end);
        v9 = height * (float)traceOffsets[traceIndex + 1][1];
        Vec3Mad(end, v9, wp->up, end);
        if (melee_debug->current.enabled)
            G_DebugLineWithDuration(start, end, colorRed, 1, 200);
        G_LocationalTrace(trace, start, end, ent->s.number, 0x2806891, bulletPriorityMap);
        Vec3Lerp(start, end, trace->fraction, endPos);
        if ((trace->surfaceFlags & 0x10) == 0 && !trace->startsolid && trace->fraction != 1.0)
            return 1;
    }
    return 0;
}

gentity_s *__cdecl Weapon_Throw_Grenade(
    gentity_s *ent,
    uint32_t grenType,
    uint8_t grenModel,
    weaponParms *wp)
{
    float scale; // [esp+4h] [ebp-50h]
    int fuseTime; // [esp+Ch] [ebp-48h]
    float iProjectileSpeedForward; // [esp+28h] [ebp-2Ch]
    gentity_s *m; // [esp+34h] [ebp-20h]
    float fAddVel; // [esp+38h] [ebp-1Ch]
    float vTossVel[3]; // [esp+3Ch] [ebp-18h] BYREF
    float forwardHoriz[3]; // [esp+48h] [ebp-Ch] BYREF

    if (!ent)
        MyAssertHandler(".\\game\\g_weapon.cpp", 302, 0, "%s", "ent");
    if (!wp)
        MyAssertHandler(".\\game\\g_weapon.cpp", 303, 0, "%s", "wp");
    scale = (float)wp->weapDef->iProjectileSpeed;
    Vec3Scale(wp->forward, scale, vTossVel);
    vTossVel[2] = (double)wp->weapDef->iProjectileSpeedUp + vTossVel[2];
    if (wp->weapDef->iProjectileSpeedForward)
    {
        forwardHoriz[0] = wp->forward[0];
        forwardHoriz[1] = wp->forward[1];
        Vec2NormalizeFast(forwardHoriz);
        iProjectileSpeedForward = (float)wp->weapDef->iProjectileSpeedForward;
        vTossVel[0] = iProjectileSpeedForward * forwardHoriz[0] + vTossVel[0];
        vTossVel[1] = iProjectileSpeedForward * forwardHoriz[1] + vTossVel[1];
    }
    if (ent->client)
        fuseTime = wp->weapDef->fuseTime;
    else
        fuseTime = wp->weapDef->aiFuseTime;
    LODWORD(forwardHoriz[2]) = fuseTime;
    m = G_FireGrenade(ent, wp->muzzleTrace, vTossVel, grenType, grenModel, 1, fuseTime);
    Vec3Normalize(vTossVel);
    fAddVel = Vec3Dot(ent->client->ps.velocity, vTossVel);
    Vec3Mad(m->s.lerp.pos.trDelta, fAddVel, vTossVel, m->s.lerp.pos.trDelta);
    if ((COERCE_UNSIGNED_INT(m->s.lerp.pos.trDelta[0]) & 0x7F800000) == 0x7F800000
        || (COERCE_UNSIGNED_INT(m->s.lerp.pos.trDelta[1]) & 0x7F800000) == 0x7F800000
        || (COERCE_UNSIGNED_INT(m->s.lerp.pos.trDelta[2]) & 0x7F800000) == 0x7F800000)
    {
        MyAssertHandler(
            ".\\game\\g_weapon.cpp",
            322,
            0,
            "%s",
            "!IS_NAN((m->s.lerp.pos.trDelta)[0]) && !IS_NAN((m->s.lerp.pos.trDelta)[1]) && !IS_NAN((m->s.lerp.pos.trDelta)[2])");
    }
    return m;
}

gentity_s *__cdecl Weapon_GrenadeLauncher_Fire(
    gentity_s *ent,
    uint32_t grenType,
    uint8_t grenModel,
    weaponParms *wp)
{
    float scale; // [esp+4h] [ebp-34h]
    gentity_s *m; // [esp+24h] [ebp-14h]
    float fAddVel; // [esp+28h] [ebp-10h]
    float vTossVel[3]; // [esp+2Ch] [ebp-Ch] BYREF

    if (!ent)
        MyAssertHandler(".\\game\\g_weapon.cpp", 334, 0, "%s", "ent");
    if (!wp)
        MyAssertHandler(".\\game\\g_weapon.cpp", 335, 0, "%s", "wp");
    scale = (float)wp->weapDef->iProjectileSpeed;
    Vec3Scale(wp->forward, scale, vTossVel);
    vTossVel[2] = (double)wp->weapDef->iProjectileSpeedUp + vTossVel[2];
    m = G_FireGrenade(ent, wp->muzzleTrace, vTossVel, grenType, grenModel, 0, 0);
    m->flags |= FL_STABLE_MISSILES;
    Vec3Normalize(vTossVel);
    fAddVel = Vec3Dot(ent->client->ps.velocity, vTossVel);
    Vec3Mad(m->s.lerp.pos.trDelta, fAddVel, vTossVel, m->s.lerp.pos.trDelta);
    if ((COERCE_UNSIGNED_INT(m->s.lerp.pos.trDelta[0]) & 0x7F800000) == 0x7F800000
        || (COERCE_UNSIGNED_INT(m->s.lerp.pos.trDelta[1]) & 0x7F800000) == 0x7F800000
        || (COERCE_UNSIGNED_INT(m->s.lerp.pos.trDelta[2]) & 0x7F800000) == 0x7F800000)
    {
        MyAssertHandler(
            ".\\game\\g_weapon.cpp",
            347,
            0,
            "%s",
            "!IS_NAN((m->s.lerp.pos.trDelta)[0]) && !IS_NAN((m->s.lerp.pos.trDelta)[1]) && !IS_NAN((m->s.lerp.pos.trDelta)[2])");
    }
    return m;
}

gentity_s *__cdecl Weapon_RocketLauncher_Fire(
    gentity_s *ent,
    uint32_t weaponIndex,
    float spread,
    weaponParms *wp,
    const float *gunVel,
    gentity_s *target,
    const float *targetOffset)
{
    float v8; // [esp+Ch] [ebp-44h]
    float v9; // [esp+10h] [ebp-40h]
    float fAimOffset; // [esp+28h] [ebp-28h]
    gentity_s *m; // [esp+2Ch] [ebp-24h]
    float r; // [esp+30h] [ebp-20h] BYREF
    float dir[3]; // [esp+34h] [ebp-1Ch] BYREF
    float launchpos[3]; // [esp+40h] [ebp-10h] BYREF
    float u; // [esp+4Ch] [ebp-4h] BYREF

    if (!ent)
        MyAssertHandler(".\\game\\g_weapon.cpp", 362, 0, "%s", "ent");
    if (!wp)
        MyAssertHandler(".\\game\\g_weapon.cpp", 363, 0, "%s", "wp");
    v9 = spread * 0.01745329238474369;
    v8 = tan(v9);
    fAimOffset = v8 * 16.0;
    gunrandom(&r, &u);
    r = r * fAimOffset;
    u = u * fAimOffset;
    Vec3Scale(wp->forward, 16.0, dir);
    Vec3Mad(dir, r, wp->right, dir);
    Vec3Mad(dir, u, wp->up, dir);
    Vec3Normalize(dir);
    launchpos[0] = wp->muzzleTrace[0];
    launchpos[1] = wp->muzzleTrace[1];
    launchpos[2] = wp->muzzleTrace[2];
    m = G_FireRocket(ent, weaponIndex, launchpos, dir, gunVel, target, targetOffset);
    if (ent->client)
        Vec3Mad(ent->client->ps.velocity, -64.0, wp->forward, ent->client->ps.velocity);
    return m;
}

void __cdecl gunrandom(float *x, float *y)
{
    float v2; // [esp+8h] [ebp-14h]
    float sinT; // [esp+Ch] [ebp-10h]
    float theta; // [esp+10h] [ebp-Ch]
    float r; // [esp+14h] [ebp-8h]
    float cosT; // [esp+18h] [ebp-4h]

    theta = G_random() * 360.0;
    r = G_random();
    v2 = theta * 0.01745329238474369;
    cosT = cos(v2);
    sinT = sin(v2);
    *x = r * cosT;
    *y = r * sinT;
}

bool __cdecl LogAccuracyHit(gentity_s *target, gentity_s *attacker)
{
    iassert(target);
    iassert(attacker);

    if (!target->takedamage)
        return 0;
    if (target == attacker)
        return 0;
    if (!target->client)
        return 0;
    if (!attacker->client)
        return 0;
#ifdef KISAK_MP
    if (target->client->ps.pm_type < PM_DEAD)
        return !OnSameTeam(target, attacker);
#elif KISAK_SP
    if (target->client->ps.stats[0] <= 0)
    {
        return 0;
    }
#endif
    return 0;
}

// OpenXR blocked-muzzle state used by FireWeapon.
bool VR_ShouldSuppressRightControllerBlockedMuzzleShot();

#if defined(KISAK_SP) && !defined(DEDICATED)
// KISAK_VR_SCOPE_BALLISTIC_CONVERGENCE_V1
// Trace the visible optic ray first, then turn the physical tag_flash ray
// toward that exact target.  A nearer obstruction seen only from the muzzle
// remains authoritative when Bullet_Fire performs its normal trace.
static bool VR_ConvergePhysicalSniperShot(
    const gentity_s* attacker,
    weaponParms* wp)
{
    if (attacker == nullptr ||
        wp == nullptr ||
        wp->weapDef == nullptr ||
        !wp->weapDef->bRifleBullet)
    {
        return false;
    }

    float scopeOrigin[3] = {};
    float scopeForward[3] = {};

    if (!VR_GetPhysicalSniperScopeAimWorld(
            scopeOrigin,
            scopeForward))
    {
        return false;
    }

    constexpr float kScopeTraceRange = 8192.0f;

    float scopeEnd[3] = {};
    Vec3Mad(
        scopeOrigin,
        kScopeTraceRange,
        scopeForward,
        scopeEnd);

    trace_t scopeTrace = {};

    G_LocationalTraceAllowChildren(
        &scopeTrace,
        scopeOrigin,
        scopeEnd,
        attacker->s.number,
        0x2806831,
        riflePriorityMap);

    float convergenceTarget[3] = {
        scopeEnd[0],
        scopeEnd[1],
        scopeEnd[2],
    };

    if (!scopeTrace.startsolid &&
        scopeTrace.fraction < 1.0f)
    {
        Vec3Lerp(
            scopeOrigin,
            scopeEnd,
            scopeTrace.fraction,
            convergenceTarget);
    }

    float convergedForward[3] = {};
    Vec3Sub(
        convergenceTarget,
        wp->muzzleTrace,
        convergedForward);

    if (Vec3Normalize(convergedForward) <= 0.0001f)
    {
        return false;
    }

    wp->forward[0] = convergedForward[0];
    wp->forward[1] = convergedForward[1];
    wp->forward[2] = convergedForward[2];

    static bool loggedVrScopeConvergence = false;

    if (!loggedVrScopeConvergence)
    {
        Com_Printf(
            0,
            "[VR] Physical sniper bullets now converge from "
            "tag_flash onto the scope-reticle trace.\n");

        loggedVrScopeConvergence = true;
    }

    return true;
}
#endif

void __cdecl FireWeapon(gentity_s *ent, int gametime)
{
    float offset[3]; // [esp+18h] [ebp-60h] BYREF
    float fAimSpreadAmount; // [esp+24h] [ebp-54h]
    float minSpread; // [esp+28h] [ebp-50h] BYREF
    float maxSpread; // [esp+2Ch] [ebp-4Ch] BYREF
    weaponParms wp; // [esp+30h] [ebp-48h] BYREF
    float aimSpreadScale; // [esp+74h] [ebp-4h]

// Optional
// Prevents AI from taking damage while being shot at.
//#ifdef KISAK_SP
//	if ((ent->client->ps.weapFlags & 8) != 0)// g_friendlyfireDist
//		return;
//#endif
    {
        Scr_Notify(ent, scr_const.weapon_fired, 0);
        wp.weapDef = BG_GetWeaponDef(ent->s.weapon);
        CalcMuzzlePoints(ent, &wp);
        aimSpreadScale = ent->client->currentAimSpreadScale;
        BG_GetSpreadForWeapon(&ent->client->ps, wp.weapDef, &minSpread, &maxSpread);
        if (ent->client->ps.fWeaponPosFrac == 1.0)
            fAimSpreadAmount = (maxSpread - wp.weapDef->fAdsSpread) * aimSpreadScale + wp.weapDef->fAdsSpread;
        else
            fAimSpreadAmount = (maxSpread - minSpread) * aimSpreadScale + minSpread;

        if (wp.weapDef->weapType)
        {
            if (wp.weapDef->weapType == WEAPTYPE_GRENADE)
            {
                Weapon_Throw_Grenade(ent, ent->s.weapon, ent->s.weaponModel, &wp);
            }
            else if (wp.weapDef->weapType == WEAPTYPE_PROJECTILE)
            {
                if (wp.weapDef->weapClass == WEAPCLASS_GRENADE)
                {
                    Weapon_GrenadeLauncher_Fire(ent, ent->s.weapon, ent->s.weaponModel, &wp);
                }
                else
                {
                    gentity_s *target;
#ifdef KISAK_SP
                    // IDA: lock-on rockets pass the locked target + its offset
                    if ((ent->client->ps.weapLockFlags & 2) != 0)
                    {
                        target = &level.gentities[ent->client->ps.weapLockedEntnum];
                        G_TargetGetOffset(target, offset);
                    }
                    else
#endif
                    {
                        target = NULL;
                        Vec3Clear(offset);
                    }

                    Weapon_RocketLauncher_Fire(ent, ent->s.weapon, fAimSpreadAmount, &wp, vec3_origin, target, offset);
                }
            }
            else
            {
                Com_Error(ERR_DROP, "Unknown weapon type %i for %s", wp.weapDef->weapType, wp.weapDef->szInternalName);
            }
        }
        else
        {
#if (defined(KISAK_MP) || defined(KISAK_SP)) && !defined(DEDICATED)
            if (VR_ShouldSuppressRightControllerBlockedMuzzleShot())
            {
                static bool loggedSuppressedVrBlockedMuzzleShot = false;

                if (!loggedSuppressedVrBlockedMuzzleShot)
                {
                    Com_Printf(
                        0,
                        "[VR] Suppressed authoritative bullet because "
                        "the physical muzzle is blocked.\n");

                    loggedSuppressedVrBlockedMuzzleShot = true;
                }

                return;
            }
#endif

#if defined(KISAK_SP) && !defined(DEDICATED)
            VR_ConvergePhysicalSniperShot(
                ent,
                &wp);
#endif

            Bullet_Fire(ent, fAimSpreadAmount, &wp, ent, gametime);
        }
    }
}

// VR aim snapshot used by CalcMuzzlePoints.
// Placed here so it shares CalcMuzzlePoints' active compile scope.
bool VR_GetRightControllerWeaponCommand(
    float* gunPitch,
    float* gunYaw,
    bool* attackPressed);


bool VR_GetRightControllerWeaponMuzzleWorld(
    float muzzleOrigin[3]);


void VR_SetRightControllerWeaponMuzzleBlocked(
    bool blocked);


void __cdecl CalcMuzzlePoints(const gentity_s *ent, weaponParms *wp)
{
    float viewang[3]; // [esp+4h] [ebp-Ch] BYREF

    iassert(ent->client);

    viewang[0] = ent->client->ps.viewangles[0];
    viewang[1] = ent->client->ps.viewangles[1];
    viewang[2] = ent->client->ps.viewangles[2];
#ifdef KISAK_SP
    if ((ent->client->ps.eFlags & 0x20000) == 0)
#endif
    {
        viewang[0] = ent->client->fGunPitch;
        viewang[1] = ent->client->fGunYaw;
    }

#if (defined(KISAK_MP) || defined(KISAK_SP)) && !defined(DEDICATED)
    float vrGunPitch = 0.0f;
    float vrGunYaw = 0.0f;
    bool vrAttackPressed = false;

    if (VR_GetRightControllerWeaponCommand(
            &vrGunPitch,
            &vrGunYaw,
            &vrAttackPressed))
    {
        viewang[0] = vrGunPitch;
        viewang[1] = vrGunYaw;

        static bool loggedVrMuzzleAim = false;

        if (!loggedVrMuzzleAim)
        {
            Com_Printf(
                0,
                "[VR] Applied right-controller aim at "
                "CalcMuzzlePoints.\n");

#if defined(KISAK_SP)
            Com_Printf(
                0,
                "[VR] Applied tracked weapon direction "
                "in SP authoritative fire.\n");
#endif

            loggedVrMuzzleAim = true;
        }
    }
    else
    {
        static bool loggedMissingVrMuzzleAim = false;

        if (!loggedMissingVrMuzzleAim)
        {
            Com_PrintWarning(
                0,
                "[VR] CalcMuzzlePoints could not read a valid "
                "right-controller aim snapshot.\n");

            loggedMissingVrMuzzleAim = true;
        }
    }
#endif

    AngleVectors(viewang, wp->forward, wp->right, wp->up);

#if (defined(KISAK_MP) || defined(KISAK_SP)) && !defined(DEDICATED)
    if (vrAttackPressed)
    {
        static int vrMuzzleDiagnosticCount = 0;

        if (vrMuzzleDiagnosticCount < 20)
        {
            Com_Printf(
                0,
                "[VR] Muzzle aim diagnostic %d: "
                "controller angles %.3f %.3f, "
                "player angles %.3f %.3f, "
                "forward %.4f %.4f %.4f.\n",
                vrMuzzleDiagnosticCount,
                vrGunPitch,
                vrGunYaw,
                ent->client->ps.viewangles[0],
                ent->client->ps.viewangles[1],
                wp->forward[0],
                wp->forward[1],
                wp->forward[2]);

            ++vrMuzzleDiagnosticCount;
        }
    }
#endif

    G_GetPlayerViewOrigin(&ent->client->ps, wp->muzzleTrace);

    bool vrUsingPhysicalMuzzle = false;

#if (defined(KISAK_MP) || defined(KISAK_SP)) && !defined(DEDICATED)
    float vrMuzzleWorld[3] = {};

    // Default to clear on every CalcMuzzlePoints call so stale state cannot
    // suppress a later shot after the gun has moved away from geometry.
    VR_SetRightControllerWeaponMuzzleBlocked(
        false);

    if (VR_GetRightControllerWeaponMuzzleWorld(
            vrMuzzleWorld))
    {
        trace_t vrMuzzleObstruction = {};

        G_LocationalTraceAllowChildren(
            &vrMuzzleObstruction,
            wp->muzzleTrace,
            vrMuzzleWorld,
            ent->s.number,
            0x2806831,
            bulletPriorityMap);

        const bool vrMuzzlePathClear =
            !vrMuzzleObstruction.startsolid &&
            vrMuzzleObstruction.fraction >= 1.0f;

        if (vrMuzzlePathClear)
        {
            VR_SetRightControllerWeaponMuzzleBlocked(
                false);

            wp->muzzleTrace[0] = vrMuzzleWorld[0];
            wp->muzzleTrace[1] = vrMuzzleWorld[1];
            wp->muzzleTrace[2] = vrMuzzleWorld[2];

            vrUsingPhysicalMuzzle = true;

            static bool loggedServerVrPhysicalMuzzle = false;

            if (!loggedServerVrPhysicalMuzzle)
            {
                Com_Printf(
                    0,
                    "[VR] Server bullets now originate "
                    "at tracked tag_flash.\n");

                loggedServerVrPhysicalMuzzle = true;
            }
        }
        else
        {
            VR_SetRightControllerWeaponMuzzleBlocked(
                true);

            static bool loggedServerVrMuzzleBlocked = false;

            if (!loggedServerVrMuzzleBlocked)
            {
                Com_Printf(
                    0,
                    "[VR] Server physical muzzle was blocked; "
                    "marking the shot for suppression.\n");

                loggedServerVrMuzzleBlocked = true;
            }
        }
    }
#endif

#ifdef KISAK_SP
    if (!vrUsingPhysicalMuzzle)
    {
        wp->muzzleTrace[0] =
            ent->client->fGunXOfs + wp->muzzleTrace[0];

        wp->muzzleTrace[1] =
            ent->client->fGunYOfs + wp->muzzleTrace[1];

        wp->muzzleTrace[2] =
            ent->client->fGunZOfs + wp->muzzleTrace[2];
    }
#endif
}

#ifdef KISAK_SP
// KISAK_SP_VR_MANUAL_GRENADE_THROW_V53
// A controller can reach through thin world geometry.  Clamp the physical
// release point to a normal arm-length segment from the authoritative player
// view, then stop just before the first obstruction on that segment.
static void VR_ClampManualGrenadeStart(
    const gentity_s* ent,
    float start[3])
{
    float viewOrigin[3] = {};

    G_GetPlayerViewOrigin(
        &ent->client->ps,
        viewOrigin);

    float viewToHand[3] = {
        start[0] - viewOrigin[0],
        start[1] - viewOrigin[1],
        start[2] - viewOrigin[2],
    };

    float viewToHandDistance =
        Vec3Length(viewToHand);

    constexpr float maximumReach = 64.0f;

    if (viewToHandDistance > maximumReach)
    {
        const float reachScale =
            maximumReach /
            viewToHandDistance;

        start[0] =
            viewOrigin[0] +
            viewToHand[0] * reachScale;

        start[1] =
            viewOrigin[1] +
            viewToHand[1] * reachScale;

        start[2] =
            viewOrigin[2] +
            viewToHand[2] * reachScale;

        viewToHand[0] = start[0] - viewOrigin[0];
        viewToHand[1] = start[1] - viewOrigin[1];
        viewToHand[2] = start[2] - viewOrigin[2];

        viewToHandDistance = maximumReach;
    }

    trace_t releaseTrace = {};

    G_LocationalTrace(
        &releaseTrace,
        viewOrigin,
        start,
        ent->s.number,
        0x2806891,
        bulletPriorityMap);

    if (releaseTrace.startsolid)
    {
        start[0] = viewOrigin[0];
        start[1] = viewOrigin[1];
        start[2] = viewOrigin[2];
    }
    else if (releaseTrace.fraction < 1.0f)
    {
        const float surfacePaddingFraction =
            viewToHandDistance > 0.001f
                ? 2.0f / viewToHandDistance
                : 0.0f;

        float safeFraction =
            releaseTrace.fraction -
            surfacePaddingFraction;

        if (safeFraction < 0.0f)
        {
            safeFraction = 0.0f;
        }

        start[0] =
            viewOrigin[0] +
            viewToHand[0] * safeFraction;

        start[1] =
            viewOrigin[1] +
            viewToHand[1] * safeFraction;

        start[2] =
            viewOrigin[2] +
            viewToHand[2] * safeFraction;
    }
}


// KISAK_SP_VR_MANUAL_GRENADE_RELEASE_CALIBRATION_V54
// Controller velocity is measured in real-world inches per second. COD4's
// stock grenade assets use deliberately exaggerated projectile speeds, so a
// raw one-to-one velocity override turns a natural VR throw into a short drop.
// Preserve the physical horizontal direction and strength variation while
// mapping a real throw onto the selected grenade's native launch-speed scale.
// A genuinely still release remains an intentional drop.
static void VR_BuildManualGrenadeLaunchVelocity(
    const WeaponDef* weaponDef,
    const float physicalVelocity[3],
    const float fallbackForward[3],
    float launchVelocity[3],
    bool* deliberateDrop,
    bool* usedFallbackDirection,
    float* normalizedStrength)
{
    if (weaponDef == nullptr ||
        physicalVelocity == nullptr ||
        fallbackForward == nullptr ||
        launchVelocity == nullptr ||
        deliberateDrop == nullptr ||
        usedFallbackDirection == nullptr ||
        normalizedStrength == nullptr)
    {
        return;
    }

    constexpr float deliberateDropSpeed = 35.0f;
    constexpr float fullStrengthHandSpeed = 260.0f;
    constexpr float stableHorizontalDirectionSpeed = 45.0f;

    const float physicalSpeed =
        Vec3Length(physicalVelocity);

    *deliberateDrop =
        physicalSpeed < deliberateDropSpeed;

    *usedFallbackDirection = false;
    *normalizedStrength = 0.0f;

    if (*deliberateDrop)
    {
        launchVelocity[0] = physicalVelocity[0];
        launchVelocity[1] = physicalVelocity[1];
        launchVelocity[2] = physicalVelocity[2];
        return;
    }

    float strength =
        (physicalSpeed - deliberateDropSpeed) /
        (fullStrengthHandSpeed - deliberateDropSpeed);

    if (strength < 0.0f)
    {
        strength = 0.0f;
    }
    else if (strength > 1.0f)
    {
        strength = 1.0f;
    }

    *normalizedStrength = strength;

    float horizontalDirection[2] = {
        physicalVelocity[0],
        physicalVelocity[1],
    };

    float horizontalDirectionLength =
        Vec2Length(horizontalDirection);

    if (horizontalDirectionLength <
        stableHorizontalDirectionSpeed)
    {
        horizontalDirection[0] = fallbackForward[0];
        horizontalDirection[1] = fallbackForward[1];
        horizontalDirectionLength =
            Vec2Length(horizontalDirection);
        *usedFallbackDirection = true;
    }

    if (horizontalDirectionLength < 0.001f)
    {
        horizontalDirection[0] = 1.0f;
        horizontalDirection[1] = 0.0f;
        horizontalDirectionLength = 1.0f;
        *usedFallbackDirection = true;
    }

    horizontalDirection[0] /=
        horizontalDirectionLength;

    horizontalDirection[1] /=
        horizontalDirectionLength;

    float nativeHorizontalSpeed =
        static_cast<float>(
            weaponDef->iProjectileSpeed);

    if (weaponDef->iProjectileSpeedForward > 0)
    {
        nativeHorizontalSpeed +=
            static_cast<float>(
                weaponDef->iProjectileSpeedForward);
    }

    if (nativeHorizontalSpeed < 1.0f)
    {
        // A player-thrown grenade asset should always configure this, but a
        // safe stock-like fallback prevents a malformed mission asset from
        // reverting to V53's zero-distance drop.
        nativeHorizontalSpeed = 700.0f;
    }

    const float horizontalLaunchSpeed =
        nativeHorizontalSpeed *
        (0.70f + 0.45f * strength);

    launchVelocity[0] =
        horizontalDirection[0] *
        horizontalLaunchSpeed;

    launchVelocity[1] =
        horizontalDirection[1] *
        horizontalLaunchSpeed;

    float nativeUpSpeed =
        static_cast<float>(
            weaponDef->iProjectileSpeedUp);

    if (nativeUpSpeed < 0.0f)
    {
        nativeUpSpeed = 0.0f;
    }

    float verticalLaunchSpeed =
        nativeUpSpeed +
        physicalVelocity[2] * 0.65f;

    float minimumArcSpeed =
        nativeUpSpeed * 0.35f;

    if (minimumArcSpeed < 80.0f)
    {
        minimumArcSpeed = 80.0f;
    }

    float maximumArcSpeed =
        nativeUpSpeed +
        nativeHorizontalSpeed * 0.55f;

    if (maximumArcSpeed < 400.0f)
    {
        maximumArcSpeed = 400.0f;
    }

    if (verticalLaunchSpeed < minimumArcSpeed)
    {
        verticalLaunchSpeed = minimumArcSpeed;
    }
    else if (verticalLaunchSpeed > maximumArcSpeed)
    {
        verticalLaunchSpeed = maximumArcSpeed;
    }

    launchVelocity[2] =
        verticalLaunchSpeed;
}


static gentity_s* VR_ThrowManualGrenade(
    gentity_s* ent,
    const uint32_t grenadeWeaponIndex,
    const uint8_t grenadeModel,
    const int fuseTimeMilliseconds,
    float start[3],
    float velocity[3])
{
    gentity_s* grenade =
        G_FireGrenade(
            ent,
            start,
            velocity,
            grenadeWeaponIndex,
            grenadeModel,
            1,
            fuseTimeMilliseconds);

    const float throwSpeed =
        Vec3Length(velocity);

    if (throwSpeed > 0.001f)
    {
        float throwDirection[3] = {
            velocity[0],
            velocity[1],
            velocity[2],
        };

        Vec3Scale(
            throwDirection,
            1.0f / throwSpeed,
            throwDirection);

        const float inheritedPlayerSpeed =
            Vec3Dot(
                ent->client->ps.velocity,
                throwDirection);

        Vec3Mad(
            grenade->s.lerp.pos.trDelta,
            inheritedPlayerSpeed,
            throwDirection,
            grenade->s.lerp.pos.trDelta);
    }

    return grenade;
}
#endif


void __cdecl G_UseOffHand(gentity_s *ent)
{
    weaponParms wp; // [esp+0h] [ebp-40h] BYREF

    if (!ent->client)
        MyAssertHandler(".\\game\\g_weapon.cpp", 545, 0, "%s", "ent->client");
    if (!ent->client->ps.offHandIndex)
        MyAssertHandler(".\\game\\g_weapon.cpp", 546, 0, "%s", "ent->client->ps.offHandIndex != WP_NONE");
    wp.weapDef = BG_GetWeaponDef(ent->client->ps.offHandIndex);
    if (wp.weapDef->weapType != WEAPTYPE_GRENADE)
        MyAssertHandler(".\\game\\g_weapon.cpp", 550, 0, "%s", "wp.weapDef->weapType == WEAPTYPE_GRENADE");
    CalcMuzzlePoints(ent, &wp);

#ifdef KISAK_SP
    float manualReleaseOrigin[3] = {};
    float manualPhysicalVelocity[3] = {};
    float manualReleaseFallbackForward[3] = {};
    unsigned int manualVelocitySampleAgeMilliseconds = 0u;
    unsigned int manualReleaseAgeMilliseconds = 0u;

    if (VR_ConsumeManualGrenadeThrow(
            ent->client->ps.offHandIndex,
            manualReleaseOrigin,
            manualPhysicalVelocity,
            manualReleaseFallbackForward,
            &manualVelocitySampleAgeMilliseconds,
            &manualReleaseAgeMilliseconds))
    {
        VR_ClampManualGrenadeStart(
            ent,
            manualReleaseOrigin);

        const int remainingFuseMilliseconds =
            ent->client->ps.grenadeTimeLeft;

        const int projectileFuseMilliseconds =
            remainingFuseMilliseconds > 0
                ? remainingFuseMilliseconds
                : wp.weapDef->fuseTime;

        float calibratedLaunchVelocity[3] = {};
        bool deliberateDrop = false;
        bool usedFallbackDirection = false;
        float normalizedStrength = 0.0f;

        VR_BuildManualGrenadeLaunchVelocity(
            wp.weapDef,
            manualPhysicalVelocity,
            manualReleaseFallbackForward,
            calibratedLaunchVelocity,
            &deliberateDrop,
            &usedFallbackDirection,
            &normalizedStrength);

        gentity_s* grenade =
            VR_ThrowManualGrenade(
                ent,
                ent->client->ps.offHandIndex,
                ent->client->ps.weaponmodels[
                    ent->client->ps.offHandIndex],
                projectileFuseMilliseconds,
                manualReleaseOrigin,
                calibratedLaunchVelocity);

        Com_Printf(
            0,
            "[VR][GRENADE] Launched weapon %u from %.2f %.2f %.2f "
            "after %u ms; selected hand velocity %.2f %.2f %.2f "
            "(sample age %u ms, speed %.2f); calibrated launch "
            "%.2f %.2f %.2f (speed %.2f, strength %.2f, direction %s, "
            "deliberate drop %s); native speeds %d/%d/%d; resulting "
            "velocity %.2f %.2f %.2f; fuse %d ms.\n",
            ent->client->ps.offHandIndex,
            manualReleaseOrigin[0],
            manualReleaseOrigin[1],
            manualReleaseOrigin[2],
            manualReleaseAgeMilliseconds,
            manualPhysicalVelocity[0],
            manualPhysicalVelocity[1],
            manualPhysicalVelocity[2],
            manualVelocitySampleAgeMilliseconds,
            Vec3Length(manualPhysicalVelocity),
            calibratedLaunchVelocity[0],
            calibratedLaunchVelocity[1],
            calibratedLaunchVelocity[2],
            Vec3Length(calibratedLaunchVelocity),
            normalizedStrength,
            usedFallbackDirection
                ? "view-forward fallback"
                : "physical horizontal",
            deliberateDrop
                ? "yes"
                : "no",
            wp.weapDef->iProjectileSpeed,
            wp.weapDef->iProjectileSpeedForward,
            wp.weapDef->iProjectileSpeedUp,
            grenade->s.lerp.pos.trDelta[0],
            grenade->s.lerp.pos.trDelta[1],
            grenade->s.lerp.pos.trDelta[2],
            projectileFuseMilliseconds);

        return;
    }
#endif

    Weapon_Throw_Grenade(
        ent,
        ent->client->ps.offHandIndex,
        ent->client->ps.weaponmodels[ent->client->ps.offHandIndex],
        &wp);
}

void __cdecl FireWeaponMelee(gentity_s *ent, int gametime)
{
    weaponParms wp; // [esp+10h] [ebp-40h] BYREF

#ifdef KISAK_MP
    if ((ent->client->ps.eFlags & 0x300) == 0 || !ent->active)
#elif KISAK_SP
    if ((ent->client->ps.eFlags & 0x20300) == 0 || !ent->active)
#endif
    {
        wp.weapDef = BG_GetWeaponDef(ent->s.weapon);

#ifdef KISAK_SP
        // The normal SP melee path traces from ps.viewangles, but the HMD
        // and tracked weapon do not directly modify those body angles.
        // Reuse the same authoritative tracked origin/direction already
        // used for firearm shots so melee follows the visible right hand.
        CalcMuzzlePoints(
            ent,
            &wp);

        static bool loggedVrTrackedMeleeTrace = false;

        if (!loggedVrTrackedMeleeTrace)
        {
            Com_Printf(
                0,
                "[VR] SP melee trace now uses the tracked weapon "
                "origin and direction.\n");

            loggedVrTrackedMeleeTrace = true;
        }
#else
        G_GetPlayerViewOrigin(
            &ent->client->ps,
            wp.muzzleTrace);

        BG_GetPlayerViewDirection(
            &ent->client->ps,
            wp.forward,
            wp.right,
            wp.up);
#endif
        if (!player_meleeRange)
            MyAssertHandler(".\\game\\g_weapon.cpp", 577, 0, "%s", "player_meleeRange");
        if (!player_meleeWidth)
            MyAssertHandler(".\\game\\g_weapon.cpp", 578, 0, "%s", "player_meleeWidth");
        if (!player_meleeHeight)
            MyAssertHandler(".\\game\\g_weapon.cpp", 579, 0, "%s", "player_meleeHeight");
        Weapon_Melee(
            ent,
            &wp,
            player_meleeRange->current.value,
            player_meleeWidth->current.value,
            player_meleeHeight->current.value,
            gametime);
    }
}

int __cdecl G_GivePlayerWeapon(playerState_s *pPS, int iWeaponIndex, uint8_t altModelIndex)
{
    int iCurrIndex; // [esp+0h] [ebp-10h]
    int newOffHandIndex; // [esp+4h] [ebp-Ch]
    WeaponDef *oldWeapDef; // [esp+8h] [ebp-8h]
    WeaponDef *weapDef; // [esp+Ch] [ebp-4h]

    if (!pPS)
    {
        MyAssertHandler(".\\game\\g_weapon.cpp", 597, 0, "%s", "pPS");
        MyAssertHandler("c:\\trees\\cod3\\src\\bgame\\../bgame/bg_weapons.h", 229, 0, "%s", "ps");
    }
    if (Com_BitCheckAssert(pPS->weapons, iWeaponIndex, 16))
        return 0;
    weapDef = BG_GetWeaponDef(iWeaponIndex);
    if (weapDef->weapClass == WEAPCLASS_TURRET)
        return 0;
    if (weapDef->weapClass == WEAPCLASS_NON_PLAYER)
        return 0;
    if (!weapDef->gunXModel[altModelIndex])
        return 0;
    if (level.clientIsSpawning)
        MyAssertHandler(
            ".\\game\\g_weapon.cpp",
            612,
            0,
            "%s\n\t(weapDef->szDisplayName) = %s",
            "(!level.clientIsSpawning)",
            weapDef->szDisplayName);
    Com_BitSetAssert(pPS->weapons, iWeaponIndex, 16);
    Com_BitClearAssert(pPS->weaponrechamber, iWeaponIndex, 16);
    Com_BitClearAssert(pPS->weaponold, iWeaponIndex, 16);
    pPS->weaponmodels[iWeaponIndex] = altModelIndex;
    if (weapDef->weapClass == WEAPCLASS_ITEM)
        return 1;
    if (weapDef->offhandClass)
    {
        if (pPS->offHandIndex)
        {
            if (BG_WeaponAmmo(pPS, pPS->offHandIndex) <= 0)
            {
                oldWeapDef = BG_GetWeaponDef(pPS->offHandIndex);
                if (!oldWeapDef)
                    MyAssertHandler(".\\game\\g_weapon.cpp", 634, 0, "%s", "oldWeapDef");
                newOffHandIndex = BG_GetFirstAvailableOffhand(pPS, oldWeapDef->offhandClass);
                if (newOffHandIndex)
                    pPS->offHandIndex = newOffHandIndex;
                else
                    pPS->offHandIndex = iWeaponIndex;
                G_SetEquippedOffHand(pPS->clientNum, pPS->offHandIndex);
            }
        }
        else
        {
            pPS->offHandIndex = iWeaponIndex;
            G_SetEquippedOffHand(pPS->clientNum, pPS->offHandIndex);
        }
        return 1;
    }
    else
    {
        for (iCurrIndex = weapDef->altWeaponIndex; iCurrIndex; iCurrIndex = BG_GetWeaponDef(iCurrIndex)->altWeaponIndex)
        {
            if (!pPS)
                MyAssertHandler("c:\\trees\\cod3\\src\\bgame\\../bgame/bg_weapons.h", 229, 0, "%s", "ps");
            if (Com_BitCheckAssert(pPS->weapons, iCurrIndex, 16))
                break;
            Com_BitSetAssert(pPS->weapons, iCurrIndex, 16);
            Com_BitClearAssert(pPS->weaponrechamber, iWeaponIndex, 16);
            pPS->weaponmodels[iCurrIndex] = altModelIndex;
        }
        return 1;
    }
}

void __cdecl G_SetupWeaponDef()
{
    Com_DPrintf(17, "----------------------\n");
    Com_DPrintf(17, "Game: G_SetupWeaponDef\n");
    if (!bg_lastParsedWeaponIndex)
    {
        Com_SetWeaponInfoMemory(1);
        ClearRegisteredItems();
        BG_ClearWeaponDef();
#ifdef KISAK_MP
        G_GetWeaponIndexForName("defaultweapon_mp");
#elif KISAK_SP
        if (level.initializing)
            BG_GetWeaponIndexForName("defaultweapon", G_RegisterWeapon);
        else
            BG_FindWeaponIndexForName("defaultweapon");
#endif
    }
    Com_DPrintf(17, "----------------------\n");
}

uint32_t __cdecl G_GetWeaponIndexForName(const char *name)
{
    if (level.initializing)
        return BG_GetWeaponIndexForName(name, G_RegisterWeapon);
    else
        return BG_FindWeaponIndexForName(name);
}

void __cdecl G_SelectWeaponIndex(int clientNum, int iWeaponIndex)
{
#ifdef KISAK_MP
    SV_GameSendServerCommand(clientNum, SV_CMD_RELIABLE, va("%c %i", 97, iWeaponIndex));
#elif KISAK_SP
    SV_GameSendServerCommand(clientNum, va("sw %i", iWeaponIndex));
#endif
}

void __cdecl G_SetEquippedOffHand(int clientNum, uint32_t offHandIndex)
{
#ifdef KISAK_MP
    BG_AssertOffhandIndexOrNone(offHandIndex);
    SV_GameSendServerCommand(clientNum, SV_CMD_RELIABLE, va("%c %i", 67, offHandIndex));
#elif KISAK_SP
    BG_AssertOffhandIndexOrNone(offHandIndex);
    SV_GameSendServerCommand(clientNum, va("offhand %i", offHandIndex));
#endif
}
