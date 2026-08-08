/**
 * Plugins - Copyright (c) 2026 Ashita Development Team
 * Author: kn0xy
 * Contact: https://www.ashitaxi.com/
 * Contact: https://discord.gg/Ashita
 *
 * This file is part of Ashita.
 *
 * Ashita is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Ashita is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Ashita.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "imguiscale.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace imguiscale
{
    namespace
    {
        constexpr float kScaleEpsilon = 0.001f;

        using ScreenToClient_fn = BOOL(WINAPI*)(HWND, LPPOINT);
        using ClientToScreen_fn = BOOL(WINAPI*)(HWND, LPPOINT);

        plugin* g_plugin                 = nullptr;
        ScreenToClient_fn g_screen_to_client = nullptr;
        ClientToScreen_fn g_client_to_screen = nullptr;
        void** g_iat_screen_to_client    = nullptr;
        void** g_iat_client_to_screen    = nullptr;

        auto find_iat_entry(HMODULE module, const char* dll_name, const char* func_name) -> void**
        {
            if (module == nullptr || dll_name == nullptr || func_name == nullptr)
            {
                return nullptr;
            }

            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return nullptr;
            }

            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
            {
                return nullptr;
            }

            const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (dir.VirtualAddress == 0 || dir.Size == 0)
            {
                return nullptr;
            }

            auto* import_desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(reinterpret_cast<uint8_t*>(module) + dir.VirtualAddress);
            for (; import_desc->Name != 0; ++import_desc)
            {
                const auto* module_name = reinterpret_cast<const char*>(reinterpret_cast<uint8_t*>(module) + import_desc->Name);
                if (_stricmp(module_name, dll_name) != 0)
                {
                    continue;
                }

                auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(reinterpret_cast<uint8_t*>(module) + import_desc->FirstThunk);
                auto* orig  = import_desc->OriginalFirstThunk != 0
                    ? reinterpret_cast<IMAGE_THUNK_DATA*>(reinterpret_cast<uint8_t*>(module) + import_desc->OriginalFirstThunk)
                    : thunk;

                for (; orig->u1.AddressOfData != 0; ++orig, ++thunk)
                {
                    if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal))
                    {
                        continue;
                    }

                    const auto* by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(reinterpret_cast<uint8_t*>(module) + orig->u1.AddressOfData);
                    if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), func_name) != 0)
                    {
                        continue;
                    }

                    return reinterpret_cast<void**>(&thunk->u1.Function);
                }
            }

            return nullptr;
        }

        auto patch_iat_entry(void** entry, void* hook, void** original_out) -> bool
        {
            if (entry == nullptr || hook == nullptr || original_out == nullptr)
            {
                return false;
            }

            DWORD protect = 0;
            if (!VirtualProtect(entry, sizeof(void*), PAGE_EXECUTE_READWRITE, &protect))
            {
                return false;
            }

            *original_out = *entry;
            *entry        = hook;

            DWORD unused = 0;
            VirtualProtect(entry, sizeof(void*), protect, &unused);
            return true;
        }

        auto restore_iat_entry(void** entry, void* original) -> void
        {
            if (entry == nullptr || original == nullptr)
            {
                return;
            }

            DWORD protect = 0;
            if (!VirtualProtect(entry, sizeof(void*), PAGE_EXECUTE_READWRITE, &protect))
            {
                return;
            }

            *entry = original;

            DWORD unused = 0;
            VirtualProtect(entry, sizeof(void*), protect, &unused);
        }

        auto WINAPI hooked_ScreenToClient(HWND hwnd, LPPOINT point) -> BOOL
        {
            const BOOL ok = g_screen_to_client != nullptr ? g_screen_to_client(hwnd, point) : FALSE;
            if (ok && g_plugin != nullptr)
            {
                g_plugin->transform_screen_to_client(hwnd, point);
            }
            return ok;
        }

        auto WINAPI hooked_ClientToScreen(HWND hwnd, LPPOINT point) -> BOOL
        {
            if (g_plugin != nullptr)
            {
                g_plugin->transform_client_to_screen(hwnd, point);
            }
            return g_client_to_screen != nullptr ? g_client_to_screen(hwnd, point) : FALSE;
        }
    }

    plugin::plugin(void)
        : core_(nullptr)
        , log_(nullptr)
        , device_(nullptr)
        , id_(0)
        , hwnd_(nullptr)
        , scale_x_(1.0f)
        , scale_y_(1.0f)
        , scaling_active_(false)
        , hooks_installed_(false)
        , mouse_x_(0.0f)
        , mouse_y_(0.0f)
        , mouse_prev_x_(0.0f)
        , mouse_prev_y_(0.0f)
        , mouse_down_prev_{}
        , mouse_block_owned_(false)
    {}

    plugin::~plugin(void)
    {}

    auto plugin::GetName(void) const -> const char*
    {
        return "imguiscale";
    }

    auto plugin::GetAuthor(void) const -> const char*
    {
        return "kn0xy";
    }

    auto plugin::GetDescription(void) const -> const char*
    {
        return "Maps ImGui mouse coordinates from screen space to the D3D render resolution.";
    }

    auto plugin::GetLink(void) const -> const char*
    {
        return "https://www.github.com/kn0xy";
    }

    auto plugin::GetVersion(void) const -> double
    {
        return 3.6;
    }

    auto plugin::GetInterfaceVersion(void) const -> double
    {
        return ASHITA_INTERFACE_VERSION;
    }

    auto plugin::GetPriority(void) const -> int32_t
    {
        // Run late so Present overwrite still wins if anything rewrites ImGui input
        return 1000;
    }

    auto plugin::GetFlags(void) const -> uint32_t
    {
        return static_cast<uint32_t>(Ashita::PluginFlags::UseDirect3D);
    }

    auto plugin::Initialize(IAshitaCore* core, ILogManager* logger, uint32_t id) -> bool
    {
        core_ = core;
        log_  = logger;
        id_   = id;
        g_plugin = this;

        if (!install_ashita_cursor_hooks())
        {
            log_->Log(static_cast<uint32_t>(Ashita::LogLevel::Warn), "imguiscale",
                "Failed to hook Ashita cursor conversion; falling back to Present overwrite only.");
        }
        else
        {
            log_->Log(static_cast<uint32_t>(Ashita::LogLevel::Info), "imguiscale",
                "Hooked Ashita.dll ScreenToClient/ClientToScreen for scaled ImGui mouse input.");
        }

        log_->Log(static_cast<uint32_t>(Ashita::LogLevel::Info), "imguiscale", "Loaded.");
        return true;
    }

    auto plugin::clear_mouse_block(void) -> void
    {
        if (!mouse_block_owned_ || core_ == nullptr)
        {
            mouse_block_owned_ = false;
            return;
        }

        auto* input = core_->GetInputManager();
        if (input != nullptr)
        {
            auto* mouse = input->GetMouse();
            if (mouse != nullptr)
            {
                mouse->SetBlockInput(false);
            }
        }
        mouse_block_owned_ = false;
    }

    auto plugin::Release(void) -> void
    {
        clear_mouse_block();
        remove_ashita_cursor_hooks();
        g_plugin = nullptr;
        device_  = nullptr;
        hwnd_    = nullptr;
    }

    auto plugin::HandleEvent(const char* eventName, const void* eventData, const uint32_t eventSize) -> void
    {
        UNREFERENCED_PARAMETER(eventName);
        UNREFERENCED_PARAMETER(eventData);
        UNREFERENCED_PARAMETER(eventSize);
    }

    auto plugin::HandleCommand(int32_t mode, const char* command, bool injected) -> bool
    {
        UNREFERENCED_PARAMETER(mode);
        UNREFERENCED_PARAMETER(command);
        UNREFERENCED_PARAMETER(injected);
        return false;
    }

    auto plugin::HandleIncomingText(int32_t mode, bool indent, const char* message, int32_t* modifiedMode, bool* modifiedIndent, char* modifiedMessage, bool injected, bool blocked) -> bool
    {
        UNREFERENCED_PARAMETER(mode);
        UNREFERENCED_PARAMETER(indent);
        UNREFERENCED_PARAMETER(message);
        UNREFERENCED_PARAMETER(modifiedMode);
        UNREFERENCED_PARAMETER(modifiedIndent);
        UNREFERENCED_PARAMETER(modifiedMessage);
        UNREFERENCED_PARAMETER(injected);
        UNREFERENCED_PARAMETER(blocked);
        return false;
    }

    auto plugin::HandleOutgoingText(int32_t mode, const char* message, int32_t* modifiedMode, char* modifiedMessage, bool injected, bool blocked) -> bool
    {
        UNREFERENCED_PARAMETER(mode);
        UNREFERENCED_PARAMETER(message);
        UNREFERENCED_PARAMETER(modifiedMode);
        UNREFERENCED_PARAMETER(modifiedMessage);
        UNREFERENCED_PARAMETER(injected);
        UNREFERENCED_PARAMETER(blocked);
        return false;
    }

    auto plugin::HandleIncomingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t sizeChunk, const uint8_t* dataChunk, bool injected, bool blocked) -> bool
    {
        UNREFERENCED_PARAMETER(id);
        UNREFERENCED_PARAMETER(size);
        UNREFERENCED_PARAMETER(data);
        UNREFERENCED_PARAMETER(modified);
        UNREFERENCED_PARAMETER(sizeChunk);
        UNREFERENCED_PARAMETER(dataChunk);
        UNREFERENCED_PARAMETER(injected);
        UNREFERENCED_PARAMETER(blocked);
        return false;
    }

    auto plugin::HandleOutgoingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t sizeChunk, const uint8_t* dataChunk, bool injected, bool blocked) -> bool
    {
        UNREFERENCED_PARAMETER(id);
        UNREFERENCED_PARAMETER(size);
        UNREFERENCED_PARAMETER(data);
        UNREFERENCED_PARAMETER(modified);
        UNREFERENCED_PARAMETER(sizeChunk);
        UNREFERENCED_PARAMETER(dataChunk);
        UNREFERENCED_PARAMETER(injected);
        UNREFERENCED_PARAMETER(blocked);
        return false;
    }

    auto plugin::Direct3DInitialize(IDirect3DDevice8* device) -> bool
    {
        device_ = device;
        return true;
    }

    auto plugin::install_ashita_cursor_hooks(void) -> bool
    {
        if (hooks_installed_)
        {
            return true;
        }

        HMODULE ashita = GetModuleHandleA("Ashita.dll");
        if (ashita == nullptr)
        {
            return false;
        }

        void** stc_entry = find_iat_entry(ashita, "USER32.dll", "ScreenToClient");
        void** cts_entry = find_iat_entry(ashita, "USER32.dll", "ClientToScreen");
        if (stc_entry == nullptr || cts_entry == nullptr)
        {
            return false;
        }

        void* stc_original = nullptr;
        void* cts_original = nullptr;
        if (!patch_iat_entry(stc_entry, reinterpret_cast<void*>(&hooked_ScreenToClient), &stc_original))
        {
            return false;
        }

        if (!patch_iat_entry(cts_entry, reinterpret_cast<void*>(&hooked_ClientToScreen), &cts_original))
        {
            restore_iat_entry(stc_entry, stc_original);
            return false;
        }

        g_screen_to_client     = reinterpret_cast<ScreenToClient_fn>(stc_original);
        g_client_to_screen     = reinterpret_cast<ClientToScreen_fn>(cts_original);
        g_iat_screen_to_client = stc_entry;
        g_iat_client_to_screen = cts_entry;
        hooks_installed_       = true;
        return true;
    }

    auto plugin::remove_ashita_cursor_hooks(void) -> void
    {
        if (!hooks_installed_)
        {
            return;
        }

        restore_iat_entry(g_iat_screen_to_client, reinterpret_cast<void*>(g_screen_to_client));
        restore_iat_entry(g_iat_client_to_screen, reinterpret_cast<void*>(g_client_to_screen));

        g_iat_screen_to_client = nullptr;
        g_iat_client_to_screen = nullptr;
        g_screen_to_client     = nullptr;
        g_client_to_screen     = nullptr;
        hooks_installed_       = false;
    }

    auto plugin::transform_screen_to_client(HWND hwnd, LPPOINT point) const -> void
    {
        if (!scaling_active_ || point == nullptr || hwnd == nullptr || hwnd_ == nullptr)
        {
            return;
        }

        if (hwnd != hwnd_)
        {
            return;
        }

        point->x = static_cast<LONG>(std::lround(static_cast<float>(point->x) * scale_x_));
        point->y = static_cast<LONG>(std::lround(static_cast<float>(point->y) * scale_y_));
    }

    auto plugin::transform_client_to_screen(HWND hwnd, LPPOINT point) const -> void
    {
        if (!scaling_active_ || point == nullptr || hwnd == nullptr || hwnd_ == nullptr)
        {
            return;
        }

        if (hwnd != hwnd_)
        {
            return;
        }

        if (std::fabs(scale_x_) < kScaleEpsilon || std::fabs(scale_y_) < kScaleEpsilon)
        {
            return;
        }

        // ImGui is in render space; convert back to real client before ClientToScreen
        point->x = static_cast<LONG>(std::lround(static_cast<float>(point->x) / scale_x_));
        point->y = static_cast<LONG>(std::lround(static_cast<float>(point->y) / scale_y_));
    }

    auto plugin::update_scale_factors(HWND hwnd) -> bool
    {
        scaling_active_ = false;
        scale_x_        = 1.0f;
        scale_y_        = 1.0f;
        hwnd_           = hwnd;

        if (device_ == nullptr || hwnd == nullptr)
        {
            return false;
        }

        D3DVIEWPORT8 viewport{};
        if (FAILED(device_->GetViewport(&viewport)))
        {
            return false;
        }

        RECT client{};
        if (!GetClientRect(hwnd, &client))
        {
            return false;
        }

        const float client_w = static_cast<float>(client.right - client.left);
        const float client_h = static_cast<float>(client.bottom - client.top);
        const float render_w = static_cast<float>(viewport.Width);
        const float render_h = static_cast<float>(viewport.Height);

        if (client_w <= 0.0f || client_h <= 0.0f || render_w <= 0.0f || render_h <= 0.0f)
        {
            return false;
        }

        scale_x_ = render_w / client_w;
        scale_y_ = render_h / client_h;

        if (std::fabs(scale_x_ - 1.0f) < kScaleEpsilon && std::fabs(scale_y_ - 1.0f) < kScaleEpsilon)
        {
            return false;
        }

        scaling_active_ = true;
        return true;
    }

    auto plugin::scale_client_point(float client_x, float client_y, float& out_x, float& out_y) const -> void
    {
        out_x = client_x * scale_x_;
        out_y = client_y * scale_y_;
    }

    auto plugin::poll_scaled_mouse_pos(HWND hwnd) -> bool
    {
        POINT cursor{};
        if (!GetCursorPos(&cursor))
        {
            return false;
        }

        if (!::ScreenToClient(hwnd, &cursor))
        {
            return false;
        }

        mouse_prev_x_ = mouse_x_;
        mouse_prev_y_ = mouse_y_;
        scale_client_point(static_cast<float>(cursor.x), static_cast<float>(cursor.y), mouse_x_, mouse_y_);
        return true;
    }

    auto plugin::apply_mouse_scaling(IGuiManager* gui) -> void
    {
        if (!scaling_active_ || gui == nullptr)
        {
            return;
        }

        auto& io = gui->GetIO();
        io.MousePos.x     = mouse_x_;
        io.MousePos.y     = mouse_y_;
        io.MousePosPrev.x = mouse_prev_x_;
        io.MousePosPrev.y = mouse_prev_y_;
        io.MouseDelta.x   = mouse_x_ - mouse_prev_x_;
        io.MouseDelta.y   = mouse_y_ - mouse_prev_y_;

        for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); ++i)
        {
            const bool is_down = io.MouseDown[i];

            if (is_down && !mouse_down_prev_[i])
            {
                io.MouseClickedPos[i].x = mouse_x_;
                io.MouseClickedPos[i].y = mouse_y_;
            }

            mouse_down_prev_[i] = is_down;
        }
    }

    auto plugin::apply_mouse_blocking(IGuiManager* gui) -> void
    {
        if (gui == nullptr || core_ == nullptr)
        {
            return;
        }

        auto* input = core_->GetInputManager();
        if (input == nullptr)
        {
            return;
        }

        auto* mouse = input->GetMouse();
        if (mouse == nullptr)
        {
            return;
        }

        const auto& io = gui->GetIO();
        if (io.WantCaptureMouse)
        {
            // Do not steal a user-enabled permanent /mouse block.
            if (!mouse->GetBlockInput() || mouse_block_owned_)
            {
                mouse->SetBlockInput(true);
                mouse_block_owned_ = true;
            }
        }
        else if (mouse_block_owned_)
        {
            mouse->SetBlockInput(false);
            mouse_block_owned_ = false;
        }
    }

    auto plugin::apply_frame_scaling(HWND hwnd) -> void
    {
        if (core_ == nullptr || hwnd == nullptr)
        {
            return;
        }

        auto* gui = core_->GetGuiManager();
        if (gui == nullptr)
        {
            return;
        }

        if (device_ != nullptr && update_scale_factors(hwnd))
        {
            poll_scaled_mouse_pos(hwnd);
            apply_mouse_scaling(gui);
        }
        else
        {
            hwnd_ = hwnd;
            for (int i = 0; i < 5; ++i)
            {
                mouse_down_prev_[i] = false;
            }
        }

        apply_mouse_blocking(gui);
    }

    auto plugin::Direct3DBeginScene(bool isRenderingBackBuffer) -> void
    {
        UNREFERENCED_PARAMETER(isRenderingBackBuffer);

        // Keep scale factors fresh before Ashita polls cursor for the frame
        if (core_ == nullptr || device_ == nullptr)
        {
            return;
        }

        const auto properties = core_->GetProperties();
        if (properties == nullptr)
        {
            return;
        }

        const HWND hwnd = properties->GetFinalFantasyHwnd();
        if (hwnd != nullptr)
        {
            update_scale_factors(hwnd);
        }
    }

    auto plugin::Direct3DEndScene(bool isRenderingBackBuffer) -> void
    {
        UNREFERENCED_PARAMETER(isRenderingBackBuffer);
    }

    auto plugin::Direct3DPresent(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) -> void
    {
        UNREFERENCED_PARAMETER(pSourceRect);
        UNREFERENCED_PARAMETER(pDestRect);
        UNREFERENCED_PARAMETER(hDestWindowOverride);
        UNREFERENCED_PARAMETER(pDirtyRegion);

        if (core_ == nullptr)
        {
            return;
        }

        const auto properties = core_->GetProperties();
        if (properties == nullptr)
        {
            return;
        }

        const HWND hwnd = properties->GetFinalFantasyHwnd();
        if (hwnd == nullptr)
        {
            return;
        }

        apply_frame_scaling(hwnd);
    }

    auto plugin::Direct3DSetRenderState(D3DRENDERSTATETYPE State, DWORD* Value) -> bool
    {
        UNREFERENCED_PARAMETER(State);
        UNREFERENCED_PARAMETER(Value);
        return false;
    }

    auto plugin::Direct3DDrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) -> bool
    {
        UNREFERENCED_PARAMETER(PrimitiveType);
        UNREFERENCED_PARAMETER(StartVertex);
        UNREFERENCED_PARAMETER(PrimitiveCount);
        return false;
    }

    auto plugin::Direct3DDrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT minIndex, UINT NumVertices, UINT startIndex, UINT primCount) -> bool
    {
        UNREFERENCED_PARAMETER(PrimitiveType);
        UNREFERENCED_PARAMETER(minIndex);
        UNREFERENCED_PARAMETER(NumVertices);
        UNREFERENCED_PARAMETER(startIndex);
        UNREFERENCED_PARAMETER(primCount);
        return false;
    }

    auto plugin::Direct3DDrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) -> bool
    {
        UNREFERENCED_PARAMETER(PrimitiveType);
        UNREFERENCED_PARAMETER(PrimitiveCount);
        UNREFERENCED_PARAMETER(pVertexStreamZeroData);
        UNREFERENCED_PARAMETER(VertexStreamZeroStride);
        return false;
    }

    auto plugin::Direct3DDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertexIndices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) -> bool
    {
        UNREFERENCED_PARAMETER(PrimitiveType);
        UNREFERENCED_PARAMETER(MinVertexIndex);
        UNREFERENCED_PARAMETER(NumVertexIndices);
        UNREFERENCED_PARAMETER(PrimitiveCount);
        UNREFERENCED_PARAMETER(pIndexData);
        UNREFERENCED_PARAMETER(IndexDataFormat);
        UNREFERENCED_PARAMETER(pVertexStreamZeroData);
        UNREFERENCED_PARAMETER(VertexStreamZeroStride);
        return false;
    }

} // namespace imguiscale

__declspec(dllexport) auto __stdcall expCreatePlugin(const char* args) -> IPlugin*
{
    UNREFERENCED_PARAMETER(args);
    return new imguiscale::plugin();
}

__declspec(dllexport) auto __stdcall expDestroyPlugin(void* instance) -> void
{
    delete static_cast<imguiscale::plugin*>(instance);
}

__declspec(dllexport) auto __stdcall expGetInterfaceVersion(void) -> double
{
    return ASHITA_INTERFACE_VERSION;
}
