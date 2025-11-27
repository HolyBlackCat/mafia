#define SDL_MAIN_USE_CALLBACKS

#include "game.h"
#include "main.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <memory>
#include <stdexcept>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

SDL_Window* window;
SDL_Renderer* renderer;

static std::unique_ptr<BasicGame> game;

const int default_redraw_frames = 4;
static int redraw_frames = default_redraw_frames;

const ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

SDL_AppResult SDLCALL SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)appstate;
    (void)argc;
    (void)argv;

    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Title", "Hello!", nullptr);
    return SDL_APP_SUCCESS;

    // Setup SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        throw std::runtime_error(std::string("`SDL_Init` failed: ") + SDL_GetError());

    // Create window with SDL_Renderer graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    #ifdef __EMSCRIPTEN__
    window_flags |= SDL_WINDOW_FULLSCREEN;
    #endif
    window = SDL_CreateWindow("Dear ImGui SDL3+SDL_Renderer example", (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (!window)
        throw std::runtime_error(std::string("`SDL_CreateWindow` failed: ") + SDL_GetError());

    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
        throw std::runtime_error(std::string("`SDL_CreateRenderer` failed: ") + SDL_GetError());
    SDL_SetRenderVSync(renderer, 1);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    #ifdef __EMSCRIPTEN__
    io.Fonts->AddFontFromFileTTF("assets/NotoSans.ttf");
    #else
    io.Fonts->AddFontFromFileTTF((std::string(SDL_GetBasePath()) + "NotoSans.ttf").c_str());
    #endif

    // Main loop
    #ifdef __EMSCRIPTEN__
    io.IniFilename = nullptr;
    #endif

    game = MakeGame();

    redraw_frames = default_redraw_frames;

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDLCALL SDL_AppIterate(void *appstate)
{
    (void)appstate;
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        redraw_frames = 0;
    else if (ImGui::IsAnyItemActive())
        redraw_frames = default_redraw_frames; // For the blinking cursor and such.
    else if (ImGui::IsPopupOpen(0, ImGuiPopupFlags_AnyPopup))
        redraw_frames = default_redraw_frames; // For the popup animation.

    if (redraw_frames > 0)
    {
        redraw_frames--;
    }
    else
    {
        return SDL_APP_CONTINUE;
    }

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    game->Tick();

    // Rendering
    ImGui::Render();
    SDL_SetRenderScale(renderer, ImGui::GetIO().DisplayFramebufferScale.x, ImGui::GetIO().DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDLCALL SDL_AppEvent(void *appstate, SDL_Event *event)
{
    (void)appstate;
    ImGui_ImplSDL3_ProcessEvent(event);

    if (event->type == SDL_EVENT_QUIT || (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event->window.windowID == SDL_GetWindowID(window)))
        return SDL_APP_SUCCESS;

    if (
        event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event->type == SDL_EVENT_MOUSE_MOTION ||
        event->type == SDL_EVENT_MOUSE_WHEEL ||
        event->type == SDL_EVENT_KEY_DOWN ||
        event->type == SDL_EVENT_KEY_UP ||
        event->type == SDL_EVENT_WINDOW_EXPOSED ||
        event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
    )
    {
        redraw_frames = default_redraw_frames;
    }

    return SDL_APP_CONTINUE;
}

void SDLCALL SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)appstate;
    (void)result;

    // ImGui_ImplSDLRenderer3_Shutdown();
    // ImGui_ImplSDL3_Shutdown();
    // ImGui::DestroyContext();

    // SDL_DestroyRenderer(renderer);
    // SDL_DestroyWindow(window);
    // SDL_Quit();
}
