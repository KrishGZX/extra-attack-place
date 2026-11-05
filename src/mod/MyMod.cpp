#include "mod/MyMod.h"

#include <atomic>
#include <filesystem>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

#include <pl/Mod.hpp>
#include <pl/Input.hpp>

namespace clange_me {

namespace {
std::once_flag gInputCallbackOnce;
std::atomic_bool gInputCallbackEnabled{false};

// Cross-Platform compilation trigger for GitHub Linux Runners
void sendVanillaClick(unsigned long downFlag, unsigned long upFlag) {
#ifdef _WIN32
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    
    // Press down
    input.mi.dwFlags = downFlag;
    SendInput(1, &input, sizeof(INPUT));
    
    // Release instantly for engine frame registration
    input.mi.dwFlags = upFlag;
    SendInput(1, &input, sizeof(INPUT));
#endif
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
#ifdef _WIN32
                    sendVanillaClick(0x0002, 0x0004); // MOUSEEVENTF_LEFTDOWN & LEFTUP
#endif
                }

                // Numpad 8 (keyCode == 152) -> Exact Inbuilt Place/Use Simulation
                if (event.keyCode == 152 && event.isKeyDown) {
                    ClangeMeMod::instance().getSelf().getLogger().info("Numpad 8: Simulating Place/Use Action");
#ifdef _WIN32
                    sendVanillaClick(0x0008, 0x0010); // MOUSEEVENTF_RIGHTDOWN & RIGHTUP
#endif
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
