#pragma once
#include <ImGui/imgui.h>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <d3d9.h>
#include "XenbonoAssets.h"

// How the XENBONO window looks: the same as the Xenon game server window, which is the Shatter websites (admin panel,
// donation site) and launcher in a game server window.
// Same font (Bricolage Grotesque), same colours, the Shatter logo in the corner, cards with a thin outline and uppercase
// labels over big numbers. Only ImGui and D3D9 in here (no engine types).
namespace XenbonoTheme
{
    namespace Col
    {
        constexpr ImVec4 Bg = ImVec4(0.051f, 0.035f, 0.086f, 1.f); // #0d0916
        constexpr ImVec4 Card = ImVec4(0.082f, 0.067f, 0.110f, 1.f); // #15111c
        constexpr ImVec4 Card2 = ImVec4(0.114f, 0.090f, 0.149f, 1.f); // #1d1726
        constexpr ImVec4 Line = ImVec4(0.153f, 0.125f, 0.192f, 1.f); // #272031
        constexpr ImVec4 Muted = ImVec4(0.604f, 0.561f, 0.659f, 1.f); // #9a8fa8
        constexpr ImVec4 Text = ImVec4(0.906f, 0.886f, 0.933f, 1.f); // #e7e2ee
        constexpr ImVec4 Accent = ImVec4(0.839f, 0.247f, 0.776f, 1.f); // #d63fc6
        constexpr ImVec4 OnAccent = ImVec4(0.102f, 0.039f, 0.090f, 1.f); // #1a0a17
        constexpr ImVec4 Good = ImVec4(0.373f, 0.827f, 0.627f, 1.f); // #5fd3a0
        constexpr ImVec4 Warn = ImVec4(0.886f, 0.714f, 0.376f, 1.f); // #e2b660
    }

    inline ImU32 U32(ImVec4 c, float alpha = 1.f)
    {
        c.w *= alpha;
        return ImGui::ColorConvertFloat4ToU32(c);
    }

    // ── the logo, as a texture ──────────────────────────────
    inline IDirect3DTexture9* LogoTexture = nullptr;

    // Uploads the embedded Shatter logo (with mip levels, it is drawn much smaller than it is stored).
    inline bool CreateLogo(IDirect3DDevice9* device)
    {
        if (LogoTexture)
        {
            LogoTexture->Release();
            LogoTexture = nullptr;
        }

        if (FAILED(device->CreateTexture(XenbonoLogoWidth, XenbonoLogoHeight, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &LogoTexture, nullptr)))
            return false;

        D3DLOCKED_RECT locked {};
        if (FAILED(LogoTexture->LockRect(0, &locked, nullptr, 0)))
        {
            LogoTexture->Release();
            LogoTexture = nullptr;
            return false;
        }

        for (unsigned y = 0; y < XenbonoLogoHeight; ++y)
        {
            unsigned char* out = (unsigned char*)locked.pBits + y * locked.Pitch;
            const unsigned char* in = XenbonoLogoRGBA + y * XenbonoLogoWidth * 4;
            for (unsigned x = 0; x < XenbonoLogoWidth; ++x, out += 4, in += 4)
            {
                out[0] = in[2]; // B
                out[1] = in[1]; // G
                out[2] = in[0]; // R
                out[3] = in[3]; // A
            }
        }

        LogoTexture->UnlockRect(0);
        LogoTexture->GenerateMipSubLevels();
        return true;
    }

    // ── widgets and colours ─────────────────────────────────
    // `scale` is the monitor's DPI scale.
    inline void Apply(ImGuiStyle& style, float scale)
    {
        style.WindowRounding = 0.f;
        style.WindowPadding = ImVec2(22, 16);
        style.WindowBorderSize = 0.f;
        style.ChildRounding = 14.f;
        style.ChildBorderSize = 1.f;
        style.FrameRounding = 11.f;
        style.FramePadding = ImVec2(14, 8);
        style.FrameBorderSize = 1.f;
        style.ItemSpacing = ImVec2(12, 10);
        style.ItemInnerSpacing = ImVec2(10, 6);
        style.GrabMinSize = 14.f;
        style.GrabRounding = 8.f;
        style.ScrollbarSize = 10.f;
        style.ScrollbarRounding = 8.f;
        style.TabRounding = 0.f;
        style.PopupRounding = 12.f;

        ImVec4* c = style.Colors;
        c[ImGuiCol_Text] = Col::Text;
        c[ImGuiCol_TextDisabled] = Col::Muted;
        c[ImGuiCol_WindowBg] = Col::Bg;
        c[ImGuiCol_ChildBg] = Col::Card;
        c[ImGuiCol_PopupBg] = Col::Card2;
        c[ImGuiCol_Border] = Col::Line;
        c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

        c[ImGuiCol_FrameBg] = Col::Card2;
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.140f, 0.112f, 0.184f, 1.f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.165f, 0.132f, 0.216f, 1.f);
        c[ImGuiCol_CheckMark] = Col::Accent;
        c[ImGuiCol_SliderGrab] = Col::Accent;
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.92f, 0.38f, 0.86f, 1.f);

        // like the sites' .btn: a card-coloured button with a hairline border; the primary one is solid accent
        c[ImGuiCol_Button] = Col::Card2;
        c[ImGuiCol_ButtonHovered] = ImVec4(0.165f, 0.132f, 0.216f, 1.f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.200f, 0.160f, 0.260f, 1.f);

        c[ImGuiCol_Header] = Col::Card2;
        c[ImGuiCol_HeaderHovered] = ImVec4(0.165f, 0.132f, 0.216f, 1.f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.200f, 0.160f, 0.260f, 1.f);
        c[ImGuiCol_Separator] = Col::Line;
        c[ImGuiCol_TextSelectedBg] = ImVec4(0.839f, 0.247f, 0.776f, 0.35f);

        c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ScrollbarGrab] = Col::Line;
        c[ImGuiCol_ScrollbarGrabHovered] = Col::Muted;
        c[ImGuiCol_ScrollbarGrabActive] = Col::Accent;

        // tabs are plain labels with a line under the current one, like a site's navigation
        // (this ImGui has no tab underline, so the current tab is the one with a faint accent fill)
        c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_TabHovered] = ImVec4(1.f, 1.f, 1.f, 0.06f);
        c[ImGuiCol_TabActive] = ImVec4(0.839f, 0.247f, 0.776f, 0.22f);
        c[ImGuiCol_TabUnfocused] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.839f, 0.247f, 0.776f, 0.14f);

        style.ScaleAllSizes(scale);
    }

    enum class State
    {
        SettingUp,
        Joinable,
        InMatch
    };

    inline const char* StateText(State s)
    {
        return s == State::SettingUp ? "Setting up" : (s == State::Joinable ? "Joinable" : "Match started");
    }

    inline ImVec4 StateColor(State s)
    {
        return s == State::SettingUp ? Col::Warn : (s == State::Joinable ? Col::Good : Col::Accent);
    }

    // ── text helpers ────────────────────────────────────────
    // The font is a variable one whose default weight is Regular, so heavier text is the same line drawn twice, a hair apart.
    inline void BoldText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text)
    {
        dl->AddText(font, size, pos, col, text);
        dl->AddText(font, size, ImVec2(pos.x + size * 0.035f, pos.y), col, text);
    }

    // Small uppercase text with space between the letters (the sites' labels). Returns its width.
    inline float SpacedCaps(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text, float spacing)
    {
        float x = pos.x;
        for (const char* p = text; *p; ++p)
        {
            char ch[2] = { (char)toupper((unsigned char)*p), 0 };
            dl->AddText(font, size, ImVec2(x, pos.y), col, ch);
            x += font->CalcTextSizeA(size, FLT_MAX, 0.f, ch).x + spacing;
        }
        return x - pos.x - spacing;
    }

    inline float SpacedCapsWidth(ImFont* font, float size, const char* text, float spacing)
    {
        float w = 0.f;
        for (const char* p = text; *p; ++p)
        {
            char ch[2] = { (char)toupper((unsigned char)*p), 0 };
            w += font->CalcTextSizeA(size, FLT_MAX, 0.f, ch).x + spacing;
        }
        return w - spacing;
    }

    // ── the page ────────────────────────────────────────────
    // The top bar: the Shatter logo, "XENBONO" and a small "game server" label on the left, what the server is doing on the
    // right, a hairline under it. Leaves the cursor below it.
    inline void Header(float scale, const char* version, State state)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        const float base = ImGui::GetFontSize();
        const ImVec2 origin = ImGui::GetWindowPos();
        const float width = ImGui::GetWindowWidth();
        const float height = 66.f * scale;
        const float padX = ImGui::GetStyle().WindowPadding.x;

        // the logo
        const float logoH = 30.f * scale;
        const float logoW = logoH * (float)XenbonoLogoWidth / (float)XenbonoLogoHeight;
        const float midY = origin.y + height * 0.5f;
        if (LogoTexture)
            dl->AddImage((ImTextureID)LogoTexture, ImVec2(origin.x + padX, midY - logoH * 0.5f), ImVec2(origin.x + padX + logoW, midY + logoH * 0.5f));

        // brand
        const float nameSize = base * 1.32f;
        const float nameX = origin.x + padX + logoW + 11.f * scale;
        const ImVec2 nameExtent = font->CalcTextSizeA(nameSize, FLT_MAX, 0.f, "XENBONO");
        BoldText(dl, font, nameSize, ImVec2(nameX, midY - nameExtent.y * 0.5f - 1.f * scale), U32(Col::Text), "XENBONO");
        const float labelSize = base * 0.68f;
        SpacedCaps(dl, font, labelSize, ImVec2(nameX + nameExtent.x + 12.f * scale, midY - labelSize * 0.5f + 2.f * scale), U32(Col::Muted), "Game server", 1.6f * scale);

        // state on the right: a dot and the words
        const char* text = StateText(state);
        const ImVec2 textSize = font->CalcTextSizeA(base, FLT_MAX, 0.f, text);
        const ImVec2 verSize = font->CalcTextSizeA(base * 0.82f, FLT_MAX, 0.f, version);
        const float right = origin.x + width - padX;
        const float dot = 4.5f * scale;
        dl->AddText(font, base, ImVec2(right - textSize.x, midY - textSize.y - 1.f * scale), U32(Col::Text), text);
        dl->AddCircleFilled(ImVec2(right - textSize.x - 10.f * scale - dot, midY - textSize.y * 0.5f - 1.f * scale), dot, U32(StateColor(state)));
        dl->AddText(font, base * 0.82f, ImVec2(right - verSize.x, midY + 2.f * scale), U32(Col::Muted), version);

        dl->AddRectFilled(ImVec2(origin.x, origin.y + height), ImVec2(origin.x + width, origin.y + height + 1.f), U32(Col::Line));
        ImGui::SetCursorPos(ImVec2(padX, height + 14.f * scale));
    }

    // A small uppercase label over a group of controls.
    inline void Kicker(float scale, const char* text)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        const float size = ImGui::GetFontSize() * 0.70f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        SpacedCaps(dl, font, size, pos, U32(Col::Muted), text, 1.5f * scale);
        ImGui::Dummy(ImVec2(1.f, size + 4.f * scale));
    }

    // One stat in a card, like the numbers on the admin panel: label above, the value big under it. `dot` puts a coloured dot
    // in front of the value.
    inline void StatCard(float scale, const char* id, const char* label, const char* value, float width, const ImVec4* dot = nullptr, bool compact = false)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16.f * scale);
        ImGui::BeginChild(id, ImVec2(width, (compact ? 66.f : 80.f) * scale), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        const float base = ImGui::GetFontSize();
        const ImVec2 origin = ImGui::GetWindowPos();
        const float pad = 16.f * scale;

        SpacedCaps(dl, font, base * 0.68f, ImVec2(origin.x + pad, origin.y + 13.f * scale), U32(Col::Muted), label, 1.4f * scale);

        float x = origin.x + pad;
        const float valueSize = base * (compact ? 1.12f : 1.62f);
        const float y = origin.y + (compact ? 33.f : 31.f) * scale;
        if (dot)
        {
            dl->AddCircleFilled(ImVec2(x + 5.f * scale, y + valueSize * 0.52f), 5.f * scale, U32(*dot));
            x += 18.f * scale;
        }
        BoldText(dl, font, valueSize, ImVec2(x, y), U32(Col::Text), value);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // Muted explanation text
    inline void Note(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::Muted);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    // A button in the sites' primary style: solid accent, dark text.
    inline bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0))
    {
        ImGui::PushStyleColor(ImGuiCol_Button, Col::Accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.33f, 0.84f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.20f, 0.71f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, Col::OnAccent);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(5);
        return pressed;
    }

    // Draws the faint logo behind everything, in the lower right (the sites do the same at a few percent).
    inline void Watermark(float scale)
    {
        if (!LogoTexture)
            return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        const float w = 250.f * scale;
        const float h = w * (float)XenbonoLogoHeight / (float)XenbonoLogoWidth;
        const ImVec2 corner(origin.x + size.x - 8.f * scale, origin.y + size.y - 8.f * scale);
        dl->AddImage((ImTextureID)LogoTexture, ImVec2(corner.x - w, corner.y - h), corner, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 12));
    }
}
