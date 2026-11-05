#include "mod/MyMod.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <windows.h> // Windows native key injection compatibility fallback

#include <pl/Mod.hpp>
#include <pl/Input.hpp>

namespace clange_me {

namespace {
std::once_flag gInputCallbackOnce;
std::atomic_bool gInputCallbackEnabled{false};

// Vanilla Core Control Simulation via Windows Thread Overlay Trigger
// Minecraft core window directly foreground me rehte hue is standard call ko native mapping me change karta h
void sendVanillaClick(DWORD downFlag, DWORD upFlag) {
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    
    // Press down
    input.mi.dwFlags = downFlag;
    SendInput(1, &input, sizeof(INPUT));
    
    // Release instantly for engine registration frame latency
    input.mi.dwFlags = upFlag;
    SendInput(1, &input, sizeof(INPUT));
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

                // Numpad 9 (keyCode == 153) -> Exact Inbuilt Attack/Destroy Simulation
                if (event.keyCode == 153 && event.isKeyDown) {
                    ClangeMeMod::instance().getSelf().getLogger().info("Numpad 9: Simulating Attack/Destroy Action");
                    sendVanillaClick(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
                }

                // Numpad 8 (keyCode == 152) -> Exact Inbuilt Place/Use Simulation
                if (event.keyCode == 152 && event.isKeyDown) {
                    ClangeMeMod::instance().getSelf().getLogger().info("Numpad 8: Simulating Place/Use Action");
                    sendVanillaClick(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
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
