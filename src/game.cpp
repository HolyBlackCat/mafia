#include "game.h"

#include <cmath>
#include <functional>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <SDL3/SDL_system.h>

#include <algorithm>
#include <array>
#include <map>
#include <utility>
#include <vector>

enum class Role
{
    // Those are ordered by their default turn order. Sync order with `Strings::roles`.
    captain, // Blocks night-time ability of any player. Targeting mafia (possibly boss) blocks their combined ability.
    sheriff, // Detects mafia, or killer if no mafia.
    prostitute, // Protects from death by vote on the next day.
    mafia_boss, // Same as normal mafia, but detects sheriff.
    mafia, // Mafia.
    yakuza, // Second kind of mafia, "yakuza".
    killer, // Its own faction, kills at night like mafia, but completely independent.
    none, // This must be last.
    _count [[maybe_unused]],
};

enum class Faction
{
    // Sync with `Strings::factions`.
    peaceful,
    mafia,
    yakuza,
    killer,
    _count [[maybe_unused]],
};

[[nodiscard]] static Faction RoleToFaction(Role role)
{
    switch (role)
    {
        case Role::captain:    return Faction::peaceful;
        case Role::sheriff:    return Faction::peaceful;
        case Role::prostitute: return Faction::peaceful;
        case Role::mafia_boss: return Faction::mafia;
        case Role::mafia:      return Faction::mafia;
        case Role::yakuza:     return Faction::yakuza;
        case Role::killer:     return Faction::killer;
        case Role::none:       return Faction::peaceful;
    }
}

static void ModalPopup(const std::string &name, std::function<void()> body)
{
    if (!ImGui::IsPopupOpen(name.c_str()))
        return;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(name.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        body();
        ImGui::EndPopup();
    }
}

struct Strings
{
    std::string button_cancel = "Отмена";

    struct RoleStrings
    {
        std::string name;
        std::string prompt;
    };

    std::array<RoleStrings, int(Role::_count)> roles = {{
        {.name = "Капитан",       .prompt = "Кого блокирует капитан?"  },
        {.name = "Шериф",         .prompt = "Кого проверяет шериф?"    },
        {.name = "Красотка",      .prompt = "К кому приходит красотка?"},
        {.name = "Дон мафии",     .prompt = "Кого проверяет дон?"      },
        {.name = "Мафия",         .prompt = "Кого убивает мафия?"      },
        {.name = "Якудза",        .prompt = "Кого убивает якудза?"     },
        {.name = "Маньяк",        .prompt = "Кого убивает маньяк?"     },
        {.name = "Мирный житель", .prompt = "Кого убивает город?"      },
    }};

    struct FactionStrings
    {
        std::string name;
        std::string name_pl;
    };

    std::array<FactionStrings, int(Faction::_count)> factions = {{
        {.name = "Мирный", .name_pl = "Мирные" },
        {.name = "Мафия",  .name_pl = "Мафия"  },
        {.name = "Якудза", .name_pl = "Якудза" },
        {.name = "Маньяк", .name_pl = "Маньяки"},
    }};

    std::string menu_button = "Меню";
    std::string menu_window = "Меню";
    std::string menu_button_back = "Назад";
    std::string menu_button_new_game = "Новая игра";

    std::string new_game_window = "Начать новую игру?";
    std::string new_game_confirm = "Новая игра";

    std::string day = "День";
    std::string night = "Ночь";
    std::string day_turn = "Город";
    std::string choosing_roles = "Перекличка";

    std::string players = "Игроки:";
    std::string turns = "Ход:";

    std::string next_turn = "Дальше";
    std::string restart_from_here = "Переиграть с этого дня";

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

struct Player
{
    int id = 0;
    std::string name;
    Role role;

    int times_targeted_by_captain = 0;
    int times_targeted_by_sheriff = 0;
    int times_targeted_by_prostitute = 0;
    int times_targeted_by_mafia_boss = 0;
};

struct Action
{
    std::vector<int> targets;
};

struct Day
{
    std::vector<Player> players;

    // The indices here are `Role`s.
    std::array<Action, int(Role::_count)> actions;

    [[nodiscard]] bool HavePlayersWithRole(Role role) const
    {
        return std::any_of(players.begin(), players.end(), [&](const Player &pl){return pl.role == role;});
    }
};

struct State
{
    std::vector<Day> days;
};

struct Settings
{
    std::array<Role, int(Role::_count)> role_order;

    void SetDefault()
    {
        for (int i = 0; i < int(Role::_count); i++)
            role_order[std::size_t(i)] = Role(i);
    }
};

struct Round
{
    State state;

    int active_day_index = 0;
    int active_role_index = 0; // This is an index into `Settings::role_order`.

    std::array<bool, int(Role::_count)> enabled_roles{};

    Round()
    {
        enabled_roles[std::size_t(Role::none)] = true;
        enabled_roles[std::size_t(Role::mafia)] = true;
    }
};

struct Game : BasicGame
{
    Settings settings;
    Round this_round;
    Strings strings;

    std::string add_player_textbox_for_modal;
    Role new_player_role_for_modal{};

    int player_id_counter = 1;

    void SetFirstActiveRole()
    {
        if (!this_round.state.days[std::size_t(this_round.active_day_index)].players.empty())
        {
            this_round.active_role_index = -1;
            NextTurn();
        }
    }

    void NextTurn()
    {
        this_round.active_role_index++;
        while (true)
        {
            if (this_round.active_role_index == int(Role::_count))
            {
                this_round.state.days.push_back(this_round.state.days.back());
                this_round.active_day_index = int(this_round.state.days.size()) - 1;
                SetFirstActiveRole();
            }

            if (this_round.state.days[std::size_t(this_round.active_day_index)].HavePlayersWithRole(settings.role_order[std::size_t(this_round.active_role_index)]))
                break;
            this_round.active_role_index++;
        }
    }

    Game()
    {
        settings.SetDefault();

        this_round.state.days.emplace_back();
        this_round.state.days.back().players.push_back({player_id_counter++, "Вася", Role::none});
        this_round.state.days.back().players.push_back({player_id_counter++, "Петя", Role::mafia});
        SetFirstActiveRole();
    }

    void Tick() override
    {
        State &state = this_round.state;

        // Clamp active day.
        this_round.active_day_index = std::clamp(this_round.active_day_index, 0, int(state.days.size()) - 1);
        const bool viewing_current_day = this_round.active_day_index + 1 == int(state.days.size());

        Day &active_day = state.days.at(std::size_t(this_round.active_day_index));

        const Role active_role = settings.role_order[std::size_t(this_round.active_role_index)];

        ImGui::SetNextWindowPos(ImVec2{});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Mafia", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);

        // Zeroes are not written here.
        std::map<Faction, int> faction_summary;
        for (const Player &pl : active_day.players)
            faction_summary[RoleToFaction(pl.role)]++;

        { // Top status.
            ImGui::BeginChild("status", ImVec2(0, ImGui::GetTextLineHeight()));

            if (this_round.active_day_index == 0 && active_role != Role::none)
            {
                ImGui::Text("%s - %s", strings.choosing_roles.c_str(), strings.roles[std::size_t(active_role)].name.c_str());
            }
            else
            {
                ImGui::Text("%s %i - %s",
                    active_role == Role::none ? strings.day.c_str() : strings.night.c_str(),
                    this_round.active_day_index,
                    strings.roles[std::size_t(active_role)].prompt.c_str()
                );
            }

            ImGui::EndChild();

            ImGui::Separator();
        }

        ImGui::BeginTable("Table", 2, ImGuiTableFlags_NoHostExtendY, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight() * 2 - ImGui::GetStyle().ItemSpacing.y * 5 - ImGui::GetTextLineHeight()));
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s (%d)", strings.players.c_str(), int(active_day.players.size()));
        ImGui::BeginChild("player_list", ImGui::GetContentRegionAvail());

        { // Player list.
            std::size_t player_index_to_remove = -1zu;

            for (std::size_t i = 0; Player &pl : active_day.players)
            {
                ImGui::BeginChild(("player_box:" + std::to_string(i)).c_str(), ImVec2(0, ImGui::GetTextLineHeight() * 2 + ImGui::GetStyle().FramePadding.y * 2), ImGuiChildFlags_FrameStyle, ImGuiWindowFlags_NoScrollbar);

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());
                ImGui::TextUnformatted(pl.name.c_str());
                ImGui::TextUnformatted(strings.roles[std::size_t(int(pl.role))].name.c_str());
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
                        ModalPopup(strings.edit_role_window, [&]
                        {
                            ImGui::TextUnformatted(pl.name.c_str());

                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                            if (ImGui::BeginCombo("###role", strings.roles[std::size_t(int(new_player_role_for_modal))].name.c_str()))
                            {
                                for (int i = 0; i < int(Role::_count); i++)
                                {
                                    if (ImGui::Selectable(strings.roles[std::size_t(i)].name.c_str(), i == int(new_player_role_for_modal)))
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
                        });
                    }

                    { // Delete the player.
                        ImGui::BeginDisabled(!viewing_current_day);
                        if (ImGui::Selectable(strings.remove_player_button.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups))
                            ImGui::OpenPopup(strings.remove_player_window.c_str());
                        ImGui::EndDisabled();
                        ModalPopup(strings.remove_player_window, [&]
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
                        });
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

            // "Add player" button.
            if (viewing_current_day)
            {
                if (ImGui::Button(strings.add_player_button.c_str()))
                {
                    ImGui::OpenPopup(strings.add_player_window.c_str());
                    add_player_textbox_for_modal.clear();
                }

                ModalPopup(strings.add_player_window, [&]
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
                });
            }
        }

        ImGui::EndChild();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", strings.turns.c_str());
        ImGui::BeginChild("turn_list");

        { // Turns.
            bool first_role = true;
            for (int i = 0; i < int(Role::_count); i++)
            {
                const Role this_role = settings.role_order[std::size_t(i)];
                Action &this_action = active_day.actions[std::size_t(this_role)];

                if (this_round.active_day_index > 0 && !this_round.enabled_roles[std::size_t(this_role)])
                    continue;

                const bool have_players = this_round.active_day_index > 0 ? active_day.HavePlayersWithRole(this_role) : this_round.enabled_roles[std::size_t(this_role)];

                std::string_view turn_name;
                if (this_role != Role::none)
                {
                    if (this_round.active_day_index > 0 && std::exchange(first_role, false))
                        ImGui::SeparatorText(strings.night.c_str());

                    turn_name = strings.roles[std::size_t(this_role)].name;
                }
                else
                {
                    if (this_round.active_day_index == 0)
                        continue;

                    ImGui::SeparatorText(strings.day.c_str());
                    turn_name = strings.day_turn;
                }

                const ImVec2 base_pos = ImGui::GetCursorPos();

                if (this_round.active_day_index == 0)
                {
                    ImGui::SetCursorPosX(base_pos.x + ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight());

                    ImGui::Checkbox(("###toggle_role:" + std::to_string(i)).c_str(), &this_round.enabled_roles[std::size_t(this_role)]);
                    ImGui::SameLine();

                    ImGui::SetCursorPos(base_pos);
                }

                ImGui::BeginDisabled(!have_players);
                ImGui::RadioButton(turn_name.data(), &this_round.active_role_index, i);
                ImGui::EndDisabled();


                if (!have_players && viewing_current_day)
                    this_action = {}; // Reset the action, just in case.
            }
        }

        ImGui::EndChild();
        ImGui::EndTable();

        { // Summary.
            ImGui::Separator();

            std::string summary_str;
            for (auto [fac, n] : faction_summary)
            {
                if (!summary_str.empty())
                    summary_str += " | ";
                const auto &strs = strings.factions[std::size_t(fac)];
                summary_str += n == 1 ? strs.name : strs.name_pl;
                summary_str += ": ";
                summary_str += std::to_string(n);
            }

            ImGui::BeginChild("factions_summary", ImVec2(0, ImGui::GetTextLineHeight()));
            ImGui::TextUnformatted(summary_str.c_str());
            ImGui::EndChild();
        }

        bool want_new_game = false;

        { // Bottom buttons.
            ImGui::Separator();

            if (ImGui::Button(strings.menu_button.c_str()))
                ImGui::OpenPopup(strings.menu_window.c_str());
            ModalPopup(strings.menu_button, [&]
            {
                // New game button.
                if (ImGui::Button(strings.menu_button_new_game.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                    ImGui::OpenPopup(strings.new_game_window.c_str());

                bool close_outer_modal = false;
                ModalPopup(strings.new_game_window, [&]
                {
                    if (ImGui::Button(strings.new_game_confirm.c_str()))
                    {
                        close_outer_modal = true;
                        want_new_game = true;
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SameLine();

                    if (ImGui::Button(strings.button_cancel.c_str()))
                        ImGui::CloseCurrentPopup();
                });
                if (close_outer_modal)
                    ImGui::CloseCurrentPopup();

                // Close menu button.
                if (ImGui::Button(strings.menu_button_back.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                    ImGui::CloseCurrentPopup();
            });

            ImGui::SameLine();

            ImGui::BeginDisabled(!viewing_current_day);
            if (ImGui::Button(strings.next_turn.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                NextTurn();
            ImGui::EndDisabled();

            const float width = std::round((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) / 4 - ImGui::GetStyle().ItemSpacing.x);

            int next_day_index = -1;

            ImGui::BeginDisabled(this_round.active_day_index == 0);
            if (ImGui::Button("|<", ImVec2(width, 0)))
                next_day_index = 0;
            ImGui::EndDisabled();

            ImGui::SameLine();

            ImGui::BeginDisabled(this_round.active_day_index == 0);
            if (ImGui::Button("<", ImVec2(width, 0)))
                next_day_index = this_round.active_day_index - 1;
            ImGui::EndDisabled();

            ImGui::SameLine();

            ImGui::BeginDisabled(this_round.active_day_index + 1 == int(state.days.size()));
            if (ImGui::Button(">", ImVec2(width, 0)))
                next_day_index = this_round.active_day_index + 1;
            ImGui::EndDisabled();

            ImGui::SameLine();

            ImGui::BeginDisabled(this_round.active_day_index + 1 == int(state.days.size()));
            if (ImGui::Button(">|", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                next_day_index = int(state.days.size()) - 1;
            ImGui::EndDisabled();

            // Actually switch day.
            if (next_day_index != -1)
            {
                this_round.active_day_index = next_day_index;
                if (viewing_current_day)
                    SetFirstActiveRole();
            }
        }

        ImGui::End();

        // Lastly, act on the "new game" button.
        if (std::exchange(want_new_game, false))
        {
            auto players = std::move(state.days.back().players);
            auto roles = std::move(this_round.enabled_roles);
            state = {};
            state.days.emplace_back();

            state.days.back().players = std::move(players);
            this_round.enabled_roles = std::move(roles);
        }
    }
};

std::unique_ptr<BasicGame> MakeGame()
{
    return std::make_unique<Game>();
}
