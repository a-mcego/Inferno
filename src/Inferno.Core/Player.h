#pragma once

#include "Types.h"
#include "Weapon.h"

namespace Inferno {
    enum class PowerupFlag : uint32 {
        Invulnerable = 1 << 0,
        BlueKey = 1 << 1,
        RedKey = 1 << 2,
        GoldKey = 1 << 3,
        Flag = 1 << 4, // Carrying flag, for CTF mode
        MapEnemies = 1 << 5, // Show enemies on the map, unused
        FullMap = 1 << 6,
        AmmoRack = 1 << 7,
        Converter = 1 << 8, // Energy to shield converter
        FullMapCheat = 1 << 9, // Same as full map, except unexplored areas aren't blue
        QuadLasers = 1 << 10,
        Cloaked = 1 << 11,
        Afterburner = 1 << 12,
        Headlight = 1 << 13,
        HeadlightOn = 1 << 14
    };

    // Serialized player info
    struct PlayerInfo {
        static constexpr int MAX_PRIMARY_WEAPONS = 10;
        static constexpr int MAX_SECONDARY_WEAPONS = 10;
        static constexpr int CALLSIGN_LEN = 8; // so can be used as a 8.3 file name

        ObjID ID = ObjID(0);       // What object number this player is

        struct {
            char Callsign[CALLSIGN_LEN + 1];
            uint8 Address[4];
            uint16 Port;
            bool Connected;
            int PacketsGot, PacketsSent;
            short KillGoal; // when Kills >= Kill goal game ends
            short Deaths;
            short Kills;
        } Net;

        // Game data
        PowerupFlag Powerups;
        float Energy = 100;
        float Shields = 100;
        uint8 Lives = 2;
        int8 Level;             // Level the player is in. Negative for secret levels
        uint8 LaserLevel;       // 0 to 5
        int8 StartingLevel;     // Level the player started the mission on. Used for final score screen.
        ObjID KilledBy = ObjID::None; // Used for multiplayer kill messages, but also gets set by robots
        uint16 PrimaryWeapons;    // Each bit represents an owned primary weapon
        uint16 SecondaryWeapons;  // Each bit represents an owned secondary weapon
        uint16 PrimaryAmmo[MAX_PRIMARY_WEAPONS];
        uint16 SecondaryAmmo[MAX_SECONDARY_WEAPONS];

        int Score;
        int64 LevelTime, TotalTime;

        float CloakTime; // Amount of cloak remaining
        float InvulnerableTime; // Amount of invulnerability remaining

        struct {
            short Kills;            // Robots killed this level. Used to prevent matcens from spawning too many robots.
            short TotalKills;       // Total kills across all levels. Used for scoring
            short Robots;           // Number of initial robots this level. Used to prevent matcens from spawning too many robots. Why is this here?
            short TotalRobots;      // Number of robots total. Used for final score ratio.
            uint16 TotalRescuedHostages; // Total hostages rescued by the player.
            uint16 TotalHostages;   // Total hostages in all levels. Used for final score ratio
            uint8 HostagesOnShip;   // How many poor sods get killed when ship is lost
            uint8 HostageOnLevel;   // Why is this here?
        } Stats;

        float HomingObjectDist; // Distance of nearest homing object. Used for lock indicators.
    };

}