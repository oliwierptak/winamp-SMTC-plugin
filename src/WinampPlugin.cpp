#include "WinampPlugin.h"

#include "WinampIPC.h"
#include "SMTC.h"

static HWND g_winamp = nullptr;
static WinampIPC g_ipc;
static SMTC g_smtc;

static int PluginInit()
{
    g_winamp = FindWindowA("Winamp v1.x", nullptr);
    if (!g_winamp)
    {
        return 0;
    }

    g_ipc.Attach(g_winamp);
    g_smtc.Initialize(g_winamp, &g_ipc);
    g_ipc.StartTimer(&g_smtc);
    return 0;
}

static void PluginConfig()
{
    MessageBoxA(nullptr, "Windows System Media Transport Controls by Oliwier Ptak, 2026", "WinampSMTC", MB_OK | MB_ICONINFORMATION);
}

static void PluginQuit()
{
    g_ipc.StopTimer();
    g_smtc.Shutdown();
    g_ipc.Detach();
    g_winamp = nullptr;
}

static winampGeneralPurposePlugin g_plugin{
    GPPHDR_VER,
    const_cast<char*>("Windows System Media Transport Controls by Oliwier Ptak"),
    PluginInit,
    PluginConfig,
    PluginQuit};

extern "C" __declspec(dllexport) winampGeneralPurposePlugin* winampGetGeneralPurposePlugin()
{
    return &g_plugin;
}

extern "C" __declspec(dllexport) void winampUninstallPlugin()
{
}
