// CompanionLeash - how far from the player it follows a led bot walks after its target (State_InCombat melee gap-close).
//
// Pure function on plain values, so the rule is unit-tested without a server.
//
// A bot the dungeon autopilot is suspended for (a human leads, Passive, a follow / stay order) fights where the player is. A mob
// standing 30 y or more from him is not walked after unless it is hitting the bot itself: it comes to the group, or the group goes
// to it on the player's order (UBRS 2026-10-26: the Urok challenge put its adds in combat with everyone on the map, and a passive
// companion warrior walked 180 y after one of them, pulling the Scarshield packs in between). The tank's own leash
// ([tank_chase], 30 y) is the same distance.
// The group's own fight is never leashed (the owner, 2026-10-27: "bots should always fight if you're fighting"): the player's own
// target, or a mob fighting a group member (within the leash of that member) who stands within the leash of the player, is walked
// after. The Urok adds, unreachable 180 y off with the tank beside the player on their threat lists, stay leashed.

#pragma once

namespace Playerbot::CompanionLeash {

constexpr float kLeashYd = 30.0f;

struct Inputs
{
    bool led = false;            // the autopilot is suspended for this bot (a human leads, Passive, follow / stay order)
    bool leader_known = false;   // the player it follows is alive on this map
    float victim_to_leader_d2 = 0.0f;
    bool victim_on_me = false;   // the mob is hitting this bot: it comes anyway
    bool leader_target = false;  // the player it follows has this mob as his target
    bool on_member_near_leader = false;   // the mob is on a group member (or a pet of one) within kLeashYd of the player, and
                                          // within kLeashYd of that member
};

inline bool StopsChase(Inputs const& in)
{
    return in.led && in.leader_known && !in.victim_on_me && !in.leader_target && !in.on_member_near_leader &&
           in.victim_to_leader_d2 > kLeashYd * kLeashYd;
}

} // namespace Playerbot::CompanionLeash
