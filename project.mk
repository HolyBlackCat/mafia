# --- Build modes ---

_win_subsystem :=

$(call NewMode,debug)
$(Mode)GLOBAL_COMMON_FLAGS := -g
$(Mode)GLOBAL_CXXFLAGS := -D_GLIBCXX_DEBUG

$(call NewMode,debug_soft)
$(Mode)GLOBAL_COMMON_FLAGS := -g
$(Mode)GLOBAL_CXXFLAGS := -D_GLIBCXX_ASSERTIONS

$(call NewMode,release)
$(Mode)GLOBAL_COMMON_FLAGS := -O3
$(Mode)GLOBAL_CXXFLAGS := -DNDEBUG
$(Mode)GLOBAL_LDFLAGS := -s
$(Mode)PROJ_COMMON_FLAGS := -flto
$(Mode)_win_subsystem := -mwindows

$(call NewMode,profile)
$(Mode)GLOBAL_COMMON_FLAGS := -O3 -pg
$(Mode)GLOBAL_CXXFLAGS := -DNDEBUG
$(Mode)_win_subsystem := -mwindows

$(call NewMode,sanitize_address_ub)
$(Mode)GLOBAL_COMMON_FLAGS := -g -fsanitize=address -fsanitize=undefined
$(Mode)GLOBAL_CXXFLAGS := -D_GLIBCXX_DEBUG
$(Mode)PROJ_RUNTIME_ENV += LSAN_OPTIONS=suppressions=misc/leak_sanitizer_suppressions.txt

DIST_NAME := $(APP)_$(TARGET_OS)_v1.*
ifneq ($(MODE),release)
DIST_NAME := $(DIST_NAME)_$(MODE)
endif

ifeq ($(TARGET_OS),emscripten)
ASSETS_IGNORED_PATTERNS := *# A temporary hack to avoid copying assets into the output directory.
endif

# --- Project config ---

PROJ_CXXFLAGS += -std=c++26 -pedantic-errors
PROJ_CXXFLAGS += -Wall -Wextra -Wdeprecated -Wextra-semi -Wimplicit-fallthrough
PROJ_CXXFLAGS += -Wconversion -Wno-implicit-int-float-conversion# Conversion warnings, but without the silly ones.
PROJ_CXXFLAGS += -ftemplate-backtrace-limit=0
PROJ_CXXFLAGS += -fmacro-backtrace-limit=1# 1 = minimal, 0 = infinite
PROJ_CXXFLAGS += -Isrc

ifeq ($(TARGET_OS),windows)
PROJ_LDFLAGS += $(_win_subsystem)
endif

# The common PCH rules for all projects.
# override _pch_rules := src/game/*->src/game/master.hpp

$(call Project,exe,mafia)
$(call ProjectSetting,source_dirs,src)
# $(call ProjectSetting,pch,$(_pch_rules))
$(call ProjectSetting,libs,*)

ifeq ($(TARGET_OS),emscripten)
$(call ProjectSetting,ldflags,--shell-file=src/emscripten_shell.html --preload-file=assets)
$(call ProjectSetting,linking_depends_on,src/emscripten_shell.html)
endif


# --- Dependencies ---

ifeq ($(TARGET_OS),emscripten)
$(call LibraryStub,sdl3,-sUSE_SDL=3)
else
$(call Library,sdl3,https://github.com/libsdl-org/SDL/releases/download/release-3.2.26/SDL3-3.2.26.tar.gz)
  $(call LibrarySetting,cmake_allow_using_system_deps,1)
endif

$(call Library,imgui,https://github.com/ocornut/imgui/archive/refs/tags/v1.92.5.tar.gz)
  $(call LibrarySetting,build_system,imgui)
  # `sdl3` is needed for the respective backend.
  $(call LibrarySetting,deps,sdl3)
override buildsystem-imgui = \
	$(call log_now,[Library] >>> Copying files...)\
	$(call safe_shell_exec,mkdir $(call quote,$(__install_dir)/include) $(call quote,$(__install_dir)/lib))\
	$(foreach x,./* misc/cpp/* backends/imgui_impl_sdl3 backends/imgui_impl_sdlrenderer3,\
		$(call safe_shell_exec,cp $(call quote,$(__source_dir))/$x.h $(call quote,$(__install_dir)/include/))\
		$(call safe_shell_exec,cp $(call quote,$(__source_dir))/$x.cpp $(call quote,$(__build_dir)))\
	) \
	$(call, ### Build a static library)\
	$(call log_now,[Library] >>> Building...)\
	$(call var,__bs_sources := $(wildcard $(__build_dir)/*.cpp))\
	$(foreach x,$(__bs_sources),\
		$(call safe_shell_exec,$(call language_command-cpp,$x,$(x:.cpp=.o),,\
			-I$(call quote,$(__build_dir))\
			-I$(call quote,$(__install_dir)/include)\
			$(call, ### Add flags for libfmt, freetype, and other deps. See above for explanation.)\
			$(call lib_cflags,$(__libsetting_deps_$(__lib_name)))\
			>>$(call quote,$(__log_path))\
		))\
	)\
	$(call safe_shell_exec,$(call MAKE_STATIC_LIB,$(__install_dir)/lib/$(PREFIX_static)imgui$(EXT_static),$(__bs_sources:.cpp=.o)) >>$(call quote,$(__log_path)))\
