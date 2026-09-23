#pragma once

namespace kisak::vr::grenade_commands
{

// This mirrors only command notifications. It never changes native key state,
// grenade buttons, ammo, or whether COD4 accepts an attempted throw.
struct State
{
    bool held = false;
    bool releaseOwned = false;
    bool needsNeutral = true;
};

struct Edges
{
    bool pressed = false;
    bool released = false;
};

inline void Reset(State* state)
{
    if (state != nullptr)
        *state = {};
}

inline Edges Update(
    State* state,
    const bool gameplayAvailable,
    const bool virtualHeld,
    const bool nativeHeld)
{
    Edges edges;
    if (state == nullptr)
        return edges;
    if (!gameplayAvailable)
    {
        Reset(state);
        return edges;
    }
    if (state->needsNeutral)
    {
        state->needsNeutral = virtualHeld;
        return edges;
    }

    if (virtualHeld && !state->held)
    {
        // Native keyboard/mouse commands already notify their own listeners.
        edges.pressed = !nativeHeld;
        state->releaseOwned = edges.pressed;
    }
    else if (!virtualHeld && state->held)
    {
        edges.released = state->releaseOwned && !nativeHeld;
        state->releaseOwned = false;
    }
    state->held = virtualHeld;
    return edges;
}

} // namespace kisak::vr::grenade_commands
