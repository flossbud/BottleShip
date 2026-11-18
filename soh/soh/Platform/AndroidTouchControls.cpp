#include "AndroidTouchControls.h"

#ifdef __ANDROID__
#include <libultraship/libultraship.h>
#include <jni.h>
#include <SDL2/SDL.h>
#include "soh/cvar_prefixes.h"

void AndroidTouchControls::Apply() {
    int32_t mode = CVarGetInteger(CVAR_TOUCH_CONTROLS_MODE, TOUCH_CONTROLS_AUTO);
    bool showToggle = CVarGetInteger(CVAR_TOUCH_CONTROLS_SHOW_TOGGLE, 0) != 0;

    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (env == nullptr) {
        return;
    }
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (activity == nullptr) {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(activityClass, "setTouchControlsConfig", "(IZ)V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method, (jint)mode, (jboolean)showToggle);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
}
#else
void AndroidTouchControls::Apply() {
}
#endif
