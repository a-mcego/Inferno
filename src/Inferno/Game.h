#pragma once
#include "Level.h"
#include "HogFile.h"
#include "Mission.h"

namespace Inferno::Game {
    enum class GameState { Game, Editor, Paused };

    inline GameState State = GameState::Editor;

    // The loaded level. Only one level can be active at a time.
    inline Inferno::Level Level;

    // The loaded mission. Not always present.
    inline Option<HogFile> Mission;

    // Only single player for now
    inline PlayerData Player = {};

    // is the game level loading?
    inline std::atomic<bool> IsLoading = false;

    void LoadLevel(Inferno::Level&&);

    void LoadMission(filesystem::path file);

    inline void ReloadMission() {
        if (!Mission) throw Exception("No mission is loaded");
        LoadMission(Mission->Path);
    }

    inline void UnloadMission() {
        Mission = {};
    }

    // Tries to read the mission file (msn / mn2) for the loaded mission
    Option<MissionInfo> TryReadMissionInfo();

    // Elapsed game time  in seconds. Stops when paused.
    inline double ElapsedTime = 0;

    void Update(float dt);
    void ToggleEditorMode();
}
