# ------ Android builds ------

# --- Preparation:
# install javac
#     pacman -S jdk-openjdk
# install android command line tooling
#     https://developer.android.com/studio/index.html#command-line-tools-only
#         Unzip at `~/.android_tools/cmdline-tools
#
# view available platforms (note, can't use `~` in `--sdk_root` or it will literally create that directory)
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --list | grep platforms
# install platform that looks like latest:
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --install "platforms;android-36.1"
#
# view available build tools
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --list | grep build-tools
# install what looks like latest (match platform package above?)
#     .android_tools/cmdline-tools/bin/sdkmanager --sdk_root=.android_tools/sdk --install "build-tools;36.1.0"
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

# Those must match your tooling installation from above:
ANDROID_SDK := ~/.android_tools/sdk
ANDROID_BUILD_TOOLS := 36.1.0
ANDROID_PLATFORM := 36.1

# Customize those for your app:
ANDROID_PACKAGE := com.holyblackcat.mafia
ANDROID_ACTIVITY := MafiaActivity
# The input resource directory.
ANDROID_RES_DIR := src/android_res

# Output directory.
ANDROID_BUILD_DIR := build/android
ANDROID_GENERATED_SRC_DIR := $(ANDROID_BUILD_DIR)/src

# This is a silly way to find SDL, but there's no better way, since the main makefile doesn't yet define the necessary functions.
ANDROID_SDL_SOURCE := $(wildcard build/deps/SDL3-*/source)
$(if $(ANDROID_SDL_SOURCE),,$(error Can't find SDL source code))

# Copy and patch manifest.
# You can edit `ANDROID_ORIGINAL_MANIFEST` to point to your own copy if the manifest if it needs even more patching.
# Here we perform following changes:
# * Add `package="com.foo.bar"` to the initial `<manifest ...>` tag.
#   (SDL's Gradle builds somehow work without this, but I couldn't find a way to do it without this attribute when building manually.)
# * Replace `SDLActivity` with our own activity class.
#   (SDL's `build-scripts/create-android-project.py` does this too.)
ANDROID_ORIGINAL_MANIFEST := $(ANDROID_SDL_SOURCE)/android-project/app/src/main/AndroidManifest.xml
ANDROID_MANIFEST := $(ANDROID_BUILD_DIR)/AndroidManifest.xml
$(ANDROID_MANIFEST): $(ANDROID_ORIGINAL_MANIFEST)
	mkdir -p $(dir $@)
	gawk -vpackage=$(call quote,$(ANDROID_PACKAGE)) -vactivity=$(call quote,$(ANDROID_ACTIVITY)) '/SDLActivity/ {gsub(/SDLActivity/, activity)} {print $$0} /^<manifest/ {print "    package=\"" package "\""}' $< >$@

# Create a java source file for the activity.
# (SDL's `build-scripts/create-android-project.py` does this too.)
# I wanted to use `$(file)` when generating this file, but something about it confuses Make in weird ways when doing parallel builds, so I'm avoiding it.
# This filename must match the name of the class inside of it, otherwise the Java compiler complains.
ANDROID_ACTIVITY_SRC := $(ANDROID_GENERATED_SRC_DIR)/$(ANDROID_ACTIVITY).java
$(ANDROID_ACTIVITY_SRC):
	mkdir -p $(dir $@)
	echo >$(call quote,$@) 'package $(ANDROID_PACKAGE);'
	echo >>$(call quote,$@) 'import org.libsdl.app.SDLActivity;'
	echo >>$(call quote,$@) 'public class $(ANDROID_ACTIVITY) extends SDLActivity {}'

# Generate some source code for the resources.
ANDROID_GENERATED_RESOURCES := $(ANDROID_GENERATED_SRC_DIR)/R.java
$(ANDROID_GENERATED_RESOURCES): $(ANDROID_MANIFEST)
	mkdir -p $(dir $@)
	$(ANDROID_SDK)/build-tools/$(ANDROID_BUILD_TOOLS)/aapt package -f -J $(dir $@) -S $(ANDROID_RES_DIR) -M $(ANDROID_MANIFEST) -I $(ANDROID_SDK)/platforms/android-$(ANDROID_PLATFORM)/android.jar

# Compile Java code. This needs JDK to be installed, see above.
ANDROID_COMPILED_JAVA_DIR := $(ANDROID_BUILD_DIR)/obj
# One of the compiled files, to test the timestamps.
ANDROID_COMPILED_JAVA_MARKER := $(ANDROID_COMPILED_JAVA_DIR)/org/libsdl/app/SDL.class
$(ANDROID_COMPILED_JAVA_MARKER): $(ANDROID_GENERATED_RESOURCES) $(ANDROID_ACTIVITY_SRC)
	javac -classpath $(ANDROID_SDK)/platforms/android-$(ANDROID_PLATFORM)/android.jar -d $(ANDROID_BUILD_DIR)/obj $(ANDROID_GENERATED_SRC_DIR)/*.java $(ANDROID_SDL_SOURCE)/android-project/app/src/main/java/org/libsdl/app/*.java

# Translate Java code to Dalvik bytecode.
# This file must be called `classes.dex`, since we only specify the output directory, and this name is set automatically.
# Since some `.class` files have dollars in them, we must escape them.
ANDROID_COMPILED_DEX := $(ANDROID_BUILD_DIR)/dex/classes.dex
$(ANDROID_COMPILED_DEX): $(ANDROID_COMPILED_JAVA_MARKER)
	mkdir -p $(dir $(ANDROID_COMPILED_DEX))
	$(ANDROID_SDK)/build-tools/$(ANDROID_BUILD_TOOLS)/d8 --release --lib $(ANDROID_SDK)/platforms/android-$(ANDROID_PLATFORM)/android.jar --output $(dir $(ANDROID_COMPILED_DEX)) $(foreach x,$(call rwildcard,$(ANDROID_COMPILED_JAVA_DIR),*.class),$(call quote,$x))
# on success, this creates build_android/apk/classes.dex

# Create an unsigned apk. The name is entirely arbitrary.
ANDROID_APK_UNSIGNED := $(ANDROID_BUILD_DIR)/$(ANDROID_PACKAGE).apk.unsigned
$(ANDROID_APK_UNSIGNED): $(ANDROID_COMPILED_DEX) $(ANDROID_MANIFEST)
	$(ANDROID_SDK)/build-tools/$(ANDROID_BUILD_TOOLS)/aapt package -f -M $(ANDROID_MANIFEST) -S $(ANDROID_RES_DIR) -I $(ANDROID_SDK)/platforms/android-$(ANDROID_PLATFORM)/android.jar -F $@ $(dir $(ANDROID_COMPILED_DEX))

# Align some sections in the APK archive (apparently optional but recommended for performance).
# The name is again entirely arbitrary.
ANDROID_APK_UNSIGNED_ALIGNED := $(ANDROID_APK_UNSIGNED).aligned
$(ANDROID_APK_UNSIGNED_ALIGNED): $(ANDROID_APK_UNSIGNED)
	$(ANDROID_SDK)/build-tools/$(ANDROID_BUILD_TOOLS)/zipalign -f -p 4 $< $@

# Generate a dummy signing key. We apparently need one even for local development.
# The `keytool` program is a part of JDK.
# Here `DevAndroidKey` is an arbitrary string, and so are the two passwords.
# This `-dname ...` removes the need for interactive input.
ANDROID_DEV_SIGNING_KEY := $(ANDROID_BUILD_DIR)/keystore.jks
$(ANDROID_DEV_SIGNING_KEY):
	keytool -genkeypair -keystore $@ -alias DevAndroidKey -validity 10000 -keyalg RSA -keysize 2048 -storepass 123456 -keypass 123456 -dname "cn=Unknown, ou=Unknown, o=Unknown, c=Unknown"

# Sign the APK.
# The name is again entirely arbitrary.
ANDROID_APK_DEV_SIGNED := $(ANDROID_BUILD_DIR)/$(ANDROID_PACKAGE).apk
$(ANDROID_APK_DEV_SIGNED): $(ANDROID_APK_UNSIGNED_ALIGNED) $(ANDROID_DEV_SIGNING_KEY)
	$(ANDROID_SDK)/build-tools/$(ANDROID_BUILD_TOOLS)/apksigner sign --ks $(ANDROID_DEV_SIGNING_KEY) --ks-key-alias DevAndroidKey --ks-pass pass:123456 --key-pass pass:123456 --out $@ $<
