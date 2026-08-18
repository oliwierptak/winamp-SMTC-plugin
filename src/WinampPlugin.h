#pragma once
#include <windows.h>
struct winampGeneralPurposePlugin {
    int version;
    char* description;
    int (*init)();
    void (*config)();
    void (*quit)();
};
constexpr int GPPHDR_VER = 0x10;
extern "C" __declspec(dllexport) winampGeneralPurposePlugin* winampGetGeneralPurposePlugin();
extern "C" __declspec(dllexport) void winampUninstallPlugin();
