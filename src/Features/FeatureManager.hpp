//
// Created by vastrakai on 6/28/2024.
//
#pragma once


#include "Command/CommandManager.hpp"
#include "Modules/ModuleManager.hpp"
#include "Scripting/ScriptManager.hpp"
#include "Features/SafeEventDispatcher.hpp"

class FeatureManager {
public:
    std::unique_ptr<SafeEventDispatcher> mDispatcher;
    std::shared_ptr<ModuleManager> mModuleManager = nullptr;
    std::shared_ptr<CommandManager> mCommandManager = nullptr;
    std::shared_ptr<ScriptManager> mScriptManager = nullptr;

    void init();
    void shutdown();
};

inline std::shared_ptr<FeatureManager> gFeatureManager = nullptr;