// Playerbot V2 - script registration
// The module hangs off ScriptMgr: every core event it reacts to arrives through a WorldScript / PlayerScript /
// UnitScript / GroupScript / GuildScript hook and is forwarded to the matching Module handler. AddSC_playerbot_v2 is
// called from the generated AddModuleScripts() (src/modules).
//
// Lifecycle: Init once the world is loaded (before the realm goes online), the tick before the maps update (bot
// worker threads read the maps, so the module must not tick while they update), Shutdown with the world.

#include "PlayerbotV2.h"
#include "Config.h"
#include "DB2Stores.h"
#include "Group.h"
#include "Guild.h"
#include "Item.h"
#include "Log.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotMovement.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"

void AddSC_playerbot_v2_commandscript();

namespace
{
using Playerbot::V2::Module;

bool IsHuman(Player const* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot();
}

// PathGenerator::LiquidRouteCheck: a bot in a capital keeps out of canals (module liquid policy, PathLiquid.h). Sampling is along
// the whole path, not only its corners.
bool BotPathTouchesAvoidedLiquid(WorldObject const* source, Movement::PointsArray const& path)
{
    using namespace Playerbot::BotMovement;
    if (!g_LiquidPathPolicy)
        return false;
    Player* player = const_cast<Player*>(source->ToPlayer());
    if (!player || !player->GetSession() || !player->GetSession()->IsBot())
        return false;
    if (g_LiquidPathPolicy(player) == LiquidPolicy::None)
        return false;
    std::vector<PathPoint> pts;
    pts.reserve(path.size());
    for (G3D::Vector3 const& v : path)
        pts.push_back({ v.x, v.y, v.z });
    return PathTouchesLiquid(pts, [player](float x, float y, float z)
    {
        return ClassifyDestinationLiquid(player, x, y, z) != LiquidVerdict::Dry;
    });
}
}

class playerbot_v2_world : public WorldScript
{
public:
    playerbot_v2_world() : WorldScript("playerbot_v2_world") { }

    void OnWorldInitialized() override
    {
        auto value = [](char const* key) { return sConfigMgr->GetStringDefault(key, "(default)"); };
        TC_LOG_INFO("server.worldserver", "[startup] bots: Population.TotalTarget={} Gear.MaxTier={} World.Enable={} Raid.Enable={}",
            value("Playerbot.Population.TotalTarget"), value("Playerbot.Gear.MaxTier"), value("Playerbot.World.Enable"), value("Playerbot.Raid.Enable"));

        Module::instance().Init();
    }

    void OnBeforeMapsUpdate(uint32 diff) override
    {
        Module::instance().OnWorldUpdate(std::chrono::milliseconds(diff));
    }

    void OnShutdown() override
    {
        Module::instance().Shutdown();
    }

    // A real player is about to take a place in a running battleground (a bot is leaving for them): no second instance
    // is opened for this queue meanwhile.
    bool CanCreateBattleground(uint32 battlemasterListId, uint8 bracketId) override
    {
        return !Module::instance().HoldNewBattleground(battlemasterListId, bracketId);
    }
};

class playerbot_v2_player : public PlayerScript
{
public:
    playerbot_v2_player() : PlayerScript("playerbot_v2_player") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        Module::instance().OnPlayerLogin(player);
    }

    void OnLogout(Player* player) override
    {
        Module::instance().OnPlayerLogout(player);
    }

    void OnLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (player->GetLevel() > oldLevel)
            Module::instance().OnLevelUp(player, player->GetLevel());
    }

    // Owner's dead companions on this map release with the owner (inside a dungeon or raid RepopAtGraveyard revives the
    // owner at the graveyard, so no ghost state ever tells them). Called before the repop teleport, on the death map.
    void OnPlayerRepop(Player* player) override
    {
        Module::instance().OnSpiritReleased(player);
    }

    // A nearby bot reciprocates a text emote; the reactor needs the animation the emote plays.
    void OnTextEmote(Player* player, uint32 textEmote, uint32 /*emoteNum*/, ObjectGuid guid) override
    {
        if (EmotesTextEntry const* em = sEmotesTextStore.LookupEntry(textEmote))
            Module::instance().OnTextEmote(player, em->EmoteID, textEmote, guid);
    }

    void OnChatSent(Player* player, uint32 type, uint32 /*lang*/, std::string const& msg) override
    {
        if (type == CHAT_MSG_SAY)
            Module::instance().OnSayChat(player, msg);
        else if (type == CHAT_MSG_YELL)
            Module::instance().OnYellChat(player, msg);
    }

    void OnChatSent(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string const& msg, Player* receiver) override
    {
        Module::instance().OnWhisperReceived(player, receiver, msg);
    }

    // Squad commands and bot chatter in party, raid and instance chat (not raid warnings).
    void OnChatSent(Player* player, uint32 type, uint32 /*lang*/, std::string const& msg, Group* group) override
    {
        switch (type)
        {
            case CHAT_MSG_PARTY:
            case CHAT_MSG_PARTY_LEADER:
            case CHAT_MSG_RAID:
            case CHAT_MSG_RAID_LEADER:
            case CHAT_MSG_INSTANCE_CHAT:
            case CHAT_MSG_INSTANCE_CHAT_LEADER:
                Module::instance().OnPartyChat(player, group, msg);
                break;
            default:
                break;
        }
    }

    void OnChatSent(Player* player, uint32 type, uint32 /*lang*/, std::string const& msg, Guild* guild) override
    {
        if (type == CHAT_MSG_GUILD)
            Module::instance().OnGuildChat(player, guild->GetId(), msg);
    }

    // The ForeverBots add-on whispers a bot this way in battlegrounds, where the client's own /whisper does nothing.
    void OnAddonMessageSent(Player* player, uint32 /*type*/, std::string const& prefix, std::string const& text, Player* receiver) override
    {
        Module::instance().OnAddonWhisper(player, receiver, prefix, text);
    }

    // The ForeverBots panel sends its bot orders as addon messages (addon chat is blocked in combat and battlegrounds).
    void OnAddonMessageSent(Player* player, uint32 /*type*/, std::string const& prefix, std::string const& text, Group* group) override
    {
        Module::instance().OnAddonOrder(player, group, prefix, text);
    }

    void OnLootItem(Player* player, Item* item) override
    {
        Module::instance().OnItemLooted(player, item->GetEntry());
    }

    // A real player (or the group they lead) joined a battleground queue: bots fill the free slots.
    void OnBattlegroundQueueJoin(Player* player, uint32 bgTypeId, uint8 bracketId) override
    {
        if (IsHuman(player))
            Module::instance().OnPlayerJoinedBgQueue(player, bgTypeId, bracketId);
    }

    // Bots port within the tick the invite is sent (the snapshot poll can miss the accept window under load).
    void OnBattlegroundInvite(Player* player, uint32 instanceId, uint32 bgTypeId) override
    {
        Module::instance().OnBGInvitationReceived(player, instanceId, bgTypeId);
    }

    void OnBattlegroundPortFailed(Player* player, uint8 reason, uint32 instanceId) override
    {
        Module::instance().OnBGPortFailed(player, reason, instanceId);
    }

    // A real player joined the dungeon finder: bots fill the remaining roles.
    void OnLfgJoin(Player* player, uint32 dungeonId, uint8 roles) override
    {
        if (IsHuman(player))
            Module::instance().OnPlayerJoinedLfg(player, dungeonId, roles);
    }

    // Bots accept a proposal well inside the proposal time instead of waiting for the snapshot poll.
    void OnLfgProposal(Player* player, uint32 proposalId) override
    {
        Module::instance().OnLfgProposalReceived(player, proposalId);
    }
};

class playerbot_v2_unit : public UnitScript
{
public:
    playerbot_v2_unit() : UnitScript("playerbot_v2_unit") { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        Module::instance().OnDamageTaken(attacker, victim, int32(damage), 0);
    }

    // Auto-loot for bots on the tap list, and a bot's own death.
    void OnUnitDeath(Unit* victim, Unit* killer) override
    {
        Module::instance().OnDeath(victim, killer);
    }
};

class playerbot_v2_group : public GroupScript
{
public:
    playerbot_v2_group() : GroupScript("playerbot_v2_group") { }

    void OnSubgroupChange(Group* group, ObjectGuid guid, uint8 subgroup) override
    {
        Module::instance().OnRaidSubgroupChanged(group, guid, subgroup);
    }
};

class playerbot_v2_guild : public GuildScript
{
public:
    playerbot_v2_guild() : GuildScript("playerbot_v2_guild") { }

    // One online bot guildmate drops a welcome line a few seconds later.
    void OnAddMember(Guild* guild, Player* player, uint8 /*plRank*/) override
    {
        if (player)
            Module::instance().OnGuildMemberAdded(guild->GetId(), player->GetGUID(), player->GetName());
    }
};

void AddSC_playerbot_v2()
{
    new playerbot_v2_world();
    new playerbot_v2_player();
    new playerbot_v2_unit();
    new playerbot_v2_group();
    new playerbot_v2_guild();
    AddSC_playerbot_v2_commandscript();

    PathGenerator::SetLiquidRouteCheck(&BotPathTouchesAvoidedLiquid);
}
