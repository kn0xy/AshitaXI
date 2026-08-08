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

#ifndef IMGUISCALE_HPP_INCLUDED
#define IMGUISCALE_HPP_INCLUDED

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#include "Ashita.h"

namespace imguiscale
{
    class plugin final : public IPlugin
    {
        IAshitaCore* core_;
        ILogManager* log_;
        IDirect3DDevice8* device_;
        uint32_t id_;

        HWND hwnd_;
        float scale_x_;
        float scale_y_;
        bool scaling_active_;
        bool hooks_installed_;

        float mouse_x_;
        float mouse_y_;
        float mouse_prev_x_;
        float mouse_prev_y_;
        bool mouse_down_prev_[5];
        bool mouse_block_owned_;

    public:
        plugin(void);
        ~plugin(void) override;

        auto GetName(void) const -> const char* override;
        auto GetAuthor(void) const -> const char* override;
        auto GetDescription(void) const -> const char* override;
        auto GetLink(void) const -> const char* override;
        auto GetVersion(void) const -> double override;
        auto GetInterfaceVersion(void) const -> double override;
        auto GetPriority(void) const -> int32_t override;
        auto GetFlags(void) const -> uint32_t override;

        auto Initialize(IAshitaCore* core, ILogManager* logger, uint32_t id) -> bool override;
        auto Release(void) -> void override;

        auto HandleEvent(const char* eventName, const void* eventData, const uint32_t eventSize) -> void override;
        auto HandleCommand(int32_t mode, const char* command, bool injected) -> bool override;
        auto HandleIncomingText(int32_t mode, bool indent, const char* message, int32_t* modifiedMode, bool* modifiedIndent, char* modifiedMessage, bool injected, bool blocked) -> bool override;
        auto HandleOutgoingText(int32_t mode, const char* message, int32_t* modifiedMode, char* modifiedMessage, bool injected, bool blocked) -> bool override;
        auto HandleIncomingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t sizeChunk, const uint8_t* dataChunk, bool injected, bool blocked) -> bool override;
        auto HandleOutgoingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t sizeChunk, const uint8_t* dataChunk, bool injected, bool blocked) -> bool override;

        auto Direct3DInitialize(IDirect3DDevice8* device) -> bool override;
        auto Direct3DBeginScene(bool isRenderingBackBuffer) -> void override;
        auto Direct3DEndScene(bool isRenderingBackBuffer) -> void override;
        auto Direct3DPresent(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) -> void override;
        auto Direct3DSetRenderState(D3DRENDERSTATETYPE State, DWORD* Value) -> bool override;
        auto Direct3DDrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) -> bool override;
        auto Direct3DDrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT minIndex, UINT NumVertices, UINT startIndex, UINT primCount) -> bool override;
        auto Direct3DDrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) -> bool override;
        auto Direct3DDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertexIndices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) -> bool override;

        // Called from Ashita.dll IAT hooks (USER32 coordinate conversion).
        auto transform_screen_to_client(HWND hwnd, LPPOINT point) const -> void;
        auto transform_client_to_screen(HWND hwnd, LPPOINT point) const -> void;

    private:
        auto install_ashita_cursor_hooks(void) -> bool;
        auto remove_ashita_cursor_hooks(void) -> void;
        auto clear_mouse_block(void) -> void;
        auto update_scale_factors(HWND hwnd) -> bool;
        auto scale_client_point(float client_x, float client_y, float& out_x, float& out_y) const -> void;
        auto poll_scaled_mouse_pos(HWND hwnd) -> bool;
        auto apply_mouse_scaling(IGuiManager* gui) -> void;
        auto apply_mouse_blocking(IGuiManager* gui) -> void;
        auto apply_frame_scaling(HWND hwnd) -> void;
    };

} // namespace imguiscale

#endif // IMGUISCALE_HPP_INCLUDED
