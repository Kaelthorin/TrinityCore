// GroupControl - who commands a bot that is in a group with players, and how far a stance lets it go.
//
// Pure functions on plain values (templated on the guid type), so the rules are unit-tested without a server.
//
// A bot in a group with a player is "led": it has a controller, and it does what the group does (no world errands, orders
// reach it). The controller is its owner when the owner is in the group (handled by the snapshot builder, not here); with no
// owner in the group it is the group leader when that is a player, else the first player in the group. A group of bots only
// has none and the bots stay autonomous.

#pragma once

#include <vector>

namespace Playerbot::GroupControl {

template <class Guid>
struct Member
{
    Guid guid;
    bool human = false;   // a player (not a bot session)
};

// `others` are the group's members other than the bot itself, in group order.
template <class Guid>
Guid ResolveController(Guid const& leader, std::vector<Member<Guid>> const& others)
{
    for (auto const& m : others)
        if (m.human && m.guid == leader)
            return leader;
    for (auto const& m : others)
        if (m.human)
            return m.guid;
    return Guid{};
}

// Passive: a mob already on the bot is always fought (it cannot be ignored); a mob on someone else is taken only while it is
// within `leash` yards of the controller, so a bot never leaves the group to chase one. leash <= 0 switches the leash off.
inline bool PassiveMayDefend(bool mob_on_self, float mob_to_controller, float leash)
{
    if (mob_on_self || leash <= 0.f)
        return true;
    return mob_to_controller <= leash;
}

// Aggressive: any hostile within `radius` yards of the controller, nothing beyond it.
inline bool AggressiveMayAttack(float mob_to_controller, float radius)
{
    return mob_to_controller <= radius;
}

}  // namespace Playerbot::GroupControl
