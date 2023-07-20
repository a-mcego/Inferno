#pragma once

// Functionality related to loading extended light data such as default colors
// and point lights

namespace Inferno {
    // This must match the light shaders
    enum class LightType : uint32 {
        Point = 0,
        Tube = 1,
        Rectangle = 2,
        Spot = 3
    };

    enum class LightWrapMode {
        None = 0,
        U = 1,
        V = 2,
        EdgeU = 3,
        EdgeV = 4 // Wraps on V, but combines the first and last light when texture wraps on U
    };

    // Defines dynamic light sources on a texture
    struct TextureLightInfo {
        LevelTexID Id = LevelTexID::None;
        LightType Type = LightType::Point;
        List<Vector2> Points = { { 0.5f, 0.5f } }; // UV positions for each light
        float Offset = 2; // light surface offset
        float Radius = 40; // light radius
        float Width = 0.25f; // U Width for rectangular lights. For wrapped lights this is aligned to the wrap direction.
        float Height = 0.25f; // V Height for rectangular lights. Unused for wrapped lights.
        Color Color = { 0, 0, 0 };
        LightWrapMode Wrap = LightWrapMode::None;

        bool IsContinuous() const {
            if (Points.size() != 2 || Type == LightType::Point) return false;
            return (Points[0].x == 0 && Points[1].x == 1) || (Points[0].y == 0 && Points[1].y == 1);
        }
    };

    // Must match MaterialInfo HLSL
    struct MaterialInfo {
        float NormalStrength = 1; // multiplier on normal map
        float SpecularStrength = 1; // multiplier on specular
        // other map generation options like contrast, brightness?
        float Metalness = 0; // How much diffuse to apply to specular
        float Roughness = 0.6; // 0 is sharp specular, 1 is no specular
        float EmissiveStrength = 0;
        float LightReceived = 1; // 0 for unlit
        int32 ID = -1; // TexID
        int32 Additive = false; // Additive blending
    };

    // Loads light info from a YAML file
    Dictionary<LevelTexID, TextureLightInfo> LoadLightTable(const string& yaml);
    // Loads material info from a YAML file
    void LoadMaterialTable(const string& yaml, span<MaterialInfo> materials);

    void SaveLightTable(std::ostream& stream, const Dictionary<LevelTexID, TextureLightInfo>& lightInfo);
    void SaveMaterialTable(std::ostream& stream, span<MaterialInfo> materials);
}
