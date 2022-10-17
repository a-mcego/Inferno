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

    Vector3 GetGunpointOffset(const Object& obj, int gun) {
        //Vector3 offset = Vector3::Zero;
        gun = std::clamp(gun, 0, 8);

        if (obj.Type == ObjectType::Robot) {
            auto& robot = Resources::GetRobotInfo(obj.ID);
            return robot.GunPoints[gun] * Vector3(1, 1, -1);
        }
        else if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Coop) {
            return Resources::GameData.PlayerShip.GunPoints[gun] * Vector3(1, 1, -1);;
            //offset = Resources::GameData.PlayerShip.GunPoints[gun] * Vector3(1, 1, -1);
        }
        else if (obj.Type == ObjectType::Reactor) {
            if (!Seq::inRange(Resources::GameData.Reactors, obj.ID)) return Vector3::Zero;
            auto& reactor = Resources::GameData.Reactors[obj.ID];
            return reactor.GunPoints[gun];
            //if (!Seq::inRange(reactor.GunPoints, gun));
        }

        return Vector3::Zero;
    }

    void FireWeapon(ObjID objId, int gun, WeaponID id, bool showFlash, const Vector2& spread) {
        auto& level = Level;
        auto& obj = level.Objects[(int)objId];
        auto gunOffset = GetGunpointOffset(obj, gun);
        auto point = Vector3::Transform(gunOffset, obj.GetTransform());
        auto& weapon = Resources::GameData.Weapons[(int)id];

        Object bullet{};
        bullet.Position = bullet.LastPosition = point;
        bullet.Rotation = bullet.LastRotation = obj.Rotation;
        auto direction = obj.Rotation.Forward();

        if (spread != Vector2::Zero) {
            direction += obj.Rotation.Right() * spread.x;
            direction += obj.Rotation.Up() * spread.y;
        }

        bullet.Movement.Type = MovementType::Physics;
        bullet.Movement.Physics.Velocity = direction * weapon.Speed[Game::Difficulty];
        bullet.Movement.Physics.Flags = weapon.Bounce > 0 ? PhysicsFlag::Bounce : PhysicsFlag::None;
        bullet.Movement.Physics.Drag = weapon.Drag;
        bullet.Movement.Physics.Mass = weapon.Mass;

        if (weapon.RenderType == WeaponRenderType::Blob) {
            bullet.Render.Type = RenderType::Laser; // Blobs overload the laser render path
            bullet.Radius = weapon.BlobSize;
            Render::LoadTextureDynamic(weapon.BlobBitmap);
        }
        else if (weapon.RenderType == WeaponRenderType::VClip) {
            bullet.Render.Type = RenderType::WeaponVClip;
            bullet.Render.VClip.ID = weapon.WeaponVClip;
            bullet.Radius = weapon.BlobSize;
            Render::LoadTextureDynamic(weapon.WeaponVClip);
        }
        else if (weapon.RenderType == WeaponRenderType::Model) {
            bullet.Render.Type = RenderType::Model;
            bullet.Render.Model.ID = weapon.Model;
            auto& model = Resources::GetModel(weapon.Model);
            bullet.Radius = model.Radius / weapon.ModelSizeRatio;
            //auto length = model.Radius * 2;
            Render::LoadModelDynamic(weapon.Model);
            Render::LoadModelDynamic(weapon.ModelInner);
        }

        bullet.Render.Rotation = Random() * DirectX::XM_2PI;

        bullet.Lifespan = weapon.Lifetime;
        //bullet.Lifespan = 3; // for testing fade-out
        bullet.Type = ObjectType::Weapon;
        bullet.ID = (int8)id;
        bullet.Parent = ObjID(0);

        if (id == WeaponID::Laser5)
            bullet.Render.Emissive = { 0.8f, 0.4f, 0.1f };
        else
            bullet.Render.Emissive = { 0.1f, 0.1f, 0.1f };

        //auto pitch = -Random() * 0.2f;
        //Sound::Sound3D sound(point, obj.Segment);

        if (showFlash) {
            Sound3D sound(ObjID(0));
            sound.Resource = Resources::GetSoundResource(weapon.FlashSound);
            sound.Volume = 0.55f;
            sound.AttachToSource = true;
            sound.AttachOffset = gunOffset;
            sound.FromPlayer = true;
            Sound::Play(sound);

            Render::Particle p{};
            p.Clip = weapon.FlashVClip;
            p.Position = point;
            p.Radius = weapon.FlashSize;
            p.Parent = ObjID(0);
            p.ParentOffset = gunOffset;
            p.FadeTime = 0.175f;
            Render::AddParticle(p);
        }

        for (auto& o : level.Objects) {
            if (o.Lifespan <= 0) {
                o = bullet;
                return; // found a dead object to reuse!
            }
        }

        level.Objects.push_back(bullet); // insert a new object
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

                    powerup.Movement.Type = MovementType::Physics;
                    powerup.Movement.Physics.Velocity = RandomVector(32);
                    powerup.Movement.Physics.Mass = 1;
                    powerup.Movement.Physics.Drag = 0.01f;
                    powerup.Movement.Physics.Flags = PhysicsFlag::Bounce;

                    // game originally times-out conc missiles, shields and energy after about 3 minutes
                    PendingNewObjects.push_back(powerup);
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

    void DestroyObject(Object& obj) {
        if (obj.Lifespan < 0) return; // already dead

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
                    debris.Velocity += obj.Movement.Physics.Velocity;
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

            default:
                // VCLIP_SMALL_EXPLOSION = 2
                break;
        }


        obj.Lifespan = -1;
    }

    // Updates on each game tick
    void FixedUpdate(float dt) {
        Player.Update(dt);

        // must check held keys inside of fixed updates so events aren't missed
        if ((Game::State == GameState::Editor && Input::IsKeyDown(Keys::Enter)) ||
            (Game::State != GameState::Editor && Input::Mouse.leftButton == Input::MouseState::HELD)) {
            if (Player.PrimaryState == FireState::None)
                Player.PrimaryState = FireState::Press;
            else if (Player.PrimaryState == FireState::Press)
                Player.PrimaryState = FireState::Hold;
        }
        else {
            if (Player.PrimaryState == FireState::Release)
                Player.PrimaryState = FireState::None;
            else if (Player.PrimaryState != FireState::None)
                Player.PrimaryState = FireState::Release;
        }

        if ((Game::State != GameState::Editor && Input::Mouse.rightButton == Input::MouseState::HELD)) {
            if (Player.SecondaryState == FireState::None)
                Player.SecondaryState = FireState::Press;
            else if (Player.SecondaryState == FireState::Press)
                Player.SecondaryState = FireState::Hold;
        }
        else {
            if (Player.SecondaryState == FireState::Release)
                Player.SecondaryState = FireState::None;
            else if (Player.SecondaryState != FireState::None)
                Player.SecondaryState = FireState::Release;
        }

        UpdateAmbientSounds();
        Render::UpdateDebris(dt);

        for (auto& obj : Level.Objects) {
            if (obj.HitPoints < 0) DestroyObject(obj);
        }

        for (auto& obj : PendingNewObjects) {
            obj.LastPosition = obj.Position;
            obj.LastRotation = obj.Rotation;
            Level.Objects.push_back(obj); // todo: search for dead object instead
        }

        PendingNewObjects.clear();
    }

    // Returns the lerp amount for the current tick
    float GameTick(float dt) {
        static double accumulator = 0;
        static double t = 0;

        accumulator += dt;
        accumulator = std::min(accumulator, 2.0);

        if (!Level.Objects.empty()) {
            auto& physics = Level.Objects[0].Movement.Physics;
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
            Render::UpdateExplosions(TICK_RATE);
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
}
