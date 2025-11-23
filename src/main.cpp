#include "game.h"
#include "main.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <SDL3/SDL.h>

#include <memory>
#include <stdexcept>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

SDL_Window* window;

int main()
{
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

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
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
    #endif

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    #ifdef __EMSCRIPTEN__
    io.IniFilename = nullptr;
    #endif

    std::unique_ptr<BasicGame> game = MakeGame();

    const int default_redraw_frames = 4;
    int redraw_frames = default_redraw_frames;

    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)))
                done = true;

            if (
                event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                event.type == SDL_EVENT_MOUSE_MOTION ||
                event.type == SDL_EVENT_MOUSE_WHEEL ||
                event.type == SDL_EVENT_KEY_DOWN ||
                event.type == SDL_EVENT_KEY_UP ||
                event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED // Also adding this, just in case.
            )
            {
                redraw_frames = default_redraw_frames;
            }
        }

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
            redraw_frames = 0;

        if (redraw_frames > 0)
        {
            redraw_frames--;
        }
        else
        {
            #ifdef __EMSCRIPTEN__
            emscripten_sleep(10);
            #else
            SDL_Delay(10);
            #endif
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        game->Tick();

        // Rendering
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColorFloat(renderer, clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        #ifdef __EMSCRIPTEN__
        emscripten_sleep(0);
        #endif
    }

    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
