#include "game.h"
#include "main.h"

#include <imgui.h>

struct Game : BasicGame
{
    void Tick() override
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        ImGui::Text("%d, %d", w, h);
        ImGui::ShowDemoWindow();
    }
};

std::unique_ptr<BasicGame> MakeGame()
{
    return std::make_unique<Game>();
}
