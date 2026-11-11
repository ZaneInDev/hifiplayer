#include <jni.h>
#include <memory>
#include "AudioEngine.h"

namespace {
    std::unique_ptr<hifi::AudioEngine> g_engine;
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeOpen(
        JNIEnv *env, jobject /*thiz*/,
        jstring pcmPath, jint sampleRate, jint channelCount, jint bitDepth) {

    const char *pathChars = env->GetStringUTFChars(pcmPath, nullptr);
    std::string path(pathChars);
    env->ReleaseStringUTFChars(pcmPath, pathChars);

    if (!g_engine) {
        g_engine = std::make_unique<hifi::AudioEngine>();
    }

    hifi::SourceFormat format;
    format.sampleRate = sampleRate;
    format.channelCount = channelCount;
    format.bitDepth = bitDepth;

    return g_engine->open(path, format) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeStart(JNIEnv *, jobject) {
    return g_engine && g_engine->start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativePause(JNIEnv *, jobject) {
    if (g_engine) g_engine->pause();
}

JNIEXPORT void JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeStop(JNIEnv *, jobject) {
    if (g_engine) g_engine->stop();
}

JNIEXPORT void JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeClose(JNIEnv *, jobject) {
    if (g_engine) g_engine->close();
}

JNIEXPORT void JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeSetVolume(JNIEnv *, jobject, jfloat gain) {
    if (g_engine) g_engine->setVolume(gain);
}

JNIEXPORT void JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeSeekToFrame(JNIEnv *, jobject, jlong frame) {
    if (g_engine) g_engine->seekToFrame(frame);
}

JNIEXPORT jlong JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeGetCurrentFrame(JNIEnv *, jobject) {
    return g_engine ? g_engine->getCurrentFrame() : 0;
}

JNIEXPORT jlong JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeGetTotalFrames(JNIEnv *, jobject) {
    return g_engine ? g_engine->getTotalFrames() : 0;
}

JNIEXPORT jboolean JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeIsPlaying(JNIEnv *, jobject) {
    return (g_engine && g_engine->isPlaying()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeSetPreferredDeviceId(JNIEnv *, jobject, jint deviceId) {
    if (g_engine) g_engine->setPreferredDeviceId(deviceId);
}

JNIEXPORT void JNICALL
Java_com_hifi_player_audio_NativeAudioEngine_nativeSetExclusiveMode(JNIEnv *, jobject, jboolean exclusive) {
    if (g_engine) g_engine->setExclusiveMode(exclusive == JNI_TRUE);
}

} // extern "C"
