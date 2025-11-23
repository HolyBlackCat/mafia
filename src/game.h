#pragma once

#include "basic_game.h"

#include <memory>

[[nodiscard]] std::unique_ptr<BasicGame> MakeGame();
