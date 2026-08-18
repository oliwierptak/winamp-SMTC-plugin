#pragma once

#include <windows.h>

#include <string>

class SMTC;

class WinampIPC {
public:
    void Attach(HWND h)
    {
        hwnd_ = h;
    }

    void Detach()
    {
        hwnd_ = nullptr;
    }

    std::wstring GetTitle();
    int GetStatus();
    int GetPositionMs();
    int GetLengthMs();
    void SeekToMs(int positionMs);
    std::string GetCurrentFilePath();

    void Play();
    void Pause();
    void Stop();
    void Previous();
    void Next();

    void StartTimer(SMTC*);
    void StopTimer();

private:
    HWND hwnd_ = nullptr;
    UINT_PTR timer_ = 0;
    SMTC* smtc_ = nullptr;
    std::wstring lastTitle_;
    int lastStatus_ = -1;

    void Poll();
    static void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD);
    static WinampIPC* active_;
};
