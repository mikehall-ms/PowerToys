#pragma once
#include <windows.h>

// Magnifier Settings Structure
struct MagnifierSettings
{
    float magnificationLevel = 2.0f;
    bool win = true;
    bool ctrl = false;
    bool alt = true; 
    bool shift = false;
    int vkCode = VK_OEM_COMMA; // Default: Win+Alt+Comma
};

// Mouse Utility API Functions
void MagnifierApplySettings(MagnifierSettings settings);
void MagnifierSwitch();
void MagnifierDisable();
bool MagnifierIsEnabled();
int MagnifierMain(HMODULE hInstance, MagnifierSettings settings);

// The PowerToy name that will be shown in the settings.
const static wchar_t* MODULE_NAME = L"Magnifier";
// Add a description that will we shown in the module settings page.
const static wchar_t* MODULE_DESC = L"Screen magnification utility";