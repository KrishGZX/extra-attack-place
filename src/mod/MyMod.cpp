
#include "mod/MyMod.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

#include <dlfcn.h>
#include <jni.h>

#include <pl/Mod.hpp>
#include <pl/Input.hpp>

namespace clange_me {

namespace {

using PojavMouseButtonFn =
    void (*)(JNIEnv *, jclass, jint, jboolean);

constexpr int NUMPAD_8 = 152;
constexpr int NUMPAD_9 = 153;

// Android MotionEvent button constants
constexpr int BUTTON_PRIMARY = 1;   // Left click
constexpr int BUTTON_SECONDARY = 2; // Right click

constexpr const char *POJAV_CLASS =
    "org/levimc/launcher/core/mods/inbuilt/nativemod/PojavControlsMod";

constexpr const char *POJAV_SYMBOL =
    "Java_org_levimc_launcher_core_mods_inbuilt_nativemod_PojavControlsMod_nativeSendMouseButton";

std::once_flag gInputCallbackOnce;

std::atomic_bool gInputCallbackEnabled{false};
std::atomic_bool gNum8Down{false};
std::atomic_bool gNum9Down{false};

JavaVM *gJavaVM = nullptr;
jclass gPojavClass = nullptr;

void *gPojavLibrary = nullptr;
PojavMouseButtonFn gPojavMouseButton = nullptr;


// Get JNI environment for the current thread.
bool getJNIEnvironment(
    JNIEnv **env,
    bool &attached
) {
    attached = false;

    if (!gJavaVM || !env) {
        return false;
    }

    jint status = gJavaVM->GetEnv(
        reinterpret_cast<void **>(env),
        JNI_VERSION_1_6
    );

    if (status == JNI_EDETACHED) {
        if (gJavaVM->AttachCurrentThread(env, nullptr) != JNI_OK) {
            return false;
        }

        attached = true;
        return true;
    }

    return status == JNI_OK;
}


// Resolve the actual exported Pojav Controls JNI function.
bool initializePojavBridge(
    JavaVM *vm,
    pl::log::Logger &logger
) {
    if (gPojavMouseButton && gPojavClass) {
        return true;
    }

    gJavaVM = vm;

    if (!gJavaVM) {
        logger.error("Pojav bridge: JavaVM unavailable");
        return false;
    }

    gPojavLibrary = dlopen(
        "libinbuiltmods.so",
        RTLD_NOW
    );

    if (!gPojavLibrary) {
        const char *error = dlerror();

        logger.error(
            "Pojav bridge: dlopen failed: {}",
            error ? error : "unknown error"
        );

        return false;
    }

    gPojavMouseButton =
        reinterpret_cast<PojavMouseButtonFn>(
            dlsym(gPojavLibrary, POJAV_SYMBOL)
        );

    if (!gPojavMouseButton) {
        logger.error(
            "Pojav bridge: nativeSendMouseButton symbol not found"
        );

        return false;
    }

    JNIEnv *env = nullptr;
    bool attached = false;

    if (!getJNIEnvironment(&env, attached)) {
        logger.error("Pojav bridge: unable to obtain JNIEnv");
        return false;
    }

    jclass localClass = env->FindClass(POJAV_CLASS);

    if (!localClass) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        if (attached) {
            gJavaVM->DetachCurrentThread();
        }

        logger.error("Pojav bridge: PojavControlsMod class not found");
        return false;
    }

    gPojavClass = static_cast<jclass>(
        env->NewGlobalRef(localClass)
    );

    env->DeleteLocalRef(localClass);

    if (attached) {
        gJavaVM->DetachCurrentThread();
    }

    if (!gPojavClass) {
        logger.error("Pojav bridge: failed to create global class reference");
        return false;
    }

    logger.info("Pojav Controls native bridge initialized");
    return true;
}


// Send a mouse-button event through Pojav Controls.
bool sendPojavMouseButton(int button, bool down) {
    if (!gJavaVM || !gPojavClass || !gPojavMouseButton) {
        return false;
    }

    JNIEnv *env = nullptr;
    bool attached = false;

    if (!getJNIEnvironment(&env, attached)) {
        return false;
    }

    gPojavMouseButton(
        env,
        gPojavClass,
        static_cast<jint>(button),
        down ? JNI_TRUE : JNI_FALSE
    );

    bool success = !env->ExceptionCheck();

    if (!success) {
        env->ExceptionClear();
    }

    if (attached) {
        gJavaVM->DetachCurrentThread();
    }

    return success;
}


// Release any button still held.
void releaseButtons() {
    if (gNum8Down.exchange(false)) {
        sendPojavMouseButton(BUTTON_PRIMARY, false);
    }

    if (gNum9Down.exchange(false)) {
        sendPojavMouseButton(BUTTON_SECONDARY, false);
    }
}

} // namespace


ClangeMeMod &ClangeMeMod::instance() {
    static ClangeMeMod instance;
    return instance;
}


ClangeMeMod::ClangeMeMod()
    : mSelf(*ll::mod::NativeMod::current()) {
}


bool ClangeMeMod::load() {
    auto &self = getSelf();

    self.getLogger().debug("Loading...");

    std::error_code ec;

    std::filesystem::create_directories(
        self.getDataDir(), ec
    );

    if (ec) {
        self.getLogger().error(
            "Failed to create data directory {}: {}",
            self.getDataDir().string(),
            ec.message()
        );

        return false;
    }

    std::filesystem::create_directories(
        self.getConfigDir(), ec
    );

    if (ec) {
        self.getLogger().error(
            "Failed to create config directory {}: {}",
            self.getConfigDir().string(),
            ec.message()
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
        self.getName(),
        self.getModDir().string()
    );

    return true;
}


bool ClangeMeMod::enable() {
    auto &self = getSelf();
    auto &logger = self.getLogger();

    logger.debug("Enabling...");

    if (!mConfig.enabled) {
        logger.info("clange_me is disabled by config");
        return true;
    }

    // Initialize Pojav Controls bridge.
    if (!initializePojavBridge(
            self.getJavaVM(),
            logger
        )) {
        logger.error("Could not initialize Pojav mouse bridge");
        return false;
    }

    gInputCallbackEnabled.store(true);

    std::call_once(gInputCallbackOnce, [] {

        pl::input::registerKeyCallback(
            [](const pl::input::KeyEvent &event) {

                if (!gInputCallbackEnabled.load()) {
                    return false;
                }

                // NUMPAD 8 -> LEFT MOUSE BUTTON
                if (event.keyCode == NUMPAD_8) {

                    bool previous = gNum8Down.exchange(
                        event.isKeyDown
                    );

                    if (previous != event.isKeyDown) {

                        bool result = sendPojavMouseButton(
                            BUTTON_PRIMARY,
                            event.isKeyDown
                        );

                        if (!result) {
                            ClangeMeMod::instance()
                                .getSelf()
                                .getLogger()
                                .error("Numpad 8: Pojav mouse call failed");
                        }
                    }
                }

                // NUMPAD 9 -> RIGHT MOUSE BUTTON
                if (event.keyCode == NUMPAD_9) {

                    bool previous = gNum9Down.exchange(
                        event.isKeyDown
                    );

                    if (previous != event.isKeyDown) {

                        bool result = sendPojavMouseButton(
                            BUTTON_SECONDARY,
                            event.isKeyDown
                        );

                        if (!result) {
                            ClangeMeMod::instance()
                                .getSelf()
                                .getLogger()
                                .error("Numpad 9: Pojav mouse call failed");
                        }
                    }
                }

                // Do not consume the physical keyboard event.
                return false;
            }
        );

        ClangeMeMod::instance()
            .getSelf()
            .getLogger()
            .info("Keyboard callback registered");
    });

    logger.info("Pojav Controls mouse mapping enabled");

    return true;
}


bool ClangeMeMod::disable() {
    gInputCallbackEnabled.store(false);

    releaseButtons();

    getSelf().getLogger().debug("Disabling...");

    return true;
}


bool ClangeMeMod::unload() {
    gInputCallbackEnabled.store(false);

    releaseButtons();

    if (gJavaVM && gPojavClass) {

        JNIEnv *env = nullptr;
        bool attached = false;

        if (getJNIEnvironment(&env, attached)) {
            env->DeleteGlobalRef(gPojavClass);

            if (attached) {
                gJavaVM->DetachCurrentThread();
            }
        }

        gPojavClass = nullptr;
    }

    // Keep the library handle alive for the process lifetime.
    gPojavMouseButton = nullptr;

    getSelf().getLogger().debug("Unloading...");

    mConfigFile.reset();

    return true;
}

} // namespace clange_me
