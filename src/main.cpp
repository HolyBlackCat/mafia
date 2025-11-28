#include <functional>
#include <iostream>
#define SDL_MAIN_USE_CALLBACKS

#include "game.h"
#include "main.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

SDL_Window* window;
SDL_Renderer* renderer;

static std::unique_ptr<BasicGame> game;

const int default_redraw_frames = 4;
static int redraw_frames = default_redraw_frames;

const ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

struct TouchController
{
    // Public config: [

    // if true, accept even mouse input, noy only touch. Good for debugging.
    bool accept_any_input_source = false;

    // How long to hold to imitate right click.
    float hold_duration_to_right_click = 0.5f;

    // By default, if the input device is a pen, hold-to-right-click is disabled,
    //   since pens should usually have their own ways to right click.
    // Setting this to true enables it.
    bool allow_hold_to_right_click_on_pen = false;

    // How far you must move finger to trigger drag.
    std::function<float()> drag_threshold_func = []{return ImGui::GetTextLineHeight();};

    // ]


    // Here we initialize everything, to silence warnings on missing fields when using designated initialization.

    // We replace the event IDs with our own, since we're inserting new events, and can't otherwise maintain sequental IDs.
    ImU32 event_counter = ImU32(-1);

    ImVec2 mouse_pos{};
    ImGuiMouseSource mouse_source{};
    ImVec2 mouse_pos_when_clicked{};
    ImGuiID window_id = 0;
    ImVec2 window_scroll_when_clicked{};
    bool mouse_held = false;
    bool fake_scroll = false;
    bool once_restore_mouse_pos = false;
    bool no_scroll_this_time = false;


    // If true, don't pause your rendering loop.
    [[nodiscard]] bool ShouldRedraw() const
    {
        return mouse_held;
    }

    void HandleEvents()
    {
        const float drag_threshold = drag_threshold_func();

        ImGuiContext &ctx = *ImGui::GetCurrentContext();

        // If true, we can't scroll.
        const bool active_id_bad =
            ctx.ActiveIdWindow &&
            (
                // If not dragging a window. This is needed despite `.ConfigWindowsMoveFromTitleBarOnly == true` above,
                //   since holding a window background still sets an active ID even with this flag.
                // The `ctx.MovingWindow` part is what excludes "dragging" the window background with no effect.
                (ImGui::GetActiveID() == ctx.ActiveIdWindow->MoveId && ctx.MovingWindow) ||
                // Not dragging either scrollbar.
                (ImGui::GetActiveID() == ImGui::GetWindowScrollbarID(ctx.ActiveIdWindow, ImGuiAxis_X)) ||
                (ImGui::GetActiveID() == ImGui::GetWindowScrollbarID(ctx.ActiveIdWindow, ImGuiAxis_Y)) ||
                // Not resizing a window using its corners.
                (ImGui::GetActiveID() == ImGui::GetWindowResizeCornerID(ctx.ActiveIdWindow, 0)) ||
                (ImGui::GetActiveID() == ImGui::GetWindowResizeCornerID(ctx.ActiveIdWindow, 1)) ||
                // Not resizing a window using a border.
                ctx.ActiveIdWindow->ResizeBorderHeld != -1
            );

        // If true, we can scroll.
        const bool active_id_good =
            !active_id_bad &&
            ctx.ActiveIdWindow &&
            (
                // Trying to move a window by its body, which does nothing when `.ConfigWindowsMoveFromTitleBarOnly == true`.
                (ImGui::GetActiveID() == ctx.ActiveIdWindow->MoveId && !ctx.MovingWindow)
            );

        // If true, we scroll only if the vertical threshold is reached before horizontal.
        const bool active_id_vert_only = !active_id_good && !active_id_bad && ImGui::GetActiveID();

        auto CheckMouseSource = [&](ImGuiMouseSource new_source)
        {
            if (new_source == ImGuiMouseSource_TouchScreen || new_source == ImGuiMouseSource_Pen || accept_any_input_source)
            {
                mouse_source = new_source;
                return true;
            }

            return false;
        };

        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;

        ImVector<ImGuiInputEvent> out_queue;

        // Act on fake scroll.
        if (fake_scroll)
        {
            if (ImGuiWindow *win = ImGui::FindWindowByID(window_id))
            {
                win->Scroll.x = mouse_pos_when_clicked.x - mouse_pos.x + window_scroll_when_clicked.x;
                win->Scroll.y = mouse_pos_when_clicked.y - mouse_pos.y + window_scroll_when_clicked.y;

                if (win->Scroll.x < 0)
                    win->Scroll.x = 0;
                else if (win->Scroll.x > win->ScrollMax.x)
                    win->Scroll.x = win->ScrollMax.x;

                if (win->Scroll.y < 0)
                    win->Scroll.y = 0;
                else if (win->Scroll.y > win->ScrollMax.y)
                    win->Scroll.y = win->ScrollMax.y;
            }
        }

        // Restore mouse pos after a single frame delay, after stopping the scroll.
        if (std::exchange(once_restore_mouse_pos, false))
        {
            // Return the mouse to its correct position.
            out_queue.push_back({});
            ImGuiInputEvent &mouse_reset_pos_event = out_queue.back();
            mouse_reset_pos_event.EventId = event_counter++;
            mouse_reset_pos_event.Source = ImGuiInputSource_Mouse;
            mouse_reset_pos_event.Type = ImGuiInputEventType_MousePos;
            mouse_reset_pos_event.MousePos.MouseSource = mouse_source;
            mouse_reset_pos_event.MousePos.PosX = mouse_pos.x;
            mouse_reset_pos_event.MousePos.PosY = mouse_pos.y;
        }

        // Transform long tap to right click.
        if (
            mouse_held &&
            !no_scroll_this_time &&
            // Known bad IDs don't need our right click.
            // Known good ones should be allowed, since the demo has e.g. right-clickable text labels, which we don't see and treat as dragging the window, which is good.
            !active_id_bad &&
            // Held long enough.
            ImGui::GetIO().MouseDownDuration[ImGuiMouseButton_Left] > hold_duration_to_right_click &&
            // Don't enable for pen, unless the user explicitly opts in.
            (allow_hold_to_right_click_on_pen || mouse_source != ImGuiMouseSource_Pen)
        )
        {
            mouse_held = false;

            // Prevent mouse release from registering as a click.
            ImGui::ClearActiveID();

            // Release left button.
            out_queue.push_back({});
            ImGuiInputEvent &mouse_release_left_event = out_queue.back();
            mouse_release_left_event.EventId = event_counter++;
            mouse_release_left_event.Source = ImGuiInputSource_Mouse;
            mouse_release_left_event.Type = ImGuiInputEventType_MouseButton;
            mouse_release_left_event.MouseButton.MouseSource = mouse_source;
            mouse_release_left_event.MouseButton.Button = ImGuiMouseButton_Left;
            mouse_release_left_event.MouseButton.Down = false;

            // Press right button.
            out_queue.push_back({});
            ImGuiInputEvent &mouse_press_right_event = out_queue.back();
            mouse_press_right_event.EventId = event_counter++;
            mouse_press_right_event.Source = ImGuiInputSource_Mouse;
            mouse_press_right_event.Type = ImGuiInputEventType_MouseButton;
            mouse_press_right_event.MouseButton.MouseSource = mouse_source;
            mouse_press_right_event.MouseButton.Button = ImGuiMouseButton_Right;
            mouse_press_right_event.MouseButton.Down = true;

            // Release right button immediately, since we'll never get a release event for either the left button (because it's already considered released)
            //   or the right button (since it's not actually held).
            out_queue.push_back({});
            ImGuiInputEvent &mouse_release_right_event = out_queue.back();
            mouse_release_right_event.EventId = event_counter++;
            mouse_release_right_event.Source = ImGuiInputSource_Mouse;
            mouse_release_right_event.Type = ImGuiInputEventType_MouseButton;
            mouse_release_right_event.MouseButton.MouseSource = mouse_source;
            mouse_release_right_event.MouseButton.Button = ImGuiMouseButton_Right;
            mouse_release_right_event.MouseButton.Down = false;
        }

        // Handle the events.
        for (const ImGuiInputEvent &in_event : ctx.InputEventsQueue)
        {
            out_queue.push_back(in_event);
            ImGuiInputEvent &out_event = out_queue.back();

            if (event_counter == ImU32(-1))
                event_counter = in_event.EventId + 1;
            else
                out_event.EventId = event_counter++;

            if (in_event.Type == ImGuiInputEventType_MousePos)
            {
                if (CheckMouseSource(in_event.MousePos.MouseSource))
                {
                    mouse_pos = ImVec2(in_event.MousePos.PosX, in_event.MousePos.PosY);

                    if (fake_scroll)
                    {
                        out_queue.pop_back();
                        event_counter--;
                        continue;
                    }

                    if (
                        mouse_held &&
                        ctx.HoveredWindow &&
                        !no_scroll_this_time &&
                        !active_id_bad &&
                        (active_id_good || ImLengthSqr(ImVec2(mouse_pos.x - mouse_pos_when_clicked.x, mouse_pos.y - mouse_pos_when_clicked.y)) > drag_threshold * drag_threshold)
                    )
                    {
                        // For certain active widgets, triggering the horizontal threshold first disables the scroll behaviors.
                        if (active_id_vert_only && std::abs(mouse_pos.x - mouse_pos_when_clicked.x) > std::abs(mouse_pos.y - mouse_pos_when_clicked.y))
                        {
                            no_scroll_this_time = true;

                            // Update mouse position, since we've been eating mouse events before.
                            ImGuiInputEvent &mouse_restore_pos_event = out_event;
                            mouse_restore_pos_event.Source = ImGuiInputSource_Mouse;
                            mouse_restore_pos_event.Type = ImGuiInputEventType_MousePos;
                            mouse_restore_pos_event.MousePos.MouseSource = mouse_source;
                            mouse_restore_pos_event.MousePos.PosX = mouse_pos.x;
                            mouse_restore_pos_event.MousePos.PosY = mouse_pos.y;
                            continue;
                        }

                        ImGui::ClearActiveID();

                        mouse_held = false;
                        fake_scroll = true;
                        window_id = ctx.HoveredWindow->ID;
                        window_scroll_when_clicked = ctx.HoveredWindow->Scroll;

                        // No reading the original event beyond this point.

                        // Move mouse to a dummy position.
                        ImGuiInputEvent &mouse_dummy_pos_event = out_event;
                        mouse_dummy_pos_event.Source = ImGuiInputSource_Mouse;
                        mouse_dummy_pos_event.Type = ImGuiInputEventType_MousePos;
                        mouse_dummy_pos_event.MousePos.MouseSource = mouse_source;
                        // Move the mouse to the window corner, so if someone sends e.g. a scroll event, this window will catch it. Not sure how useful this actually is.
                        mouse_dummy_pos_event.MousePos.PosX = ctx.HoveredWindow->Pos.x;
                        mouse_dummy_pos_event.MousePos.PosY = ctx.HoveredWindow->Pos.y;
                        continue;
                    }

                    // Undecided if we should scroll yet, eat the mouse events for now.
                    if (mouse_held && active_id_vert_only && !no_scroll_this_time)
                    {
                        out_queue.pop_back();
                        event_counter--;
                        continue;
                    }
                }

                continue;
            }

            if (in_event.Type == ImGuiInputEventType_MouseButton)
            {
                if (CheckMouseSource(in_event.MouseButton.MouseSource) && in_event.MouseButton.Button == ImGuiMouseButton_Left)
                {
                    if (!in_event.MouseButton.Down)
                    {
                        mouse_held = false;

                        if (fake_scroll)
                        {
                            fake_scroll = false;
                            once_restore_mouse_pos = true;
                        }

                        continue;
                    }

                    if (!mouse_held)
                    {
                        mouse_pos_when_clicked = mouse_pos;
                        no_scroll_this_time = false;
                    }

                    mouse_held = true;
                }

                continue;
            }
        }

        ctx.InputEventsQueue.swap(out_queue);
    }
};

TouchController touch_controller{
    .accept_any_input_source = true
};

SDL_AppResult SDLCALL SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)appstate;
    (void)argc;
    (void)argv;

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
    #ifdef __ANDROID__
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    #endif

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    main_scale *= 1.5f;
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    ;

    #if defined(__ANDROID__)
    std::size_t font_size = 0;
    // Can't free this data, ImGui needs it to stay alive.
    static void *font = SDL_LoadFile("NotoSans.ttf", &font_size);
    io.Fonts->AddFontFromMemoryTTF(font, (int)font_size);
    #elif defined(__EMSCRIPTEN__)
    io.Fonts->AddFontFromFileTTF("assets/NotoSans.ttf");
    #else
    io.Fonts->AddFontFromFileTTF((std::string(SDL_GetBasePath()) + "NotoSans.ttf").c_str());
    #endif

    io.IniFilename = nullptr;

    game = MakeGame();

    redraw_frames = default_redraw_frames;

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDLCALL SDL_AppIterate(void *appstate)
{
    touch_controller.HandleEvents();

    (void)appstate;
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        redraw_frames = 0;
    if (SDL_GetMouseState(nullptr, nullptr))
        redraw_frames = default_redraw_frames; // Mouse held, needed for certain interactions.
    else if (touch_controller.ShouldRedraw())
        redraw_frames = default_redraw_frames; // For the blinking cursor and such.
    else if (ImGui::IsAnyItemActive())
        redraw_frames = default_redraw_frames; // For the blinking cursor and such.
    else if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
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

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
