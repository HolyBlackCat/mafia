# ------ Android builds ------

# This is partially based on: https://www.hanshq.net/command-line-android.html

# --- Preparation:
# install javac
#     pacman -S jdk-openjdk
# install android command line tooling
#     https://developer.android.com/studio/index.html#command-line-tools-only
#         Unzip at `~/.android_tools/cmdline-tools
#
# Check the minimum recommended "android API level" at: https://support.google.com/googleplay/android-developer/answer/11926878?hl=en
# Right now it's 35. It'll appear in a few places below.
#
# view available platforms (note, can't use `~` in `--sdk_root` or it will literally create that directory)
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --list | grep platforms
# install the one matching theo chosen API level
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --install "platforms;android-35"
#
# view available build tools
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --list | grep build-tools
# install the one matching theo chosen API level (latest minor version, I assume?)
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --install "build-tools;35.0.1"
#
# install platform tools (not versioned?)
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --install "platform-tools"
#
# view available ndks
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --list | grep ndk
# install what looks like latest
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --install "ndk;29.0.14206865"

# --- How to create new project:
# Copy directory `SDL/android-project/app/src/main/res/` to e.g. `src/android_res`.
# In `src/android_res/values/strings.xml`, set your app name, replacing `Game`.
# You might also want to replace the icons and some default colors in `src/values/colors.xml`.
# You get one assets directory for custom files, see `ANDROID_ASSETS_DIR` below. Must load those files using `SDL_IOStream`,
#   using relative paths. (Don't add the directory name.) `SDL_GetBasePath()` returns `./` on Android.

# Those must match your tooling installation from above:
ANDROID_SDK := ~/.android_tools/sdk
ANDROID_BUILD_TOOLS := 35.0.1
ANDROID_PLATFORM := 35
ANDROID_NDK := 29.0.14206865
# This should probably match `ANDROID_PLATFORM`.
# This will be used as a binary suffix for `$(ANDROID_SDK)/ndk/$(ANDROID_NDK)/toolchains/llvm/prebuilt/$(ANDROID_HOST)/bin/aarch64-linux-android____-clang`.
# Check that those binaries exist.
ANDROID_NDK_COMPILER := 35

# Set this based on `SDL/docs/README-android.md`.
ANDROID_PLATFORM_MIN := 21

# This is needed on newer `javac` (from Java JDK) to work with older `d8` (from Android NDK). You can make this empty if not needed.
# Failing to set this can cause error `Unsupported class file major version` from `d8`.
# The value can be guessed experimentally. `23` was guessed for `ANDROID_NDK_COMPILER == 35` (this is the highest number that works for this NDK version).
# This value is the Java version that we want to be compatible with, see the conversion table between this and the class file format version at: https://en.wikipedia.org/wiki/Java_version_history#Release_table
JAVA_COMPILER_RELEASE := 23

ifeq ($(HOST_OS),windows)
# I haven't tested this on Windows.
ANDROID_HOST := windows-x86_64
else
ANDROID_HOST := linux-x86_64
endif

# Customize those for your app:
ANDROID_PACKAGE := com.holyblackcat.mafia
ANDROID_ACTIVITY := MafiaActivity
# The input resource directory. Icons and some system XMLs go here.
ANDROID_RES_DIR := src/android_res
# Custom assets go here.
ANDROID_ASSETS_DIR := assets

# Output directory.
ANDROID_BUILD_DIR := build/android
ANDROID_GENERATED_SRC_DIR := $(ANDROID_BUILD_DIR)/src

# This is a silly way to find SDL, but there's no better way, since the main makefile doesn't yet define the necessary functions.
ANDROID_SDL_SOURCE := $(wildcard build/deps/SDL3-*/source)
$(if $(ANDROID_SDL_SOURCE),,$(error Can't find SDL source code))

override comma := ,
override quote = '$(subst ','"'"',$1)'
override rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))



# --- Compile native code:

# The list of ABIs to compile for.
# $1 is the primary name, from `SDL/source/android-projectapp/build.gradle`, and in a format that the toolchain file likes.
# $2 is the compiler name prefix.
# $3 is usually same as $2, except on `armeabi-v7a`. It's the subdirectory name in `$(ANDROID_SDK)/ndk/$(ANDROID_NDK)/toolchains/llvm/prebuilt/$(ANDROID_HOST)/sysroot/usr/lib`.
# $4 is optionally the extra compiler flags. Currently only `armeabi-v7a` has those. How to obtain those:
#     Build a test CMake project with following contents, and look closely at the configuration output:
#         cmake_minimum_required(VERSION 3.23)
#         project(Proj)
#         message(CMAKE_C_FLAGS: ${CMAKE_C_FLAGS})
#         message(CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS})
#         message(CMAKE_EXE_LINKER_FLAGS: ${CMAKE_EXE_LINKER_FLAGS})
#         message(CMAKE_SHARED_LINKER_FLAGS: ${CMAKE_SHARED_LINKER_FLAGS})
#         message(CMAKE_SYSROOT: ${CMAKE_SYSROOT})
#         add_library(Lib SHARED 1.cpp)
#     Build with CMake flags used below in `CMAKE_EXTRA_FLAGS=...`.
ANDROID_ABIS := \
	armeabi-v7a|armv7a-linux-androideabi|arm-linux-androideabi|-march=armv7-a,-mthumb \
	arm64-v8a|aarch64-linux-android|aarch64-linux-android \
	x86|i686-linux-android|i686-linux-android \
	x86_64|x86_64-linux-android|x86_64-linux-android

# The mode for our native builds.
MODE := release


# The APK contents will be assembled in this directory.
ANDROID_APK_CONTENTS_DIR := $(ANDROID_BUILD_DIR)/apk_contents

# First run our main makefile, then copy the resulting libraries into their intended directories. Also copy the standard library.
# Here `-DANDROID_STL=c++_shared` is optional. If you omit it, then when compiling manually, add `-static-libstdc++` to linker flags (sic, even for libc++ for some reason).
#   You can confirm this by compiling the test project above without setting `ANDROID_STL` and looking at the flags.
# Also if you do this, don't copy `libc++_shared.so` to the output directory below.
# Interestingly, the original article uses lowercase `-fpic` instead of `-fPIC` (on Arm only), but CMake always uses uppercase `-fPIC`, so we always use the uppercase one too.
override define codesnippet_android_build_native =
$(ANDROID_BUILD_DIR)/markers/native-$1.txt:
	$(MAKE) build-all \
		MODE=$(MODE) \
		TARGET_OS=android-$1 \
		CMAKE_EXTRA_FLAGS="-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_SDK)/ndk/$(ANDROID_NDK)/build/cmake/android.toolchain.cmake -DANDROID_ABI=$1 -DANDROID_NATIVE_API_LEVEL=$(ANDROID_NDK_COMPILER) -DANDROID_STL=c++_shared" \
		CC=$(ANDROID_SDK)/ndk/$(ANDROID_NDK)/toolchains/llvm/prebuilt/$(ANDROID_HOST)/bin/$2$(ANDROID_NDK_COMPILER)-clang \
		CXX=$(ANDROID_SDK)/ndk/$(ANDROID_NDK)/toolchains/llvm/prebuilt/$(ANDROID_HOST)/bin/$2$(ANDROID_NDK_COMPILER)-clang++ \
		GLOBAL_CFLAGS=$(call quote,$4) \
		GLOBAL_CXXFLAGS=$(call quote,$4)
	mkdir -p $(ANDROID_APK_CONTENTS_DIR)/lib/$1
	cp build/bin/android-$1/$(MODE)/*.so $(ANDROID_APK_CONTENTS_DIR)/lib/$1
	cp $(ANDROID_SDK)/ndk/$(ANDROID_NDK)/toolchains/llvm/prebuilt/$(ANDROID_HOST)/sysroot/usr/lib/$3/libc++_shared.so $(ANDROID_APK_CONTENTS_DIR)/lib/$1
	mkdir -p $$(dir $$@)
	touch $$@
endef
$(foreach x,$(ANDROID_ABIS),$(eval $(call codesnippet_android_build_native,$(word 1,$(subst |, ,$x)),$(word 2,$(subst |, ,$x)),$(word 3,$(subst |, ,$x)),$(subst $(comma), ,$(word 4,$(subst |, ,$x))))))

# Targets can depend on this to depend on all native code being built.
override android_all_native_markers := $(foreach x,$(ANDROID_ABIS),$(ANDROID_BUILD_DIR)/markers/native-$(word 1,$(subst |, ,$x)).txt)



# --- Create the APK:

# Copy and patch manifest.
# You can edit `ANDROID_ORIGINAL_MANIFEST` to point to your own copy if the manifest if it needs even more patching.
# Here we perform following changes:
# * Add `package="com.foo.bar"` to the initial `<manifest ...>` tag.
#   (SDL's Gradle builds somehow work without this, but I couldn't find a way to do it without this attribute when building manually.)
# * Replace `SDLActivity` with our own activity class.
#   (SDL's `build-scripts/create-android-project.py` does this too.)
# * Add `<uses-sdk ... />` with our SDK version, since otherwise installing the APK fails with the error that it was built for version 0.
ANDROID_ORIGINAL_MANIFEST := $(ANDROID_SDL_SOURCE)/android-project/app/src/main/AndroidManifest.xml
ANDROID_MANIFEST := $(ANDROID_BUILD_DIR)/AndroidManifest.xml
$(ANDROID_MANIFEST): $(ANDROID_ORIGINAL_MANIFEST)
	mkdir -p $(dir $@)
	gawk '/SDLActivity/ {gsub(/SDLActivity/, "$(ANDROID_ACTIVITY)")} {print $$0} /^<manifest/ {print "    package=\"$(ANDROID_PACKAGE)\""} /installLocation/ {print "    <uses-sdk android:minSdkVersion=\"$(ANDROID_PLATFORM_MIN)\" android:targetSdkVersion=\"$(ANDROID_PLATFORM)\" />"}' $< >$@

# Create a java source file for the activity.
# (SDL's `build-scripts/create-android-project.py` does this too.)
# I wanted to use `$(file)` when generating this file, but something about it confuses Make in weird ways when doing parallel builds, so I'm avoiding it.
# This filename must match the name of the class inside of it, otherwise the Java compiler complains.
# Note the `getLibraries()` override. It must return the string that matches your main library name, minus the `lib` prefix and `.so` suffix.
# It seems you don't need to list your dependencies here.
ANDROID_ACTIVITY_SRC := $(ANDROID_GENERATED_SRC_DIR)/$(ANDROID_ACTIVITY).java
$(ANDROID_ACTIVITY_SRC):
	mkdir -p $(dir $@)
	echo >$(call quote,$@) 'package $(ANDROID_PACKAGE);'
	echo >>$(call quote,$@) 'import org.libsdl.app.SDLActivity;'
	echo >>$(call quote,$@) 'public class $(ANDROID_ACTIVITY) extends SDLActivity'
	echo >>$(call quote,$@) '{'
	echo >>$(call quote,$@) '    protected String[] getLibraries() { return new String[] {"$(shell make -npq | grep -oP '(?<=^APP := ).*')"}; }'
	echo >>$(call quote,$@) '}'

# Generate some source code for the resources.
ANDROID_GENERATED_RESOURCES := $(ANDROID_GENERATED_SRC_DIR)/R.java
$(ANDROID_GENERATED_RESOURCES): $(ANDROID_MANIFEST)
	mkdir -p $(dir $@)
	$(ANDROID_SDK)/build-tools/$(ANDROID_BUILD_TOOLS)/aapt package -f -J $(dir $@) -S $(ANDROID_RES_DIR) $(if $(ANDROID_ASSETS_DIR),-A $(ANDROID_ASSETS_DIR)) -M $(ANDROID_MANIFEST) -I $(ANDROID_SDK)/platforms/android-$(ANDROID_PLATFORM)/android.jar

# Compile Java code. This needs JDK to be installed, see above.
ANDROID_COMPILED_JAVA_DIR := $(ANDROID_BUILD_DIR)/obj
# One of the compiled files, to test the timestamps.
ANDROID_COMPILED_JAVA_MARKER := $(ANDROID_COMPILED_JAVA_DIR)/org/libsdl/app/SDL.class
$(ANDROID_COMPILED_JAVA_MARKER): $(ANDROID_GENERATED_RESOURCES) $(ANDROID_ACTIVITY_SRC)
	javac $(if $(JAVA_COMPILER_RELEASE),--release $(JAVA_COMPILER_RELEASE)) -classpath $(ANDROID_SDK)/platforms/android-$(ANDROID_PLATFORM)/android.jar -d $(ANDROID_BUILD_DIR)/obj $(ANDROID_GENERATED_SRC_DIR)/*.java $(ANDROID_SDL_SOURCE)/android-project/app/src/main/java/org/libsdl/app/*.java

# Translate Java code to Dalvik bytecode.
# This file must be called `classes.dex`, since we only specify the output directory, and this name is set automatically.
# Note that since some `.class` files have dollars in them, we must escape them (note `$(quote)` in this recipe).
ANDROID_COMPILED_DEX := $(ANDROID_APK_CONTENTS_DIR)/classes.dex
$(ANDROID_COMPILED_DEX): $(ANDROID_COMPILED_JAVA_MARKER)
	mkdir -p $(dir $(ANDROID_COMPILED_DEX))
	$(ANDROID_SDK)/build-tools/$(ANDROID_BUILD_TOOLS)/d8 --release --lib $(ANDROID_SDK)/platforms/android-$(ANDROID_PLATFORM)/android.jar --output $(dir $(ANDROID_COMPILED_DEX)) $(foreach x,$(call rwildcard,$(ANDROID_COMPILED_JAVA_DIR),*.class),$(call quote,$x))


# Create an unsigned apk. The name is entirely arbitrary.
ANDROID_APK_UNSIGNED := $(ANDROID_BUILD_DIR)/unfinished_apks/$(ANDROID_PACKAGE).apk.unsigned
$(ANDROID_APK_UNSIGNED): $(ANDROID_COMPILED_DEX) $(ANDROID_MANIFEST) $(android_all_native_markers)
	mkdir -p $(dir $@)
	$(ANDROID_SDK)/build-tools/$(ANDROID_BUILD_TOOLS)/aapt package -f -M $(ANDROID_MANIFEST) $(if $(ANDROID_ASSETS_DIR),-A $(ANDROID_ASSETS_DIR)) -S $(ANDROID_RES_DIR) -I $(ANDROID_SDK)/platforms/android-$(ANDROID_PLATFORM)/android.jar -F $@ $(dir $(ANDROID_COMPILED_DEX))

# Align some sections in the APK archive (apparently optional but recommended for performance).
# The name is again entirely arbitrary.
ANDROID_APK_UNSIGNED_ALIGNED := $(ANDROID_APK_UNSIGNED).aligned
$(ANDROID_APK_UNSIGNED_ALIGNED): $(ANDROID_APK_UNSIGNED)
	$(ANDROID_SDK)/build-tools/$(ANDROID_BUILD_TOOLS)/zipalign -f -p 4 $< $@

# Generate a dummy signing key. We apparently need one even for local development.
# The `keytool` program is a part of JDK.
# Here `DevAndroidKey` is an arbitrary string, and so are the two passwords.
# This `-dname ...` removes the need for interactive input.
# This is outside of the build directory, since recreating the key breaks `adb install __.apk`
#   until you manually remove the old application from the device, due to key mismatch.
ANDROID_DEV_SIGNING_KEY := .android_keystore.jks
$(ANDROID_DEV_SIGNING_KEY):
	keytool -genkeypair -keystore $@ -alias DevAndroidKey -validity 10000 -keyalg RSA -keysize 2048 -storepass 123456 -keypass 123456 -dname "cn=Unknown, ou=Unknown, o=Unknown, c=Unknown"

# Sign the APK.
# The name is again entirely arbitrary.
ANDROID_APK_DEV_SIGNED := $(ANDROID_BUILD_DIR)/$(ANDROID_PACKAGE).apk
.DEFAULT_GOAL := $(ANDROID_APK_DEV_SIGNED)
$(ANDROID_APK_DEV_SIGNED): $(ANDROID_APK_UNSIGNED_ALIGNED) $(ANDROID_DEV_SIGNING_KEY)
	$(ANDROID_SDK)/build-tools/$(ANDROID_BUILD_TOOLS)/apksigner sign --ks $(ANDROID_DEV_SIGNING_KEY) --ks-key-alias DevAndroidKey --ks-pass pass:123456 --key-pass pass:123456 --out $@ $<
