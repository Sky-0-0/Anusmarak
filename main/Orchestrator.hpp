#pragma once
#include "Utilities.hpp"

// ------- Orchestrator -------
class Orchestrator {
public:
    explicit Orchestrator(Hardware& Board) : _Board(Board) {}
    static void Run();
    void Initialize();
private:
    void _Task();
    Hardware& _Board;
    Logger _Log{"ORCHESTRATOR", Configuration::Logger::LF::ORCHESTRATOR};
};
