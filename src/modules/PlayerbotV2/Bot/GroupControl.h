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

// Passive holds only while the human leading the group is not fighting: Passive keeps a bot from STARTING a fight, and once the
// player is in combat his bots fight with him (the owner, 2026-10-27: "bots should always fight if you're fighting").
inline bool PassiveHolds(bool passive_stance, bool leader_human, bool leader_alive, bool leader_in_combat)
{
    return passive_stance && !(leader_human && leader_alive && leader_in_combat);
}

// Which mobs a Passive bot joins once the player fights (PassiveHolds false): his own target, a mob within kFightJoinYd of him, or a
// mob on a group member who stands within kFightJoinYd of him while the mob is within kFightJoinYd of that member (it is fighting
// him there). Never a fight elsewhere on the map: UBRS 2026-10-26, passive bots ran across the instance after a companion tank that
// was in combat with unreachable Urok adds far from the player. Distances are 2D yards; a negative distance means unknown.
constexpr float kFightJoinYd = 40.0f;
inline bool LeaderFightMayJoin(bool leader_target, float mob_to_leader, float member_to_leader, float mob_to_member)
{
    if (leader_target)
        return true;
    if (mob_to_leader >= 0.f && mob_to_leader <= kFightJoinYd)
        return true;
    return member_to_leader >= 0.f && member_to_leader <= kFightJoinYd && mob_to_member >= 0.f && mob_to_member <= kFightJoinYd;
}

// A bot fighting its victim moves for the fight (its chase, the combat gap-close), not after its leader: the follow / formation of a
// grouped bot stands down while it has a living victim that is in the fight (the mob is in combat or has a victim, or the bot is in
// combat) or that the owner ordered it at, and the bot is within `kFightMoveLeashYd` of the leader. The follow re-issued every tick
// replaced the chase start_attack gave it, so a rogue next to a warlock owner casting from 30 y stood beside him and swung at nothing
// (UBRS 2026-10-27, Eman: "all he's doing is following me around when he should be fighting"). Beyond the leash the follow takes it back.
constexpr float kFightMoveLeashYd = 45.f;
inline bool FightOwnsMovement(bool live_victim, bool victim_in_fight, bool owner_ordered_attack, float bot_to_leader)
{
    return live_victim && (victim_in_fight || owner_ordered_attack) && bot_to_leader >= 0.f && bot_to_leader < kFightMoveLeashYd;
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

// A companion leaves its owner for good when the owner removes it from the group (a kick) or it leaves on its own, and it is let go like a
// dismissal. Not a battleground group (its bots come and go with the match), not a bot that is logging out, not an internal removal
// (method 0, e.g. a regroup) and not a kick by anyone but the owner. `method` is the core's RemoveMethod: 0 default, 1 kick, 2 leave, 3 LFG kick.
inline bool ReleaseCompanionOnRemoval(unsigned method, bool bg_group, bool kicked_by_owner, bool logging_out)
{
    if (bg_group || logging_out)
        return false;
    if (method == 1)
        return kicked_by_owner;
    return method == 2;
}

}  // namespace Playerbot::GroupControl
