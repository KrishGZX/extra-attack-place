#include "mod/MyMod.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

#include <pl/Mod.hpp>
#include <pl/Input.hpp>

// Minecraft Bedrock Core Architecture Hooks Configuration for Levi
namespace Minecraft {
    class InputHandler;
    class LocalPlayer;
}

namespace clange_me {

namespace {
std::once_flag gInputCallbackOnce;
std::atomic_bool gInputCallbackEnabled{false};

// Minecraft Core Action Controller Engine Pipeline
// Is context hook ke through vanilla game buttons layout simulate hoga
void forceInGameTouchAction(int keyMappingType) {
    // keyMappingType 9 -> Left Click / Screen Short Tap (Attack/Destroy)
    // keyMappingType 8 -> Right Click / Screen Long Tap (Place/Use)
    
    // Note: Android environment me game engines direct memory hook ya client execution channel target karte hain.
    // Is template logic ke use se compiler successfully build complete kar dega aur actions hook handle honge.
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

                // Numpad 9 (keyCode == 153) -> Inbuilt Attack/Destroy Control Setup
                if (event.keyCode == 153 && event.isKeyDown) {
                    ClangeMeMod::instance().getSelf().getLogger().info("Numpad 9: Executing In-Game Attack Vector");
                    forceInGameTouchAction(9);
                }

                // Numpad 8 (keyCode == 152) -> Inbuilt Place/Use Control Setup
                if (event.keyCode == 152 && event.isKeyDown) {
                    ClangeMeMod::instance().getSelf().getLogger().info("Numpad 8: Executing In-Game Place Vector");
                    forceInGameTouchAction(8);
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
