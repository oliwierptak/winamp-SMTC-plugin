#include "WinampIPC.h"

#include "SMTC.h"

WinampIPC* WinampIPC::active_ = nullptr;

std::string WinampIPC::GetTitle()
{
    char b[1024]{};
    if (hwnd_)
    {
        GetWindowTextA(hwnd_, b, 1024);
    }

    return b;
}

int WinampIPC::GetStatus()
{
    return hwnd_ ? static_cast<int>(SendMessage(hwnd_, WM_USER, 0, 104)) : 0;
}

int WinampIPC::GetPositionMs()
{
    return hwnd_ ? static_cast<int>(SendMessage(hwnd_, WM_USER, 0, 105)) : 0;
}

int WinampIPC::GetLengthMs()
{
    return hwnd_ ? static_cast<int>(SendMessage(hwnd_, WM_USER, 1, 105)) * 1000 : 0;
}

void WinampIPC::SeekToMs(int positionMs)
{
    if (hwnd_)
    {
        SendMessage(hwnd_, WM_USER, static_cast<WPARAM>(positionMs), 106);
    }
}

void WinampIPC::Play()
{
    if (hwnd_)
    {
        PostMessage(hwnd_, WM_COMMAND, 40045, 0);
    }
}

void WinampIPC::Pause()
{
    if (hwnd_)
    {
        PostMessage(hwnd_, WM_COMMAND, 40046, 0);
    }
}

void WinampIPC::Stop()
{
    if (hwnd_)
    {
        PostMessage(hwnd_, WM_COMMAND, 40047, 0);
    }
}

void WinampIPC::Previous()
{
    if (hwnd_)
    {
        PostMessage(hwnd_, WM_COMMAND, 40044, 0);
    }
}

void WinampIPC::Next()
{
    if (hwnd_)
    {
        PostMessage(hwnd_, WM_COMMAND, 40048, 0);
    }
}

void CALLBACK WinampIPC::TimerProc(HWND, UINT, UINT_PTR id, DWORD)
{
    if (active_ && active_->timer_ == id)
    {
        active_->Poll();
    }
}

void WinampIPC::StartTimer(SMTC* s)
{
    smtc_ = s;
    active_ = this;
    timer_ = SetTimer(nullptr, 0, 500, TimerProc);
}

void WinampIPC::StopTimer()
{
    if (timer_)
    {
        KillTimer(nullptr, timer_);
    }

    timer_ = 0;
    if (active_ == this)
    {
        active_ = nullptr;
    }

    smtc_ = nullptr;
}

void WinampIPC::Poll()
{
    if (!smtc_)
    {
        return;
    }

    auto t = GetTitle();
    auto s = GetStatus();

    if (t != lastTitle_ || s != lastStatus_)
    {
        lastTitle_ = t;
        lastStatus_ = s;
        smtc_->Update(t, s, GetPositionMs(), GetLengthMs());
    }
}
