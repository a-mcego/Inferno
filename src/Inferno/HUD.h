#pragma once

#include "Graphics/Render.h"
#include "Graphics/Render.Canvas.h"

namespace Inferno {

    enum class Gauges {
        Shield = 0, // 0 to 9 in decreasing strength
        Invincible = 10, // 10 to 19
        Afterburner = 20,
        BlueKey = 24,
        GoldKey = 25,
        RedKey = 26,
        //BlueKeyOff = 27,
        //GoldKeyOff = 28,
        //RedKeyOff = 29,
        Lives = 37,
        Ship = 38, // 8 Colors
        ReticleCross = 46, // 2 frames: not ready, ready
        ReticlePrimary = 48, // 3 frames: not ready, center ready, quad ready
        ReticleSecondary = 51, // 5 frames: not ready, left ready, right ready, center not ready, center ready
        HomingWarningOn = 56,
        HomingWarningOff = 57,
    };

    inline TexID GetGaugeTexID(Gauges gauge) {
        return Game::Level.IsDescent1() ? Resources::GameData.Gauges[(int)gauge] : Resources::GameData.HiResGauges[(int)gauge];
    }

    inline TexID GetWeaponTexID(Weapon& weapon) {
        return Game::Level.IsDescent1() ? weapon.Icon : weapon.HiresIcon;
    }

    constexpr float WEAPON_TEXT_Y_OFFSET = -75;
    constexpr float WEAPON_TEXT_AMMO_Y_OFFSET = WEAPON_TEXT_Y_OFFSET + 15;
    constexpr float WEAPON_BMP_Y_OFFSET = -20;
    constexpr Color MonitorGreenText = { 0, 0.7f, 0 };
    constexpr Color MonitorRedText = { 0.8f, 0, 0 };

    inline void DrawMonitorBitmap(Render::CanvasBitmapInfo& info, float shadow = 0.6f) {
        Render::HudGlowCanvas->DrawBitmap(info);

        info.Scanline = 0.0f;
        info.Color = { 0, 0, 0, shadow };
        Render::HudCanvas->DrawBitmap(info);
    }

    // Draws text with a dark background, easier to read
    inline void DrawMonitorText(string_view text, Render::DrawTextInfo& info, float shadow = 0.6f) {
        Render::HudGlowCanvas->DrawGameText(text, info);
        info.Color = { 0, 0, 0, shadow };
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawGameText(text, info);
    }

    inline void DrawReticleBitmap(const Vector2& offset, Gauges gauge, int frame, float scale) {
        TexID id = GetGaugeTexID(Gauges((int)gauge + frame));
        scale *= Render::HudCanvas->GetScale();
        auto& material = Render::Materials->Get(id);

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::CenterTop;
        info.Scanline = 0.0f;
        Render::HudCanvas->DrawBitmap(info);

        info.Scanline = 0.4f;
        Render::HudCanvas->DrawBitmap(info);
    }

    inline void DrawShipBitmap(const Vector2& offset, const Material2D& material, float sizeScale) {
        auto scale = Render::HudCanvas->GetScale();

        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale * sizeScale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorBitmap(info, 0.90f);
    }

    inline void DrawOpaqueBitmap(const Vector2& offset, AlignH align, const Material2D& material) {
        auto scale = Render::HudCanvas->GetScale();
        Inferno::Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale;
        info.Texture = material.Handles[0];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        Render::HudCanvas->DrawBitmap(info);
    }

    inline void DrawOpaqueBitmap(const Vector2& offset, AlignH align, string bitmapName) {
        auto& material = Render::Materials->GetOutrageMaterial(bitmapName);
        DrawOpaqueBitmap(offset, align, material);
    }

    inline void DrawAdditiveBitmap(const Vector2& offset, AlignH align, const Material2D& material, float sizeScale, float scanline) {
        float scale = Render::HudCanvas->GetScale();
        Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale * sizeScale;
        info.Texture = material.Handles[Material2D::Diffuse];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = scanline;
        Render::HudGlowCanvas->DrawBitmap(info);
    }

    inline void DrawAdditiveBitmap(const Vector2& offset, AlignH align, Gauges gauge, float sizeScale, float scanline = 0.4f) {
        TexID id = GetGaugeTexID(gauge);
        auto& material = Render::Materials->Get(id);
        DrawAdditiveBitmap(offset, align, material, sizeScale, scanline);
    }

    inline void DrawAdditiveBitmap(const Vector2& offset, AlignH align, string bitmapName, float sizeScale, float scanline = 0.4f) {
        auto& material = Render::Materials->GetOutrageMaterial(bitmapName);
        DrawAdditiveBitmap(offset, align, material, sizeScale, scanline);
    }

    inline void DrawWeaponBitmap(const Vector2& offset, AlignH align, TexID id, float sizeScale) {
        Render::LoadTextureDynamic(id);
        auto& ti = Resources::GetTextureInfo(id);
        float scale = Render::HudCanvas->GetScale();

        Render::CanvasBitmapInfo info;
        info.Position = offset * scale;
        info.Size = Vector2((float)ti.Width, (float)ti.Height) * scale * sizeScale;
        info.Texture = Render::Materials->Get(id).Handles[Material2D::Diffuse];
        info.HorizontalAlign = align;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.4f;
        DrawMonitorBitmap(info);
    }

    inline void DrawReticle() {
        const Vector2 crossOffset(0/*-8*/, -5);
        const Vector2 primaryOffset(0/*-30*/, 14);
        const Vector2 secondaryOffset(0/*-24*/, 2);

        float scale = Game::Level.IsDescent1() ? 2.0f : 1.0f;
        DrawReticleBitmap(crossOffset, Gauges::ReticleCross, 1, scale); // gauss, vulkan
        DrawReticleBitmap(primaryOffset, Gauges::ReticlePrimary, 2, scale);
        DrawReticleBitmap(secondaryOffset, Gauges::ReticleSecondary, 4, scale);

        //TexID id = Resources::GameData.HiResGauges[RETICLE_PRIMARY];
        //auto& ti = Resources::GetTextureInfo(id);
        //auto pos = size / 2;
        //auto ratio = size.y / BASE_RESOLUTION_Y;

        //Vector2 scaledSize = { ti.Width * ratio, ti.Height * ratio };
        //pos -= scaledSize / 2;
        //Render::Canvas->DrawBitmap(id, pos, scaledSize);
    }

    inline void DrawEnergyBar(float spacing, bool flipX) {
        constexpr float ENERGY_HEIGHT = -125;
        constexpr float ENERGY_SPACING = -9;

        auto& material = Render::Materials->GetOutrageMaterial("gauge03b");
        auto scale = Render::HudCanvas->GetScale();
        Render::CanvasBitmapInfo info;
        info.Position = Vector2(spacing + (flipX ? ENERGY_SPACING : -ENERGY_SPACING), ENERGY_HEIGHT) * scale;
        info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        info.Size *= scale;
        info.Texture = material.Handles[0];
        info.Scanline = 1.0f;
        if (flipX) {
            info.UV1.x = 0;
            info.UV0.x = 1;
        }
        info.HorizontalAlign = flipX ? AlignH::CenterRight : AlignH::CenterLeft;
        info.VerticalAlign = AlignV::Bottom;
        //info.Color.w = 0.5f;

        Render::HudGlowCanvas->DrawBitmap(info);
        //info.Color *= 0.5f;
        //Render::HudCanvas->DrawBitmap(info);
    }

    inline void DrawLeftMonitor(float x) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterLeft, "cockpit-left");

        auto scale = Render::HudCanvas->GetScale();

        {
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = MonitorGreenText;
            info.Position = Vector2(x - 90, WEAPON_TEXT_Y_OFFSET) * scale;
            info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
            info.VerticalAlign = AlignV::Bottom;
            info.Scanline = 0.5f;
            // todo: fix alignment to be from top
            //DrawMonitorText("S.LASER\nLVL: 5", info);
            DrawMonitorText("PLASMA\n", info);
        }

        {
            float resScale = Game::Level.IsDescent1() ? 2.0f : 1.0f;
            auto texId = GetWeaponTexID(Resources::GameData.Weapons[13]);
            DrawWeaponBitmap({ x - 100, WEAPON_BMP_Y_OFFSET }, AlignH::CenterLeft, texId, resScale);
        }

        DrawEnergyBar(x, false);

        DrawAdditiveBitmap({ x - 151, -38 }, AlignH::CenterLeft, "gauge02b", 1);
    }

    inline void DrawRightMonitor(float x) {
        DrawOpaqueBitmap({ x, 0 }, AlignH::CenterRight, "cockpit-right");

        auto scale = Render::HudCanvas->GetScale();
        Render::DrawTextInfo info;
        info.Font = FontSize::Small;
        info.Color = MonitorGreenText;
        info.Position = Vector2(x + 25, WEAPON_TEXT_Y_OFFSET) * scale;
        info.HorizontalAlign = AlignH::CenterRight; // Justify the left edge of the text to the center
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorText("CONCSN\nMISSILE", info);

        // Ammo counter
        info.Color = MonitorRedText;
        info.Position = Vector2(x + 50, WEAPON_TEXT_AMMO_Y_OFFSET) * scale;
        info.HorizontalAlign = AlignH::CenterRight;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorText("004", info);

        float resScale = Game::Level.IsDescent1() ? 2.0f : 1.0f;
        {
            // concussion
            auto texId = GetWeaponTexID(Resources::GameData.Weapons[8]);
            DrawWeaponBitmap({ x + 75, WEAPON_BMP_Y_OFFSET }, AlignH::CenterRight, texId, resScale);
        }

        DrawEnergyBar(x, true);


        // Bomb counter
        info.Color = MonitorRedText;
        info.Position = Vector2(x + 157, -26) * scale;
        info.HorizontalAlign = AlignH::CenterRight;
        info.VerticalAlign = AlignV::Bottom;
        info.Scanline = 0.5f;
        DrawMonitorText("B:04", info);


        // Draw Keys
        float keyScanline = 0.0f;
        DrawAdditiveBitmap({ x + 147, -90 }, AlignH::CenterRight, Gauges::BlueKey, resScale, keyScanline);
        DrawAdditiveBitmap({ x + 147 + 2, -90 + 21 }, AlignH::CenterRight, Gauges::GoldKey, keyScanline);
        DrawAdditiveBitmap({ x + 147 + 4, -90 + 42 }, AlignH::CenterRight, Gauges::RedKey, keyScanline);
    }

    inline void DrawCenterMonitor() {
        DrawOpaqueBitmap({ 0, 0 }, AlignH::Center, "cockpit-ctr");
        // Draw shields, invuln state, shield / energy count

        {
            auto scale = Render::HudCanvas->GetScale();
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = { 0.54f, 0.54f, 0.71f };
            info.Position = Vector2(2, -120) * scale;
            info.HorizontalAlign = AlignH::Center;
            info.VerticalAlign = AlignV::Bottom;
            info.Scanline = 0.5f;
            DrawMonitorText("100", info, 0.5f);
            //info.Scanline = 0.0f;
            //info.Color *= 0.1;
            //info.Color.z = 0.8f;

            info.Color = { 0.78f, 0.56f, 0.18f };
            info.Position = Vector2(2, -150) * scale;
            info.Scanline = 0.5f;
            DrawMonitorText("100", info, 0.5f);
        }

        {
            TexID ship = GetGaugeTexID(Gauges::Ship);
            if (Game::Level.IsDescent1())
                DrawShipBitmap({ 0, -46 }, Render::Materials->Get(ship), 2);
            else
                DrawShipBitmap({ 0, -40 }, Render::Materials->Get(ship), 1);

            DrawShipBitmap({ 0, -29 }, Render::Materials->GetOutrageMaterial("gauge01b#0"), 1);
        }
    }

    inline void DrawHighlights(bool flip, float opacity = 0.07f) {
        auto& material = Render::Materials->GetOutrageMaterial("SmHilite");
        auto scale = Render::HudCanvas->GetScale() * 1.5f;
        auto& screen = Render::HudCanvas->GetSize();
        int fl = flip ? 1 : -1;

        auto height = (float)material.Textures[0].GetWidth() * scale;
        auto width = (float)material.Textures[0].GetHeight() * scale * fl;

        Color color(1, 1, 1, opacity);

        const int steps = 16;
        const float vStep = 1.0f / steps;
        const float yStep = height / steps * 0.75f;
        float offset = screen.x / 2 + 150 * scale * fl;
        float yOffset = 10 * scale;

        for (int i = 0; i < steps; i++) {
            Render::CanvasPayload payload;
            payload.Texture = material.Handles[0];

            float x0 = -cos((steps - i) * 3.14f / steps / 2 + 0.2f) * width * scale * 0.7f + offset;
            float x1 = -cos((steps - i - 1) * 3.14f / steps / 2 + 0.2f) * width * scale * 0.7f + offset;
            float y0 = yOffset + yStep * i;
            float y1 = yOffset + yStep * (i + 1);

            Vector2 v0 = { x0, y0 };
            Vector2 v1 = { x0 + (width * 2), y0 };
            Vector2 v2 = { x1 + (width * 2), y1 };
            Vector2 v3 = { x1, y1 };

            payload.V0 = CanvasVertex{ v0, { 1 - vStep * i      , 0 }, color.RGBA().v }; // bottom left
            payload.V1 = CanvasVertex{ v1, { 1 - vStep * i      , 1 }, color.RGBA().v }; // bottom right
            payload.V2 = CanvasVertex{ v2, { 1 - vStep * (i + 1), 1 }, color.RGBA().v }; // top right
            payload.V3 = CanvasVertex{ v3, { 1 - vStep * (i + 1), 0 }, color.RGBA().v }; // top left
            Render::HudGlowCanvas->Draw(payload);
        }
    }

    inline string HudMessages[4]{};
    inline int HudMessageCount = 0;
    inline float HudTimer = 0;

    // Shifts all messages down by one
    inline void ShiftHudMessages() {
        if (HudMessageCount <= 0) return;

        for (int i = 0; i < (int)std::size(HudMessages) - 1; i++) {
            HudMessages[i] = std::move(HudMessages[i + 1]);
            //HudMessages[i] = HudMessages[i + 1];
        }

        HudMessages[std::size(HudMessages) - 1] = "";
        HudMessageCount--;
    }

    inline void PrintHudMessage(string_view msg) {
        //if (msg == HudMessages[0]) return; // duplicated

        if (HudMessageCount >= std::size(HudMessages)) {
            ShiftHudMessages();
        }

        HudMessages[HudMessageCount] = msg.data();
        HudMessageCount++;
        HudTimer = 3;
    }

    inline void DrawHudMessages(float dt) {
        auto scale = Render::HudCanvas->GetScale();
        float offset = 5;

        Render::DrawTextInfo info;
        info.Font = FontSize::Small;
        info.Color = MonitorGreenText;
        info.HorizontalAlign = AlignH::Center;
        info.VerticalAlign = AlignV::Top;
        info.Scanline = 0.5f;

        for (auto& msg : HudMessages) {
            info.Position = Vector2(0, offset) * scale;
            Render::HudCanvas->DrawGameText(msg, info);
            offset += 16;
        }

        HudTimer -= dt;
        if (HudTimer <= 0) {
            ShiftHudMessages();
            HudTimer = 3;
        }
    }

    inline void DrawHUD(float dt) {
        float spacing = 100;
        DrawLeftMonitor(-spacing);
        DrawRightMonitor(spacing);
        DrawCenterMonitor();

        DrawReticle();

        auto scale = Render::HudCanvas->GetScale();

        {
            // Life text
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = MonitorGreenText;
            info.Position = Vector2(30, 5) * scale;
            info.HorizontalAlign = AlignH::Left;
            info.VerticalAlign = AlignV::Top;
            info.Scanline = 0.5f;
            Render::HudCanvas->DrawGameText("X 2", info);
        }

        {
            // Life marker
            Inferno::Render::CanvasBitmapInfo info;
            info.Position = Vector2(5, 5) * scale;
            auto& material = Render::Materials->Get(GetGaugeTexID(Gauges::Lives));
            info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
            info.Size *= scale;
            info.Texture = material.Handles[0];
            info.HorizontalAlign = AlignH::Left;
            info.VerticalAlign = AlignV::Top;
            info.Scanline = 0.5f;
            Render::HudCanvas->DrawBitmap(info);
        }

        {
            // Score
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = MonitorGreenText;
            info.Position = Vector2(-5, 5) * scale;
            info.HorizontalAlign = AlignH::Right;
            info.VerticalAlign = AlignV::Top;
            info.Scanline = 0.5f;
            Render::HudCanvas->DrawGameText("SCORE:       0", info);
        }

        {
            // Lock text
            Render::DrawTextInfo info;
            info.Font = FontSize::Small;
            info.Color = MonitorRedText;
            info.Position = Vector2(0, 40) * scale;
            info.HorizontalAlign = AlignH::Center;
            info.VerticalAlign = AlignV::CenterTop;
            info.Scanline = 0.8f;
            //DrawMonitorText("!LOCK!", info);
        }

        DrawHighlights(false);
        DrawHighlights(true);

        // Lock warning
        //DrawAdditiveBitmap({ -220, -230 }, AlignH::CenterRight, "gauge16b", 1);

        //{
        //    auto& material = Render::Materials->GetOutrageMaterial("gauge16b");

        //    Render::CanvasBitmapInfo info;
        //    info.Position = { -300, 0 } *scale;
        //    info.Size = { (float)material.Textures[0].GetWidth(), (float)material.Textures[0].GetHeight() };
        //    info.Size *= scale;
        //    info.Texture = material.Handles[Material2D::Diffuse];
        //    info.HorizontalAlign = AlignH::CenterRight;
        //    info.VerticalAlign = AlignV::CenterTop;
        //    info.Scanline = 1;
        //    Render::HudGlowCanvas->DrawBitmap(info);
        //}

        DrawHudMessages(dt);
    }
}