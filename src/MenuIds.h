#pragma once
#include "Common.h"

namespace Glow {

enum : UINT {
    WM_TRAY_CALLBACK = WM_APP + 1,

    ID_PRESET_BASE   = 0x2000,   // + preset index (24)
    ID_DEVICE_BASE   = 0x3000,   // + device index
    ID_POSITION_BASE = 0x3400,   // + OrbPosition
    ID_STATE_BASE    = 0x3500,   // + state mode
    ID_GAIN_BASE     = 0x3600,   // + gain preset index
    ID_SIZE_BASE     = 0x3700,   // + size delta index
    ID_MONITOR_BASE  = 0x3800,   // + monitor index

    ID_TOGGLE_PARTICLES   = 0x4001,
    ID_TOGGLE_CLICKTHROUGH = 0x4002,
    ID_TOGGLE_TOPMOST      = 0x4003,
    ID_TOGGLE_PAUSE        = 0x4004,
    ID_TOGGLE_CAPTURE      = 0x4005,
    ID_TOGGLE_OPAQUE       = 0x4006,
    ID_NEXT_PRESET         = 0x4007,
    ID_SETTINGS            = 0x4008,
    ID_ABOUT               = 0x4009,
    ID_OPEN_CONFIG         = 0x400A,
    ID_RESET               = 0x400B,
    ID_EXIT                = 0x400C,

    // Settings window controls
    IDS_PRESET_COMBO  = 0x5001,
    IDS_DEVICE_COMBO  = 0x5002,
    IDS_POSITION_COMBO = 0x5003,
    IDS_STATE_COMBO   = 0x5004,
    IDS_MONITOR_COMBO = 0x5005,
    IDS_SLIDER_BASE   = 0x5100,
    IDS_CHECK_BASE    = 0x5300,
    IDS_BUTTON_RESET  = 0x5401,
    IDS_BUTTON_CLOSE  = 0x5402,
    IDS_BUTTON_FOLDER = 0x5403,
};

} // namespace Glow
