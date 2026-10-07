#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#include "remapper.h"
#include "scheme_manager.h"

namespace {
constexpr wchar_t AppName[] = L"RestoreChineseSymbol";
constexpr wchar_t WindowClass[] = L"RestoreChineseSymbol.Window";
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT InputFailureMessage = WM_APP + 2;
constexpr UINT ToggleCommand = 100;
constexpr UINT AboutCommand = 101;
constexpr UINT ExitCommand = 102;
constexpr UINT NextSchemeCommand = 103;
constexpr UINT ConfigDetailsCommand = 104;
constexpr UINT SchemeCommandBase = 1000;
constexpr int ToggleHotkey = 1;
constexpr int NextSchemeHotkey = 2;
constexpr UINT_PTR SmokeTimer = 1;
constexpr ULONG_PTR InputTag = 0x52435331;

HWND window = nullptr;
HHOOK keyboardHook = nullptr;
symbols::Remapper remapper;
std::unique_ptr<symbols::SchemeManager> schemes;
std::wstring configPath;
bool enabled = true;
bool trayAdded = false;
bool hotkeyRegistered = false;
bool nextHotkeyRegistered = false;
bool inputFailureReported = false;
bool smokeTest = false;
UINT taskbarCreatedMessage = 0;

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count == 0) return L"(invalid UTF-8)";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), &result[0], count);
    return result;
}

symbols::Configuration loadConfiguration() {
    symbols::Configuration fallback;
    auto failed = [&](const std::string& message) {
        fallback.issues.push_back({0, "file", message});
        return fallback;
    };
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return failed("Cannot determine executable directory.");
    configPath.assign(path.data(), length);
    const auto slash = configPath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return failed("Executable path has no directory.");
    configPath.resize(slash + 1);
    configPath += L"schemes.ini";

    HANDLE file = CreateFileW(configPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return fallback;
        return failed("Cannot open schemes.ini. Windows error: " + std::to_string(error));
    }
    // A bounded startup read, including one extra byte to detect an oversized
    // file. No file handles, reload timers, or watchers survive this function.
    std::string bytes(symbols::MaxConfigBytes + 1, '\0');
    DWORD total = 0;
    DWORD error = ERROR_SUCCESS;
    while (total < bytes.size()) {
        DWORD read = 0;
        if (!ReadFile(file, &bytes[total], static_cast<DWORD>(bytes.size() - total), &read, nullptr)) {
            error = GetLastError();
            break;
        }
        if (read == 0) break;
        total += read;
    }
    CloseHandle(file);
    if (error != ERROR_SUCCESS)
        return failed("Cannot read schemes.ini. Windows error: " + std::to_string(error));
    bytes.resize(total);
    return symbols::parseConfig(bytes);
}

std::wstring currentName() {
    return wide(schemes->current().name);
}

std::wstring nextHotkeyLabel() {
    return wide(symbols::hotkeyText(schemes->config().nextHotkey));
}

std::wstring menuText(const std::wstring& text) {
    std::wstring escaped;
    for (wchar_t ch : text) {
        escaped += ch;
        if (ch == L'&') escaped += L'&';
    }
    return escaped;
}

void showError(const wchar_t* operation, DWORD error) {
    const std::wstring message = std::wstring(operation) +
        L"\nWindows error: " + std::to_wstring(error);
    MessageBoxW(nullptr, message.c_str(), AppName, MB_OK | MB_ICONERROR);
}

NOTIFYICONDATAW trayData() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = 1;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = TrayMessage;
    data.hIcon = LoadIconW(nullptr, enabled ? IDI_INFORMATION : IDI_WARNING);
    std::wstring tip = (enabled ? L"启用 · " : L"暂停 · ") + currentName();
    tip += hotkeyRegistered ? L"\nCtrl+Alt+F9 启用/暂停" : L"\n暂停快捷键不可用，右键操作";
    tip += nextHotkeyRegistered ? L"\n" + nextHotkeyLabel() + L" 切换方案"
                               : L"\n切换快捷键不可用，右键选择方案";
    lstrcpynW(data.szTip, tip.c_str(), static_cast<int>(sizeof(data.szTip) / sizeof(wchar_t)));
    return data;
}

bool updateTray(bool add = false) {
    auto data = trayData();
    const bool success = Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data) != FALSE;
    if (add) trayAdded = success;
    return success;
}

void notify(const wchar_t* message, DWORD icon = NIIF_INFO) {
    auto data = trayData();
    data.uFlags = NIF_INFO;
    lstrcpynW(data.szInfoTitle, L"符号替换", 64);
    lstrcpynW(data.szInfo, message, 256);
    data.dwInfoFlags = icon;
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void toggleEnabled() {
    enabled = !enabled;
    inputFailureReported = false;
    updateTray();
}

void applySelection(bool changed) {
    if (!changed) return;
    remapper.selectTable(schemes->current().table);
    inputFailureReported = false;
    updateTray();
    const std::wstring message = L"当前方案：" + currentName() +
        (enabled ? L"（启用）" : L"（暂停，恢复后生效）");
    notify(message.c_str());
}

std::wstring characterText(wchar_t ch) {
    if (ch == 0) return L"透传";
    wchar_t code[16]{};
    std::swprintf(code, sizeof(code) / sizeof(wchar_t), L"U+%04X", static_cast<unsigned>(ch));
    return std::wstring(1, ch) + L" (" + code + L")";
}

void showAbout(HWND hwnd) {
    std::wstring text = L"当前方案：" + currentName() + L" [" + wide(schemes->current().id) + L"]\n";
    text += L"美式键位：无 Shift / 按住 Shift\n";
    const auto& table = schemes->current().table;
    for (unsigned key = 0; key < symbols::KeyCount; ++key) {
        if (table.plain[key] == 0 && table.shifted[key] == 0) continue;
        text += wide(symbols::mappingKeyName(key, false)) + L"：" + characterText(table.plain[key]) +
            L" / " + characterText(table.shifted[key]) + L"\n";
    }
    text += L"\n未列出的按键透传；Ctrl、Alt、Win 组合键透传。\n";
    text += L"Ctrl+Alt+F9 或单击托盘：启用/暂停。\n";
    text += nextHotkeyLabel() + L"：下一方案；右键菜单可直接选择。\n";
    text += L"切换时已按住的键沿用原表，松开后新按键使用新表。\n";
    text += L"schemes.ini 仅在启动时加载，修改后需重启。\n";
    text += L"不主动提交候选词，不处理粘贴、符号面板或已有文本。";
    MessageBoxW(hwnd, text.c_str(), L"符号替换 · 当前映射", MB_OK | MB_ICONINFORMATION);
}

void showConfigDetails(HWND hwnd) {
    const auto& config = schemes->config();
    std::wstring text = L"配置位置：\n" + configPath + L"\n\n已加载 " +
        std::to_wstring(config.schemes.size()) + L" 个方案（含内置默认）。\n";
    text += hotkeyRegistered ? L"暂停快捷键可用。\n" : L"暂停快捷键注册失败，可使用托盘。\n";
    text += nextHotkeyRegistered ? L"方案切换快捷键可用。\n" : L"方案切换快捷键注册失败，可使用托盘。\n";
    if (config.issues.empty()) text += L"配置无错误；文件不存在或为空时使用内置默认。";
    else {
        text += L"\n加载问题：" + std::to_wstring(config.issues.size()) + L" 项\n";
        std::size_t shown = 0;
        for (const auto& issue : config.issues) {
            if (shown++ == 20) { text += L"（仅显示前 20 项，请修正后重启检查。）\n"; break; }
            text += L"行 " + std::to_wstring(issue.line) + L" [" + wide(issue.section) + L"] " +
                wide(issue.message) + L"\n";
        }
    }
    MessageBoxW(hwnd, text.c_str(), L"符号替换 · 配置加载详情", MB_OK | MB_ICONINFORMATION);
}

unsigned normalizeModifier(const KBDLLHOOKSTRUCT& key) {
    switch (key.vkCode) {
    case VK_SHIFT: return key.scanCode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    case VK_CONTROL: return key.flags & LLKHF_EXTENDED ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU: return key.flags & LLKHF_EXTENDED ? VK_RMENU : VK_LMENU;
    default: return key.vkCode;
    }
}

bool sendCharacter(wchar_t character) {
    INPUT input[2]{};
    for (auto& event : input) {
        event.type = INPUT_KEYBOARD;
        event.ki.wScan = static_cast<WORD>(character);
        event.ki.dwFlags = KEYEVENTF_UNICODE;
        event.ki.dwExtraInfo = InputTag;
    }
    input[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    const UINT sent = SendInput(2, input, sizeof(INPUT));
    if (sent != 2) {
        // A partial send may already have inserted the character; never append
        // the original symbol as well. Best-effort release of that Unicode key.
        if (sent == 1) SendInput(1, &input[1], sizeof(INPUT));
        if (!inputFailureReported) {
            inputFailureReported = true;
            PostMessageW(window, InputFailureMessage, sent, 0);
        }
    }
    return sent != 0;
}

LRESULT CALLBACK keyboardProcedure(int code, WPARAM message, LPARAM parameter) {
    if (code != HC_ACTION) return CallNextHookEx(keyboardHook, code, message, parameter);
    const auto& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(parameter);
    if ((key.flags & LLKHF_INJECTED) && key.dwExtraInfo == InputTag)
        return CallNextHookEx(keyboardHook, code, message, parameter);

    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (!down && !up) return CallNextHookEx(keyboardHook, code, message, parameter);
    const symbols::KeyEvent event{
        normalizeModifier(key), down, (key.flags & LLKHF_INJECTED) != 0};
    if (remapper.process(event, enabled, sendCharacter)) return 1;
    return CallNextHookEx(keyboardHook, code, message, parameter);
}

void showTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | (enabled ? MF_CHECKED : MF_UNCHECKED),
                ToggleCommand, hotkeyRegistered ? L"启用符号替换\tCtrl+Alt+F9"
                                               : L"启用符号替换（快捷键不可用）");
    HMENU schemeMenu = CreatePopupMenu();
    if (schemeMenu) {
        const auto& entries = schemes->config().schemes;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const std::wstring label = menuText(wide(entries[i].name) + L" [" + wide(entries[i].id) + L"]");
            AppendMenuW(schemeMenu, MF_STRING | (i == schemes->currentIndex() ? MF_CHECKED : MF_UNCHECKED),
                SchemeCommandBase + static_cast<UINT>(i), label.c_str());
        }
        if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(schemeMenu), L"选择方案"))
            DestroyMenu(schemeMenu);
    }
    const std::wstring nextLabel = L"下一方案\t" +
        (nextHotkeyRegistered ? nextHotkeyLabel() : L"快捷键不可用");
    AppendMenuW(menu, MF_STRING | (schemes->config().schemes.size() > 1 ? MF_ENABLED : MF_GRAYED),
        NextSchemeCommand, nextLabel.c_str());
    AppendMenuW(menu, MF_STRING, AboutCommand, L"当前映射与使用说明");
    AppendMenuW(menu, MF_STRING, ConfigDetailsCommand, L"配置加载详情");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ExitCommand, L"退出");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
    PostMessageW(window, WM_NULL, 0, 0);
    if (command != 0) PostMessageW(window, WM_COMMAND, command, 0);
}

LRESULT CALLBACK windowProcedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (taskbarCreatedMessage != 0 && message == taskbarCreatedMessage && !smokeTest) {
        updateTray(true);
        return 0;
    }
    switch (message) {
    case WM_HOTKEY:
        if (wparam == ToggleHotkey) toggleEnabled();
        else if (wparam == NextSchemeHotkey) applySelection(schemes->next());
        return 0;
    case TrayMessage:
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) showTrayMenu();
        else if (lparam == WM_LBUTTONUP) toggleEnabled();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) >= SchemeCommandBase &&
            LOWORD(wparam) < SchemeCommandBase + schemes->config().schemes.size()) {
            applySelection(schemes->select(static_cast<std::size_t>(LOWORD(wparam) - SchemeCommandBase)));
            return 0;
        }
        switch (LOWORD(wparam)) {
        case ToggleCommand: toggleEnabled(); break;
        case NextSchemeCommand: applySelection(schemes->next()); break;
        case AboutCommand: showAbout(hwnd); break;
        case ConfigDetailsCommand: showConfigDetails(hwnd); break;
        case ExitCommand: DestroyWindow(hwnd); break;
        }
        return 0;
    case InputFailureMessage:
        notify(wparam == 0
            ? L"字符注入失败，已放行原按键。管理员应用可能需要相同权限。"
            : L"字符注入未完整完成。建议暂停替换并检查目标应用。", NIIF_WARNING);
        return 0;
    case WM_TIMER:
        if (wparam == SmokeTimer) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (keyboardHook) {
            UnhookWindowsHookEx(keyboardHook);
            keyboardHook = nullptr;
        }
        if (hotkeyRegistered) UnregisterHotKey(hwnd, ToggleHotkey);
        if (nextHotkeyRegistered) UnregisterHotKey(hwnd, NextSchemeHotkey);
        if (trayAdded) {
            auto data = trayData();
            Shell_NotifyIconW(NIM_DELETE, &data);
            trayAdded = false;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    smokeTest = std::wcscmp(commandLine, L"--smoke-test") == 0;
    if (*commandLine != L'\0' && !smokeTest) {
        MessageBoxW(nullptr, L"支持的参数：--smoke-test（暂停状态下检查钩子并自动退出）",
                    AppName, MB_OK | MB_ICONINFORMATION);
        return 2;
    }
    enabled = !smokeTest;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\RestoreChineseSymbol.Instance");
    if (!mutex) {
        if (!smokeTest) showError(L"无法创建单实例锁", GetLastError());
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        if (!smokeTest) MessageBoxW(nullptr, L"程序已运行，请在系统托盘操作。", AppName, MB_OK);
        return 2;
    }

    schemes.reset(new symbols::SchemeManager(loadConfiguration()));
    remapper.selectTable(schemes->current().table);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = WindowClass;
    if (!RegisterClassW(&windowClass)) {
        if (!smokeTest) showError(L"窗口类注册失败", GetLastError());
        CloseHandle(mutex);
        return 1;
    }
    // A hidden top-level window receives TaskbarCreated after Explorer restarts.
    window = CreateWindowExW(0, WindowClass, AppName, WS_OVERLAPPED,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) {
        if (!smokeTest) showError(L"窗口创建失败", GetLastError());
        CloseHandle(mutex);
        return 1;
    }

    // Seed already-held modifiers before installing the hook. Inside the hook,
    // use the event stream: asynchronous state has not yet applied that event.
    for (unsigned key : {symbols::LeftShift, symbols::RightShift,
         symbols::LeftControl, symbols::RightControl, symbols::LeftAlt,
         symbols::RightAlt, symbols::LeftWindows, symbols::RightWindows}) {
        remapper.setModifier(key, (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0);
    }
    keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProcedure, instance, 0);
    if (!keyboardHook) {
        if (!smokeTest) showError(L"键盘钩子安装失败", GetLastError());
        DestroyWindow(window);
        CloseHandle(mutex);
        return 1;
    }

    if (smokeTest) {
        if (!SetTimer(window, SmokeTimer, 250, nullptr)) {
            DestroyWindow(window);
            CloseHandle(mutex);
            return 1;
        }
    } else {
        hotkeyRegistered = RegisterHotKey(window, ToggleHotkey,
            MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F9) != FALSE;
        const auto& hotkey = schemes->config().nextHotkey;
        const UINT modifiers = MOD_NOREPEAT | (hotkey.control ? MOD_CONTROL : 0) |
            (hotkey.alt ? MOD_ALT : 0) | (hotkey.shift ? MOD_SHIFT : 0) |
            (hotkey.windows ? MOD_WIN : 0);
        nextHotkeyRegistered = RegisterHotKey(window, NextSchemeHotkey, modifiers, hotkey.key) != FALSE;
        taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
        if (!updateTray(true)) {
            DestroyWindow(window);
            MessageBoxW(nullptr, L"无法创建托盘图标，程序已退出。", AppName, MB_OK | MB_ICONERROR);
            CloseHandle(mutex);
            return 1;
        }
        std::wstring startupMessage = L"已启用：" + currentName() + L"。已加载 " +
            std::to_wstring(schemes->config().schemes.size()) + L" 个方案。\n";
        startupMessage += hotkeyRegistered ? L"Ctrl+Alt+F9 暂停。" : L"暂停快捷键不可用，请单击托盘。";
        startupMessage += nextHotkeyRegistered ? nextHotkeyLabel() + L" 切换方案。"
                                              : L"切换快捷键不可用，请右键选择方案。";
        if (!schemes->config().issues.empty()) startupMessage += L"\n配置存在问题，请右键查看“配置加载详情”。";
        notify(startupMessage.c_str(), hotkeyRegistered && nextHotkeyRegistered && schemes->config().issues.empty()
            ? NIIF_INFO : NIIF_WARNING);
    }

    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (IsWindow(window)) DestroyWindow(window);
    CloseHandle(mutex);
    if (result == -1) return 1;
    if (smokeTest && !schemes->config().issues.empty()) return 3;
    return static_cast<int>(message.wParam);
}
