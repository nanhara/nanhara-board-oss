#pragma once

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>

#include <array>
#include <memory>
#include <system_error>
#include <thread>

#include "update_version.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

namespace board_update
{
inline constexpr wchar_t releases_url[] = L"https://github.com/nanhara/nanhara-board-oss/releases";

inline bool preview_is_available() noexcept
{
    using internet_handle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;
    const internet_handle session(
        WinHttpOpen(L"NanharaBoard/preview", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), WinHttpCloseHandle);
    if (!session || !WinHttpSetTimeouts(session.get(), 2500, 2500, 3000, 3000))
        return false;
    const internet_handle connection(
        WinHttpConnect(session.get(), L"raw.githubusercontent.com", INTERNET_DEFAULT_HTTPS_PORT, 0),
        WinHttpCloseHandle);
    if (!connection)
        return false;
    const internet_handle request(
        WinHttpOpenRequest(connection.get(), L"GET",
                           L"/nanhara/nanhara-board-oss/main/releases/windows-preview.txt",
                           nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE),
        WinHttpCloseHandle);
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!request || !WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY,
                                     &redirect_policy, static_cast<DWORD>(sizeof(redirect_policy))) ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr))
        return false;
    DWORD status{}, size = static_cast<DWORD>(sizeof(status));
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) ||
        status != 200)
        return false;
    std::array<char, 64> body{};
    DWORD used{}, received{};
    while (used < static_cast<DWORD>(body.size()))
    {
        if (!WinHttpReadData(request.get(), body.data() + used,
                             static_cast<DWORD>(body.size()) - used, &received))
            return false;
        if (received == 0)
            return is_newer_preview(std::string_view(body.data(), used), current_preview);
        used += received;
    }
    return false;
}

inline void check_on_startup(HWND window, UINT update_message)
{
    try
    {
        std::thread([window, update_message] {
            if (!preview_is_available())
                return;
            DWORD owner{};
            GetWindowThreadProcessId(window, &owner);
            if (owner == GetCurrentProcessId())
                PostMessageW(window, update_message, 0, 0);
        }).detach();
    }
    catch (const std::system_error&)
    {
        // Update checks are optional when the board starts offline or under load.
    }
}
}
