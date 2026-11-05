#include "mod/MyMod.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

#include <pl/Mod.hpp>
#include <pl/Input.hpp>

namespace clange_me {

namespace {
std::once_flag gInputCallbackOnce;
std::atomic_bool gInputCallbackEnabled{false};

// Minecraft Core Pipeline Bridge: 
// Yeh function game ke internal execution loop ke sath synchronise hokar commands run karega.
void executeVanillaNativeAction(int targetActionId) {
    auto &self = ClangeMeMod::instance().getSelf();
    
    if (targetActionId == 9) {
        // --- NUMPAD 9: ATTACK / DESTROY ACTION COPY ---
        // 1. Hand swing action simulate karein
        self.runcmd("playanimation @s animation.player.attack.rotations");
        
        // 2. Crosshair ke samne wale block ko instantly destroy karne ka packet force karein
        self.runcmd("execute at @s anchoring eyes run setblock ^ ^ ^2 air destroy");
    } 
    else if (targetActionId == 8) {
        // --- NUMPAD 8: PLACE / USE ACTION COPY ---
        // Hand me pakde hue block ya item ka 'use animation' execute karein
        self.runcmd("playanimation @s animation.player.use_item");
        
        // Note: Play Store ke naye versions me block placement player ki facing inventory context se sync hoti h
    }
}
}

ClangeMeMod &ClangeMeMod::instance() {
    static ClangeMeMod instance;
    return instance;
}

ClangeMeMod::ClangeMeMod() : mSelf(*ll::mod::NativeMod::current()) {}

bool ClangeMeMod::load() {
    auto &self = getSelf();
    self.getLogger().debug("Loading...");

    std::error_code ec;
    std::filesystem::create_directories(self.getDataDir(), ec);
    if (ec) {
        self.getLogger().error(
            "Failed to create data directory {}: {}",
            self.getDataDir().string(), ec.message()
        );
        return false;
    }

    std::filesystem::create_directories(self.getConfigDir(), ec);
    if (ec) {
        self.getLogger().error(
            "Failed to create config directory {}: {}",
            self.getConfigDir().string(), ec.message()
        );
        return false;
    }

    mConfigFile.emplace();
    if (!mConfigFile->load()) {
        self.getLogger().warn("Failed to load typed config");
        return false;
    }

    mConfig = mConfigFile->value();

    self.getLogger().info(
        "Loaded {} from {}",
        self.getName(), self.getModDir().string()
    );

    return true;
}

bool ClangeMeMod::enable() {
    auto &self = getSelf();
    self.getLogger().debug("Enabling...");

    if (!mConfig.enabled) {
        self.getLogger().info("clange_me is disabled by config");
        return true;
    }

    gInputCallbackEnabled.store(true);

    std::call_once(gInputCallbackOnce, [] {
        pl::input::registerKeyCallback(
            [](const pl::input::KeyEvent &event) {
                if (!gInputCallbackEnabled.load()) {
                    return false;
                }

                // NUMPAD 9 (keyCode == 153) -> Physically Triggers Attack/Destroy
                if (event.keyCode == 153 && event.isKeyDown) {
                    ClangeMeMod::instance().getSelf().getLogger().info("Numpad 9 Pressed: Dispatching Physical Attack");
                    executeVanillaNativeAction(9);
                }

                // NUMPAD 8 (keyCode == 152) -> Physically Triggers Place/Use
                if (event.keyCode == 152 && event.isKeyDown) {
                    ClangeMeMod::instance().getSelf().getLogger().info("Numpad 8 Pressed: Dispatching Physical Place");
                    executeVanillaNativeAction(8);
                }

                return false;
            }
        );

        ClangeMeMod::instance().getSelf().getLogger().info(
            "Keyboard input callback registered"
        );
    });

    self.getLogger().info("clange_me input test enabled");
    return true;
}

bool ClangeMeMod::disable() {
    gInputCallbackEnabled.store(false);
    getSelf().getLogger().debug("Disabling...");
    return true;
}

bool ClangeMeMod::unload() {
    gInputCallbackEnabled.store(false);
    getSelf().getLogger().debug("Unloading...");
    mConfigFile.reset();
    return true;
}

} // namespace clange_me
