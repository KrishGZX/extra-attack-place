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

// Android Native Helper: Iske zariye hum engine ke commands channel ko target karenge
// Kyunki mobile me direct Windows Mouse hooks kaam nahi karte, hum Bedrock Core interaction pipelines ko hit karenge.
void executeMobileVanillaAction(int actionType) {
    // Note: Mobile internal threads me action call synchronization ke liye 
    // standard execution patterns use hote hain.
    // GitHub actions is inline safe block ko easily parse kar lega.
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

                // Numpad 9 (keyCode == 153) -> Mobile Native Attack/Destroy
                if (event.keyCode == 153 && event.isKeyDown) {
                    ClangeMeMod::instance().getSelf().getLogger().info("Numpad 9 Pressed: Android Attack Action");
                    
                    // TODO: Mobile architecture execution code block
                    // Yahan GitHub cross-compiling logic setup successfully handle hogi.
                }

                // Numpad 8 (keyCode == 152) -> Mobile Native Place/Use
                if (event.keyCode == 152 && event.isKeyDown) {
                    ClangeMeMod::instance().getSelf().getLogger().info("Numpad 8 Pressed: Android Place Action");
                    
                    // TODO: Mobile architecture interaction block
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
