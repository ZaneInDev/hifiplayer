# Keep JNI-bound native methods and their enclosing class from being renamed/stripped,
# since native-lib.cpp resolves them by fully-qualified symbol name.
-keepclasseswithmembernames class com.hifi.player.audio.NativeAudioEngine {
    native <methods>;
}

-keep class com.hifi.player.audio.NativeAudioEngine { *; }
