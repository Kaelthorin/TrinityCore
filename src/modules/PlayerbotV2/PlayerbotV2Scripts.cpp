// Playerbot V2 - script registration
// The module's lifecycle runs on WorldScript hooks: Init once the world is loaded (before the realm goes online), the
// tick before the maps update (bot worker threads read the maps, so the module must not tick while they update), and
// Shutdown with the world. AddSC_playerbot_v2 is called from the generated AddModuleScripts() (src/modules).

#include "PlayerbotV2.h"
#include "Config.h"
#include "Log.h"
#include "ScriptMgr.h"

class playerbot_v2_world : public WorldScript
{
public:
    playerbot_v2_world() : WorldScript("playerbot_v2_world") { }

    void OnWorldInitialized() override
    {
        auto value = [](char const* key) { return sConfigMgr->GetStringDefault(key, "(default)"); };
        TC_LOG_INFO("server.worldserver", "[startup] bots: Population.TotalTarget={} Gear.MaxTier={} World.Enable={} Raid.Enable={}",
            value("Playerbot.Population.TotalTarget"), value("Playerbot.Gear.MaxTier"), value("Playerbot.World.Enable"), value("Playerbot.Raid.Enable"));

        Playerbot::V2::Module::instance().Init();
    }

    void OnBeforeMapsUpdate(uint32 diff) override
    {
        Playerbot::V2::Module::instance().OnWorldUpdate(std::chrono::milliseconds(diff));
    }

    void OnShutdown() override
    {
        Playerbot::V2::Module::instance().Shutdown();
    }
};

void AddSC_playerbot_v2()
{
    new playerbot_v2_world();
}
