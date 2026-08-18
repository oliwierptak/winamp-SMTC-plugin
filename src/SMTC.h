#pragma once

#include <windows.h>

#include <string>

class WinampIPC;

class SMTC {
public:
    bool Initialize(HWND, WinampIPC*);
    void Shutdown();
    void Update(const std::wstring&, int, int, int, const std::string&);

private:
    void* controls_ = nullptr;
    void* updater_ = nullptr;
    WinampIPC* ipc_ = nullptr;
    std::string lastFilePath_;
};
