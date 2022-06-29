#include "pch.h"
#include "Editor.Object.h"
#include "Editor.h"
#include "Graphics/Render.h"

namespace Inferno::Editor {

    void DeleteObject(Level& level, ObjID id) {
        auto pObj = level.TryGetObject(id);
        if (!pObj) return;

        Seq::removeAt(level.Objects, (int)id);
        // Shift object? are there any refs?
    }

    // If center is true the object is moved to segment center, otherwise it is moved to the selected face
    bool AlignObjectToSide(Level& level, ObjID id, PointTag tag, bool center) {
        auto obj = level.TryGetObject(id);
        auto seg = level.TryGetSegment(tag.Segment);
        if (!obj || !seg) return false;

        auto face = Face::FromSide(level, *seg, tag.Side);
        auto edge = face.VectorForEdge(tag.Point);
        auto normal = face.AverageNormal();

        Matrix transform;
        transform.Up(normal);
        transform.Forward(edge.Cross(normal));
        transform.Right(edge);
        if (center)
            transform.Translation(seg->Center);
        else
            transform.Translation(face.Center() + normal * obj->Radius); // position on face

        obj->Segment = tag.Segment;
        obj->Transform = transform;
        Editor::Gizmo.UpdatePosition();
        return true;
    }

    bool MoveObjectToSegment(Level& level, ObjID id, SegID segId) {
        auto obj = level.TryGetObject(id);
        auto seg = level.TryGetSegment(segId);
        if (!obj || !seg) return false;

        obj->Segment = segId;
        obj->Transform.Translation(seg->Center);
        Editor::Gizmo.UpdatePosition();
        return true;
    }

    int GetObjectCount(Level& level, ObjectType type) {
        int i = 0;
        for (auto& obj : level.Objects)
            if (obj.Type == type) i++;

        return i;
    }

    float GetObjectRadius(const Level& level, const Object& obj) {
        constexpr float playerRadius = FixToFloat(0x46c35L);

        switch (obj.Type) {
            case ObjectType::Player:
            case ObjectType::Coop:
                return playerRadius;

            case ObjectType::Robot:
            {
                auto& ri = Resources::GetRobotInfo(obj.ID);
                return Resources::GetModel(ri.Model).Radius;
            }

            case ObjectType::Hostage:
                return 5;

            case ObjectType::Powerup:
            {
                auto& info = Resources::GameData.Powerups.at(obj.ID);
                return info.Size;
            }

            case ObjectType::Reactor:
            {
                auto& info = Resources::GameData.Reactors.at(obj.ID);
                return Resources::GetModel(info.Model).Radius;
            }

            case ObjectType::Weapon: // For placeable mines
            {
                // Only time the editor should create a weapon is if it's a mine
                if (level.IsDescent1()) return 5; // No mines in D1
                return Resources::GetModel(Models::PlaceableMine).Radius;
            }
        }

        return 5;
    }

    void InitObject(const Level& level, Object& obj, ObjectType type) {
        const ModelID playerModel = level.IsDescent1() ? Models::D1Player : Models::D2Player;
        const ModelID coopModel = level.IsDescent1() ? Models::D1Coop : Models::D2Coop;

        obj.Type = type;
        obj.ID = 0; // can only have one ID 0 player, fix it later
        obj.Movement = {};
        obj.Control = {};
        obj.Render = {};

        switch (type) {
            case ObjectType::Player:
                obj.Control.Type = obj.ID == 0 ? ControlType::None : ControlType::Slew; // Player 0 only
                obj.Movement.Type = MovementType::Physics;
                obj.Render.Type = RenderType::Polyobj;
                obj.Render.Model.ID = playerModel;
                break;

            case ObjectType::Coop:
                obj.Movement.Type = MovementType::Physics;
                obj.Render.Type = RenderType::Polyobj;
                obj.Render.Model.ID = coopModel;
                break;

            case ObjectType::Robot:
            {
                auto& ri = Resources::GetRobotInfo(0);
                obj.Control.Type = ControlType::AI;
                obj.Movement.Type = MovementType::Physics;
                obj.Render.Type = RenderType::Polyobj;
                obj.Shields = ri.Strength;
                obj.Render.Model.ID = ri.Model;
                obj.Control.AI.Behavior = AIBehavior::Normal;
                obj.Contains.Type = ObjectType::None;
                break;
            }
            case ObjectType::Hostage:
                obj.Control.Type = ControlType::Powerup;
                obj.Render.Type = RenderType::Hostage;
                obj.Render.VClip.ID = VClipID(33);
                break;

            case ObjectType::Powerup:
            {
                obj.Control.Type = ControlType::Powerup;
                obj.Render.Type = RenderType::Powerup;
                auto& info = Resources::GameData.Powerups.at(0);
                obj.Render.VClip.ID = info.VClip;
                break;
            }

            case ObjectType::Reactor:
            {
                obj.Control.Type = ControlType::Reactor;
                obj.Render.Type = RenderType::Polyobj;
                auto& info = Resources::GameData.Reactors.at(0);
                obj.Render.Model.ID = info.Model;
                obj.Shields = 200;
                break;
            }

            case ObjectType::Weapon: // For placeable mines
            {
                // Only time the editor should create a weapon is if it's a mine
                if (level.IsDescent1()) return; // No mines in D1
                obj.Control.Type = ControlType::Weapon;
                obj.Control.Weapon.Parent = ObjID::None;
                obj.Control.Weapon.ParentSig = (ObjSig)-1;
                obj.Control.Weapon.ParentType = obj.Type;

                obj.Movement.Type = MovementType::Physics;
                obj.Movement.Physics.Mass = FixToFloat(65536);
                obj.Movement.Physics.Drag = FixToFloat(2162);
                obj.Movement.Physics.AngularVelocity.y = (Random() - Random()) * 1.25f; // value between -1.25 and 1.25
                obj.Movement.Physics.Flags = PhysicsFlag::Mine;

                obj.ID = 51;
                obj.Render.Type = RenderType::Polyobj;
                obj.Render.Model.ID = Models::PlaceableMine;
                obj.Shields = 20;
            }
        }

        obj.Radius = GetObjectRadius(level, obj);

        if (obj.Render.Type == RenderType::Polyobj)
            Render::LoadModelDynamic(obj.Render.Model.ID);

        if (obj.Render.Type == RenderType::Hostage || obj.Render.Type == RenderType::Powerup)
            Render::LoadTextureDynamic(obj.Render.VClip.ID);
    }

    ObjID AddObject(Level& level, PointTag tag, Object obj) {
        if (!level.SegmentExists(tag)) return ObjID::None;

        if (level.Objects.size() + 1 >= level.Limits.Objects) {
            ShowWarningMessage(L"Out of room for objects!");
            return ObjID::None;
        }

        switch (obj.Type) {
            case ObjectType::Player:
                if (GetObjectCount(level, ObjectType::Player) >= level.Limits.Players) {
                    SetStatusMessage("Cannot add more than {} players!", level.Limits.Players);
                    return ObjID::None;
                }
                break;
            case ObjectType::Coop:
                if (GetObjectCount(level, ObjectType::Coop) >= level.Limits.Coop) {
                    SetStatusMessage("Cannot add more than {} co-op players!", level.Limits.Coop);
                    return ObjID::None;
                }
                break;
            case ObjectType::Reactor:
                if (GetObjectCount(level, ObjectType::Reactor) >= level.Limits.Reactor) {
                    SetStatusMessage("Cannot add more than {} reactor!", level.Limits.Reactor);
                    return ObjID::None;
                }
                break;
        }

        auto id = (ObjID)level.Objects.size();
        level.Objects.push_back(obj);

        Selection.SetSelection(id);
        AlignObjectToSide(level, id, tag, true);

        Events::TexturesChanged();
        return id;
    }

    ObjID AddObject(Level& level, PointTag tag, ObjectType type) {
        Object obj{};
        InitObject(level, obj, type);
        auto id = AddObject(level, tag, obj);
        Events::TexturesChanged();
        return id;
    }

    // Copies changes to the selected object to marked objects
    void Commands::ChangeMarkedObjects() {
        auto src = Game::Level.TryGetObject(Editor::Selection.Object);
        if (!src) return;

        src->Radius = GetObjectRadius(Game::Level, *src);

        for (auto& id : Editor::Marked.Objects) {
            if (id == Editor::Selection.Object) continue;
            if (auto obj = Game::Level.TryGetObject(id)) {
                auto seg = obj->Segment;
                auto xform = obj->Transform;
                *obj = *src;
                obj->Segment = seg;
                obj->Transform = xform;
            }
        }

        Editor::History.SnapshotLevel("Edit object");
    }

    // Adds an object to represent the secret exit return so it can be manipulated
    void AddSecretLevelReturnMarker(Level& level) {
        Object marker{};
        marker.Type = ObjectType::SecretExitReturn;
        marker.Render.Type = RenderType::Polyobj;
        //marker.Render.Model.ID = Resources::GameData.MarkerModel;
        marker.Render.Model.ID = Resources::GameData.PlayerShip.Model;
        marker.Render.Model.TextureOverride = LevelTexID(426);
        marker.Radius = 5;

        if (!level.SegmentExists(level.SecretExitReturn))
            level.SecretExitReturn = {};

        marker.Segment = level.SecretExitReturn;
        marker.Transform = level.SecretReturnOrientation;
        if (auto seg = level.TryGetSegment(level.SecretExitReturn))
            marker.Transform.Translation(seg->Center);

        level.Objects.push_back(marker);
        Render::LoadModelDynamic(marker.Render.Model.ID);
    }

    void RemoveSecretLevelReturnMarker(Level& level) {
        int i = 0;
        for (; i < level.Objects.size(); i++) {
            if (level.Objects[i].Type == ObjectType::SecretExitReturn) break;
        }

        DeleteObject(Game::Level, ObjID(i));
    }

    void UpdateSecretLevelReturnMarker() {
        auto& level = Game::Level;
        if (!level.IsDescent2()) return;

        if (level.HasSecretExit())
            AddSecretLevelReturnMarker(level);
        else
            RemoveSecretLevelReturnMarker(level);
    }

    namespace Commands {
        Command MoveObjectToSide{
            .SnapshotAction = [] {
                if (!AlignObjectToSide(Game::Level, Editor::Selection.Object, Editor::Selection.PointTag()))
                    return "";

                return "Move Object to Side";
            },
            .Name = "Move Object to Side"
        };


        Command MoveObjectToSegment{
            .SnapshotAction = [] {
                if (!Editor::MoveObjectToSegment(Game::Level, Editor::Selection.Object, Editor::Selection.Segment))
                    return "";

                return "Move Object to Segment";
            },
            .Name = "Move Object to Segment"
        };

        Command AddObject{
            .SnapshotAction = [] {
                if (auto obj = Game::Level.TryGetObject(Editor::Selection.Object)) {
                    Object copy = *obj;
                    auto id = Editor::AddObject(Game::Level, Selection.PointTag(), copy);
                    if (id == ObjID::None) return "";
                    Editor::Selection.Object = id;
                    return "Add Object";
                }
                else {
                    auto type = Game::Level.Objects.empty() ? ObjectType::Player : ObjectType::Robot;
                    auto id = Editor::AddObject(Game::Level, Selection.PointTag(), type);
                    if (id == ObjID::None) return "";
                    return "Add Object";
                }
            },
            .Name = "Add Object"
        };
    }
}
