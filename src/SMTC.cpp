#include "SMTC.h"

#include "CoverArt.h"
#include "WinampIPC.h"

#include <roapi.h>
#include <shcore.h>
#include <shobjidl_core.h>
#include <SystemMediaTransportControlsInterop.h>
#include <windows.foundation.h>
#include <windows.media.h>
#include <windows.storage.streams.h>
#include <winstring.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <cstdio>



namespace {

template <typename T>
void SafeRelease(T*& value)
{
    if (value)
    {
        value->Release();
        value = nullptr;
    }
}

std::wstring ToWide(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }

    int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    UINT codePage = CP_UTF8;

    if (length <= 0)
    {
        codePage = CP_ACP;
        length = MultiByteToWideChar(codePage, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    }

    if (length <= 0)
    {
        return {};
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(codePage, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::wstring Trim(std::wstring value)
{
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
    {
        return {};
    }

    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

void ParseTrackInfo(const std::string& rawTitle, std::wstring& artist, std::wstring& title)
{
    artist.clear();
    title = Trim(ToWide(rawTitle));

    const std::wstring suffix = L" - Winamp";
    if (title.size() > suffix.size())
    {
        const auto suffixPos = title.rfind(suffix);
        if (suffixPos != std::wstring::npos && suffixPos == title.size() - suffix.size())
        {
            title = Trim(title.substr(0, suffixPos));
        }
    }

    const auto firstSeparator = title.find(L" - ");
    if (firstSeparator == std::wstring::npos)
    {
        return;
    }

    std::wstring left = Trim(title.substr(0, firstSeparator));
    std::wstring right = Trim(title.substr(firstSeparator + 3));
    if (left.empty() || right.empty())
    {
        return;
    }

    size_t digits = 0;
    while (digits < left.size() && left[digits] >= L'0' && left[digits] <= L'9')
    {
        ++digits;
    }

    if (digits > 0)
    {
        while (digits < left.size() && (left[digits] == L'.' || left[digits] == L')' || left[digits] == L' ' || left[digits] == L'-'))
        {
            ++digits;
        }

        left = Trim(left.substr(digits));
    }

    if (!left.empty() && !right.empty())
    {
        artist = left;
        title = right;
    }
}

bool CreateHString(const std::wstring& value, HSTRING& out)
{
    return SUCCEEDED(WindowsCreateString(value.c_str(), static_cast<UINT32>(value.size()), &out));
}

bool CreateStreamReferenceFromFile(const std::wstring& path, Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStreamReference>& streamRef)
{
    Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStream> stream;
    if (FAILED(CreateRandomAccessStreamOnFile(
            path.c_str(),
            ABI::Windows::Storage::FileAccessMode_Read,
            IID_PPV_ARGS(&stream))))
    {
        return false;
    }

    Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStreamReferenceStatics> streamRefStatics;
    if (FAILED(::RoGetActivationFactory(
            Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Storage_Streams_RandomAccessStreamReference).Get(),
            IID_PPV_ARGS(&streamRefStatics))))
    {
        return false;
    }

    return SUCCEEDED(streamRefStatics->CreateFromStream(stream.Get(), &streamRef));
}

std::wstring ToWideFilePath(const std::string& path)
{
    if (path.empty())
    {
        return {};
    }

    const int length = MultiByteToWideChar(CP_ACP, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
    if (length <= 0)
    {
        return {};
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), static_cast<int>(path.size()), result.data(), length);
    return result;
}

void UpdateThumbnail(
    ABI::Windows::Media::ISystemMediaTransportControlsDisplayUpdater* updater,
    const std::string& filePath)
{
    const std::wstring trackPath = ToWideFilePath(filePath);

    std::wstring imagePath;
    if (!CoverArt::Resolve(trackPath, imagePath))
    {
        updater->put_Thumbnail(nullptr);
        return;
    }

    Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStreamReference> streamRef;
    if (CreateStreamReferenceFromFile(imagePath, streamRef))
    {
        updater->put_Thumbnail(streamRef.Get());
    }
}

void HandleButtonPressed(WinampIPC* ipc, ABI::Windows::Media::SystemMediaTransportControlsButton button)
{
    if (!ipc)
    {
        return;
    }

    switch (button)
    {
    case ABI::Windows::Media::SystemMediaTransportControlsButton_Play:
        ipc->Play();
        break;
    case ABI::Windows::Media::SystemMediaTransportControlsButton_Pause:
        ipc->Pause();
        break;
    case ABI::Windows::Media::SystemMediaTransportControlsButton_Stop:
        ipc->Stop();
        break;
    case ABI::Windows::Media::SystemMediaTransportControlsButton_FastForward:
    {
        const int current = ipc->GetPositionMs();
        const int length = ipc->GetLengthMs();
        int target = current + 5000;
        if (length > 0 && target > length)
        {
            target = length;
        }

        ipc->SeekToMs(target);
        break;
    }
    case ABI::Windows::Media::SystemMediaTransportControlsButton_Rewind:
    {
        const int current = ipc->GetPositionMs();
        ipc->SeekToMs((current > 5000) ? (current - 5000) : 0);
        break;
    }
    case ABI::Windows::Media::SystemMediaTransportControlsButton_Previous:
        ipc->Previous();
        break;
    case ABI::Windows::Media::SystemMediaTransportControlsButton_Next:
        ipc->Next();
        break;
    default:
        break;
    }
}

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

bool AcquireSmtcInterfaces(
    HWND hwnd,
    ComPtr<ABI::Windows::Media::ISystemMediaTransportControls>& transportControls,
    ComPtr<ABI::Windows::Media::ISystemMediaTransportControlsDisplayUpdater>& updater)
{
    ComPtr<ISystemMediaTransportControlsInterop> interop;
    if (FAILED(::RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_Media_SystemMediaTransportControls).Get(),
            IID_PPV_ARGS(&interop))))
    {
        return false;
    }

    if (FAILED(interop->GetForWindow(hwnd, IID_PPV_ARGS(&transportControls))))
    {
        return false;
    }

    return SUCCEEDED(transportControls->get_DisplayUpdater(&updater));
}

void EnableButtons(ABI::Windows::Media::ISystemMediaTransportControls* transportControls)
{
    transportControls->put_IsEnabled(true);
    transportControls->put_IsPlayEnabled(true);
    transportControls->put_IsPauseEnabled(true);
    transportControls->put_IsStopEnabled(true);
    transportControls->put_IsFastForwardEnabled(true);
    transportControls->put_IsRewindEnabled(true);
    transportControls->put_IsPreviousEnabled(true);
    transportControls->put_IsNextEnabled(true);
}

bool RegisterHandlers(
    ABI::Windows::Media::ISystemMediaTransportControls* transportControls,
    ABI::Windows::Media::ISystemMediaTransportControls2* transportControlsV2,
    WinampIPC* ipc)
{
    using ButtonPressedHandler = __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgs;
    using PlaybackPositionChangeHandler = __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CPlaybackPositionChangeRequestedEventArgs;

    auto buttonHandler = Microsoft::WRL::Callback<ButtonPressedHandler>(
        [ipc](ABI::Windows::Media::ISystemMediaTransportControls*, ABI::Windows::Media::ISystemMediaTransportControlsButtonPressedEventArgs* args) -> HRESULT {
            ABI::Windows::Media::SystemMediaTransportControlsButton button{};
            if (args && SUCCEEDED(args->get_Button(&button)))
            {
                HandleButtonPressed(ipc, button);
            }
            return S_OK;
        });

    EventRegistrationToken token{};
    if (FAILED(transportControls->add_ButtonPressed(buttonHandler.Get(), &token)))
    {
        return false;
    }

    auto positionHandler = Microsoft::WRL::Callback<PlaybackPositionChangeHandler>(
        [ipc](ABI::Windows::Media::ISystemMediaTransportControls*, ABI::Windows::Media::IPlaybackPositionChangeRequestedEventArgs* args) -> HRESULT {
            ABI::Windows::Foundation::TimeSpan requested{};
            if (ipc && args && SUCCEEDED(args->get_RequestedPlaybackPosition(&requested)))
            {
                ipc->SeekToMs(static_cast<int>(requested.Duration / 10000));
            }
            return S_OK;
        });

    return SUCCEEDED(transportControlsV2->add_PlaybackPositionChangeRequested(positionHandler.Get(), &token));
}

void UpdatePlaybackStatus(
    ABI::Windows::Media::ISystemMediaTransportControls* transportControls,
    int status)
{
    ABI::Windows::Media::MediaPlaybackStatus playbackStatus = ABI::Windows::Media::MediaPlaybackStatus_Stopped;
    if (status == 1)
    {
        playbackStatus = ABI::Windows::Media::MediaPlaybackStatus_Playing;
    }
    else if (status == 3)
    {
        playbackStatus = ABI::Windows::Media::MediaPlaybackStatus_Paused;
    }

    transportControls->put_PlaybackStatus(playbackStatus);
}

void UpdateTimeline(
    ABI::Windows::Media::ISystemMediaTransportControls2* transportControlsV2,
    int positionMs,
    int lengthMs)
{
    if (!transportControlsV2 || lengthMs <= 0)
    {
        return;
    }

    Microsoft::WRL::ComPtr<IInspectable> timelineInspectable;
    if (FAILED(::RoActivateInstance(
            Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Media_SystemMediaTransportControlsTimelineProperties).Get(),
            &timelineInspectable)) || !timelineInspectable)
    {
        return;
    }

    Microsoft::WRL::ComPtr<ABI::Windows::Media::ISystemMediaTransportControlsTimelineProperties> timeline;
    if (FAILED(timelineInspectable.As(&timeline)) || !timeline)
    {
        return;
    }

    ABI::Windows::Foundation::TimeSpan start{};
    ABI::Windows::Foundation::TimeSpan end{};
    ABI::Windows::Foundation::TimeSpan position{};

    end.Duration = static_cast<INT64>(lengthMs) * 10000;
    position.Duration = static_cast<INT64>(positionMs) * 10000;
    if (position.Duration < 0)
    {
        position.Duration = 0;
    }
    if (position.Duration > end.Duration)
    {
        position.Duration = end.Duration;
    }

    timeline->put_StartTime(start);
    timeline->put_EndTime(end);
    timeline->put_MinSeekTime(start);
    timeline->put_MaxSeekTime(end);
    timeline->put_Position(position);
    transportControlsV2->UpdateTimelineProperties(timeline.Get());
}

void UpdateMetadata(
    ABI::Windows::Media::ISystemMediaTransportControlsDisplayUpdater* updater,
    const std::string& windowTitle)
{
    std::wstring artist;
    std::wstring title;
    ParseTrackInfo(windowTitle, artist, title);

    Microsoft::WRL::ComPtr<ABI::Windows::Media::IMusicDisplayProperties> music;
    if (FAILED(updater->get_MusicProperties(&music)) || !music)
    {
        return;
    }

    HSTRING titleText = nullptr;
    if (CreateHString(title, titleText))
    {
        music->put_Title(titleText);
        WindowsDeleteString(titleText);
    }

    HSTRING artistText = nullptr;
    if (CreateHString(artist, artistText))
    {
        music->put_Artist(artistText);
        WindowsDeleteString(artistText);
    }
}

} // namespace

bool SMTC::Initialize(HWND hwnd, WinampIPC* ipc)
{
    ipc_ = ipc;

    SetCurrentProcessExplicitAppUserModelID(L"Winamp");

    const HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    ComPtr<ABI::Windows::Media::ISystemMediaTransportControls> transportControls;
    ComPtr<ABI::Windows::Media::ISystemMediaTransportControlsDisplayUpdater> updater;
    if (!AcquireSmtcInterfaces(hwnd, transportControls, updater))
    {
        return false;
    }

    EnableButtons(transportControls.Get());

    ComPtr<ABI::Windows::Media::ISystemMediaTransportControls2> transportControlsV2;
    if (FAILED(transportControls.As(&transportControlsV2)) || !transportControlsV2)
    {
        return false;
    }

    if (!RegisterHandlers(transportControls.Get(), transportControlsV2.Get(), ipc_))
    {
        return false;
    }

    updater->put_Type(ABI::Windows::Media::MediaPlaybackType_Music);
    updater->Update();

    controls_ = transportControls.Detach();
    updater_ = updater.Detach();
    return true;
}

void SMTC::Shutdown()
{
    SafeRelease(reinterpret_cast<ABI::Windows::Media::ISystemMediaTransportControlsDisplayUpdater*&>(updater_));
    SafeRelease(reinterpret_cast<ABI::Windows::Media::ISystemMediaTransportControls*&>(controls_));
    ipc_ = nullptr;
}

void SMTC::Update(const std::string& windowTitle, int status, int positionMs, int lengthMs, const std::string& filePath)
{
    auto* transportControls = reinterpret_cast<ABI::Windows::Media::ISystemMediaTransportControls*>(controls_);
    auto* updater = reinterpret_cast<ABI::Windows::Media::ISystemMediaTransportControlsDisplayUpdater*>(updater_);
    if (!transportControls || !updater)
    {
        return;
    }

    Microsoft::WRL::ComPtr<ABI::Windows::Media::ISystemMediaTransportControls2> transportControlsV2;
    transportControls->QueryInterface(IID_PPV_ARGS(&transportControlsV2));

    UpdatePlaybackStatus(transportControls, status);
    UpdateTimeline(transportControlsV2.Get(), positionMs, lengthMs);
    UpdateMetadata(updater, windowTitle);

    if (filePath != lastFilePath_)
    {
        lastFilePath_ = filePath;
        UpdateThumbnail(updater, filePath);
    }

    updater->Update();
}
