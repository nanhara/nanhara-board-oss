#include "resource.h"
#include "update_check.h"

#include <windows.h>
#include <commctrl.h>
#include <dcomp.h>
#include <inkpresenterdesktop.h>

#include <winrt/base.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.Inking.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "windowsapp.lib")

namespace inking = winrt::Windows::UI::Input::Inking;
namespace core = winrt::Windows::UI::Core;
namespace devices = winrt::Windows::Devices::Input;
namespace foundation = winrt::Windows::Foundation;
namespace colors = winrt::Windows::UI;

namespace
{
constexpr wchar_t kMainWindowClass[] = L"NanharaBoardInkDesktopHostWindow";
constexpr wchar_t kCanvasWindowClass[] = L"NanharaBoardInkDesktopHostCanvas";
constexpr wchar_t kWindowTitle[] = L"南荒白板 · 多页书写预览 0.2.0-alpha.10";
constexpr std::size_t kMaxUndoHistoryEntries = 512;

constexpr COLORREF kBrandInk = RGB(23, 59, 52);
constexpr COLORREF kBrandInkDeep = RGB(16, 43, 38);
constexpr COLORREF kBrandPaperBright = RGB(251, 248, 240);
constexpr COLORREF kBrandStage = RGB(221, 215, 202);
constexpr COLORREF kBrandDock = RGB(235, 229, 217);
constexpr COLORREF kBrandCinnabar = RGB(223, 96, 57);
constexpr COLORREF kBrandCinnabarDark = RGB(184, 68, 40);
constexpr COLORREF kBrandBlueGray = RGB(95, 126, 130);
constexpr COLORREF kBrandMuted = RGB(102, 116, 111);
constexpr COLORREF kBrandLine = RGB(199, 194, 183);
constexpr COLORREF kInkBlack = RGB(27, 27, 31);
constexpr COLORREF kInkCinnabar = RGB(166, 60, 38);
constexpr COLORREF kInkBlueGray = RGB(63, 102, 109);
constexpr COLORREF kCanvasChalkboard = RGB(39, 64, 33);
constexpr COLORREF kCanvasGraphite = RGB(48, 54, 51);
constexpr COLORREF kCanvasWarmPaper = RGB(244, 239, 228);
constexpr COLORREF kCanvasSoftWhite = RGB(246, 247, 243);
constexpr COLORREF kCanvasSoftGrid = RGB(238, 241, 236);
constexpr COLORREF kCanvasGridLine = RGB(213, 221, 213);
constexpr COLORREF kChalkWhite = RGB(246, 241, 226);
constexpr COLORREF kChalkCoral = RGB(246, 160, 127);
constexpr COLORREF kChalkYellow = RGB(231, 211, 126);
constexpr COLORREF kChalkBlue = RGB(128, 196, 198);
constexpr float kPenThinSize = 3.5F;
constexpr float kPenMediumSize = 6.0F;
constexpr float kPenThickSize = 10.0F;
constexpr DWORD kInkShutdownTimeoutMs = 5000;
constexpr ULONGLONG kReleasedInkGraceMs = 1000;

constexpr UINT kMessagePointerStarted = WM_APP + 1;
constexpr UINT kMessageStrokeCount = WM_APP + 2;
constexpr UINT kMessageInkError = WM_APP + 3;
constexpr UINT kMessagePageChanged = WM_APP + 4;
constexpr UINT kMessagePreviewAvailable = WM_APP + 5;
constexpr LPARAM kPageChangeFailed = -1;

enum class InkOperationStage : int
{
    Unknown = 0,
    CompositionDevice,
    CompositionTree,
    HostActivation,
    PresenterCreation,
    PresenterRootVisual,
    PresenterSize,
    PresenterConfiguration,
    RuntimeOperation,
    Shutdown,
};

enum ControlId : int
{
    ControlBlack = 1001,
    ControlRed = 1002,
    ControlGreen = 1003,
    ControlBlue = 1004,
    ControlThin = 1011,
    ControlMedium = 1012,
    ControlThick = 1013,
    ControlEraser = 1021,
    ControlUndo = 1022,
    ControlClear = 1023,
    ControlFullscreen = 1024,
    ControlBackground = 1025,
    ControlAddPage = 1031,
    ControlRailAddPage = 1032,
    ControlPreviousPage = 1033,
    ControlNextPage = 1034,
    ControlShowUpdate = 1035,
};

enum class CanvasTheme
{
    Chalkboard,
    Graphite,
    WarmPaper,
    SoftWhite,
    SoftGrid,
};

enum ThemeCommand : UINT
{
    ThemeChalkboard = 2101,
    ThemeGraphite = 2102,
    ThemeWarmPaper = 2103,
    ThemeSoftWhite = 2104,
    ThemeSoftGrid = 2105,
};

constexpr std::array<int, 4> kColorControls{{
    ControlBlack,
    ControlRed,
    ControlGreen,
    ControlBlue,
}};

constexpr std::array<int, 3> kWidthControls{{
    ControlThin,
    ControlMedium,
    ControlThick,
}};

int ScaleForDpi(const int value, const UINT dpi) noexcept
{
    return MulDiv(value, static_cast<int>(dpi), 96);
}

colors::Color ToInkColor(const COLORREF value) noexcept
{
    return colors::Color{
        255,
        GetRValue(value),
        GetGValue(value),
        GetBValue(value),
    };
}

std::wstring FormatHresult(const HRESULT result)
{
    wchar_t systemMessage[512]{};
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(result),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        systemMessage,
        static_cast<DWORD>(std::size(systemMessage)),
        nullptr);

    std::wostringstream text;
    text << L"0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(result);
    if (length > 0)
    {
        std::wstring message(systemMessage, length);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
        {
            message.pop_back();
        }
        text << L" · " << message;
    }
    return text.str();
}

const wchar_t* InkStageName(const InkOperationStage stage) noexcept
{
    switch (stage)
    {
    case InkOperationStage::CompositionDevice:
        return L"创建 DirectComposition 设备";
    case InkOperationStage::CompositionTree:
        return L"创建 DirectComposition 画布";
    case InkOperationStage::HostActivation:
        return L"激活 InkDesktopHost";
    case InkOperationStage::PresenterCreation:
        return L"在 InkDesktopHost 线程创建 InkPresenter";
    case InkOperationStage::PresenterRootVisual:
        return L"在 InkDesktopHost 线程绑定 DirectComposition visual";
    case InkOperationStage::PresenterSize:
        return L"在 InkDesktopHost 线程设置画布尺寸";
    case InkOperationStage::PresenterConfiguration:
        return L"在 InkDesktopHost 线程配置墨迹";
    case InkOperationStage::RuntimeOperation:
        return L"执行墨迹操作";
    case InkOperationStage::Shutdown:
        return L"释放 InkDesktopHost 线程资源";
    case InkOperationStage::Unknown:
    default:
        return L"未知阶段";
    }
}

struct InkWorkCompletion final
{
    explicit InkWorkCompletion(const InkOperationStage initialStage)
        : event(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
          stage(static_cast<int>(initialStage))
    {
        if (event == nullptr)
        {
            winrt::throw_last_error();
        }
    }

    ~InkWorkCompletion()
    {
        CloseHandle(event);
    }

    HANDLE event{};
    std::atomic<HRESULT> result{E_PENDING};
    std::atomic<int> stage{};
};

struct InkWorkItem : winrt::implements<InkWorkItem, IInkHostWorkItem>
{
    InkWorkItem(
        std::function<void()> action,
        const HWND errorTarget,
        const InkOperationStage errorStage = InkOperationStage::RuntimeOperation,
        std::shared_ptr<InkWorkCompletion> completion = nullptr)
        : action_(std::move(action)),
          errorTarget_(errorTarget),
          errorStage_(errorStage),
          completion_(std::move(completion))
    {
    }

    HRESULT __stdcall Invoke() noexcept final
    {
        HRESULT result = S_OK;
        try
        {
            action_();
        }
        catch (...)
        {
            result = winrt::to_hresult();
        }

        if (completion_)
        {
            completion_->result.store(result, std::memory_order_release);
            SetEvent(completion_->event);
        }
        else if (FAILED(result) && IsWindow(errorTarget_))
        {
            PostMessageW(
                errorTarget_,
                kMessageInkError,
                static_cast<WPARAM>(result),
                static_cast<LPARAM>(errorStage_));
        }
        return result;
    }

private:
    std::function<void()> action_;
    HWND errorTarget_{};
    InkOperationStage errorStage_{InkOperationStage::RuntimeOperation};
    std::shared_ptr<InkWorkCompletion> completion_;
};

enum class InkHistoryKind
{
    Added,
    Erased,
    Cleared,
};

struct InkHistoryEntry final
{
    InkHistoryKind kind{InkHistoryKind::Added};
    std::vector<std::uint32_t> strokeIds;
    std::vector<inking::InkStroke> strokeSnapshots;
};

struct InkPage final
{
    inking::InkStrokeContainer strokes{nullptr};
    std::deque<InkHistoryEntry> undoHistory;
};

struct InkPointer final
{
    std::uint32_t id;
    bool down;
    bool awaitingCollection;
    ULONGLONG releasedAt{};
};

struct InkThreadState final
{
    winrt::com_ptr<IInkPresenterDesktop> desktopPresenter;
    inking::InkPresenter presenter{nullptr};
    std::vector<InkPage> pages;
    std::size_t activePage{};
    std::vector<InkPointer> pendingPointers;
    std::unordered_map<std::uint32_t, std::size_t> strokePages;
    std::atomic_bool hasDrawn{};
    bool applyingHistory{};

    void FinishPointer(const std::uint32_t pointerId, const bool canceled)
    {
        const auto pointer = std::find_if(pendingPointers.begin(), pendingPointers.end(),
            [pointerId](const InkPointer& item) { return item.id == pointerId && item.down; });
        if (pointer == pendingPointers.end())
        {
            return;
        }
        if (canceled || !pointer->awaitingCollection)
        {
            pendingPointers.erase(pointer);
        }
        else
        {
            pointer->down = false;
            pointer->releasedAt = GetTickCount64();
        }
    }

    void CollectStroke(const std::uint32_t pointerId)
    {
        const auto pointer = std::find_if(pendingPointers.begin(), pendingPointers.end(),
            [pointerId](const InkPointer& item) { return item.id == pointerId && item.awaitingCollection; });
        if (pointer == pendingPointers.end())
        {
            return;
        }
        if (pointer->down)
        {
            pointer->awaitingCollection = false;
        }
        else
        {
            pendingPointers.erase(pointer);
        }
    }

    bool ReadyToSwitchPage(const ULONGLONG now)
    {
        // A canceled gesture can end without producing a dry stroke. Recover from a missing
        // collection callback after giving released pointers time to finish on the ink thread.
        std::erase_if(pendingPointers, [now](const InkPointer& pointer)
        {
            return !pointer.down && now - pointer.releasedAt >= kReleasedInkGraceMs;
        });
        return pendingPointers.empty();
    }

    void PushHistory(const std::size_t pageIndex, InkHistoryEntry entry)
    {
        if (entry.strokeIds.empty() && entry.strokeSnapshots.empty())
        {
            return;
        }
        auto& undoHistory = pages[pageIndex].undoHistory;
        if (undoHistory.size() >= kMaxUndoHistoryEntries)
        {
            undoHistory.pop_front();
        }
        undoHistory.push_back(std::move(entry));
    }

    void RemapStrokeId(const std::uint32_t previousId, const std::uint32_t currentId)
    {
        if (previousId == currentId)
        {
            return;
        }
        for (auto& entry : pages[activePage].undoHistory)
        {
            for (auto& strokeId : entry.strokeIds)
            {
                if (strokeId == previousId)
                {
                    strokeId = currentId;
                }
            }
        }
    }
};

class InkHistoryReplayGuard final
{
public:
    explicit InkHistoryReplayGuard(InkThreadState& state) : state_(state)
    {
        state_.applyingHistory = true;
    }

    ~InkHistoryReplayGuard()
    {
        state_.applyingHistory = false;
    }

    InkHistoryReplayGuard(const InkHistoryReplayGuard&) = delete;
    InkHistoryReplayGuard& operator=(const InkHistoryReplayGuard&) = delete;

private:
    InkThreadState& state_;
};

class Application final
{
public:
    explicit Application(const HINSTANCE instance) : instance_(instance)
    {
        backgroundBrush_ = CreateSolidBrush(kBrandStage);
        headerBrush_ = CreateSolidBrush(kBrandInk);
        dockBrush_ = CreateSolidBrush(kBrandDock);
        if (backgroundBrush_ == nullptr || headerBrush_ == nullptr || dockBrush_ == nullptr)
        {
            winrt::throw_last_error();
        }
    }

    ~Application()
    {
        ShutdownInk();
        if (titleFont_ != nullptr)
        {
            DeleteObject(titleFont_);
        }
        if (uiFont_ != nullptr)
        {
            DeleteObject(uiFont_);
        }
        if (metaFont_ != nullptr)
        {
            DeleteObject(metaFont_);
        }
        if (backgroundBrush_ != nullptr)
        {
            DeleteObject(backgroundBrush_);
        }
        if (headerBrush_ != nullptr)
        {
            DeleteObject(headerBrush_);
        }
        if (dockBrush_ != nullptr)
        {
            DeleteObject(dockBrush_);
        }
    }

    int Run(const int showCommand)
    {
        RegisterWindowClasses();

        window_ = CreateWindowExW(
            0,
            kMainWindowClass,
            kWindowTitle,
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1400,
            900,
            nullptr,
            nullptr,
            instance_,
            this);
        if (window_ == nullptr)
        {
            winrt::throw_last_error();
        }

        ShowWindow(window_, showCommand);
        UpdateWindow(window_);
        board_update::check_on_startup(window_, kMessagePreviewAvailable);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (message.message == WM_KEYDOWN &&
                (message.hwnd == window_ || IsChild(window_, message.hwnd)) &&
                HandleShortcut(message.wParam, message.lParam))
            {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        ShutdownInk();
        return static_cast<int>(message.wParam);
    }

private:
    bool HandleShortcut(const WPARAM key, const LPARAM flags)
    {
        int command{};
        if (key == VK_F11 || (key == VK_ESCAPE && fullscreen_))
        {
            command = ControlFullscreen;
        }
        else if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            switch (key)
            {
            case VK_RETURN: command = ControlAddPage; break;
            case VK_PRIOR: command = ControlPreviousPage; break;
            case VK_NEXT: command = ControlNextPage; break;
            case 'Z': command = ControlUndo; break;
            default: break;
            }
        }
        if (command == 0)
        {
            return false;
        }
        if ((flags & (1L << 30)) == 0)
        {
            SendMessageW(window_, WM_COMMAND, MAKEWPARAM(command, 0), 0);
        }
        return true;
    }

    static LRESULT CALLBACK MainWindowProc(
        const HWND window,
        const UINT message,
        const WPARAM wParam,
        const LPARAM lParam) noexcept
    {
        Application* application = nullptr;
        if (message == WM_NCCREATE)
        {
            const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            application = static_cast<Application*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
            application->window_ = window;
        }
        else
        {
            application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (application != nullptr)
        {
            try
            {
                return application->HandleMessage(message, wParam, lParam);
            }
            catch (...)
            {
                const HRESULT result = winrt::to_hresult();
                application->ShowFatalError(result);
                return message == WM_CREATE ? -1 : 0;
            }
        }

        return DefWindowProcW(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK CanvasWindowProc(
        const HWND window,
        const UINT message,
        const WPARAM wParam,
        const LPARAM lParam) noexcept
    {
        Application* application = nullptr;
        if (message == WM_NCCREATE)
        {
            const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            application = static_cast<Application*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
        }
        else
        {
            application = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            const HDC context = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            const COLORREF background = application != nullptr
                ? application->canvasBackground_
                : kCanvasChalkboard;
            SetDCBrushColor(context, background);
            FillRect(context, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if (application != nullptr && application->canvasGrid_)
            {
                const int saved = SaveDC(context);
                SelectObject(context, GetStockObject(DC_PEN));
                SetDCPenColor(context, kCanvasGridLine);
                const UINT dpi = GetDpiForWindow(window);
                const int spacing = ScaleForDpi(24, dpi);
                for (int x = spacing; x < client.right; x += spacing)
                {
                    MoveToEx(context, x, 0, nullptr);
                    LineTo(context, x, client.bottom);
                }
                for (int y = spacing; y < client.bottom; y += spacing)
                {
                    MoveToEx(context, 0, y, nullptr);
                    LineTo(context, client.right, y);
                }
                RestoreDC(context, saved);
            }
            EndPaint(window, &paint);
            return 0;
        }

        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    void RegisterWindowClasses()
    {
        appIcon_ = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_NANHARA_BOARD));

        WNDCLASSEXW mainClass{};
        mainClass.cbSize = sizeof(mainClass);
        mainClass.style = CS_HREDRAW | CS_VREDRAW;
        mainClass.lpfnWndProc = MainWindowProc;
        mainClass.hInstance = instance_;
        mainClass.hIcon = appIcon_;
        mainClass.hIconSm = appIcon_;
        mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        mainClass.hbrBackground = backgroundBrush_;
        mainClass.lpszClassName = kMainWindowClass;
        if (RegisterClassExW(&mainClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            winrt::throw_last_error();
        }

        WNDCLASSEXW canvasClass{};
        canvasClass.cbSize = sizeof(canvasClass);
        canvasClass.style = CS_HREDRAW | CS_VREDRAW;
        canvasClass.lpfnWndProc = CanvasWindowProc;
        canvasClass.hInstance = instance_;
        canvasClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        canvasClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        canvasClass.lpszClassName = kCanvasWindowClass;
        if (RegisterClassExW(&canvasClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            winrt::throw_last_error();
        }
    }

    LRESULT HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            CreateUi();
            InitializeInk();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintChrome();
            return 0;

        case WM_DRAWITEM:
            if (DrawToolbarButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam)))
            {
                return TRUE;
            }
            break;

        case WM_GETMINMAXINFO:
        {
            const UINT dpi = window_ != nullptr ? GetDpiForWindow(window_) : 96;
            auto info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = ScaleForDpi(900, dpi);
            info->ptMinTrackSize.y = ScaleForDpi(640, dpi);
            return 0;
        }

        case WM_SIZE:
            LayoutUi();
            return 0;

        case WM_DPICHANGED:
        {
            const auto suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(
                window_,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            ApplyFonts();
            LayoutUi();
            return 0;
        }

        case WM_COMMAND:
            HandleCommand(LOWORD(wParam));
            return 0;

        case kMessagePointerStarted:
            inputSource_ = DeviceName(static_cast<devices::PointerDeviceType>(wParam));
            lastInputX_ = static_cast<int>(LOWORD(lParam));
            lastInputY_ = static_cast<int>(HIWORD(lParam));
            ++inputSequence_;
            RefreshStatus();
            return 0;

        case kMessageStrokeCount:
            strokeCount_ = static_cast<std::uint32_t>(wParam);
            undoDepth_ = static_cast<std::uint32_t>(lParam);
            UpdateCommandAvailability();
            RefreshStatus();
            return 0;

        case kMessageInkError:
            lastInkErrorStage_ = static_cast<InkOperationStage>(lParam);
            inputSource_ = L"原生墨迹错误 " + FormatHresult(static_cast<HRESULT>(wParam));
            RefreshStatus();
            return 0;

        case kMessagePageChanged:
            pageChangePending_ = false;
            if (wParam == 0)
            {
                inputSource_ = lParam == kPageChangeFailed
                    ? L"换页失败"
                    : L"请抬笔，等待笔迹完成后再换页";
            }
            else
            {
                activePage_ = static_cast<std::size_t>(wParam) - 1;
                pageCount_ = static_cast<std::size_t>(lParam);
                inputSource_ = L"等待落笔";
            }
            UpdateCommandAvailability();
            RefreshStatus();
            InvalidateRect(window_, &pageRailRect_, FALSE);
            return 0;

        case kMessagePreviewAvailable:
            ShowWindow(updateButton_, SW_SHOWNA);
            return 0;

        case WM_CTLCOLORSTATIC:
        {
            const HWND control = reinterpret_cast<HWND>(lParam);
            const HDC context = reinterpret_cast<HDC>(wParam);
            SetTextColor(
                context,
                control == title_ ? kBrandPaperBright : RGB(210, 220, 214));
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(headerBrush_);
        }

        case WM_CLOSE:
            if (ConfirmClose())
            {
                ShutdownInk();
                DestroyWindow(window_);
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }

        return DefWindowProcW(window_, message, wParam, lParam);
    }

    void PaintChrome()
    {
        PAINTSTRUCT paint{};
        const HDC target = BeginPaint(window_, &paint);
        RECT client{};
        GetClientRect(window_, &client);
        const int width = std::max(1L, client.right);
        const int height = std::max(1L, client.bottom);
        const UINT dpi = GetDpiForWindow(window_);

        const HDC buffer = CreateCompatibleDC(target);
        const HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
        if (buffer == nullptr || bitmap == nullptr)
        {
            if (bitmap != nullptr)
            {
                DeleteObject(bitmap);
            }
            if (buffer != nullptr)
            {
                DeleteDC(buffer);
            }
            FillRect(target, &client, backgroundBrush_);
            EndPaint(window_, &paint);
            return;
        }

        const HGDIOBJ previousBitmap = SelectObject(buffer, bitmap);
        const HGDIOBJ previousPen = SelectObject(buffer, GetStockObject(DC_PEN));
        const HGDIOBJ previousBrush = SelectObject(buffer, GetStockObject(DC_BRUSH));
        FillRect(buffer, &client, backgroundBrush_);

        RECT header{0, 0, width, ScaleForDpi(78, dpi)};
        FillRect(buffer, &header, headerBrush_);
        RECT dock{workspaceLeft_, height - ScaleForDpi(104, dpi), width, height};
        FillRect(buffer, &dock, dockBrush_);

        SetDCPenColor(buffer, kBrandLine);
        MoveToEx(buffer, dock.left, dock.top, nullptr);
        LineTo(buffer, dock.right, dock.top);

        if (pageRailVisible_)
        {
            FillRect(buffer, &pageRailRect_, dockBrush_);
            MoveToEx(buffer, pageRailRect_.right, pageRailRect_.top, nullptr);
            LineTo(buffer, pageRailRect_.right, pageRailRect_.bottom);

            SetDCBrushColor(buffer, kBrandPaperBright);
            SetDCPenColor(buffer, kBrandCinnabar);
            RoundRect(
                buffer,
                pageItemRect_.left,
                pageItemRect_.top,
                pageItemRect_.right,
                pageItemRect_.bottom,
                ScaleForDpi(7, dpi),
                ScaleForDpi(7, dpi));

            SetDCBrushColor(buffer, canvasBackground_);
            SetDCPenColor(buffer, RGB(180, 188, 180));
            Rectangle(
                buffer,
                pagePreviewRect_.left,
                pagePreviewRect_.top,
                pagePreviewRect_.right,
                pagePreviewRect_.bottom);
            if (canvasGrid_)
            {
                SetDCPenColor(buffer, kCanvasGridLine);
                const int previewSpacing = ScaleForDpi(12, dpi);
                for (int x = pagePreviewRect_.left + previewSpacing;
                     x < pagePreviewRect_.right;
                     x += previewSpacing)
                {
                    MoveToEx(buffer, x, pagePreviewRect_.top, nullptr);
                    LineTo(buffer, x, pagePreviewRect_.bottom);
                }
                for (int y = pagePreviewRect_.top + previewSpacing;
                     y < pagePreviewRect_.bottom;
                     y += previewSpacing)
                {
                    MoveToEx(buffer, pagePreviewRect_.left, y, nullptr);
                    LineTo(buffer, pagePreviewRect_.right, y);
                }
            }
        }

        RECT shadow = canvasFrameRect_;
        OffsetRect(&shadow, 0, ScaleForDpi(5, dpi));
        SetDCBrushColor(buffer, RGB(195, 188, 175));
        SetDCPenColor(buffer, RGB(195, 188, 175));
        RoundRect(
            buffer,
            shadow.left,
            shadow.top,
            shadow.right,
            shadow.bottom,
            ScaleForDpi(18, dpi),
            ScaleForDpi(18, dpi));
        SetDCBrushColor(buffer, kBrandPaperBright);
        SetDCPenColor(buffer, kBrandLine);
        RoundRect(
            buffer,
            canvasFrameRect_.left,
            canvasFrameRect_.top,
            canvasFrameRect_.right,
            canvasFrameRect_.bottom,
            ScaleForDpi(18, dpi),
            ScaleForDpi(18, dpi));

        if (appIcon_ != nullptr)
        {
            DrawIconEx(
                buffer,
                ScaleForDpi(18, dpi),
                ScaleForDpi(15, dpi),
                appIcon_,
                ScaleForDpi(46, dpi),
                ScaleForDpi(46, dpi),
                0,
                nullptr,
                DI_NORMAL);
        }

        SetBkMode(buffer, TRANSPARENT);
        SelectObject(buffer, metaFont_);
        SetTextColor(buffer, RGB(227, 165, 143));
        RECT productMeta{
            ScaleForDpi(77, dpi),
            ScaleForDpi(48, dpi),
            ScaleForDpi(310, dpi),
            ScaleForDpi(70, dpi),
        };
        DrawTextW(
            buffer,
            L"NH–004 · FREE PRODUCT PREVIEW",
            -1,
            &productMeta,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        if (pageRailVisible_)
        {
            SetDCBrushColor(buffer, kBrandInkDeep);
            SetDCPenColor(buffer, RGB(47, 82, 73));
            RoundRect(
                buffer,
                documentTitleRect_.left,
                documentTitleRect_.top,
                documentTitleRect_.right,
                documentTitleRect_.bottom,
                ScaleForDpi(6, dpi),
                ScaleForDpi(6, dpi));
            SelectObject(buffer, uiFont_);
            SetTextColor(buffer, kBrandPaperBright);
            RECT documentName = documentTitleRect_;
            documentName.bottom = documentTitleRect_.top + ScaleForDpi(34, dpi);
            DrawTextW(
                buffer,
                L"未命名白板",
                -1,
                &documentName,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

            SelectObject(buffer, metaFont_);
            SetTextColor(buffer, RGB(158, 178, 168));
            RECT documentState = documentTitleRect_;
            documentState.top = documentTitleRect_.top + ScaleForDpi(29, dpi);
            DrawTextW(
                buffer,
                L"临时白板 · 本次运行",
                -1,
                &documentState,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        SelectObject(buffer, metaFont_);
        if (pageRailVisible_)
        {
            SetTextColor(buffer, kBrandMuted);
            RECT pageHeading{
                ScaleForDpi(16, dpi),
                ScaleForDpi(91, dpi),
                pageRailRect_.right - ScaleForDpi(16, dpi),
                ScaleForDpi(119, dpi),
            };
            DrawTextW(
                buffer,
                L"页面",
                -1,
                &pageHeading,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SetTextColor(buffer, kBrandInk);
            const auto pageCountText = std::to_wstring(pageCount_);
            DrawTextW(
                buffer,
                pageCountText.c_str(),
                -1,
                &pageHeading,
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

            SetTextColor(buffer, kBrandBlueGray);
            RECT pageNumber{
                pageItemRect_.left + ScaleForDpi(9, dpi),
                pagePreviewRect_.top,
                pagePreviewRect_.left - ScaleForDpi(4, dpi),
                pagePreviewRect_.bottom,
            };
            const auto pageNumberText = std::to_wstring(activePage_ + 1);
            DrawTextW(
                buffer,
                pageNumberText.c_str(),
                -1,
                &pageNumber,
                DT_LEFT | DT_SINGLELINE | DT_TOP | DT_NOPREFIX);
            SetTextColor(buffer, kBrandMuted);
            RECT pageName{
                pagePreviewRect_.left,
                pagePreviewRect_.bottom + ScaleForDpi(7, dpi),
                pageItemRect_.right - ScaleForDpi(8, dpi),
                pageItemRect_.bottom - ScaleForDpi(7, dpi),
            };
            const auto pageNameText = L"第 " + std::to_wstring(activePage_ + 1) + L" 页";
            DrawTextW(
                buffer,
                pageNameText.c_str(),
                -1,
                &pageName,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        SetTextColor(buffer, kBrandCinnabarDark);
        RECT canvasKind = canvasMetaRect_;
        canvasKind.right = canvasKind.left + ScaleForDpi(72, dpi);
        DrawTextW(
            buffer,
            L"自由白板",
            -1,
            &canvasKind,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SetTextColor(buffer, kBrandMuted);
        RECT canvasName = canvasMetaRect_;
        canvasName.left += ScaleForDpi(82, dpi);
        const std::wstring canvasLabel = L"当前白板 · " + std::wstring(CanvasThemeLabel());
        DrawTextW(
            buffer,
            canvasLabel.c_str(),
            -1,
            &canvasName,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        SetTextColor(buffer, kBrandMuted);
        DrawTextW(
            buffer,
            L"颜色  COLOR",
            -1,
            &colorGroupLabelRect_,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        DrawTextW(
            buffer,
            L"笔触  STROKE",
            -1,
            &widthGroupLabelRect_,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        DrawTextW(
            buffer,
            L"操作  ACTION",
            -1,
            &actionGroupLabelRect_,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        BitBlt(target, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, previousBrush);
        SelectObject(buffer, previousPen);
        SelectObject(buffer, previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(window_, &paint);
    }

    bool DrawToolbarButton(const DRAWITEMSTRUCT* item) const
    {
        if (item == nullptr || item->CtlType != ODT_BUTTON)
        {
            return false;
        }

        const int identifier = static_cast<int>(item->CtlID);
        const bool disabled = (item->itemState & ODS_DISABLED) != 0;
        const bool pressed = (item->itemState & ODS_SELECTED) != 0;
        const bool isColor = std::find(
            kColorControls.cbegin(),
            kColorControls.cend(),
            identifier) != kColorControls.cend();
        const bool isWidth = std::find(
            kWidthControls.cbegin(),
            kWidthControls.cend(),
            identifier) != kWidthControls.cend();
        const bool settingSelected = identifier == activeColorControl_
            || identifier == activeWidthControl_;
        const bool actionSelected = identifier == ControlEraser && eraserActive_;

        COLORREF background = kBrandPaperBright;
        COLORREF border = settingSelected ? (eraserActive_ ? kBrandMuted : kBrandCinnabar) : kBrandLine;
        COLORREF foreground = kBrandInk;
        if (actionSelected)
        {
            background = kBrandCinnabar;
            border = kBrandCinnabarDark;
            foreground = RGB(255, 250, 240);
        }
        else if (isWidth && settingSelected && !eraserActive_)
        {
            background = kBrandInk;
            border = kBrandInkDeep;
            foreground = kBrandPaperBright;
        }
        if (pressed && !disabled)
        {
            background = actionSelected || (isWidth && settingSelected && !eraserActive_)
                ? kBrandInkDeep
                : RGB(232, 225, 212);
        }
        if (disabled)
        {
            background = RGB(230, 225, 215);
            border = RGB(210, 204, 194);
            foreground = RGB(155, 158, 151);
        }

        const UINT dpi = GetDpiForWindow(window_);
        const int radius = ScaleForDpi(7, dpi);
        const int saved = SaveDC(item->hDC);
        SetBkMode(item->hDC, TRANSPARENT);
        const HPEN outline = CreatePen(
            PS_SOLID,
            settingSelected || actionSelected ? ScaleForDpi(2, dpi) : 1,
            border);
        const HBRUSH fill = CreateSolidBrush(background);
        SelectObject(item->hDC, outline);
        SelectObject(item->hDC, fill);
        RoundRect(
            item->hDC,
            item->rcItem.left + 1,
            item->rcItem.top + 1,
            item->rcItem.right - 1,
            item->rcItem.bottom - 1,
            radius,
            radius);

        wchar_t label[64]{};
        GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
        if (isColor)
        {
            const COLORREF swatch = PaletteColor(identifier);
            const int centerX = (item->rcItem.left + item->rcItem.right) / 2;
            const int centerY = item->rcItem.top + ScaleForDpi(19, dpi);
            const int swatchRadius = ScaleForDpi(8, dpi);
            const HBRUSH swatchBrush = CreateSolidBrush(disabled ? RGB(165, 165, 162) : swatch);
            const HPEN swatchPen = CreatePen(PS_SOLID, 1, disabled ? RGB(150, 150, 148) : swatch);
            SelectObject(item->hDC, swatchPen);
            SelectObject(item->hDC, swatchBrush);
            Ellipse(
                item->hDC,
                centerX - swatchRadius,
                centerY - swatchRadius,
                centerX + swatchRadius,
                centerY + swatchRadius);
            SelectObject(item->hDC, outline);
            SelectObject(item->hDC, fill);
            DeleteObject(swatchBrush);
            DeleteObject(swatchPen);

            SelectObject(item->hDC, metaFont_);
            SetTextColor(item->hDC, foreground);
            RECT labelRect = item->rcItem;
            labelRect.top += ScaleForDpi(31, dpi);
            DrawTextW(
                item->hDC,
                label,
                -1,
                &labelRect,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        else if (isWidth)
        {
            int lineWidth = ScaleForDpi(3, dpi);
            if (identifier == ControlMedium)
            {
                lineWidth = ScaleForDpi(6, dpi);
            }
            else if (identifier == ControlThick)
            {
                lineWidth = ScaleForDpi(10, dpi);
            }
            const HPEN strokePen = CreatePen(PS_SOLID, lineWidth, foreground);
            SelectObject(item->hDC, strokePen);
            const int centerY = item->rcItem.top + ScaleForDpi(19, dpi);
            MoveToEx(item->hDC, item->rcItem.left + ScaleForDpi(13, dpi), centerY, nullptr);
            LineTo(item->hDC, item->rcItem.right - ScaleForDpi(13, dpi), centerY);
            SelectObject(item->hDC, outline);
            DeleteObject(strokePen);

            SelectObject(item->hDC, metaFont_);
            SetTextColor(item->hDC, foreground);
            RECT labelRect = item->rcItem;
            labelRect.top += ScaleForDpi(31, dpi);
            DrawTextW(
                item->hDC,
                label,
                -1,
                &labelRect,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        else
        {
            SelectObject(item->hDC, uiFont_);
            SetTextColor(item->hDC, foreground);
            RECT labelRect = item->rcItem;
            InflateRect(&labelRect, -ScaleForDpi(5, dpi), -ScaleForDpi(5, dpi));
            DrawTextW(
                item->hDC,
                label,
                -1,
                &labelRect,
                DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
        }

        if ((item->itemState & ODS_FOCUS) != 0)
        {
            RECT focus = item->rcItem;
            InflateRect(&focus, -ScaleForDpi(5, dpi), -ScaleForDpi(5, dpi));
            DrawFocusRect(item->hDC, &focus);
        }

        RestoreDC(item->hDC, saved);
        DeleteObject(fill);
        DeleteObject(outline);
        return true;
    }

    HWND ButtonForControl(const int identifier) const noexcept
    {
        for (const HWND button : buttons_)
        {
            if (button != nullptr && GetDlgCtrlID(button) == identifier)
            {
                return button;
            }
        }
        return nullptr;
    }

    bool UsesChalkPalette() const noexcept
    {
        return canvasTheme_ == CanvasTheme::Chalkboard
            || canvasTheme_ == CanvasTheme::Graphite;
    }

    COLORREF PaletteColor(const int identifier) const noexcept
    {
        if (UsesChalkPalette())
        {
            switch (identifier)
            {
            case ControlRed:
                return kChalkCoral;
            case ControlGreen:
                return kChalkYellow;
            case ControlBlue:
                return kChalkBlue;
            case ControlBlack:
            default:
                return kChalkWhite;
            }
        }

        switch (identifier)
        {
        case ControlRed:
            return kInkCinnabar;
        case ControlGreen:
            return kBrandInk;
        case ControlBlue:
            return kInkBlueGray;
        case ControlBlack:
        default:
            return kInkBlack;
        }
    }

    const wchar_t* PaletteLabel(const int identifier) const noexcept
    {
        if (UsesChalkPalette())
        {
            switch (identifier)
            {
            case ControlRed:
                return L"朱砂粉";
            case ControlGreen:
                return L"粉黄";
            case ControlBlue:
                return L"粉蓝";
            case ControlBlack:
            default:
                return L"粉白";
            }
        }

        switch (identifier)
        {
        case ControlRed:
            return L"朱砂";
        case ControlGreen:
            return L"松绿";
        case ControlBlue:
            return L"青蓝";
        case ControlBlack:
        default:
            return L"墨黑";
        }
    }

    void RefreshPaletteLabels() const noexcept
    {
        for (const int identifier : kColorControls)
        {
            if (const HWND button = ButtonForControl(identifier); button != nullptr)
            {
                SetWindowTextW(button, PaletteLabel(identifier));
            }
        }
    }

    void SelectPaletteColor(const int identifier)
    {
        SetPenColor(
            ToInkColor(PaletteColor(identifier)),
            PaletteLabel(identifier),
            identifier);
    }

    const wchar_t* CanvasThemeLabel() const noexcept
    {
        switch (canvasTheme_)
        {
        case CanvasTheme::Graphite:
            return L"灰黑板";
        case CanvasTheme::WarmPaper:
            return L"暖纸";
        case CanvasTheme::SoftWhite:
            return L"柔白";
        case CanvasTheme::SoftGrid:
            return L"浅网格";
        case CanvasTheme::Chalkboard:
        default:
            return L"青黑板";
        }
    }

    void SetCanvasTheme(const CanvasTheme theme)
    {
        canvasTheme_ = theme;
        canvasGrid_ = false;
        switch (theme)
        {
        case CanvasTheme::Graphite:
            canvasBackground_ = kCanvasGraphite;
            break;
        case CanvasTheme::WarmPaper:
            canvasBackground_ = kCanvasWarmPaper;
            break;
        case CanvasTheme::SoftWhite:
            canvasBackground_ = kCanvasSoftWhite;
            break;
        case CanvasTheme::SoftGrid:
            canvasBackground_ = kCanvasSoftGrid;
            canvasGrid_ = true;
            break;
        case CanvasTheme::Chalkboard:
        default:
            canvasBackground_ = kCanvasChalkboard;
            break;
        }

        if (const HWND button = ButtonForControl(ControlBackground); button != nullptr)
        {
            const std::wstring label = L"背景\n" + std::wstring(CanvasThemeLabel());
            SetWindowTextW(button, label.c_str());
        }
        RefreshPaletteLabels();
        SelectPaletteColor(activeColorControl_);
        InvalidateRect(canvas_, nullptr, FALSE);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void ShowBackgroundMenu()
    {
        const HWND button = ButtonForControl(ControlBackground);
        if (button == nullptr)
        {
            return;
        }

        const HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            winrt::throw_last_error();
        }
        const auto flagsFor = [this](const CanvasTheme theme)
        {
            return static_cast<UINT>(MF_STRING | (canvasTheme_ == theme ? MF_CHECKED : MF_UNCHECKED));
        };
        AppendMenuW(menu, flagsFor(CanvasTheme::Chalkboard), ThemeChalkboard, L"青黑板  #274021（推荐）");
        AppendMenuW(menu, flagsFor(CanvasTheme::Graphite), ThemeGraphite, L"灰黑板（低亮度）");
        AppendMenuW(menu, flagsFor(CanvasTheme::WarmPaper), ThemeWarmPaper, L"暖纸（会议记录）");
        AppendMenuW(menu, flagsFor(CanvasTheme::SoftWhite), ThemeSoftWhite, L"柔白（文档批注）");
        AppendMenuW(menu, flagsFor(CanvasTheme::SoftGrid), ThemeSoftGrid, L"浅网格（图示）");

        RECT buttonRect{};
        GetWindowRect(button, &buttonRect);
        const int command = TrackPopupMenu(
            menu,
            TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
            buttonRect.right,
            buttonRect.top,
            0,
            window_,
            nullptr);
        DestroyMenu(menu);

        switch (command)
        {
        case ThemeChalkboard:
            SetCanvasTheme(CanvasTheme::Chalkboard);
            break;
        case ThemeGraphite:
            SetCanvasTheme(CanvasTheme::Graphite);
            break;
        case ThemeWarmPaper:
            SetCanvasTheme(CanvasTheme::WarmPaper);
            break;
        case ThemeSoftWhite:
            SetCanvasTheme(CanvasTheme::SoftWhite);
            break;
        case ThemeSoftGrid:
            SetCanvasTheme(CanvasTheme::SoftGrid);
            break;
        default:
            break;
        }
    }

    void InvalidateToolbar() const noexcept
    {
        for (const HWND button : buttons_)
        {
            if (button != nullptr)
            {
                InvalidateRect(button, nullptr, TRUE);
            }
        }
    }

    void UpdateCommandAvailability() const noexcept
    {
        if (const HWND undo = ButtonForControl(ControlUndo); undo != nullptr)
        {
            EnableWindow(undo, !pageChangePending_ && undoDepth_ > 0);
        }
        if (const HWND clear = ButtonForControl(ControlClear); clear != nullptr)
        {
            EnableWindow(clear, !pageChangePending_ && strokeCount_ > 0);
        }
        EnableWindow(ButtonForControl(ControlAddPage), inkReady_ && !pageChangePending_);
        EnableWindow(railButtons_[0], inkReady_ && !pageChangePending_);
        EnableWindow(railButtons_[1], !pageChangePending_ && activePage_ > 0);
        EnableWindow(railButtons_[2], !pageChangePending_ && activePage_ + 1 < pageCount_);
        InvalidateToolbar();
    }

    HWND CreateToolButton(const int identifier, const wchar_t* label)
    {
        const HWND button = CreateWindowExW(
            0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)), instance_, nullptr);
        if (button == nullptr)
        {
            winrt::throw_last_error();
        }
        return button;
    }

    void CreateUi()
    {
        title_ = CreateWindowExW(
            0,
            L"STATIC",
            L"南荒白板",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0,
            0,
            0,
            0,
            window_,
            nullptr,
            instance_,
            nullptr);

        const std::array<std::pair<int, const wchar_t*>, 13> buttonDefinitions{{
            {ControlBlack, L"粉白"},
            {ControlRed, L"朱砂粉"},
            {ControlGreen, L"粉黄"},
            {ControlBlue, L"粉蓝"},
            {ControlThin, L"细"},
            {ControlMedium, L"中"},
            {ControlThick, L"粗"},
            {ControlEraser, L"橡皮"},
            {ControlUndo, L"撤销\nCtrl+Z"},
            {ControlClear, L"清空\n可撤销"},
            {ControlFullscreen, L"全屏\nF11"},
            {ControlBackground, L"背景\n青黑板"},
            {ControlAddPage, L"＋ 新页"},
        }};

        for (std::size_t index = 0; index < buttonDefinitions.size(); ++index)
        {
            const auto [identifier, label] = buttonDefinitions[index];
            buttons_[index] = CreateToolButton(identifier, label);
        }
        railButtons_[0] = CreateToolButton(ControlRailAddPage, L"＋ 新建空白页");
        railButtons_[1] = CreateToolButton(ControlPreviousPage, L"上一页");
        railButtons_[2] = CreateToolButton(ControlNextPage, L"下一页");
        updateButton_ = CreateToolButton(ControlShowUpdate, L"查看新版");
        ShowWindow(updateButton_, SW_HIDE);

        canvas_ = CreateWindowExW(
            0,
            kCanvasWindowClass,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0,
            0,
            0,
            0,
            window_,
            nullptr,
            instance_,
            this);

        status_ = CreateWindowExW(
            0,
            L"STATIC",
            L"正在准备原生画布…",
            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            0,
            0,
            0,
            0,
            window_,
            nullptr,
            instance_,
            nullptr);

        if (title_ == nullptr || canvas_ == nullptr || status_ == nullptr)
        {
            winrt::throw_last_error();
        }

        ApplyFonts();
        LayoutUi();
        UpdateCommandAvailability();
    }

    void ApplyFonts()
    {
        if (title_ == nullptr)
        {
            return;
        }

        if (titleFont_ != nullptr)
        {
            DeleteObject(titleFont_);
        }
        if (uiFont_ != nullptr)
        {
            DeleteObject(uiFont_);
        }
        if (metaFont_ != nullptr)
        {
            DeleteObject(metaFont_);
        }

        const UINT dpi = GetDpiForWindow(window_);
        titleFont_ = CreateFontW(
            -ScaleForDpi(22, dpi),
            0,
            0,
            0,
            FW_BOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_ROMAN,
            L"SimSun");
        uiFont_ = CreateFontW(
            -ScaleForDpi(13, dpi),
            0,
            0,
            0,
            FW_SEMIBOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Microsoft YaHei UI");
        metaFont_ = CreateFontW(
            -ScaleForDpi(10, dpi),
            0,
            0,
            0,
            FW_SEMIBOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        if (titleFont_ == nullptr || uiFont_ == nullptr || metaFont_ == nullptr)
        {
            winrt::throw_last_error();
        }

        SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
        SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        SendMessageW(updateButton_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        for (const HWND button : buttons_)
        {
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
        for (const HWND button : railButtons_)
        {
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
    }

    void LayoutUi()
    {
        if (canvas_ == nullptr)
        {
            return;
        }

        RECT client{};
        GetClientRect(window_, &client);
        const UINT dpi = GetDpiForWindow(window_);
        const int clientWidth = std::max(1L, client.right);
        const int clientHeight = std::max(1L, client.bottom);
        const int headerHeight = ScaleForDpi(78, dpi);
        const int dockHeight = ScaleForDpi(104, dpi);
        const int stageMargin = ScaleForDpi(14, dpi);
        const int buttonHeight = ScaleForDpi(58, dpi);
        const int toolbarGap = ScaleForDpi(6, dpi);
        const int groupGap = ScaleForDpi(20, dpi);

        pageRailVisible_ = clientWidth >= ScaleForDpi(1120, dpi);
        const int pageRailWidth = pageRailVisible_ ? ScaleForDpi(176, dpi) : 0;
        workspaceLeft_ = pageRailWidth + (pageRailVisible_ ? 1 : 0);
        const int workspaceWidth = std::max(1, clientWidth - workspaceLeft_);
        pageRailRect_ = pageRailVisible_
            ? RECT{0, headerHeight, pageRailWidth, clientHeight}
            : RECT{};
        pageItemRect_ = pageRailVisible_
            ? RECT{
                ScaleForDpi(13, dpi),
                headerHeight + ScaleForDpi(52, dpi),
                pageRailWidth - ScaleForDpi(13, dpi),
                headerHeight + ScaleForDpi(190, dpi),
            }
            : RECT{};
        pagePreviewRect_ = pageRailVisible_
            ? RECT{
                ScaleForDpi(47, dpi),
                headerHeight + ScaleForDpi(64, dpi),
                pageRailWidth - ScaleForDpi(21, dpi),
                headerHeight + ScaleForDpi(139, dpi),
            }
            : RECT{};
        documentTitleRect_ = pageRailVisible_
            ? RECT{
                clientWidth / 2 - ScaleForDpi(180, dpi),
                ScaleForDpi(11, dpi),
                clientWidth / 2 + ScaleForDpi(180, dpi),
                ScaleForDpi(66, dpi),
            }
            : RECT{};

        MoveWindow(
            title_,
            ScaleForDpi(76, dpi),
            ScaleForDpi(8, dpi),
            ScaleForDpi(205, dpi),
            ScaleForDpi(42, dpi),
            TRUE);
        MoveWindow(updateButton_, ScaleForDpi(282, dpi), ScaleForDpi(15, dpi),
                   ScaleForDpi(94, dpi), ScaleForDpi(40, dpi), TRUE);

        const int statusWidth = pageRailVisible_
            ? std::min(ScaleForDpi(360, dpi), clientWidth / 3)
            : std::min(ScaleForDpi(500, dpi), clientWidth / 2);
        MoveWindow(
            status_,
            clientWidth - stageMargin - statusWidth,
            ScaleForDpi(17, dpi),
            statusWidth,
            ScaleForDpi(42, dpi),
            TRUE);

        const int canvasX = workspaceLeft_ + stageMargin;
        const int canvasY = headerHeight + stageMargin + ScaleForDpi(32, dpi);
        const int canvasWidth = std::max(1, workspaceWidth - stageMargin * 2);
        const int dockTop = clientHeight - dockHeight;
        const int canvasHeight = std::max(1, dockTop - stageMargin - canvasY);
        canvasMetaRect_ = RECT{
            canvasX,
            headerHeight + ScaleForDpi(7, dpi),
            canvasX + std::min(ScaleForDpi(360, dpi), canvasWidth),
            canvasY - ScaleForDpi(4, dpi),
        };
        canvasFrameRect_ = RECT{
            canvasX - ScaleForDpi(2, dpi),
            canvasY - ScaleForDpi(2, dpi),
            canvasX + canvasWidth + ScaleForDpi(2, dpi),
            canvasY + canvasHeight + ScaleForDpi(2, dpi),
        };
        MoveWindow(canvas_, canvasX, canvasY, canvasWidth, canvasHeight, TRUE);
        if (const HRGN canvasRegion = CreateRoundRectRgn(
                0,
                0,
                canvasWidth + 1,
                canvasHeight + 1,
                ScaleForDpi(10, dpi),
                ScaleForDpi(10, dpi));
            canvasRegion != nullptr)
        {
            if (SetWindowRgn(canvas_, canvasRegion, TRUE) == 0)
            {
                DeleteObject(canvasRegion);
            }
        }

        ShowWindow(railButtons_[0], pageRailVisible_ ? SW_SHOWNA : SW_HIDE);
        if (pageRailVisible_)
        {
            const int x = ScaleForDpi(13, dpi);
            const int y = pageItemRect_.bottom + ScaleForDpi(12, dpi);
            const int width = pageRailWidth - x * 2;
            const int halfWidth = (width - toolbarGap) / 2;
            const int height = ScaleForDpi(44, dpi);
            MoveWindow(railButtons_[0], x, y, width, height, TRUE);
            MoveWindow(railButtons_[1], x, y + height + toolbarGap, halfWidth, height, TRUE);
            MoveWindow(railButtons_[2], x + halfWidth + toolbarGap, y + height + toolbarGap, halfWidth, height, TRUE);
        }
        else
        {
            const int width = ScaleForDpi(82, dpi);
            const int height = ScaleForDpi(40, dpi);
            const int y = headerHeight + ScaleForDpi(2, dpi);
            const int nextX = clientWidth - stageMargin - width;
            MoveWindow(railButtons_[1], nextX - toolbarGap - width, y, width, height, TRUE);
            MoveWindow(railButtons_[2], nextX, y, width, height, TRUE);
        }

        const std::array<int, 13> logicalWidths{{52, 52, 52, 52, 50, 50, 50, 60, 74, 64, 86, 76, 76}};
        int toolbarWidth = 0;
        for (const int logicalWidth : logicalWidths)
        {
            toolbarWidth += ScaleForDpi(logicalWidth, dpi);
        }
        toolbarWidth += toolbarGap * 9 + groupGap * 2;

        int buttonX = workspaceLeft_ + std::max(stageMargin, (workspaceWidth - toolbarWidth) / 2);
        const int buttonY = dockTop + ScaleForDpi(35, dpi);
        int colorStart = buttonX;
        int colorEnd = buttonX;
        int widthStart = buttonX;
        int widthEnd = buttonX;
        int actionStart = buttonX;
        int actionEnd = buttonX;
        for (std::size_t index = 0; index < buttons_.size(); ++index)
        {
            const int buttonWidth = ScaleForDpi(logicalWidths[index], dpi);
            MoveWindow(buttons_[index], buttonX, buttonY, buttonWidth, buttonHeight, TRUE);
            const int buttonRight = buttonX + buttonWidth;
            if (index <= 3)
            {
                colorEnd = buttonRight;
            }
            else if (index <= 6)
            {
                if (index == 4)
                {
                    widthStart = buttonX;
                }
                widthEnd = buttonRight;
            }
            else
            {
                if (index == 7)
                {
                    actionStart = buttonX;
                }
                actionEnd = buttonRight;
            }

            buttonX = buttonRight;
            if (index == 3 || index == 6)
            {
                buttonX += groupGap;
            }
            else if (index + 1 < buttons_.size())
            {
                buttonX += toolbarGap;
            }
        }

        const int labelTop = dockTop + ScaleForDpi(8, dpi);
        const int labelBottom = dockTop + ScaleForDpi(29, dpi);
        colorGroupLabelRect_ = RECT{colorStart, labelTop, colorEnd, labelBottom};
        widthGroupLabelRect_ = RECT{widthStart, labelTop, widthEnd, labelBottom};
        actionGroupLabelRect_ = RECT{actionStart, labelTop, actionEnd, labelBottom};

        InvalidateRect(window_, nullptr, FALSE);
        ResizeInk();
        RefreshStatus();
    }

    void InitializeInk()
    {
        RECT canvasClient{};
        GetClientRect(canvas_, &canvasClient);
        const float width = static_cast<float>(std::max(1L, canvasClient.right));
        const float height = static_cast<float>(std::max(1L, canvasClient.bottom));

        lastInkErrorStage_ = InkOperationStage::CompositionDevice;
        // InkPresenterDesktop does not accept a visual created by the legacy v1 device on all
        // Windows builds (SetRootVisual returns E_NOINTERFACE). Create a system desktop-capable
        // DirectComposition device, matching Microsoft's InkCanvas implementation. Requesting the
        // v1 interface keeps the rest of this small probe simple while the underlying objects are
        // current-generation DirectComposition objects.
        winrt::check_hresult(DCompositionCreateDevice3(
            nullptr,
            __uuidof(IDCompositionDevice),
            compositionDevice_.put_void()));
        // Fail at the device stage, rather than later inside SetRootVisual, if the returned object
        // is not a desktop DirectComposition device.
        static_cast<void>(compositionDevice_.as<IDCompositionDesktopDevice>());

        lastInkErrorStage_ = InkOperationStage::CompositionTree;
        winrt::check_hresult(compositionDevice_->CreateTargetForHwnd(canvas_, TRUE, compositionTarget_.put()));
        winrt::check_hresult(compositionDevice_->CreateVisual(inkVisual_.put()));
        winrt::check_hresult(compositionTarget_->SetRoot(inkVisual_.get()));

        lastInkErrorStage_ = InkOperationStage::HostActivation;
        winrt::check_hresult(CoCreateInstance(
            __uuidof(InkDesktopHost),
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(inkHost_.put())));

        const auto threadState = std::make_shared<InkThreadState>();
        inkThreadState_ = threadState;
        const auto inkHost = inkHost_;
        const auto rootVisual = inkVisual_;
        const auto compositionDevice = compositionDevice_;
        const HWND statusWindow = window_;

        RunInkWorkAndWait(
            [threadState, inkHost, rootVisual, compositionDevice, width, height, statusWindow](
                std::atomic<int>& stage)
            {
                stage.store(
                    static_cast<int>(InkOperationStage::PresenterCreation),
                    std::memory_order_release);
                auto desktopPresenter = winrt::capture<IInkPresenterDesktop>(
                    inkHost,
                    &IInkDesktopHost::CreateInkPresenter);
                auto presenter = desktopPresenter.as<inking::InkPresenter>();

                stage.store(
                    static_cast<int>(InkOperationStage::PresenterRootVisual),
                    std::memory_order_release);
                winrt::check_hresult(desktopPresenter->SetRootVisual(rootVisual.get(), nullptr));

                stage.store(
                    static_cast<int>(InkOperationStage::PresenterSize),
                    std::memory_order_release);
                winrt::check_hresult(desktopPresenter->SetSize(width, height));

                stage.store(
                    static_cast<int>(InkOperationStage::PresenterConfiguration),
                    std::memory_order_release);
                presenter.InputDeviceTypes(
                    core::CoreInputDeviceTypes::Mouse
                    | core::CoreInputDeviceTypes::Pen
                    | core::CoreInputDeviceTypes::Touch);

                inking::InkDrawingAttributes attributes;
                attributes.Color(ToInkColor(kChalkWhite));
                attributes.Size(foundation::Size{kPenMediumSize, kPenMediumSize});
                attributes.IgnorePressure(true);
                attributes.FitToCurve(false);
                presenter.UpdateDefaultDrawingAttributes(attributes);

                threadState->pages.push_back({presenter.StrokeContainer(), {}});
                const auto strokeInput = presenter.StrokeInput();
                strokeInput.StrokeStarted(
                    [threadState, statusWindow](
                        const inking::InkStrokeInput& sender, const core::PointerEventArgs& args) noexcept
                    {
                        try
                        {
                            const auto point = args.CurrentPoint();
                            const auto properties = point.Properties();
                            const bool drawing = sender.InkPresenter().InputProcessingConfiguration().Mode()
                                == inking::InkInputProcessingMode::Inking
                                && !properties.IsEraser()
                                && !properties.IsInverted();
                            threadState->pendingPointers.push_back({point.PointerId(), true, drawing});
                            threadState->hasDrawn.store(true, std::memory_order_relaxed);
                            const auto position = point.Position();
                            const int x = std::clamp(static_cast<int>(std::lround(position.X)), 0, 65535);
                            const int y = std::clamp(static_cast<int>(std::lround(position.Y)), 0, 65535);
                            const auto device = point.PointerDevice().PointerDeviceType();
                            if (IsWindow(statusWindow))
                            {
                                PostMessageW(
                                    statusWindow,
                                    kMessagePointerStarted,
                                    static_cast<WPARAM>(device),
                                    MAKELPARAM(x, y));
                            }
                        }
                        catch (...)
                        {
                        }
                    });
                const auto finishPointer = [threadState](const core::PointerEventArgs& args, const bool canceled) noexcept
                {
                    try
                    {
                        threadState->FinishPointer(args.CurrentPoint().PointerId(), canceled);
                    }
                    catch (...)
                    {
                    }
                };
                strokeInput.StrokeEnded(
                    [finishPointer](const inking::InkStrokeInput&, const core::PointerEventArgs& args) noexcept
                    {
                        finishPointer(args, false);
                    });
                strokeInput.StrokeCanceled(
                    [finishPointer](const inking::InkStrokeInput&, const core::PointerEventArgs& args) noexcept
                    {
                        finishPointer(args, true);
                    });

                presenter.StrokesCollected(
                    [threadState, statusWindow](
                        const inking::InkPresenter& sender,
                        const inking::InkStrokesCollectedEventArgs& args) noexcept
                    {
                        try
                        {
                            if (threadState->pages.empty())
                            {
                                return;
                            }
                            // The presenter can batch completed pointers. Finish each pointer
                            // independently and keep one undo entry per physical stroke.
                            for (const auto& stroke : args.Strokes())
                            {
                                threadState->CollectStroke(stroke.PointerId());
                                if (!threadState->applyingHistory)
                                {
                                    threadState->strokePages[stroke.Id()] = threadState->activePage;
                                    InkHistoryEntry entry;
                                    entry.kind = InkHistoryKind::Added;
                                    entry.strokeIds.push_back(stroke.Id());
                                    threadState->PushHistory(threadState->activePage, std::move(entry));
                                }
                            }
                            const auto count = sender.StrokeContainer().GetStrokes().Size();
                            if (IsWindow(statusWindow))
                            {
                                PostMessageW(
                                    statusWindow,
                                    kMessageStrokeCount,
                                    count,
                                    static_cast<LPARAM>(threadState->pages[threadState->activePage].undoHistory.size()));
                            }
                        }
                        catch (...)
                        {
                        }
                    });

                presenter.StrokesErased(
                    [threadState, statusWindow](
                        const inking::InkPresenter& sender,
                        const inking::InkStrokesErasedEventArgs& args) noexcept
                    {
                        try
                        {
                            if (threadState->pages.empty())
                            {
                                return;
                            }
                            if (!threadState->applyingHistory)
                            {
                                // The erase notification may arrive after pen-up and a page swap.
                                // Attribute its undo to the page that owned each removed stroke.
                                std::unordered_map<std::size_t, InkHistoryEntry> historyByPage;
                                for (const auto& stroke : args.Strokes())
                                {
                                    const auto owner = threadState->strokePages.find(stroke.Id());
                                    if (owner == threadState->strokePages.end())
                                    {
                                        continue;
                                    }
                                    auto& entry = historyByPage[owner->second];
                                    entry.kind = InkHistoryKind::Erased;
                                    // A stroke cannot belong to two InkStrokeContainers. Keep a
                                    // detached clone that can be cloned again when undo restores it.
                                    entry.strokeSnapshots.push_back(stroke.Clone());
                                    threadState->strokePages.erase(owner);
                                }
                                for (auto& [pageIndex, entry] : historyByPage)
                                {
                                    threadState->PushHistory(pageIndex, std::move(entry));
                                }
                            }
                            const auto count = sender.StrokeContainer().GetStrokes().Size();
                            if (IsWindow(statusWindow))
                            {
                                PostMessageW(
                                    statusWindow,
                                    kMessageStrokeCount,
                                    count,
                                    static_cast<LPARAM>(threadState->pages[threadState->activePage].undoHistory.size()));
                            }
                        }
                        catch (...)
                        {
                        }
                    });

                winrt::check_hresult(compositionDevice->Commit());

                // The OS presenter is apartment-bound to the InkDesktopHost thread. Store it only
                // after every throwing initialization step has succeeded, and never read these
                // fields from the Win32 UI thread.
                threadState->desktopPresenter = std::move(desktopPresenter);
                threadState->presenter = std::move(presenter);
            },
            InkOperationStage::PresenterCreation);

        inkReady_ = true;
        lastCanvasWidth_ = static_cast<int>(width);
        lastCanvasHeight_ = static_cast<int>(height);
        lastInkErrorStage_ = InkOperationStage::Unknown;

        UpdateCommandAvailability();
        RefreshStatus();
    }

    template <typename Action>
    void QueueInkWork(Action&& action)
    {
        if (!inkReady_ || !inkHost_)
        {
            return;
        }

        auto item = winrt::make_self<InkWorkItem>(
            std::function<void()>(std::forward<Action>(action)),
            window_);
        winrt::check_hresult(inkHost_->QueueWorkItem(item.get()));
    }

    void RunInkWorkAndWait(
        std::function<void(std::atomic<int>&)> action,
        const InkOperationStage initialStage,
        const DWORD timeout = INFINITE)
    {
        if (!inkHost_)
        {
            winrt::throw_hresult(RO_E_CLOSED);
        }

        const auto completion = std::make_shared<InkWorkCompletion>(initialStage);
        auto item = winrt::make_self<InkWorkItem>(
            [completion, action = std::move(action)]() mutable
            {
                action(completion->stage);
            },
            window_,
            initialStage,
            completion);
        winrt::check_hresult(inkHost_->QueueWorkItem(item.get()));

        HANDLE event = completion->event;
        DWORD signaledIndex = 0;
        winrt::check_hresult(CoWaitForMultipleHandles(
            COWAIT_DISPATCH_CALLS,
            timeout,
            1,
            &event,
            &signaledIndex));

        const HRESULT result = completion->result.load(std::memory_order_acquire);
        if (FAILED(result))
        {
            lastInkErrorStage_ = static_cast<InkOperationStage>(
                completion->stage.load(std::memory_order_acquire));
            winrt::throw_hresult(result);
        }
    }

    void ResizeInk()
    {
        if (!inkReady_ || canvas_ == nullptr)
        {
            return;
        }

        RECT client{};
        GetClientRect(canvas_, &client);
        const int width = std::max(1L, client.right);
        const int height = std::max(1L, client.bottom);
        if (width == lastCanvasWidth_ && height == lastCanvasHeight_)
        {
            return;
        }

        lastCanvasWidth_ = width;
        lastCanvasHeight_ = height;
        const auto threadState = inkThreadState_;
        const auto compositionDevice = compositionDevice_;
        QueueInkWork([threadState, compositionDevice, width, height]()
        {
            if (!threadState || !threadState->desktopPresenter)
            {
                return;
            }
            winrt::check_hresult(threadState->desktopPresenter->SetSize(
                static_cast<float>(width),
                static_cast<float>(height)));
            winrt::check_hresult(compositionDevice->Commit());
        });
    }

    void HandleCommand(const int identifier)
    {
        switch (identifier)
        {
        case ControlBlack:
            SelectPaletteColor(ControlBlack);
            break;
        case ControlRed:
            SelectPaletteColor(ControlRed);
            break;
        case ControlGreen:
            SelectPaletteColor(ControlGreen);
            break;
        case ControlBlue:
            SelectPaletteColor(ControlBlue);
            break;
        case ControlThin:
            SetPenWidth(kPenThinSize, L"细笔", ControlThin);
            break;
        case ControlMedium:
            SetPenWidth(kPenMediumSize, L"中笔", ControlMedium);
            break;
        case ControlThick:
            SetPenWidth(kPenThickSize, L"粗笔", ControlThick);
            break;
        case ControlEraser:
            SetEraser();
            break;
        case ControlUndo:
            UndoLastStroke();
            break;
        case ControlClear:
            if (strokeCount_ > 0
                && MessageBoxW(
                       window_,
                       L"清空当前白板上的全部笔迹？\n\n清空后仍可立即使用“撤销”恢复。",
                       L"清空当前白板",
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
            {
                ClearInk();
            }
            break;
        case ControlFullscreen:
            ToggleFullscreen();
            break;
        case ControlAddPage:
        case ControlRailAddPage:
            ChangePage(pageCount_);
            break;
        case ControlPreviousPage:
            if (activePage_ > 0)
            {
                ChangePage(activePage_ - 1);
            }
            break;
        case ControlNextPage:
            if (activePage_ + 1 < pageCount_)
            {
                ChangePage(activePage_ + 1);
            }
            break;
        case ControlBackground:
            ShowBackgroundMenu();
            break;
        case ControlShowUpdate:
            ShellExecuteW(window_, L"open", board_update::releases_url, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        default:
            break;
        }
    }

    void ApplyPenSettings()
    {
        eraserActive_ = false;
        mode_ = penColorLabel_ + L" · " + penWidthLabel_;
        const auto threadState = inkThreadState_;
        const auto color = penColor_;
        const float size = penSize_;
        QueueInkWork([threadState, color, size]()
        {
            if (!threadState || !threadState->presenter)
            {
                return;
            }
            const auto& presenter = threadState->presenter;
            auto attributes = presenter.CopyDefaultDrawingAttributes();
            attributes.Color(color);
            attributes.Size(foundation::Size{size, size});
            attributes.IgnorePressure(true);
            attributes.FitToCurve(false);
            presenter.UpdateDefaultDrawingAttributes(attributes);
            presenter.InputProcessingConfiguration().Mode(inking::InkInputProcessingMode::Inking);
        });
        InvalidateToolbar();
        RefreshStatus();
    }

    void SetPenColor(const colors::Color color, std::wstring label, const int control)
    {
        penColor_ = color;
        penColorLabel_ = std::move(label);
        activeColorControl_ = control;
        ApplyPenSettings();
    }

    void SetPenWidth(const float size, std::wstring label, const int control)
    {
        penSize_ = size;
        penWidthLabel_ = std::move(label);
        activeWidthControl_ = control;
        ApplyPenSettings();
    }

    void SetEraser()
    {
        eraserActive_ = true;
        mode_ = L"橡皮";
        const auto threadState = inkThreadState_;
        QueueInkWork([threadState]()
        {
            if (threadState && threadState->presenter)
            {
                threadState->presenter.InputProcessingConfiguration().Mode(
                    inking::InkInputProcessingMode::Erasing);
            }
        });
        InvalidateToolbar();
        RefreshStatus();
    }

    void ChangePage(const std::size_t nextPage)
    {
        if (!inkReady_ || pageChangePending_)
        {
            return;
        }

        const auto threadState = inkThreadState_;
        const HWND statusWindow = window_;
        QueueInkWork([threadState, statusWindow, nextPage]()
        {
            // Physical pen-up can precede the presenter's dry-ink collection callback.
            if (!threadState->ReadyToSwitchPage(GetTickCount64()))
            {
                PostMessageW(statusWindow, kMessagePageChanged, 0, 0);
                return;
            }

            auto& pages = threadState->pages;
            const bool adding = nextPage == pages.size();
            try
            {
                if (adding)
                {
                    pages.push_back({inking::InkStrokeContainer(), {}});
                }
                threadState->presenter.StrokeContainer(pages.at(nextPage).strokes);
            }
            catch (...)
            {
                if (adding && pages.size() > nextPage)
                {
                    pages.pop_back();
                }
                PostMessageW(statusWindow, kMessagePageChanged, 0, kPageChangeFailed);
                throw;
            }

            threadState->activePage = nextPage;
            PostMessageW(
                statusWindow, kMessageStrokeCount, pages[nextPage].strokes.GetStrokes().Size(),
                static_cast<LPARAM>(pages[nextPage].undoHistory.size()));
            PostMessageW(
                statusWindow, kMessagePageChanged,
                static_cast<WPARAM>(nextPage + 1), static_cast<LPARAM>(pages.size()));
        });
        pageChangePending_ = true;
        UpdateCommandAvailability();
    }

    bool ConfirmClose()
    {
        if (!inkReady_)
        {
            return true;
        }
        return !inkThreadState_->hasDrawn.load(std::memory_order_relaxed) || MessageBoxW(
            window_,
            L"此预览版的页面和笔迹只保留到本次运行结束。\n\n确定关闭白板？",
            L"关闭南荒白板", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
    }

    void UndoLastStroke()
    {
        if (pageChangePending_)
        {
            return;
        }
        const auto threadState = inkThreadState_;
        const auto compositionDevice = compositionDevice_;
        const HWND statusWindow = window_;
        QueueInkWork([threadState, compositionDevice, statusWindow]()
        {
            if (!threadState || !threadState->presenter)
            {
                return;
            }
            auto& undoHistory = threadState->pages[threadState->activePage].undoHistory;
            if (undoHistory.empty())
            {
                return;
            }

            InkHistoryEntry entry = std::move(undoHistory.back());
            undoHistory.pop_back();

            const auto& presenter = threadState->presenter;
            const auto container = presenter.StrokeContainer();
            {
                InkHistoryReplayGuard replayGuard(*threadState);
                if (entry.kind == InkHistoryKind::Added)
                {
                    const auto strokes = container.GetStrokes();
                    for (const auto& stroke : strokes)
                    {
                        stroke.Selected(false);
                        if (std::find(
                                entry.strokeIds.cbegin(),
                                entry.strokeIds.cend(),
                                stroke.Id()) != entry.strokeIds.cend())
                        {
                            stroke.Selected(true);
                        }
                    }
                    container.DeleteSelected();
                    for (const auto strokeId : entry.strokeIds)
                    {
                        threadState->strokePages.erase(strokeId);
                    }
                }
                else
                {
                    for (const auto& snapshot : entry.strokeSnapshots)
                    {
                        const std::uint32_t previousId = snapshot.Id();
                        auto restoredStroke = snapshot.Clone();
                        container.AddStroke(restoredStroke);
                        threadState->strokePages[restoredStroke.Id()] = threadState->activePage;
                        threadState->RemapStrokeId(previousId, restoredStroke.Id());
                    }
                }
                winrt::check_hresult(compositionDevice->Commit());
            }
            if (IsWindow(statusWindow))
            {
                PostMessageW(
                    statusWindow,
                    kMessageStrokeCount,
                    container.GetStrokes().Size(),
                    static_cast<LPARAM>(undoHistory.size()));
            }
        });
    }

    void ClearInk()
    {
        const auto threadState = inkThreadState_;
        const auto compositionDevice = compositionDevice_;
        const HWND statusWindow = window_;
        QueueInkWork([threadState, compositionDevice, statusWindow]()
        {
            if (!threadState || !threadState->presenter)
            {
                return;
            }
            const auto container = threadState->presenter.StrokeContainer();
            const auto strokes = container.GetStrokes();
            if (strokes.Size() == 0)
            {
                return;
            }

            InkHistoryEntry entry;
            entry.kind = InkHistoryKind::Cleared;
            for (const auto& stroke : strokes)
            {
                entry.strokeSnapshots.push_back(stroke.Clone());
            }
            threadState->PushHistory(threadState->activePage, std::move(entry));

            {
                InkHistoryReplayGuard replayGuard(*threadState);
                container.Clear();
                const auto activePage = threadState->activePage;
                std::erase_if(threadState->strokePages,
                    [activePage](const auto& item) { return item.second == activePage; });
                winrt::check_hresult(compositionDevice->Commit());
            }
            if (IsWindow(statusWindow))
            {
                PostMessageW(
                    statusWindow,
                    kMessageStrokeCount,
                    0,
                    static_cast<LPARAM>(threadState->pages[threadState->activePage].undoHistory.size()));
            }
        });
    }

    void ToggleFullscreen()
    {
        if (!fullscreen_)
        {
            const HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{sizeof(info)};
            if (!GetMonitorInfoW(monitor, &info) || !GetWindowPlacement(window_, &windowedPlacement_))
            {
                winrt::throw_last_error();
            }
            windowedStyle_ = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
            fullscreen_ = true;
            SetWindowLongPtrW(window_, GWL_STYLE, windowedStyle_ & ~(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE));
            SetWindowPos(
                window_,
                HWND_TOP,
                info.rcMonitor.left,
                info.rcMonitor.top,
                info.rcMonitor.right - info.rcMonitor.left,
                info.rcMonitor.bottom - info.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            if (const HWND button = ButtonForControl(ControlFullscreen); button != nullptr)
            {
                SetWindowTextW(button, L"退出全屏\nEsc");
            }
        }
        else
        {
            fullscreen_ = false;
            SetWindowLongPtrW(window_, GWL_STYLE, windowedStyle_ & ~WS_MAXIMIZE);
            SetWindowPlacement(window_, &windowedPlacement_);
            SetWindowPos(
                window_,
                nullptr,
                0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
            if (const HWND button = ButtonForControl(ControlFullscreen); button != nullptr)
            {
                SetWindowTextW(button, L"全屏\nF11");
            }
        }
        InvalidateToolbar();
    }

    static std::wstring DeviceName(const devices::PointerDeviceType type)
    {
        switch (type)
        {
        case devices::PointerDeviceType::Touch:
            return L"Touch";
        case devices::PointerDeviceType::Pen:
            return L"Pen";
        case devices::PointerDeviceType::Mouse:
            return L"Mouse";
        default:
            return L"未知输入";
        }
    }

    void RefreshStatus()
    {
        if (status_ == nullptr || canvas_ == nullptr)
        {
            return;
        }

        const UINT dpi = GetDpiForWindow(canvas_);
        const HMONITOR monitor = MonitorFromWindow(canvas_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{sizeof(monitorInfo)};
        GetMonitorInfoW(monitor, &monitorInfo);

        std::wostringstream text;
        text << inputSource_
             << L" · " << MulDiv(100, static_cast<int>(dpi), 96) << L"%"
             << L" · " << ((monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0 ? L"主屏" : L"副屏")
             << L" · " << mode_
             << L" · " << strokeCount_ << L" 笔"
             << L" · " << activePage_ + 1 << L"/" << pageCount_;
        SetWindowTextW(status_, text.str().c_str());
    }

    void ShowFatalError(const HRESULT result) const noexcept
    {
        const std::wstring message =
            L"Windows Ink 原生链路初始化失败。\n\n阶段：" + std::wstring(InkStageName(lastInkErrorStage_))
            + L"\n" + FormatHresult(result)
            + L"\n\n请记录 Windows 版本并把此窗口拍照发回运营群。";
        MessageBoxW(window_, message.c_str(), L"南荒白板产品预览", MB_OK | MB_ICONERROR);
    }

    void ShutdownInk() noexcept
    {
        if (!inkReady_ && !inkHost_)
        {
            return;
        }

        inkReady_ = false;
        const auto threadState = inkThreadState_;
        if (inkHost_ && threadState)
        {
            try
            {
                RunInkWorkAndWait(
                    [threadState](std::atomic<int>& stage)
                    {
                        stage.store(
                            static_cast<int>(InkOperationStage::Shutdown),
                            std::memory_order_release);
                        // Release every apartment-bound presenter interface on the thread that
                        // created it before the UI thread releases the InkDesktopHost.
                        threadState->pages.clear();
                        threadState->strokePages.clear();
                        threadState->presenter = nullptr;
                        threadState->desktopPresenter = nullptr;
                    },
                    InkOperationStage::Shutdown,
                    kInkShutdownTimeoutMs);
            }
            catch (...)
            {
                // Shutdown is best effort; the process is already exiting and must not throw from
                // the Application destructor.
            }
        }
        inkThreadState_.reset();
        if (compositionTarget_ && compositionDevice_)
        {
            compositionTarget_->SetRoot(nullptr);
            compositionDevice_->Commit();
        }
        inkHost_ = nullptr;
        inkVisual_ = nullptr;
        compositionTarget_ = nullptr;
        compositionDevice_ = nullptr;
    }

    HINSTANCE instance_{};
    HWND window_{};
    HWND title_{};
    HWND canvas_{};
    HWND status_{};
    HWND updateButton_{};
    std::array<HWND, 13> buttons_{};
    std::array<HWND, 3> railButtons_{};
    HFONT titleFont_{};
    HFONT uiFont_{};
    HFONT metaFont_{};
    HBRUSH backgroundBrush_{};
    HBRUSH headerBrush_{};
    HBRUSH dockBrush_{};
    HICON appIcon_{};
    RECT canvasFrameRect_{};
    RECT canvasMetaRect_{};
    RECT pageRailRect_{};
    RECT pageItemRect_{};
    RECT pagePreviewRect_{};
    RECT documentTitleRect_{};
    RECT colorGroupLabelRect_{};
    RECT widthGroupLabelRect_{};
    RECT actionGroupLabelRect_{};

    winrt::com_ptr<IDCompositionDevice> compositionDevice_;
    winrt::com_ptr<IDCompositionTarget> compositionTarget_;
    winrt::com_ptr<IDCompositionVisual> inkVisual_;
    winrt::com_ptr<IInkDesktopHost> inkHost_;
    std::shared_ptr<InkThreadState> inkThreadState_;

    bool inkReady_{};
    bool fullscreen_{};
    bool pageChangePending_{};
    std::size_t activePage_{};
    std::size_t pageCount_{1};
    bool pageRailVisible_{};
    bool canvasGrid_{};
    CanvasTheme canvasTheme_{CanvasTheme::Chalkboard};
    COLORREF canvasBackground_{kCanvasChalkboard};
    int workspaceLeft_{};
    DWORD windowedStyle_{};
    WINDOWPLACEMENT windowedPlacement_{sizeof(WINDOWPLACEMENT)};
    int lastCanvasWidth_{};
    int lastCanvasHeight_{};
    int lastInputX_{-1};
    int lastInputY_{-1};
    std::uint32_t inputSequence_{};
    std::uint32_t strokeCount_{};
    std::uint32_t undoDepth_{};
    colors::Color penColor_{ToInkColor(kChalkWhite)};
    float penSize_{kPenMediumSize};
    int activeColorControl_{ControlBlack};
    int activeWidthControl_{ControlMedium};
    bool eraserActive_{};
    InkOperationStage lastInkErrorStage_{InkOperationStage::Unknown};
    std::wstring inputSource_{L"等待落笔"};
    std::wstring penColorLabel_{L"粉白"};
    std::wstring penWidthLabel_{L"中笔"};
    std::wstring mode_{L"粉白 · 中笔"};
};
}

int WINAPI wWinMain(
    const HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    const int showCommand)
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        INITCOMMONCONTROLSEX commonControls{};
        commonControls.dwSize = sizeof(commonControls);
        commonControls.dwICC = ICC_STANDARD_CLASSES;
        if (!InitCommonControlsEx(&commonControls))
        {
            winrt::throw_last_error();
        }

        Application application(instance);
        return application.Run(showCommand);
    }
    catch (...)
    {
        const HRESULT result = winrt::to_hresult();
        const std::wstring message = L"南荒白板启动失败：\n\n" + FormatHresult(result);
        MessageBoxW(nullptr, message.c_str(), L"南荒白板产品预览", MB_OK | MB_ICONERROR);
        return static_cast<int>(result);
    }
}
