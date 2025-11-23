#pragma once

struct BasicGame
{
    virtual ~BasicGame() = default;
    virtual void Tick() = 0;
};
