#include "pch.h"
#include "Game.h"
#include "FileSystem.h"
#include "Graphics/Render.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Input.h"
#include "Graphics/Render.Particles.h"
#include "Physics.h"
#include "Graphics/Render.Debug.h"
#include "imgui_local.h"
#include "Editor/Editor.h"
#include "Editor/UI/EditorUI.h"
#include "Editor/Editor.Object.h"
#include "Game.Input.h"
#include "DebugOverlay.h"
#include "HUD.h"

using namespace DirectX;

namespace Inferno::Game {
    uint16 ObjSigIndex = 0;

    ObjSig GetObjectSig() {
        return ObjSig(ObjSigIndex++);
    }

    void LoadLevel(Inferno::Level&& level) {
        Inferno::Level backup = Level;

        try {
            assert(level.FileName != "");
            bool reload = level.FileName == Level.FileName;

            Editor::LoadTextureFilter(level);
            bool forceReload =
                level.IsDescent2() != Level.IsDescent2() ||
                Resources::HasCustomTextures() ||
                !String::InvariantEquals(level.Palette, Level.Palette);

            IsLoading = true;

            Level = std::move(level); // Move to global so resource loading works properly
            Resources::LoadLevel(Level);

            ObjSigIndex = 0;
            for (auto& obj : Level.Objects) {
                obj.LastPosition = obj.Position;
                obj.LastRotation = obj.Rotation;
                obj.Signature = GetObjectSig();
            }

            if (forceReload || Resources::HasCustomTextures()) // Check for custom textures before or after load
                Render::Materials->Unload();

            Render::Materials->LoadLevelTextures(Level, forceReload);
            Render::LoadLevel(Level);
            IsLoading = false;

            //Sound::Reset();
            Editor::OnLevelLoad(reload);
            Render::Materials->Prune();
            Render::Adapter->PrintMemoryUsage();
        }
        catch (const std::exception&) {
            Level = backup; // restore the old level if something went wrong
            throw;
        }
    }

    void LoadMission(filesystem::path file) {
        Mission = HogFile::Read(FileSystem::FindFile(file));
    }

    // Tries to read the mission file (msn / mn2) for the loaded mission
    Option<MissionInfo> TryReadMissionInfo() {
        try {
            if (!Mission) return {};
            auto path = Mission->GetMissionPath();
            MissionInfo mission{};
            if (!mission.Read(path)) return {};
            return mission;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR(e.what());
            return {};
        }
    }

    void UpdateAmbientSounds() {
        auto& player = Level.Objects[0];
        bool hasLava = bool(Level.GetSegment(player.Segment).AmbientSound & SoundFlag::AmbientLava);
        bool hasWater = bool(Level.GetSegment(player.Segment).AmbientSound & SoundFlag::AmbientWater);

        SoundID sound{};

        if (hasLava) {
            sound = SoundID::AmbientLava;
            if (hasWater && Random() > 0.5f) // if both water and lava pick one at random
                sound = SoundID::AmbientWater;
        }
        else if (hasWater) {
            sound = SoundID::AmbientWater;
        }
        else {
            return;
        }

        if (Random() < 0.003f) {
            // Playing the sound at player is what the original game does,
            // but it would be nicer to come from the environment instead...
            Sound3D s(ObjID(0));
            s.Volume = Random() + 0.5f;
            s.Resource = Resources::GetSoundResource(sound);
            s.AttachToSource = true;
            s.FromPlayer = true;
            Sound::Play(s);
        }
    }

    using Keys = Keyboard::Keys;

    void HandleGlobalInput() {
        if (Input::IsKeyPressed(Keys::F1))
            Game::ShowDebugOverlay = !Game::ShowDebugOverlay;

        if (Input::IsKeyPressed(Keys::F2))
            Game::ToggleEditorMode();

        if (Input::IsKeyPressed(Keys::F3))
            Settings::Inferno.ScreenshotMode = !Settings::Inferno.ScreenshotMode;

        if (Input::IsKeyPressed(Keys::F5))
            Render::Adapter->ReloadResources();

        if (Input::IsKeyPressed(Keys::F6))
            Render::ReloadTextures();

        if (Input::IsKeyPressed(Keys::F7)) {
            Settings::Graphics.HighRes = !Settings::Graphics.HighRes;
            Render::ReloadTextures();
        }
    }

    Object& AllocObject() {
        for (auto& obj : Level.Objects) {
            if (!obj.IsAlive()) {
                obj = {};
                return obj;
            }
        }

        return Level.Objects.emplace_back();
    }

    // Objects to be added at the end of this tick.
    // Exists to prevent modifying object list size mid-update.
    List<Object> PendingNewObjects;

    void DropContainedItems(const Object& obj) {
        switch (obj.Contains.Type) {
            case ObjectType::Powerup:
            {
                for (int i = 0; i < obj.Contains.Count; i++) {
                    Object powerup{};
                    powerup.Position = obj.Position;
                    powerup.Type = ObjectType::Powerup;
                    powerup.ID = obj.Contains.ID;
                    powerup.Segment = obj.Segment;

                    auto vclip = Resources::GameData.Powerups[powerup.ID].VClip;
                    powerup.Render.Type = RenderType::Powerup;
                    powerup.Render.VClip.ID = vclip;
                    powerup.Render.VClip.Frame = 0;
                    powerup.Render.VClip.FrameTime = Resources::GetVideoClip(vclip).FrameTime;

                    powerup.Movement = MovementType::Physics;
                    powerup.Physics.Velocity = RandomVector(32);
                    powerup.Physics.Mass = 1;
                    powerup.Physics.Drag = 0.01f;
                    powerup.Physics.Flags = PhysicsFlag::Bounce;

                    // game originally times-out conc missiles, shields and energy after about 3 minutes
                    AddObject(powerup);
                }
                break;
            }
            case ObjectType::Robot:
            {

                break;
            }
            default:
                break;
        }

    }

    void AddObject(const Object& obj) {
        PendingNewObjects.push_back(obj);
    }

    void DestroyObject(Object& obj) {
        if (obj.Lifespan < 0 && obj.HitPoints < 0) return; // already dead

        switch (obj.Type) {
            case ObjectType::Fireball:
            {
                break;
            }

            case ObjectType::Robot:
            {
                constexpr float EXPLOSION_DELAY = 0.2f;

                auto& robot = Resources::GetRobotInfo(obj.ID);

                Render::ExplosionInfo expl;
                expl.Sound = robot.ExplosionSound2;
                expl.Clip = robot.ExplosionClip2;
                expl.MinRadius = expl.MaxRadius = obj.Radius * 1.9f;
                expl.Segment = obj.Segment;
                expl.Position = obj.GetPosition(LerpAmount);
                Render::CreateExplosion(expl);

                expl.Sound = SoundID::None;
                expl.InitialDelay = EXPLOSION_DELAY;
                expl.MinRadius = obj.Radius * 1.15f;
                expl.MaxRadius = obj.Radius * 1.55f;
                expl.Variance = obj.Radius * 0.5f;
                expl.Instances = 1;
                Render::CreateExplosion(expl);

                auto& model = Resources::GetModel(robot.Model);
                for (int i = 0; i < model.Submodels.size(); i++) {
                    // accumulate the offsets for each submodel
                    auto submodelOffset = model.GetSubmodelOffset(i);
                    Matrix transform = Matrix::Lerp(obj.GetLastTransform(), obj.GetTransform(), Game::LerpAmount);
                    //auto transform = obj.GetLastTransform();
                    transform.Forward(-transform.Forward()); // flip z axis to correct for LH models
                    auto world = Matrix::CreateTranslation(submodelOffset) * transform;
                    //world = obj.GetLastTransform();

                    auto explosionVec = world.Translation() - obj.Position;
                    explosionVec.Normalize();

                    auto hitForce = obj.LastHitForce * (1.0f + Random() * 0.5f);

                    Render::Debris debris;
                    //Vector3 vec(Random() + 0.5, Random() + 0.5, Random() + 0.5);
                    //auto vec = RandomVector(obj.Radius * 5);
                    //debris.Velocity = vec + obj.LastHitVelocity / (4 + obj.Movement.Physics.Mass);
                    //debris.Velocity =  RandomVector(obj.Radius * 5);
                    debris.Velocity = i == 0 ? hitForce
                        : explosionVec * 25 + RandomVector(10) + hitForce;
                    debris.Velocity += obj.Physics.Velocity;
                    debris.AngularVelocity = RandomVector(std::min(obj.LastHitForce.Length(), 3.14f));
                    debris.Transform = world;
                    //debris.Transform.Translation(debris.Transform.Translation() + RandomVector(obj.Radius / 2));
                    debris.PrevTransform = world;
                    debris.Mass = 1; // obj.Movement.Physics.Mass;
                    debris.Drag = 0.0075f; // obj.Movement.Physics.Drag;
                    // It looks weird if the main body (sm 0) sticks around too long, so destroy it quicker
                    debris.Life = 0.15f + Random() * (i == 0 ? 0.0f : 1.75f);
                    debris.Segment = obj.Segment;
                    debris.Radius = model.Submodels[i].Radius;
                    //debris.Model = (ModelID)Resources::GameData.DeadModels[(int)robot.Model];
                    debris.Model = robot.Model;
                    debris.Submodel = i;
                    debris.TexOverride = Resources::LookupLevelTexID(obj.Render.Model.TextureOverride);
                    Render::AddDebris(debris);
                }

                DropContainedItems(obj);
                break;
            }

            case ObjectType::Player:
            {
                // Player_ship->expl_vclip_num
                break;
            }

            case ObjectType::Weapon:
            {
                if (obj.ID == (int)WeaponID::ProxMine) {
                    auto& weapon = Resources::GetWeapon((WeaponID)obj.ID);
                    ExplodeBomb(weapon, obj);
                }
                break;
            }

            default:
                // VCLIP_SMALL_EXPLOSION = 2
                break;
        }


        obj.Lifespan = -1;
        obj.HitPoints = -1;
    }

    Tuple<ObjID, float> FindNearestObject(const Object& src) {
        ObjID id = ObjID::None;
        //auto& srcObj = Level.GetObject(src);
        float dist = FLT_MAX;

        for (int i = 0; i < Level.Objects.size(); i++) {
            auto& obj = Level.Objects[i];
            if (&obj == &src) continue;
            auto d = Vector3::Distance(obj.Position, src.Position);
            if (d < dist) {
                id = (ObjID)i;
                dist = d;
            }
        }

        return { id, dist };
    }

    void UpdatePlayerFireState(Inferno::Player& player) {
        // must check held keys inside of fixed updates so events aren't missed due to the state changing
        // on a frame that doesn't have a game tick
        if ((Game::State == GameState::Editor && Input::IsKeyDown(Keys::Enter)) ||
            (Game::State != GameState::Editor && Input::Mouse.leftButton == Input::MouseState::HELD)) {
            if (player.PrimaryState == FireState::None)
                player.PrimaryState = FireState::Press;
            else if (player.PrimaryState == FireState::Press)
                player.PrimaryState = FireState::Hold;
        }
        else {
            if (player.PrimaryState == FireState::Release)
                player.PrimaryState = FireState::None;
            else if (player.PrimaryState != FireState::None)
                player.PrimaryState = FireState::Release;
        }

        if ((Game::State != GameState::Editor && Input::Mouse.rightButton == Input::MouseState::HELD)) {
            if (player.SecondaryState == FireState::None)
                player.SecondaryState = FireState::Press;
            else if (player.SecondaryState == FireState::Press)
                player.SecondaryState = FireState::Hold;
        }
        else {
            if (player.SecondaryState == FireState::Release)
                player.SecondaryState = FireState::None;
            else if (player.SecondaryState != FireState::None)
                player.SecondaryState = FireState::Release;
        }
    }

    void AddPendingObjects() {
        for (auto& obj : PendingNewObjects) {
            obj.LastPosition = obj.Position;
            obj.LastRotation = obj.Rotation;
            obj.Signature = GetObjectSig();

            bool foundExisting = false;
            ObjID id = ObjID::None;

            for (int i = 0; i < Level.Objects.size(); i++) {
                auto& o = Level.Objects[i];
                if (!o.IsAlive()) {
                    o = obj;
                    foundExisting = true;
                    id = ObjID(i);
                    break;
                }
            }

            if (!foundExisting) {
                id = ObjID(Level.Objects.size());
                Level.Objects.push_back(obj);
            }

            // Hack to insert tracers due to not having the object ID in firing code
            if (obj.Type == ObjectType::Weapon) {
                auto& weapon = Resources::GetWeapon((WeaponID)obj.ID);

                if ((WeaponID)obj.ID == WeaponID::Vulcan) {
                    Render::TracerInfo tracer{
                        .Parent = id,
                        .Length = 30.0f,
                        .Width = 0.30f,
                        .Texture = "vausstracer",
                        .BlobTexture = "Tracerblob",
                        .Color = { 2, 2, 2 },
                        .FadeSpeed = 0.10f,
                    };
                    Render::AddTracer(tracer);
                }

                if ((WeaponID)obj.ID == WeaponID::Gauss) {
                    Render::TracerInfo tracer{
                        .Parent = id,
                        .Length = 30.0f,
                        .Width = 0.60f,
                        .Texture = "MassDriverTracer",
                        .BlobTexture = "MassTracerblob",
                        .Color = { 2, 2, 2 },
                        .FadeSpeed = 0.20f,
                    };
                    Render::AddTracer(tracer);
                }
            }
        }

        PendingNewObjects.clear();
    }

    // Updates on each game tick
    void FixedUpdate(float dt) {
        UpdatePlayerFireState(Player);
        Player.Update(dt);

        Render::UpdateDebris(dt);
        Render::UpdateExplosions(dt);
        UpdateAmbientSounds();

        for (int i = 0; i < Level.Objects.size(); i++) {
            auto& obj = Level.Objects[i];

            if (obj.HitPoints < 0 || obj.Lifespan < 0) {
                DestroyObject(obj);
                continue;
            }

            if (obj.Type == ObjectType::Weapon)
                UpdateWeapon(obj, dt);
        }

        AddPendingObjects();
    }

    // Returns the lerp amount for the current tick
    float GameTick(float dt) {
        static double accumulator = 0;
        static double t = 0;

        accumulator += dt;
        accumulator = std::min(accumulator, 2.0);

        if (!Level.Objects.empty()) {
            auto& physics = Level.Objects[0].Physics; // player
            physics.Thrust = Vector3::Zero;
            physics.AngularThrust = Vector3::Zero;

            if (Game::State == GameState::Editor) {
                if (Settings::Editor.EnablePhysics)
                    HandleEditorDebugInput(dt);
            }
            else {
                HandleInput(dt);
            }

            // Clamp max input speeds
            auto maxAngularThrust = Resources::GameData.PlayerShip.MaxRotationalThrust;
            auto maxThrust = Resources::GameData.PlayerShip.MaxThrust;
            Vector3 maxAngVec(Settings::Inferno.LimitPitchSpeed ? maxAngularThrust / 2 : maxAngularThrust, maxAngularThrust, maxAngularThrust);
            physics.AngularThrust.Clamp(-maxAngVec, maxAngVec);
            Vector3 maxThrustVec(maxThrust, maxThrust, maxThrust);
            physics.Thrust.Clamp(-maxThrustVec, maxThrustVec);
        }

        while (accumulator >= TICK_RATE) {
            UpdatePhysics(Game::Level, t, TICK_RATE); // catch up if physics falls behind
            FixedUpdate(TICK_RATE);
            accumulator -= TICK_RATE;
            t += TICK_RATE;
            Game::DeltaTime += TICK_RATE;
        }

        if (Game::ShowDebugOverlay) {
            auto vp = ImGui::GetMainViewport();
            DrawDebugOverlay({ vp->Size.x, 0 }, { 1, 0 });
            DrawGameDebugOverlay({ 10, 10 }, { 0, 0 });
        }

        return float(accumulator / TICK_RATE);
    }

    Inferno::Editor::EditorUI EditorUI;

    void MoveCameraToObject(Camera& camera, const Object& obj, float lerp) {
        Matrix transform = Matrix::Lerp(obj.GetLastTransform(), obj.GetTransform(), lerp);
        camera.Position = transform.Translation();
        camera.Target = camera.Position + transform.Forward();
        camera.Up = transform.Up();
    }

    void Update(float dt) {
        Inferno::Input::Update();
        HandleGlobalInput();
        Render::Debug::BeginFrame(); // enable debug calls during updates
        Game::DeltaTime = 0;

        g_ImGuiBatch->BeginFrame();
        switch (State) {
            case GameState::Game:
                LerpAmount = GameTick(dt);
                if (!Level.Objects.empty())
                    MoveCameraToObject(Render::Camera, Level.Objects[0], LerpAmount);

                Render::UpdateParticles(Level, dt);
                break;
            case GameState::Editor:
                if (Settings::Editor.EnablePhysics) {
                    LerpAmount = Settings::Editor.EnablePhysics ? GameTick(dt) : 1;
                }
                else {
                    LerpAmount = 1;
                }

                Render::UpdateParticles(Level, dt);
                Editor::Update();
                if (!Settings::Inferno.ScreenshotMode) EditorUI.OnRender();
                break;
            case GameState::Paused:
                break;
        }

        g_ImGuiBatch->EndFrame();
        Render::Present(LerpAmount);
    }

    Camera EditorCameraSnapshot;

    SoundID GetSoundForSide(const SegmentSide& side) {
        auto ti1 = Resources::TryGetEffectClip(side.TMap);
        auto ti2 = Resources::TryGetEffectClip(side.TMap2);

        if (ti1 && ti1->Sound != SoundID::None)
            return ti1->Sound;
        if (ti2 && ti2->Sound != SoundID::None)
            return ti2->Sound;

        return SoundID::None;
    }

    // Adds sound sources from eclips such as lava and forcefields
    void AddSoundSources() {
        // todo: clear sounds

        for (int i = 0; i < Level.Segments.size(); i++) {
            SegID segid = SegID(i);
            auto& seg = Level.GetSegment(segid);
            for (auto& sid : SideIDs) {
                if (!seg.SideIsSolid(sid, Level)) continue;

                auto& side = seg.GetSide(sid);
                auto sound = GetSoundForSide(side);
                if (sound == SoundID::None) continue;

                if (auto cside = Level.TryGetConnectedSide({ segid, sid })) {
                    auto csound = GetSoundForSide(*cside);
                    if (csound == sound && seg.GetConnection(sid) < segid)
                        continue; // skip sound on lower numbered segment
                }

                // Place the sound behind the wall to reduce the doppler effect of flying by it
                Sound3D s(side.Center - side.AverageNormal * 15, segid);
                s.Looped = true;
                s.Radius = 150;
                s.Resource = Resources::GetSoundResource(sound);
                s.Volume = 0.55f;
                s.Occlusion = false;
                Sound::Play(s);
            }
        }
    }

    void MarkNearby(SegID id, span<int8> marked, int depth) {
        if (depth < 0) return;
        marked[(int)id] = true;

        auto& seg = Level.GetSegment(id);
        for (auto& sid : SideIDs) {
            auto conn = seg.GetConnection(sid);
            if (conn > SegID::None && !seg.SideIsWall(sid) && !marked[(int)conn])
                MarkNearby(conn, marked, depth - 1);
        }
    }

    void MarkAmbientSegments(SoundFlag sflag, TextureFlag tflag) {
        List<int8> marked(Level.Segments.size());

        for (auto& seg : Level.Segments) {
            seg.AmbientSound &= ~sflag;
        }

        for (int i = 0; i < Level.Segments.size(); i++) {
            auto& seg = Level.Segments[i];
            for (auto& sid : SideIDs) {
                auto& side = seg.GetSide(sid);
                auto& tmi1 = Resources::GetLevelTextureInfo(side.TMap);
                auto& tmi2 = Resources::GetLevelTextureInfo(side.TMap2);
                if (tmi1.HasFlag(tflag) || tmi2.HasFlag(tflag)) {
                    seg.AmbientSound |= sflag;
                }
            }
        }

        constexpr auto MAX_DEPTH = 5;

        for (int i = 0; i < Level.Segments.size(); i++) {
            auto& seg = Level.Segments[i];
            if (bool(seg.AmbientSound & sflag))
                MarkNearby(SegID(i), marked, MAX_DEPTH);
        }

        for (int i = 0; i < Level.Segments.size(); i++) {
            if (marked[i])
                Level.Segments[i].AmbientSound |= sflag;
        }
    }

    void ToggleEditorMode() {
        if (State == GameState::Game) {
            // Activate editor mode
            Editor::History.Undo();
            State = GameState::Editor;
            Render::Camera = EditorCameraSnapshot;
            Input::SetMouselook(false);
            Sound::Reset();
            LerpAmount = 1;
        }
        else if (State == GameState::Editor) {
            if (!Level.Objects.empty() && Level.Objects[0].Type == ObjectType::Player) {
                Editor::InitObject(Level, Level.Objects[0], ObjectType::Player);
            }
            else {
                SPDLOG_ERROR("No player start at object 0!");
                return; // no player start!
            }

            // Activate game mode
            Editor::History.SnapshotLevel("Playtest");

            State = GameState::Game;

            for (auto& obj : Level.Objects) {
                obj.LastPosition = obj.Position;
                obj.LastRotation = obj.Rotation;

                if (obj.Type == ObjectType::Robot) {
                    auto& ri = Resources::GetRobotInfo(obj.ID);
                    obj.HitPoints = ri.HitPoints;
                }
            }

            Sound::Reset();
            MarkAmbientSegments(SoundFlag::AmbientLava, TextureFlag::Volatile);
            MarkAmbientSegments(SoundFlag::AmbientWater, TextureFlag::Water);
            AddSoundSources();

            EditorCameraSnapshot = Render::Camera;
            Settings::Editor.RenderMode = RenderMode::Shaded;
            Input::SetMouselook(true);
            Render::LoadHUDTextures();

            string customHudTextures[] = {
                "cockpit-ctr",
                "cockpit-left",
                "cockpit-right",
                "gauge01b#0",
                "gauge01b#1",
                "gauge01b#2",
                "gauge01b#3",
                "gauge01b#4",
                "gauge01b#5",
                "gauge01b#6",
                "gauge01b#7",
                "gauge01b#8",
                "gauge02b",
                "gauge03b",
                //"gauge16b", // lock
                "Hilite",
                "SmHilite"
            };

            Render::Materials->LoadTextures(customHudTextures);
            Player.GiveWeapon(PrimaryWeaponIndex::Laser);
            Player.GiveWeapon(PrimaryWeaponIndex::Vulcan);
            Player.GiveWeapon(PrimaryWeaponIndex::Spreadfire);
            Player.GiveWeapon(PrimaryWeaponIndex::Helix);
            Player.GiveWeapon(PrimaryWeaponIndex::Fusion);
            Player.GiveWeapon(SecondaryWeaponIndex::Concussion);
            uint16 VULCAN_AMMO_MAX = Level.IsDescent1() ? 10000 : 20000;
            Player.PrimaryWeapons = 0xffff;
            Player.SecondaryWeapons = 0xffff;
            std::ranges::generate(Player.SecondaryAmmo, [] { return 20; });
            std::ranges::generate(Player.PrimaryAmmo, [VULCAN_AMMO_MAX] { return VULCAN_AMMO_MAX; });

            //TexID weaponTextures[] = {
            //    TexID(30), TexID(11), TexID(
            //};
        }
    }

    bool PlayerCanOpenDoor(const Wall& wall) {
        if (wall.Type != WallType::Door) return false;

        if (wall.HasFlag(WallFlag::DoorLocked)) return false;

        if (bool(wall.Keys & WallKey::Red) && !Player.HasPowerup(PowerupFlag::RedKey))
            return false;

        if (bool(wall.Keys & WallKey::Blue) && !Player.HasPowerup(PowerupFlag::BlueKey))
            return false;

        if (bool(wall.Keys & WallKey::Gold) && !Player.HasPowerup(PowerupFlag::GoldKey))
            return false;

        return true;
    }
}
