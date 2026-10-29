#include "v8_smoke.hpp"

#include <android/log.h>
#include <jni.h>

namespace
{

void log_message(const char* message)
{
    __android_log_write(ANDROID_LOG_INFO, "EngineV8", message);
}

} // namespace

extern "C" JNIEXPORT jboolean JNICALL Java_org_engine_minimal_EngineActivity_nativeRunV8Smoke(
    JNIEnv* environment, jclass, jstring icu_data_file)
{
    if (icu_data_file == nullptr)
    {
        __android_log_write(ANDROID_LOG_ERROR, "EngineV8", "FAIL: ICU data path is missing");
        return JNI_FALSE;
    }
    const char* path = environment->GetStringUTFChars(icu_data_file, nullptr);
    if (path == nullptr)
    {
        __android_log_write(ANDROID_LOG_ERROR, "EngineV8", "FAIL: ICU data path conversion");
        return JNI_FALSE;
    }
    const bool passed = engine::tests::run_v8_smoke(nullptr, path, log_message);
    environment->ReleaseStringUTFChars(icu_data_file, path);
    return passed ? JNI_TRUE : JNI_FALSE;
}
