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

using PojavMouseButtonFn = void (*)(JNIEnv *, jclass, jint, jboolean);

constexpr int NUMPAD_8 = 152;
constexpr int NUMPAD_9 = 153;

// Android MotionEvent button constants
constexpr int BUTTON_PRIMARY = 1;   // Left click
constexpr int BUTTON_SECONDARY = 2; // Right click

constexpr const char *POJAV_CLASS_JNI =
    "org/levimc/launcher/core/mods/inbuilt/nativemod/PojavControlsMod";
constexpr const char *POJAV_CLASS_DOT =
    "org.levimc.launcher.core.mods.inbuilt.nativemod.PojavControlsMod";
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

bool getJNIEnvironment(JNIEnv **env, bool &attached) {
    attached = false;
    if (!gJavaVM || !env) {
        return false;
    }

    jint status = gJavaVM->GetEnv(reinterpret_cast<void **>(env), JNI_VERSION_1_6);
    if (status == JNI_OK) {
        return true;
    }
    if (status == JNI_EDETACHED) {
        if (gJavaVM->AttachCurrentThread(env, nullptr) == JNI_OK) {
            attached = true;
            return true;
        }
    }
    return false;
}

jclass resolvePojavClass(JNIEnv *env, pl::log::Logger &logger) {
    if (!env) {
        return nullptr;
    }

    jclass localClass = env->FindClass(POJAV_CLASS_JNI);
    if (localClass) {
        return localClass;
    }

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    // Fallback: try ClassLoader if FindClass fails on native/non-app thread
    jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
    if (activityThreadClass) {
        jmethodID currentActivityThread = env->GetStaticMethodID(
            activityThreadClass, "currentActivityThread", "()Landroid/app/ActivityThread;");
        if (currentActivityThread) {
            jobject activityThread = env->CallStaticObjectMethod(
                activityThreadClass, currentActivityThread);
            if (activityThread) {
                jmethodID getApplication = env->GetMethodID(
                    activityThreadClass, "getApplication", "()Landroid/app/Application;");
                jobject app = getApplication
                    ? env->CallObjectMethod(activityThread, getApplication)
                    : nullptr;
                if (app) {
                    jclass contextClass = env->GetObjectClass(app);
                    jmethodID getClassLoader = env->GetMethodID(
                        contextClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
                    jobject classLoader = getClassLoader
                        ? env->CallObjectMethod(app, getClassLoader)
                        : nullptr;
                    if (classLoader) {
                        jclass loaderClass = env->GetObjectClass(classLoader);
                        jmethodID loadClassMethod = env->GetMethodID(
                            loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
                        if (loadClassMethod) {
                            jstring classNameStr = env->NewStringUTF(POJAV_CLASS_DOT);
                            jobject loaded = env->CallObjectMethod(
                                classLoader, loadClassMethod, classNameStr);
                            env->DeleteLocalRef(classNameStr);
                            if (env->ExceptionCheck()) {
                                env->ExceptionClear();
                            }
                            if (loaded) {
                                env->DeleteLocalRef(loaderClass);
                                env->DeleteLocalRef(classLoader);
                                env->DeleteLocalRef(contextClass);
                                env->DeleteLocalRef(app);
                                env->DeleteLocalRef(activityThread);
                                env->DeleteLocalRef(activityThreadClass);
                                return static_cast<jclass>(loaded);
                            }
                        }
                        env->DeleteLocalRef(loaderClass);
                    }
                    env->DeleteLocalRef(classLoader);
                    env->DeleteLocalRef(contextClass);
                }
                env->DeleteLocalRef(activityThread);
            }
        }
        env->DeleteLocalRef(activityThreadClass);
    }

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    logger.error("Pojav bridge: PojavControlsMod class not found");
    return nullptr;
}

bool initializePojavBridge(JavaVM *vm, pl::log::Logger &logger) {
    if (gPojavMouseButton && gPojavClass) {
        return true;
    }

    gJavaVM = vm;
    if (!gJavaVM) {
        logger.error("Pojav bridge: JavaVM unavailable");
        return false;
    }

    dlerror();
    gPojavLibrary = dlopen("libinbuiltmods.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!gPojavLibrary) {
        gPojavLibrary = dlopen("libinbuiltmods.so", RTLD_NOW);
    }

    if (!gPojavLibrary) {
        const char *error = dlerror();
        logger.error("Pojav bridge: dlopen failed: {}", error ? error : "unknown error");
        return false;
    }

    dlerror();
    gPojavMouseButton = reinterpret_cast<PojavMouseButtonFn>(
        dlsym(gPojavLibrary, POJAV_SYMBOL));
    if (!gPojavMouseButton) {
        const char *error = dlerror();
        logger.error("Pojav bridge: nativeSendMouseButton not found: {}",
                     error ? error : "unknown symbol");
        return false;
    }

    JNIEnv *env = nullptr;
    bool attached = false;
    if (!getJNIEnvironment(&env, attached)) {
        logger.error("Pojav bridge: unable to obtain JNIEnv");
        return false;
    }

    jclass localClass = resolvePojavClass(env, logger);
    if (!localClass) {
        if (attached) {
            gJavaVM->DetachCurrentThread();
        }
        return false;
    }

    gPojavClass = static_cast<jclass>(env->NewGlobalRef(localClass));
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
        down ? JNI_TRUE : JNI_FALSE);

    bool success = !env->ExceptionCheck();
    if (!success) {
        env->ExceptionClear();
    }

    if (attached) {
        gJavaVM->DetachCurrentThread();
    }

    return success;
}

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
    std::filesystem::create_directories(self.getDataDir(), ec);
    if (ec) {
        self.getLogger().error(
            "Failed to create data directory {}: {}",
            self.getDataDir().string(), ec.message());
        return false;
    }

    std::filesystem::create_directories(self.getConfigDir(), ec);
    if (ec) {
        self.getLogger().error(
            "Failed to create config directory {}: {}",
            self.getConfigDir().string(), ec.message());
        return false;
    }

    mConfigFile.emplace();
    if (!mConfigFile->load()) {
        self.getLogger().warn("Failed to load typed config");
        return false;
    }

    mConfig = mConfigFile->value();
    self.getLogger().info("Loaded {} from {}", self.getName(), self.getModDir().string());
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

    if (!initializePojavBridge(self.getJavaVM(), logger)) {
        logger.error("Could not initialize Pojav mouse bridge");
        return false;
    }

    gInputCallbackEnabled.store(true);

    std::call_once(gInputCallbackOnce, [] {
        pl::input::registerKeyCallback([](const pl::input::KeyEvent &event) {
            if (!gInputCallbackEnabled.load()) {
                return false;
            }

            if (event.keyCode == NUMPAD_8) {
                bool previous = gNum8Down.exchange(event.isKeyDown);
                if (previous != event.isKeyDown) {
                    if (!sendPojavMouseButton(BUTTON_PRIMARY, event.isKeyDown)) {
                        ClangeMeMod::instance().getSelf().getLogger()
                            .error("Numpad 8: Pojav mouse call failed");
                    }
                }
            }

            if (event.keyCode == NUMPAD_9) {
                bool previous = gNum9Down.exchange(event.isKeyDown);
                if (previous != event.isKeyDown) {
                    if (!sendPojavMouseButton(BUTTON_SECONDARY, event.isKeyDown)) {
                        ClangeMeMod::instance().getSelf().getLogger()
                            .error("Numpad 9: Pojav mouse call failed");
                    }
                }
            }

            return false;
        });

        ClangeMeMod::instance().getSelf().getLogger().info("Keyboard callback registered");
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

    gPojavMouseButton = nullptr;
    getSelf().getLogger().debug("Unloading...");
    mConfigFile.reset();
    return true;
}

} // namespace clange_me
