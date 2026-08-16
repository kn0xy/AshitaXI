--[[
* Addons - Copyright (c) 2025 Ashita Development Team
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
--]]

addon.name      = 'ahprice';
addon.author    = 'kn0xy';
addon.version   = '1.1';
addon.desc      = 'Allows typing the price of an item in the auction house.';
addon.link      = 'https://www.knoxy.tk';

require 'common';

local ffi = require 'ffi';

local ffxi = ashita.memory.get_base('FFXiMain.dll') or 0;
local ahprice = T{
    active     = false,
    session    = false,
    buffer     = '',
    menus      = '',
    bind_block = false,
    binds      = T{ },
};

local PRICE_OFFSET = 0x28;
local STATIC_ADDR  = 0x0057824C;
local MAX_DIGITS   = 8;
local MAX_PRICE    = 99999999;
local KEY_BACK   = 0x0E;
local KEY_DELETE = 0xD3;
local DIGITS = T{
    [0x02] = 1, [0x03] = 2, [0x04] = 3, [0x05] = 4,
    [0x06] = 5, [0x07] = 6, [0x08] = 7, [0x09] = 8,
    [0x0A] = 9, [0x0B] = 0
};
local CAPTURE_KEYS = T{
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    KEY_BACK, KEY_DELETE
};


local function parse_bind_combo(combo)
    if (combo == nil or combo == '') then
        return nil;
    end

    local mods, key = combo:match('^([!%^@#+%$]*)(.+)$');
    if (key == nil or key == '') then
        return nil;
    end

    local dik = 0;
    local kb = AshitaCore:GetInputManager():GetKeyboard();
    if (kb ~= nil) then
        local ok, value = pcall(function ()
            return kb:S2D(key);
        end);
        if (ok and value ~= nil) then
            dik = value;
        end
    end

    return T{
        combo        = combo,
        key          = key,
        dik          = dik,
        down         = true,
        alt          = mods:find('!', 1, true) ~= nil,
        apps         = mods:find('#', 1, true) ~= nil,
        ctrl         = mods:find('^', 1, true) ~= nil,
        shift        = mods:find('+', 1, true) ~= nil,
        win          = mods:find('@', 1, true) ~= nil,
        input_closed = false,
        input_open   = mods:find('$', 1, true) ~= nil,
        command      = '',
    };
end


local function bind_matches(a, b)
    return a.dik == b.dik
        and a.down == b.down
        and a.alt == b.alt
        and a.apps == b.apps
        and a.ctrl == b.ctrl
        and a.shift == b.shift
        and a.win == b.win
        and a.input_closed == b.input_closed
        and a.input_open == b.input_open;
end


local function upsert_bind(bind)
    local idx = ahprice.binds:find_if(function (v)
        return bind_matches(v, bind);
    end);

    if (idx ~= nil) then
        ahprice.binds[idx] = bind;
        return;
    end

    ahprice.binds:append(bind);
end


local function remove_bind(bind)
    local idx = ahprice.binds:find_if(function (v)
        return bind_matches(v, bind);
    end);

    if (idx ~= nil) then
        ahprice.binds:remove(idx);
    end
end


local function parse_bind_args(args)
    if (#args < 3) then
        return nil;
    end

    local bind = parse_bind_combo(args[2]);
    if (bind == nil) then
        return nil;
    end

    local cmd_idx = 3;
    if (args[3]:any('down', 'up')) then
        bind.down = args[3]:ieq('down');
        cmd_idx = 4;
    end

    if (#args < cmd_idx) then
        return nil;
    end

    bind.command = args:concat(' ', cmd_idx);
    return bind;
end


local function load_default_binds()
    local path = ('%s\\scripts\\default.txt'):fmt(AshitaCore:GetInstallPath());
    local f = io.open(path, 'r');
    if (f == nil) then
        return;
    end

    for line in f:lines() do
        line = line:trim();
        if (line ~= '' and line:sub(1, 1) ~= '#') then
            local args = line:args();
            if (#args >= 3 and args[1]:ieq('/bind') and (not args[2]:any('list', 'block', 'silent'))) then
                local bind = parse_bind_args(args);
                if (bind ~= nil) then
                    upsert_bind(bind);
                end
            end
        end
    end

    f:close();
end


local function bind_can_fire_without_chat(bind)
    return bind.dik ~= nil and bind.dik ~= 0 and (not bind.input_open);
end


local function apply_binds(enabled)
    local kb = AshitaCore:GetInputManager():GetKeyboard();
    if (kb == nil) then
        return;
    end

    local silent = kb:GetSilentBinds();
    kb:SetSilentBinds(true);

    ahprice.binds:each(function (bind)
        if (not bind_can_fire_without_chat(bind)) then
            return;
        end

        if (enabled) then
            kb:Bind(bind.dik, bind.down, bind.alt, bind.apps, bind.ctrl, bind.shift, bind.win, bind.input_closed, bind.input_open, bind.command);
        else
            kb:Unbind(bind.dik, bind.down, bind.alt, bind.apps, bind.ctrl, bind.shift, bind.win, bind.input_closed, bind.input_open);
        end
    end);

    kb:SetSilentBinds(silent);
end


load_default_binds();


local function read_u32(addr)
    if (addr == nil or addr == 0) then
        return nil;
    end

    local ok, value = pcall(ashita.memory.read_uint32, addr);
    if (not ok) then
        return nil;
    end

    return value;
end


local function get_price_obj()
    if (ffxi == 0) then
        ffxi = ashita.memory.get_base('FFXiMain.dll') or 0;
        if (ffxi == 0) then
            return nil;
        end
    end

    local obj = read_u32(ffxi + STATIC_ADDR);
    if (obj == nil or obj == 0) then
        return nil;
    end

    return obj;
end


local function set_price(price)
    local obj = get_price_obj();
    if (obj == nil) then
        return false;
    end

    local ok = pcall(ashita.memory.write_uint32, obj + PRICE_OFFSET, price);
    return ok == true;
end


local function normalize_menu_name(name)
    if (name == nil) then
        return '';
    end

    return name:gsub('%z', ''):gsub('^menu%s+', ''):trim():lower();
end


local function get_open_menu_names()
    local names = T{ };
    local ptr = AshitaCore:GetPointerManager():Get('menu');
    if (ptr == 0) then
        return names;
    end

    ptr = read_u32(ptr);
    if (ptr == nil or ptr == 0) then
        return names;
    end

    ptr = read_u32(ptr);
    if (ptr == nil or ptr == 0) then
        return names;
    end

    T{ 0x00, 0x04 }:each(function (off)
        local header = read_u32(ptr + off);
        if (header == nil or header == 0) then
            return;
        end

        local ok, raw = pcall(ashita.memory.read_string, header + 0x46, 16);
        if (not ok or raw == nil) then
            return;
        end

        local name = normalize_menu_name(raw);
        if (name ~= '') then
            names:append(name);
        end
    end);

    return names;
end


local function menu_names_are_ah(names)
    local found = false;
    names:each(function (name)
        if (name:match('auc') ~= nil or name:match('comyn') ~= nil) then
            found = true;
        end
    end);
    return found;
end


local function is_game_menu_open()
    local ok, flag = pcall(function ()
        return AshitaCore:GetMemoryManager():GetTarget():GetIsMenuOpen();
    end);
    return ok and flag ~= nil and flag ~= 0;
end


local function is_ah_open()
    local names = get_open_menu_names();
    local open = false;

    if (menu_names_are_ah(names)) then
        ahprice.session = true;
        open = true;
    elseif (ahprice.session) then
        if (#names > 0 or is_game_menu_open()) then
            open = true;
        else
            ahprice.session = false;
        end
    end

    local signature = names:join('|');
    if (ahprice.session and signature ~= ahprice.menus) then
        ahprice.buffer = '';
    end
    ahprice.menus = signature;

    return open;
end


local function is_chat_open()
    return AshitaCore:GetChatManager():IsInputOpen() ~= 0;
end


local function set_bind_block(enabled)
    if (enabled) then
        apply_binds(false);
        ahprice.bind_block = true;
        return;
    end

    if (ahprice.bind_block) then
        apply_binds(true);
        ahprice.bind_block = false;
    end
end


local function apply_buffer()
    local price = tonumber(ahprice.buffer) or 0;
    if (price > MAX_PRICE) then
        price = MAX_PRICE;
        ahprice.buffer = tostring(price);
    end

    set_price(price);
end


local function sync_capture_state()
    local ah_open = is_ah_open();
    local active = (not is_chat_open()) and ah_open;
    set_bind_block(active);

    if (active ~= ahprice.active) then
        ahprice.active = active;
        ahprice.buffer = '';
    end

    return active;
end


local function is_capture_key(key)
    return DIGITS[key] ~= nil or key == KEY_BACK or key == KEY_DELETE;
end


local function handle_capture_key(key)
    if (key == KEY_DELETE) then
        ahprice.buffer = '';
        apply_buffer();
        return;
    end

    if (key == KEY_BACK) then
        if (#ahprice.buffer > 0) then
            ahprice.buffer = ahprice.buffer:sub(1, -2);
            apply_buffer();
        end
        return;
    end

    local digit = DIGITS[key];
    if (digit == nil or #ahprice.buffer >= MAX_DIGITS) then
        return;
    end

    ahprice.buffer = ahprice.buffer .. tostring(digit);
    apply_buffer();
end


ashita.events.register('packet_in', 'packet_in_cb', function (e)
    if (e.id == 0x04C) then
        ahprice.session = true;
        return;
    end

    if (e.id == 0x00A) then
        ahprice.session = false;
        ahprice.active = false;
        ahprice.buffer = '';
        ahprice.menus = '';
        set_bind_block(false);
    end
end);


ashita.events.register('command', 'command_cb', function (e)
    local args = e.command:args();
    if (#args == 0) then
        return;
    end

    if (args[1]:ieq('/bind') and #args >= 3 and (not args[2]:any('list', 'block', 'silent'))) then
        local bind = parse_bind_args(args);
        if (bind ~= nil) then
            upsert_bind(bind);
        end
        return;
    end

    if (not args[1]:ieq('/unbind')) then
        return;
    end

    if (#args >= 2 and args[2]:ieq('all')) then
        ahprice.binds = T{ };
        return;
    end

    if (#args >= 2) then
        local bind = parse_bind_combo(args[2]);
        if (bind == nil) then
            return;
        end

        if (#args >= 3 and args[3]:any('down', 'up')) then
            bind.down = args[3]:ieq('down');
        end

        remove_bind(bind);
    end
end);


ashita.events.register('unload', 'unload_cb', function ()
    set_bind_block(false);
end);


ashita.events.register('d3d_present', 'present_cb', function ()
    sync_capture_state();
end);


ashita.events.register('key_data', 'key_data_cb', function (e)
    if (not sync_capture_state()) then
        return;
    end

    if (not is_capture_key(e.key)) then
        return;
    end

    if (e.down) then
        handle_capture_key(e.key);
    end

    e.blocked = true;
end);


ashita.events.register('key_state', 'key_state_cb', function (e)
    if (not ahprice.active) then
        return;
    end

    local ptr = ffi.cast('uint8_t*', e.data_raw);
    CAPTURE_KEYS:each(function (key)
        if (ptr[key] ~= 0) then
            ptr[key] = 0;
        end
    end);
end);
