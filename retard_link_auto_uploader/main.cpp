#include <windows.h>
#include <commctrl.h>
#include <wininet.h>
#include <string>
#include <thread>
#include <regex>
#include <atomic>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "wininet.lib")

// Constants
constexpr UINT WM_TRAYICON = WM_USER + 1;
constexpr UINT IDI_TRAY = 101;
constexpr UINT IDM_EXIT = 102;
constexpr UINT IDM_AUTOSTART = 103;
constexpr UINT IDM_LENGTH_4 = 104;
constexpr UINT IDM_LENGTH_5 = 105;
constexpr UINT IDM_LENGTH_6 = 106;
constexpr UINT IDM_LENGTH_7 = 107;
constexpr UINT IDM_LENGTH_8 = 108;
constexpr UINT IDI_MYICON = 109;
constexpr UINT IDM_ABOUT = 110;
constexpr UINT HOTKEY_ID = 1;

// Global variables
static HWND g_hwnd = nullptr;
static NOTIFYICONDATA g_nid = { 0 };
static std::atomic<bool> g_autostart{ false };
static std::atomic<int> g_id_length{ 4 }; // Default ID length now 4
static std::atomic<bool> g_processing{ false }; // Prevent multiple simultaneous requests

// Application constants
static constexpr const char* APP_NAME = "RetardLink";
static constexpr const char* APP_VERSION = "1.1";
static constexpr const char* APP_AUTHOR = "nloginov";
static constexpr const char* APP_WEBSITE = "https://nlog.us/ls/";
static constexpr const char* APP_BUILD_DATE = __DATE__;
static constexpr const char* API_URL = "https://nlog.us/ls/main.php";
static constexpr const char* BASE_SHORT_URL = "https://nlog.us/r/";

// Pre-compiled regex for better performance
static const std::regex url_regex_pattern(R"(https?://[^\s<>"{}|\\^`\[\]]+)", std::regex::optimize);
static const std::regex json_short_url_pattern("\"short_url\"\\s*:\\s*\"([^\"]+)\"", std::regex::optimize);

// Function prototypes
LRESULT CALLBACK window_proc(HWND hwnd, UINT u_msg, WPARAM w_param, LPARAM l_param);
void create_tray_icon(HWND hwnd);
void remove_tray_icon();
std::string get_clipboard_text();
bool is_valid_url(const std::string& text);
std::string shorten_url(const std::string& url, int length);
void set_autostart(bool enable);
bool is_autostart_enabled();
std::string url_escape(const std::string& url);
void show_about_dialog(HWND hwnd);
void save_settings();
void load_settings();
void show_notification(const char* title, const char* message, DWORD flags);
std::string extract_code_from_url(const std::string& url);

// URL shortener namespace
namespace url_shortener {
    int start_application(HINSTANCE h_instance) {
        // Initialize COM for WinINet
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
            MessageBox(nullptr, "Failed to initialize COM!", "Error", MB_ICONERROR);
            return 1;
        }

        // Initialize common controls
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icc.dwICC = ICC_WIN95_CLASSES;
        if (!InitCommonControlsEx(&icc)) {
            CoUninitialize();
            return 1;
        }

        // Register window class
        WNDCLASSEX wc{};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = window_proc;
        wc.hInstance = h_instance;
        wc.lpszClassName = "URLShortenerClass";
        wc.hIcon = LoadIcon(h_instance, MAKEINTRESOURCE(IDI_MYICON));
        wc.hIconSm = LoadIcon(h_instance, MAKEINTRESOURCE(IDI_MYICON));

        if (!RegisterClassEx(&wc)) {
            CoUninitialize();
            return 1;
        }

        // Create hidden window
        g_hwnd = CreateWindowEx(
            0, "URLShortenerClass", APP_NAME,
            0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, h_instance, nullptr
        );

        if (!g_hwnd) {
            MessageBox(nullptr, "Failed to create window!", "Error", MB_ICONERROR);
            CoUninitialize();
            return 1;
        }

        // Load settings
        load_settings();

        // Register hotkey (Ctrl+Alt+U)
        if (!RegisterHotKey(g_hwnd, HOTKEY_ID, MOD_CONTROL | MOD_ALT, 'U')) {
            MessageBox(nullptr, "Failed to register hotkey (Ctrl+Alt+U)!\nAnother application may be using this hotkey.", "Warning", MB_ICONWARNING);
        }

        // Create tray icon
        create_tray_icon(g_hwnd);

        // Check if autostart is enabled
        g_autostart = is_autostart_enabled();

        // Message loop
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Cleanup
        remove_tray_icon();
        UnregisterHotKey(g_hwnd, HOTKEY_ID);
        CoUninitialize();

        return static_cast<int>(msg.wParam);
    }

    void handle_clipboard_url() {
        // Prevent multiple simultaneous requests
        if (g_processing.exchange(true)) {
            show_notification("URL Shortener", "Already processing a request, please wait...", NIIF_INFO);
            return;
        }

        // Get clipboard content
        std::string clipboard_text = get_clipboard_text();

        if (clipboard_text.empty()) {
            show_notification("URL Shortener", "Clipboard is empty.", NIIF_WARNING);
            g_processing = false;
            return;
        }

        // Check if it's a URL
        if (!is_valid_url(clipboard_text)) {
            show_notification("URL Shortener", "No valid URL found in clipboard.", NIIF_WARNING);
            g_processing = false;
            return;
        }

        // Shorten URL in a separate thread
        std::thread shortener_thread([clipboard_text]() {
            const int current_length = g_id_length.load();
            std::string short_url = shorten_url(clipboard_text, current_length);

            if (!short_url.empty()) {
                // Copy shortened URL to clipboard
                if (OpenClipboard(g_hwnd)) {
                    EmptyClipboard();
                    const size_t url_size = short_url.size() + 1;
                    HGLOBAL h_global = GlobalAlloc(GMEM_MOVEABLE, url_size);
                    if (h_global) {
                        char* p_global = static_cast<char*>(GlobalLock(h_global));
                        if (p_global) {
                            memcpy(p_global, short_url.c_str(), url_size);
                            GlobalUnlock(h_global);
                            SetClipboardData(CF_TEXT, h_global);

                            // Show success notification with better formatting
                            std::string message = "Shortened URL copied to clipboard:\n" + short_url;
                            show_notification("URL Shortened Successfully", message.c_str(), NIIF_INFO);
                        }
                        else {
                            GlobalFree(h_global);
                            show_notification("Error", "Failed to copy URL to clipboard.", NIIF_ERROR);
                        }
                    }
                    else {
                        show_notification("Error", "Failed to allocate memory for clipboard.", NIIF_ERROR);
                    }
                    CloseClipboard();
                }
                else {
                    show_notification("Error", "Failed to access clipboard.", NIIF_ERROR);
                }
            }
            else {
                show_notification("Error", "Failed to shorten URL.\nPlease check your internet connection.", NIIF_ERROR);
            }

            g_processing = false;
            });
        shortener_thread.detach();
    }
}

// Window procedure
LRESULT CALLBACK window_proc(HWND hwnd, UINT u_msg, WPARAM w_param, LPARAM l_param) {
    switch (u_msg) {
    case WM_HOTKEY:
        if (w_param == HOTKEY_ID) {
            url_shortener::handle_clipboard_url();
        }
        break;

    case WM_TRAYICON:
        if (l_param == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);

            // Create length submenu
            HMENU h_length_menu = CreatePopupMenu();
            AppendMenu(h_length_menu, g_id_length == 4 ? MF_CHECKED : MF_UNCHECKED, IDM_LENGTH_4, "4 characters");
            AppendMenu(h_length_menu, g_id_length == 5 ? MF_CHECKED : MF_UNCHECKED, IDM_LENGTH_5, "5 characters");
            AppendMenu(h_length_menu, g_id_length == 6 ? MF_CHECKED : MF_UNCHECKED, IDM_LENGTH_6, "6 characters");
            AppendMenu(h_length_menu, g_id_length == 7 ? MF_CHECKED : MF_UNCHECKED, IDM_LENGTH_7, "7 characters");
            AppendMenu(h_length_menu, g_id_length == 8 ? MF_CHECKED : MF_UNCHECKED, IDM_LENGTH_8, "8 characters");

            // Create main menu
            HMENU h_menu = CreatePopupMenu();
            AppendMenu(h_menu, g_autostart ? MF_CHECKED : MF_UNCHECKED, IDM_AUTOSTART, "Start with Windows");
            AppendMenu(h_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(h_length_menu), "ID Length");
            AppendMenu(h_menu, MF_SEPARATOR, 0, nullptr);
            AppendMenu(h_menu, MF_STRING, IDM_ABOUT, "About");
            AppendMenu(h_menu, MF_SEPARATOR, 0, nullptr);
            AppendMenu(h_menu, MF_STRING, IDM_EXIT, "Exit");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(h_menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
            PostMessage(hwnd, WM_NULL, 0, 0);

            DestroyMenu(h_length_menu);
            DestroyMenu(h_menu);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(w_param)) {
        case IDM_EXIT:
            PostQuitMessage(0);
            break;

        case IDM_AUTOSTART:
            g_autostart = !g_autostart;
            set_autostart(g_autostart);
            break;

        case IDM_LENGTH_4:
        case IDM_LENGTH_5:
        case IDM_LENGTH_6:
        case IDM_LENGTH_7:
        case IDM_LENGTH_8:
            g_id_length = LOWORD(w_param) - IDM_LENGTH_4 + 4;
            save_settings();
            break;

        case IDM_ABOUT:
            show_about_dialog(hwnd);
            break;
        }
        break;

    case WM_DESTROY:
        if (g_nid.hIcon && g_nid.hIcon != LoadIcon(nullptr, IDI_APPLICATION)) {
            DestroyIcon(g_nid.hIcon);
        }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, u_msg, w_param, l_param);
    }

    return 0;
}

void show_about_dialog(HWND hwnd) {
    char about_message[512];
    sprintf_s(about_message,
        "%s v%s\n\n"
        "Author: %s\n"
        "Website: %s\n"
        "Build Date: %s\n\n"
        "A lightweight URL shortener utility.\n"
        "Use Ctrl+Alt+U to shorten URLs from clipboard.\n\n"
        "Features:\n"
        "• Configurable ID length (4-8 characters)\n"
        "• Auto-start with Windows\n"
        "• System tray integration",
        APP_NAME, APP_VERSION, APP_AUTHOR, APP_WEBSITE, APP_BUILD_DATE);

    MessageBox(hwnd, about_message, "About RetardLink", MB_ICONINFORMATION | MB_OK);
}

void create_tray_icon(HWND hwnd) {
    HICON h_icon = nullptr;

    // Try loading from resources first
    h_icon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_MYICON));

    // If that fails, try loading from file
    if (!h_icon) {
        char exe_path[MAX_PATH];
        GetModuleFileName(nullptr, exe_path, MAX_PATH);

        // Try to extract icon from the exe itself
        UINT extracted = ExtractIconEx(exe_path, 0, &h_icon, nullptr, 1);
        if (extracted == 0) {
            h_icon = LoadIcon(nullptr, IDI_APPLICATION); // Fall back to system icon
        }
    }

    // Setup the notification icon data
    ZeroMemory(&g_nid, sizeof(NOTIFYICONDATA));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hwnd;
    g_nid.uID = IDI_TRAY;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = h_icon;
    strcpy_s(g_nid.szTip, "URL Shortener (Ctrl+Alt+U)");
    Shell_NotifyIcon(NIM_ADD, &g_nid);

    // Also set the window icon
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(h_icon));
    SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(h_icon));
}

void remove_tray_icon() {
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

void show_notification(const char* title, const char* message, DWORD flags) {
    g_nid.uFlags = NIF_INFO;
    strcpy_s(g_nid.szInfoTitle, title);
    strcpy_s(g_nid.szInfo, message);
    g_nid.dwInfoFlags = flags;
    g_nid.uTimeout = 5000; // 5 seconds
    Shell_NotifyIcon(NIM_MODIFY, &g_nid);
}

// API related functions
std::string get_clipboard_text() {
    std::string clipboard_text;

    if (OpenClipboard(g_hwnd)) {
        HANDLE h_data = GetClipboardData(CF_TEXT);
        if (h_data) {
            char* psz_text = static_cast<char*>(GlobalLock(h_data));
            if (psz_text) {
                clipboard_text = psz_text;
                GlobalUnlock(h_data);
            }
        }
        CloseClipboard();
    }

    return clipboard_text;
}

bool is_valid_url(const std::string& text) {
    return std::regex_search(text, url_regex_pattern);
}

std::string extract_code_from_url(const std::string& url) {
    // Extract code from various URL formats
    std::regex code_regex(R"([?&]code=([^&]+))");
    std::smatch match;

    if (std::regex_search(url, match, code_regex)) {
        return match[1].str();
    }

    return "";
}

std::string shorten_url(const std::string& url, int length) {
    // Use RAII for automatic cleanup
    struct internet_handle {
        HINTERNET handle;
        internet_handle(HINTERNET h) : handle(h) {}
        ~internet_handle() { if (handle) InternetCloseHandle(handle); }
        operator HINTERNET() const { return handle; }
    };

    internet_handle h_internet(InternetOpen("URL Shortener Client/1.1", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0));
    if (!h_internet) {
        return "";
    }

    // Extract first URL from text using the pre-compiled regex
    std::smatch match;
    std::string original_url;

    if (std::regex_search(url, match, url_regex_pattern)) {
        original_url = match[0].str();
    }
    else {
        return "";
    }

    // Build the API request URL
    std::string api_request_url = std::string(API_URL) +
        "?action=create" +
        "&url=" + url_escape(original_url) +
        "&length=" + std::to_string(length) +
        "&password=";

    internet_handle h_connect(InternetOpenUrl(h_internet, api_request_url.c_str(), nullptr, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0));
    if (!h_connect) {
        return "";
    }

    // Read the response with better buffer management
    constexpr DWORD BUFFER_SIZE = 4096;
    std::string response;
    response.reserve(BUFFER_SIZE);

    char buffer[BUFFER_SIZE];
    DWORD bytes_read;

    while (InternetReadFile(h_connect, buffer, BUFFER_SIZE - 1, &bytes_read) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        response += buffer;
    }

    // Parse the JSON response using pre-compiled regex
    std::string short_url;

    if (std::regex_search(response, match, json_short_url_pattern)) {
        std::string temp_url = match[1].str();

        // Extract code and convert to the new format
        std::string code = extract_code_from_url(temp_url);
        if (!code.empty()) {
            short_url = std::string(BASE_SHORT_URL) + code;
        }
        else {
            short_url = temp_url; // Fallback to original if code extraction fails
        }
    }

    return short_url;
}

std::string url_escape(const std::string& url) {
    std::string result;
    result.reserve(url.length() * 3); // Reserve space to avoid reallocations

    for (unsigned char c : url) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        }
        else {
            char buffer[4];
            sprintf_s(buffer, "%%%02X", c);
            result += buffer;
        }
    }
    return result;
}

// Windows autostart functionality
void set_autostart(bool enable) {
    HKEY h_key;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &h_key) == ERROR_SUCCESS) {
        if (enable) {
            char path[MAX_PATH];
            GetModuleFileName(nullptr, path, MAX_PATH);
            DWORD path_length = static_cast<DWORD>(strlen(path) + 1);
            RegSetValueEx(h_key, APP_NAME, 0, REG_SZ, reinterpret_cast<const BYTE*>(path), path_length);
        }
        else {
            RegDeleteValue(h_key, APP_NAME);
        }
        RegCloseKey(h_key);
    }
}

bool is_autostart_enabled() {
    HKEY h_key;
    bool enabled = false;

    if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &h_key) == ERROR_SUCCESS) {
        char value[MAX_PATH];
        DWORD value_size = sizeof(value);

        if (RegQueryValueEx(h_key, APP_NAME, nullptr, nullptr, reinterpret_cast<BYTE*>(value), &value_size) == ERROR_SUCCESS) {
            enabled = true;
        }

        RegCloseKey(h_key);
    }

    return enabled;
}

// Settings management
void save_settings() {
    HKEY h_key;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\URLShortener", 0, nullptr, 0, KEY_WRITE, nullptr, &h_key, nullptr) == ERROR_SUCCESS) {
        int length = g_id_length.load();
        RegSetValueEx(h_key, "IDLength", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&length), sizeof(length));
        RegCloseKey(h_key);
    }
}

void load_settings() {
    HKEY h_key;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\URLShortener", 0, KEY_READ, &h_key) == ERROR_SUCCESS) {
        int length;
        DWORD value_size = sizeof(length);
        DWORD type;

        if (RegQueryValueEx(h_key, "IDLength", nullptr, &type, reinterpret_cast<BYTE*>(&length), &value_size) == ERROR_SUCCESS) {
            // Validate the loaded value
            if (length >= 4 && length <= 8) {
                g_id_length = length;
            }
        }
        RegCloseKey(h_key);
    }
}

// Main entry point
int WINAPI WinMain(_In_ HINSTANCE h_instance, _In_opt_ HINSTANCE h_prev_instance, _In_ LPSTR lp_cmd_line, _In_ int n_cmd_show) {
    return url_shortener::start_application(h_instance);
}