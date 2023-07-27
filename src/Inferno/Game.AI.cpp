#include "pch.h"

#include "Types.h"
#include "Game.AI.h"
#include "Game.h"
#include "Resources.h"
#include "Physics.h"
#include "logging.h"
#include "Physics.Math.h"
#include "Editor/Editor.Selection.h"
#include "Graphics/Render.Debug.h"

namespace Inferno {
    const RobotDifficultyInfo& Difficulty(const RobotInfo& info) {
        return info.Difficulty[Game::Difficulty];
    }

    Tuple<Vector3, float> GetDirectionAndDistance(const Vector3& target, const Vector3& point) {
        auto dir = target - point;
        float length = dir.Length();
        dir.Normalize();
        return { dir, length };
    }

    bool CanSeePlayer(const Object& obj, const Vector3& playerDir, float playerDist) {
        if (Game::Player.HasPowerup(PowerupFlag::Cloak)) return false; // Can't see cloaked player

        LevelHit hit{};
        Ray ray = { obj.Position, playerDir };
        return !IntersectRayLevel(Game::Level, ray, obj.Segment, playerDist, true, false, hit);
    }

    bool CheckPlayerVisibility(Object& obj, ObjID id, const RobotInfo& robot) {
        auto& player = Game::Level.Objects[0];
        auto [playerDir, dist] = GetDirectionAndDistance(player.Position, obj.Position);
        if (!CanSeePlayer(obj, playerDir, dist)) return false;

        //auto angle = AngleBetweenVectors(playerDir, obj.Rotation.Forward());
        auto dot = playerDir.Dot(obj.Rotation.Forward());
        //dot = vm_vec_dot(vec_to_player, &objp->orient.fvec);

        auto& diff = robot.Difficulty[Game::Difficulty];
        if (dot < diff.FieldOfView) return false;

        auto prevAwareness = obj.Control.AI.ail.Awareness;
        obj.Control.AI.ail.Awareness = 1;

        // only play sound when robot was asleep
        if (prevAwareness < 0.3f) {
            Sound3D sound(id);
            sound.AttachToSource = true;
            sound.Resource = Resources::GetSoundResource(robot.SeeSound);
            Sound::Play(sound);
        }

        return true;
    }

    bool SegmentIsAdjacent(const Segment& src, SegID adjacent) {
        for (auto& conn : src.Connections) {
            if (conn == adjacent) return true;
        }
        return false;
    }

    //SegID GetGoalPathRoom(const AIRuntime& ai) {
    //    if (ai.GoalPath.empty()) return SegID::None;
    //    return *ai.GoalPath.end();
    //}

    void PerformDodge(Object& obj, Vector2 direction) { }

    bool PathIsValid(Object& obj) {
        auto& ai = obj.Control.AI.ail;

        if (obj.GoalPath.empty()) return false;
        if (obj.GoalPath.back() != ai.GoalSegment) return false; // Goal isn't this path anymore
        return Seq::contains(obj.GoalPath, obj.Segment); // Check if robot strayed from path
    }

    SegID GetNextPathSegment(span<SegID> path, SegID current) {
        for (int i = 0; i < path.size(); i++) {
            if (path[i] == current) {
                if (i + 1 >= path.size()) break; // already at end
                return path[i + 1];
            }
        }

        return SegID::None;
    }

    //Array<SegID, 3> GetNextPathSegments(SegID start, span<SegID> path) {
    //    Array<SegID, 3> result = { SegID::None, SegID::None, SegID::None };
    //    if (path.size() < 3) return result;

    //    for (int i = 0; i < path.size() - 2; i++) {
    //        if (path[i] == start) {
    //            result[0] = start;
    //            result[1] = path[i + 1];
    //            result[2] = path[i + 2];
    //            break;
    //        }
    //    }

    //    return result;
    //}

    // Similar to TurnTowardsVector but adds angular thrust
    void RotateTowards(Object& obj, Vector3 point, float angularThrust) {
        auto dir = point - obj.Position;
        dir.Normalize();

        // transform towards to local coordinates
        Matrix basis(obj.Rotation);
        basis = basis.Invert();
        dir = Vector3::Transform(dir, basis); // transform towards to basis of object
        dir.z *= -1; // hack: correct for LH object matrix

        auto rotation = Quaternion::FromToRotation(Vector3::UnitZ, dir); // rotation to the target vector
        auto euler = rotation.ToEuler() * angularThrust;
        euler.z = 0; // remove roll
        //obj.Physics.AngularVelocity = euler;
        obj.Physics.AngularThrust += euler;
    }

    void MoveTowardsPoint(Object& obj, const Vector3& point, float thrust) {
        auto dir = point - obj.Position;
        dir.Normalize();

        //auto& robot = Resources::GetRobotInfo(obj.ID);
        //RotateTowards(obj, dir, Difficulty(robot).TurnTime);
        //obj.Physics.Velocity += dir * Difficulty(robot).MaxSpeed * 2 * dt;
        // v = t / m
        // max thrust = v * m -> 70
        obj.Physics.Thrust += dir * thrust;
    }

    void ClampThrust(Object& obj) {
        auto& robot = Resources::GetRobotInfo(obj.ID);

        auto maxSpeed = Difficulty(robot).MaxSpeed / 8;
        Vector3 maxThrust(maxSpeed, maxSpeed, maxSpeed);
        obj.Physics.Thrust.Clamp(-maxThrust, maxThrust);

        auto maxAngle = 1 / Difficulty(robot).TurnTime;
        Vector3 maxAngVel(maxAngle, maxAngle, maxAngle);
        obj.Physics.AngularThrust.Clamp(-maxAngVel, maxAngVel);
    }

    Tag GetNextConnection(span<SegID> _path, Level& level, SegID segId) {
        if (segId == SegID::None) return {};

        for (int i = 0; i < _path.size() - 1; i++) {
            if (_path[i] == segId) {
                auto& seg = level.GetSegment(segId);

                // Find the connection to the next segment in the path
                for (auto& sideId : SideIDs) {
                    auto connId = seg.GetConnection(sideId);
                    if (connId == _path[i + 1]) {
                        return { segId, sideId };
                    }
                }
            }
        }

        return {};
    }

    class SegmentPath {
        List<SegID> _path;

    public:
        Tag GetNextConnection(Level& level, SegID segId) const {
            for (int i = 0; i < _path.size() - 1; i++) {
                if (_path[i] == segId) {
                    auto& seg = level.GetSegment(segId);

                    // Find the connection to the next segment in the path
                    for (auto& sideId : SideIDs) {
                        auto connId = seg.GetConnection(sideId);
                        if (connId == _path[i + 1]) {
                            return { connId, sideId };
                        }
                    }
                }
            }

            return {};
        }

        SegID GetNextPathSegment(SegID current) const {
            for (int i = 0; i < _path.size(); i++) {
                if (_path[i] == current) {
                    if (i + 1 >= _path.size()) break; // already at end
                    return _path[i + 1];
                }
            }

            return current;
        }
    };

    // Returns true if the ray is within the radius of a face edge. Intended for edge avoidance.
    bool CheckLevelEdges(Level& level, const Ray& ray, span<SegID> segments, float radius) {
        for (auto& segId : segments) {
            auto seg = level.TryGetSegment(segId);
            if (!seg) continue;

            for (auto& side : SideIDs) {
                if (!seg->SideIsSolid(side, level)) continue;
                auto face = Face::FromSide(level, *seg, side);
                if (face.AverageNormal().Dot(ray.direction) > 0)
                    continue; // don't hit test faces pointing away

                auto planeNormal = face.AverageNormal();
                auto planeOrigin = face.Center();
                auto length = planeNormal.Dot(ray.position - planeOrigin) / planeNormal.Dot(-ray.direction);
                if (std::isinf(length)) continue;
                auto point = ray.position + ray.direction * length;

                //auto maybePoint = ProjectRayOntoPlane(ray, face.Center(), face.AverageNormal());
                //if (!maybePoint) continue;
                //auto& point = *maybePoint;
                auto edge = face.GetClosestEdge(point);
                auto closest = ClosestPointOnLine(face[edge], face[edge + 1], point);

                if (Vector3::Distance(closest, point) < radius) {
                    //Render::Debug::DrawPoint(point, Color(1, 0, 0));
                    //Render::Debug::DrawPoint(closest, Color(1, 0, 0));
                    //Render::Debug::DrawLine(closest, point, Color(1, 0, 0));
                    return true;
                }
            }
        }

        return false;
    }

    // Returns the tag of the 'parallel' side of the adjacent side to an edge
    Tag GetConnectedAdjacentSide(Level& level, Tag tag, int edge) {
        if (!level.SegmentExists(tag)) return {};
        auto& seg = level.GetSegment(tag);
        auto indices = seg.GetVertexIndicesRef(tag.Side);
        PointID edgeIndices[] = { *indices[edge], *indices[(edge + 1) % 4] };

        auto adjacent = GetAdjacentSide(tag.Side, edge);
        auto connSide = level.GetConnectedSide({ tag.Segment, adjacent });
        if (!connSide) return {};
        auto& connSeg = level.GetSegment(connSide.Segment);

        for (auto& sideId : SideIDs) {
            auto otherIndices = connSeg.GetVertexIndicesRef(sideId);
            int matches = 0;

            for (auto& i : edgeIndices) {
                for (auto& other : otherIndices) {
                    if (i == *other) matches++;
                }
            }

            if (matches == 2)
                return { connSide.Segment, sideId };
        }

        return {};
    }


    void AvoidSideEdges(Level& level, const Ray& ray, Segment& seg, SideID sideId, Object& obj, float thrust, Vector3& target) {
        if(!seg.SideIsSolid(sideId, level)) return;

        // project ray onto side
        auto& side = seg.GetSide(sideId);
        if (side.AverageNormal.Dot(ray.direction) >= 0) return; // ignore sides pointing away
        auto offset = side.AverageNormal * obj.Radius;
        offset = Vector3::Zero;
        auto point = ProjectRayOntoPlane(ray, side.Center + offset, side.AverageNormal);
        if (!point) return;
        auto dist = Vector3::Distance(*point, obj.Position);
        if (dist > 20) return;

        auto pointDir = *point - obj.Position;
        pointDir.Normalize();
        if(pointDir.Dot(ray.direction) <= 0) 
            return; // facing away (why did the above not catch it?)

        auto face = Face::FromSide(level, seg, sideId);

        // check point vs each edge
        for (int edge = 0; edge < 4; edge++) {
            // check if edge's adjacent side is closed
            //auto adjacent = GetAdjacentSide(tag.Side, edge);
            //if (!seg.SideIsSolid(adjacent, level)) continue;
            //auto adjacentFace = Face::FromSide(level, seg, adjacent);


            // check distance
            auto edgePoint = ClosestPointOnLine(face[edge] + offset, face[edge + 1] + offset, *point);
            if (Vector3::Distance(edgePoint, *point) < obj.Radius) {
                //auto edges = Editor::FindSharedEdges(level, tag, { tag.Segment, adjacent });
                //auto vec = face.GetEdgeMidpoint(edge) - face.Center();
                auto adjacent = GetAdjacentSide(sideId, edge);
                Vector3 vec;
                auto edgeMidpoint = face.GetEdgeMidpoint(edge);

                if (!seg.SideIsSolid(adjacent, level)) {
                    // if adjacent side isn't solid, shift goal point forward into next segment
                    auto adjacentFace = Face::FromSide(level, seg, adjacent);
                    vec = adjacentFace.Center() - face.Center();
                } else {
                    vec = edgeMidpoint - face.Center();
                }

                vec.Normalize();

                //auto vec2 = face.Center() + face.AverageNormal()
                //vec += face.Center() - adjacentFace.Center();
                //vec.Normalize();
                //auto target = adjacentFace.Center() + vec * 2;
                target += edgeMidpoint + vec * 25;
                target /= 2;
                //auto target = edgeMidpoint + vec * 20;
                Render::Debug::DrawLine(edgeMidpoint + vec * 20, edgeMidpoint, Color(1, 0, 1));
                Render::Debug::DrawPoint(target, Color(1, 0, 1));
                Render::Debug::DrawPoint(side.Center, Color(1, 0, 1));
                //MoveTowardsPoint(obj, target, thrust);

                // avoid this edge
                return;
            }
        }
    }

    //bool AvoidConnectionEdges(Level& level, const Ray& ray, Tag tag, Object& obj, float thrust) {
    //    auto& seg = level.GetSegment(tag);
    //    // project ray onto side
    //    auto& side = seg.GetSide(tag.Side);
    //    auto point = ProjectRayOntoPlane(ray, side.Center, side.AverageNormal);
    //    if (!point) return false;
    //    auto face = Face::FromSide(level, seg, tag.Side);

    //    // check point vs each edge
    //    for (int edge = 0; edge < 4; edge++) {
    //        // check if edge's adjacent side is closed
    //        // todo: this can fail if there is a connection with a solid adjacent side
    //        auto adjacent = GetAdjacentSide(tag.Side, edge);
    //        if (!seg.SideIsSolid(adjacent, level)) continue;
    //        auto adjacentFace = Face::FromSide(level, seg, adjacent);
    //        

    //        // check distance
    //        auto edgePoint = ClosestPointOnLine(face[edge], face[edge + 1], *point);
    //        if (Vector3::Distance(edgePoint, *point) < obj.Radius * 1.5f) {
    //            //auto edges = Editor::FindSharedEdges(level, tag, { tag.Segment, adjacent });
    //            auto vec = face.GetEdgeMidpoint(edge) - adjacentFace.Center();
    //            //auto vec2 = face.Center() + face.AverageNormal()
    //            vec += face.Center() - adjacentFace.Center();
    //            //vec.Normalize();
    //            auto target = adjacentFace.Center() + vec * 2;
    //            Render::Debug::DrawLine(target, adjacentFace.Center(), Color(1, 0, 1));
    //            MoveTowardsPoint(obj, target, thrust);

    //            // avoid this edge
    //            return true;
    //        }
    //    }

    //    return false;
    //}

    //void AvoidConnectionEdges(Level& level, const Ray& ray, int length, Object& obj, float thrust) {
    //    auto index = Seq::indexOf(obj.GoalPath, obj.Segment);
    //    if (!index) return;

    //    auto startConn = GetNextConnection(obj.GoalPath, level, obj.Segment);
    //    auto& startSide = level.GetSide(startConn);

    //    //auto room = Game::Rooms.GetRoom(obj.Room);
    //    //for (auto& segId : room) { }

    //    for (int i = 0; i < length; i++) {
    //        if (i >= obj.GoalPath.size()) return;

    //        auto conn = GetNextConnection(obj.GoalPath, level, obj.GoalPath[*index + i]);

    //        bool avoided = AvoidConnectionEdges(level, ray, conn, obj, thrust);

    //        if (!avoided) {
    //            // check the adjacent side connections as well
    //            for (int edge = 0; edge < 4; edge++) {
    //                if (auto adj = GetConnectedAdjacentSide(level, conn, edge)) {
    //                    if (AvoidConnectionEdges(level, ray, adj, obj, thrust)) {
    //                        avoided = true;
    //                        break;
    //                    }
    //                }
    //            }
    //        }

    //        //if (avoided) {
    //        //    Render::Debug::DrawLine(obj.Position, startSide.Center, Color(1, 0, 1));
    //        //    MoveTowardsPoint(obj, startSide.Center, thrust);
    //        //    break;
    //        //}
    //    }
    //}

    void AvoidRoomEdges(Level& level, const Ray& ray, Object& obj, float thrust, Vector3& target) {
        auto room = Game::Rooms.GetRoom(obj.Room);
        if(!room) return;

        for (auto& segId : room->Segments) {
            auto& seg = level.GetSegment(segId);

            for (auto& sideId : SideIDs) {
                AvoidSideEdges(level, ray, seg, sideId, obj, thrust, target);
            }
        }
    }

    void PathTowardsGoal(Level& level, Object& obj, float /*dt*/) {
        auto& ai = obj.Control.AI.ail;
        //auto& seg = level.GetSegment(obj.Segment);

        auto checkGoalReached = [&obj, &ai] {
            if (Vector3::Distance(obj.Position, ai.GoalPosition) <= std::max(obj.Radius, 5.0f)) {
                SPDLOG_INFO("Robot {} reached the goal!", obj.Signature);
                ai.GoalSegment = SegID::None; // Reached the goal!
            }
        };

        if (!PathIsValid(obj)) {
        //if(obj.GoalPath.empty()) {
            // Calculate a new path
            SPDLOG_INFO("Robot {} updating goal path", obj.Signature);
            obj.GoalPath = Game::Navigation.NavigateTo(obj.Segment, ai.GoalSegment, Game::Rooms, level);
            if (obj.GoalPath.empty()) {
                // Unable to find a valid path, clear the goal and give up
                ai.GoalSegment = SegID::None;
                ai.GoalRoom = RoomID::None;
                return;
            }
        }

        auto& robot = Resources::GetRobotInfo(obj.ID);
        auto thrust = Difficulty(robot).MaxSpeed / 8;
        auto angThrust = 1 / Difficulty(robot).TurnTime / 8;

        if (ai.GoalSegment == obj.Segment) {
            // Reached the goal segment
            MoveTowardsPoint(obj, ai.GoalPosition, thrust);
            RotateTowards(obj, ai.GoalPosition, angThrust);
            checkGoalReached();
        }
        else {
            auto getPathSeg = [&obj](size_t index) {
                //if (!Seq::inRange(obj.GoalPath, index)) return obj.GoalPath.back();
                if (!Seq::inRange(obj.GoalPath, index)) return SegID::None;
                return obj.GoalPath[index];
            };


            auto pathIndex = Seq::indexOf(obj.GoalPath, obj.Segment);
            if (!pathIndex) {
                SPDLOG_ERROR("Invalid path index for obj {}", obj.Signature);
            }

            //auto next1 =  GetNextPathSegment(obj.GoalPath, obj.Segment);
            //auto next2 = GetNextPathSegment(obj.GoalPath, next1);
            auto next1 = getPathSeg(*pathIndex + 1);
            auto next2 = getPathSeg(*pathIndex + 2);
            auto next3 = getPathSeg(*pathIndex + 3);

            SegID segs[] = { obj.Segment, next1, next2, next3 };

            auto nextSideTag = GetNextConnection(obj.GoalPath, level, obj.Segment);
            auto& nextSide = level.GetSide(nextSideTag);
            Vector3 targetPosition = nextSide.Center; // default to the next side


            int desiredIndex = 0;

            Vector3 desiredPosition;
            for (int i = (int)std::size(segs) - 1; i > 0; i--) {
                if (auto nextSeg = level.TryGetSegment(segs[i])) {
                    desiredIndex = i;
                    desiredPosition = nextSeg->Center;
                    break;
                }
            }

            auto findVisibleTarget = [&] {
                auto [dir, maxDist] = GetDirectionAndDistance(desiredPosition, obj.Position);
                Ray ray(obj.Position, dir);

                // Try pathing directly across multiple segments
                for (int i = 0; i < std::size(segs); i++) {
                    if (auto nextSeg = level.TryGetSegment(segs[i])) {
                        if (i == 0) {
                            // check the surrounding segments of the start location
                            for (auto& conn : nextSeg->Connections) {
                                if (IntersectRaySegment(level, ray, conn, maxDist, false, false, nullptr)) {
                                    //Render::Debug::DrawLine(obj.Position, desiredPosition, Color(1, 0, 0));
                                    //return; // wall in the way, don't try going any further

                                    // try a shorter path
                                    while (desiredIndex > 1) {
                                        desiredIndex--;
                                        if (!IntersectRaySegment(level, ray, segs[desiredIndex], maxDist, false, false, nullptr)) {
                                            nextSeg = level.TryGetSegment(segs[desiredIndex]);
                                            targetPosition = nextSeg->Center;
                                            return;
                                        }
                                    }

                                    if (desiredIndex == 0)
                                        return; // wall in the way, don't try going any further
                                }
                            }
                        }

                        if (IntersectRaySegment(level, ray, segs[i], maxDist, false, false, nullptr)) {
                            //Render::Debug::DrawLine(obj.Position, desiredPosition, Color(1, 0, 0));
                            break; // wall in the way, don't try going any further
                        }

                        if (i > 0)
                            targetPosition = nextSeg->Center;
                    }
                }
            };

            findVisibleTarget();


            //if (next2 == SegID::None) {
            //    // Target segment is adjacent, try pathing directly to it.
            //    auto [dir, maxDist] = GetDirectionAndDistance(ai.GoalPosition, obj.Position);
            //    Ray ray(obj.Position, dir);
            //    if (!IntersectRaySegments(level, ray, segs, maxDist, false, false, nullptr))
            //        targetPosition = ai.GoalPosition;

            //    checkGoalReached(); // it's possible for the final seg to be small, so check completion if we're adjacent to it
            //}

            //auto [dir, maxDist] = GetDirectionAndDistance(side2Center, obj.Position);
            //Ray ray(obj.Position, dir);
            //if (!IntersectRaySegments(level, ray, segs, maxDist, false, false, nullptr))
            //    targetPosition = side2Center;

            //if (next2 == SegID::None) {
            //    // Target segment is adjacent, try pathing directly to it.
            //    auto [dir, maxDist] = GetDirectionAndDistance(ai.GoalPosition, obj.Position);
            //    Ray ray(obj.Position, dir);
            //    if (!IntersectRaySegments(level, ray, segs, maxDist, false, false, nullptr))
            //        targetPosition = ai.GoalPosition;

            //    checkGoalReached(); // it's possible for the final seg to be small, so check completion if we're adjacent to it
            //}
            //else {
            //    // Try pathing directly across multiple segments
            //    if (auto nextSideTag2 = GetNextConnection(obj.GoalPath, level, next1)) {
            //        // nothing between current seg and target so use a direct path
            //        auto side2Center = level.GetSide(nextSideTag2).Center;

            //        auto [dir, maxDist] = GetDirectionAndDistance(side2Center, obj.Position);
            //        Ray ray(obj.Position, dir);
            //        if (!IntersectRaySegments(level, ray, segs, maxDist, false, false, nullptr))
            //            targetPosition = side2Center;
            //    }
            //}

            // Check for edge collisions and dodge
            //auto [dir, maxDist] = GetDirectionAndDistance(targetPosition, obj.Position);
            //Ray ray(obj.Position, dir);
            //AvoidConnectionEdges(level, ray, desiredIndex, obj, thrust);
            //Render::Debug::DrawLine(ray.position, ray.position + ray.direction * 20, Color(1, .5f, 0));
            //AvoidRoomEdges(level, ray, obj, thrust, targetPosition);

            Render::Debug::DrawLine(obj.Position, targetPosition, Color(0, 1, 0));

            //auto& seg1 = Game::Level.GetSegment(next1);
            //auto& seg2 = Game::Level.GetSegment(next2);
            targetPosition = (targetPosition * 2 + nextSide.Center) /3;
            MoveTowardsPoint(obj, targetPosition, thrust);
            RotateTowards(obj, targetPosition, angThrust);


            //if (CheckLevelEdges(level, ray, segs, obj.Radius)) {
            //    // MoveTowardsPoint(obj, nextSide.Center, thrust);
            //    Render::Debug::DrawLine(obj.Position, nextSide.Center, Color(1, 0, 0));
            //}
        }
    }

    //void PathTowardsGoal(Level& level, Object& obj, float /*dt*/) {
    //    if (obj.GoalPath.empty()) return;
    //    if (!Seq::inRange(obj.GoalPath, obj.GoalPathIndex)) return;
    //    //auto& ai = obj.Control.AI.ail;

    //    auto& robot = Resources::GetRobotInfo(obj.ID);
    //    auto thrust = Difficulty(robot).MaxSpeed / 8;
    //    auto angularThrust = 1 / Difficulty(robot).TurnTime / 8;
    //    const auto& nextPoint = obj.GoalPath[obj.GoalPathIndex];

    //    MoveTowardsPoint(obj, nextPoint, thrust);
    //    RotateTowards(obj, nextPoint, angularThrust);

    //    if (Vector3::DistanceSquared(obj.Position, nextPoint) < 5 * 5.0f) {
    //        // got close to node, move to next
    //        obj.GoalPathIndex++;
    //    }

    //    if (obj.GoalPathIndex >= obj.GoalPath.size()) {
    //        // reached the end
    //        obj.GoalPath.clear();
    //        obj.GoalPathIndex = -1;
    //        SPDLOG_INFO("Robot {} reached the goal!", obj.Signature);
    //    }
    //}

    struct AiExtended {
        float AwarenessDecay = 0.2f; // Awareness decay per second
        float Fear = 0.2f; // Taking damage increases flee state
        float Curiosity = 0.2f; // How much awareness from noise / likeliness to investigate
    };

    AiExtended DefaultAi{};

    void UpdateAI(Object& obj, float dt) {
        if (obj.NextThinkTime == NEVER_THINK || obj.NextThinkTime > Game::Time)
            return;

        // todo: check if robot is in active set of segments (use rooms)

        if (obj.Type == ObjectType::Robot) {
            auto id = ObjID(&obj - Game::Level.Objects.data());

            auto& robot = Resources::GetRobotInfo(obj.ID);
            auto& ai = obj.Control.AI.ail;
            auto& player = Game::Level.Objects[0];

            // Reset thrust accumulation
            obj.Physics.Thrust = Vector3::Zero;
            obj.Physics.AngularThrust = Vector3::Zero;

            if (ai.GoalSegment != SegID::None) {
                // goal path takes priority over other behaviors
                PathTowardsGoal(Game::Level, obj, dt);
            }
            else if (ai.Awareness > 0.5f) {
                // in combat?
                auto [playerDir, dist] = GetDirectionAndDistance(player.Position, obj.Position);
                if (CanSeePlayer(obj, playerDir, dist)) {
                    TurnTowardsVector(obj, playerDir, Difficulty(robot).TurnTime);
                    ai.AimTarget = player.Position;
                }
                else {
                    // Lost sight of player, decay awareness based on AI
                    auto deltaTime = float(Game::Time - ai.LastUpdate);
                    ai.Awareness -= DefaultAi.AwarenessDecay * deltaTime;
                    if (ai.Awareness < 0) ai.Awareness = 0;

                    // todo: move towards last known location
                }

                obj.NextThinkTime = Game::Time + Game::TICK_RATE;
            }
            else {
                //if (CheckPlayerVisibility(obj, id, robot)) {
                //    obj.NextThinkTime = Game::Time + Game::TICK_RATE;
                //}
                //else {
                //    // Nothing nearby
                //    obj.NextThinkTime = Game::Time + 1.0f;
                //}
                obj.NextThinkTime = Game::Time + 1.0f;
            }

            ClampThrust(obj);
            ai.LastUpdate = Game::Time;
        }
        else if (obj.Type == ObjectType::Reactor) {
            // check facing, fire weapon from gunpoint
        }
    }
}
