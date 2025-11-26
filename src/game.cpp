#include "game.h"
#include "main.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <numeric>
#include <utility>
#include <vector>

enum class Role
{
    none, // This must be first.
    // Those are ordered by their default turn order. Sync order with `Strings::role_names`.
    captain, // Blocks night-time ability of any player. Targeting mafia (possibly boss) blocks their combined ability.
    sheriff, // Detects mafia, or killer if no mafia.
    prostitute, // Protects from death by vote on the next day.
    mafia_boss, // Same as normal mafia, but detects sheriff.
    mafia, // Mafia.
    mafia_alt, // Second kind of mafia, "yakuza".
    killer, // Its own faction, kills at night like mafia, but completely independent.
    _count,
};

struct Strings
{
    std::string button_cancel = "Отмена";

    std::array<std::string, std::to_underlying(Role::_count)> role_names = {
        "Мирный житель",
        "Капитан",
        "Шериф",
        "Красотка",
        "Дон мафии",
        "Мафия",
        "Якудза",
        "Маньяк",
    };

    std::string add_player_button = "+ игрок";
    std::string add_player_window = "Добавить игрока";
    std::string add_player_name_hint = "Имя";
    std::string add_player_confirm = "Добавить";

    std::string remove_player_button = "Удалить";
    std::string remove_player_window = "Удалить игрока";
    std::string remove_player_confirm = "Удалить";

    std::string edit_role_button = "Сменить роль";
    std::string edit_role_window = "Сменить роль";
    std::string edit_role_confirm = "Сменить";
};
Strings strings;

struct Player
{
    int id = 0;
    std::string name;
    Role role;
};

struct Day
{
    std::vector<Player> players;
};

struct State
{
    std::vector<Day> days;
};

struct Settings
{
    std::array<Role, std::to_underlying(Role::_count)> role_order;

    void SetDefault()
    {
        for (int i = 0; i < std::to_underlying(Role::_count); i++)
            role_order[std::size_t(i)] = Role(i);
    }
};

struct Game : BasicGame
{
    State state;
    std::size_t active_day_index = 0;

    std::string add_player_textbox_for_modal;
    Role new_player_role_for_modal{};

    int player_id_counter = 0;

    Game()
    {
        state.days.emplace_back();
        state.days.back().players.push_back({player_id_counter++, "Вася", Role::none});
        state.days.back().players.push_back({player_id_counter++, "Петя", Role::mafia});
    }

    void Tick() override
    {
        // Clamp active day.
        active_day_index = std::clamp(active_day_index, 0zu, state.days.size() - 1);
        const bool viewing_current_day = active_day_index + 1 == state.days.size();

        Day &active_day = state.days.at(active_day_index);

        { // Player list.
            std::size_t player_index_to_remove = -1zu;

            for (std::size_t i = 0; Player &pl : active_day.players)
            {
                ImGui::BeginChild(("player_box:" + std::to_string(i)).c_str(), ImVec2(0, ImGui::GetTextLineHeight() * 2 + ImGui::GetStyle().FramePadding.y * 2), ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
                ImGui::TextUnformatted(pl.name.c_str());
                ImGui::TextUnformatted(strings.role_names[std::size_t(std::to_underlying(pl.role))].c_str());
                ImGui::PopStyleVar();

                if (ImGui::BeginPopupContextWindow())
                {
                    bool close_menu = false;

                    ImGui::TextDisabled("%s", pl.name.c_str());
                    ImGui::Separator();

                    { // Edit player role.
                        ImGui::BeginDisabled(!viewing_current_day);
                        if (ImGui::Selectable(strings.edit_role_button.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups))
                        {
                            ImGui::OpenPopup(strings.edit_role_window.c_str());
                            new_player_role_for_modal = pl.role;
                        }
                        ImGui::EndDisabled();
                        if (ImGui::IsPopupOpen(strings.edit_role_window.c_str()))
                        {
                            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                            if (ImGui::BeginPopupModal(strings.edit_role_window.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
                            {
                                ImGui::TextUnformatted(pl.name.c_str());

                                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                                if (ImGui::BeginCombo("###role", strings.role_names[std::size_t(std::to_underlying(new_player_role_for_modal))].c_str()))
                                {
                                    for (int i = 0; i < std::to_underlying(Role::_count); i++)
                                    {
                                        if (ImGui::Selectable(strings.role_names[std::size_t(i)].c_str(), i == std::to_underlying(new_player_role_for_modal)))
                                            new_player_role_for_modal = Role(i);
                                    }
                                    ImGui::EndCombo();
                                }

                                ImGui::Spacing();

                                // Confirm button.
                                if (ImGui::Button(strings.edit_role_confirm.c_str()))
                                {
                                    close_menu = true;
                                    pl.role = new_player_role_for_modal;
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::SameLine();
                                // Cancel button.
                                if (ImGui::Button(strings.button_cancel.c_str()) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                                {
                                    close_menu = true;
                                    ImGui::CloseCurrentPopup();
                                }

                                ImGui::EndPopup();
                            }
                        }
                    }

                    { // Delete the player.
                        ImGui::BeginDisabled(!viewing_current_day);
                        if (ImGui::Selectable(strings.remove_player_button.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups))
                            ImGui::OpenPopup(strings.remove_player_window.c_str());
                        ImGui::EndDisabled();
                        if (ImGui::IsPopupOpen(strings.remove_player_window.c_str()))
                        {
                            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                            if (ImGui::BeginPopupModal(strings.remove_player_window.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
                            {
                                ImGui::TextUnformatted(pl.name.c_str());
                                ImGui::Spacing();

                                // Confirm button.
                                if (ImGui::Button(strings.remove_player_confirm.c_str()))
                                {
                                    player_index_to_remove = i;
                                    close_menu = true;
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::SameLine();
                                // Cancel button.
                                if (ImGui::Button(strings.button_cancel.c_str()) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                                {
                                    close_menu = true;
                                    ImGui::CloseCurrentPopup();
                                }

                                ImGui::EndPopup();
                            }
                        }
                    }

                    if (close_menu)
                        ImGui::CloseCurrentPopup();

                    ImGui::EndPopup();
                }

                ImGui::EndChild();

                i++;
            }

            if (player_index_to_remove < active_day.players.size())
                active_day.players.erase(active_day.players.begin() + std::ptrdiff_t(player_index_to_remove));
        }

        // "Add player" button.
        if (viewing_current_day)
        {
            if (ImGui::Button(strings.add_player_button.c_str()))
            {
                ImGui::OpenPopup(strings.add_player_window.c_str());
                add_player_textbox_for_modal.clear();
            }

            if (ImGui::IsPopupOpen(strings.add_player_window.c_str()))
            {
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                if (ImGui::BeginPopupModal(strings.add_player_window.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
                {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::IsWindowAppearing())
                        ImGui::SetKeyboardFocusHere();
                    bool confirmed = ImGui::InputTextWithHint("###player name", strings.add_player_name_hint.c_str(), &add_player_textbox_for_modal, ImGuiInputTextFlags_EnterReturnsTrue);

                    ImGui::Spacing();

                    // Confirm button.
                    ImGui::BeginDisabled(add_player_textbox_for_modal.empty());
                    if (ImGui::Button(strings.add_player_confirm.c_str()) || (!add_player_textbox_for_modal.empty() && confirmed))
                    {
                        active_day.players.push_back(Player{.id = player_id_counter++, .name = std::move(add_player_textbox_for_modal), .role = Role::none});
                        add_player_textbox_for_modal.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    // Cancel button.
                    if (ImGui::Button(strings.button_cancel.c_str()) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    {
                        add_player_textbox_for_modal.clear();
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
            }
        }

        ImGui::ShowDemoWindow();
    }
};

std::unique_ptr<BasicGame> MakeGame()
{
    return std::make_unique<Game>();
}
