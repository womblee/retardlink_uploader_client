#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <wininet.h>
#include <string>
#include <thread>
#include <regex>
#include <iostream>
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "wininet.lib")

// Constants
#define WM_TRAYICON (WM_USER + 1)
#define IDI_TRAY 101
#define IDM_EXIT 102
#define IDM_AUTOSTART 103
#define IDM_LENGTH_5 104
#define IDM_LENGTH_6 105
#define IDM_LENGTH_7 106
#define IDM_LENGTH_8 107
#define IDI_MYICON 108
#define IDM_ABOUT 109
#define HOTKEY_ID 1

// Global variables
HWND g_hwnd = NULL;
NOTIFYICONDATA g_nid = { 0 };
bool g_autostart = false;
int g_id_length = 5; // Default ID length

const char* APP_NAME = "RetardLink";
const char* APP_VERSION = "1.0";
const char* APP_AUTHOR = "nloginov";
const char* APP_WEBSITE = "https://nlog.us/ls/";
const char* APP_BUILD_DATE = __DATE__;
const char* API_URL = "https://nlog.us/ls/main.php";

// Function prototypes
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void create_tray_icon(HWND hwnd);
void update_tray_icon(HWND hwnd);
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

// Shortener related
namespace big
{
    int start_url_shortener(HINSTANCE hInstance)
    {
        // Initialize COM for WinINet
        CoInitialize(NULL);

        // Initialize common controls
        INITCOMMONCONTROLSEX icc;
        icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icc.dwICC = ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icc);

        // Register window class
        WNDCLASSEX wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = "URLShortenerClass";

        // Add these lines:
        wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MYICON));
        wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MYICON));

        RegisterClassEx(&wc);

        // Create hidden window
        g_hwnd = CreateWindowEx(
            0, "URLShortenerClass", APP_NAME,
            0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL
        );

        if (!g_hwnd)
        {
            MessageBox(NULL, "Failed to create window!", "Error", MB_ICONERROR);
            return 1;
        }

        // Load settings
        load_settings();

        // Register hotkey (Ctrl+Alt+U)
        if (!RegisterHotKey(g_hwnd, HOTKEY_ID, MOD_CONTROL | MOD_ALT, 'U'))
        {
            MessageBox(NULL, "Failed to register hotkey (Ctrl+Alt+U)!", "Error", MB_ICONERROR);
        }

        // Create tray icon
        create_tray_icon(g_hwnd);

        // Check if autostart is enabled
        g_autostart = is_autostart_enabled();

        // Message loop
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Cleanup
        remove_tray_icon();
        UnregisterHotKey(g_hwnd, HOTKEY_ID);
        CoUninitialize();

        return (int)msg.wParam;
    }

    void handle_clipboard_url()
    {
        // Get clipboard content
        std::string clipboard_text = get_clipboard_text();

        // Check if it's a URL
        if (is_valid_url(clipboard_text))
        {
            // Shorten URL in a separate thread
            std::thread shortener_thread([clipboard_text]()
                {
                    std::string short_url = shorten_url(clipboard_text, g_id_length);

                    if (!short_url.empty())
                    {
                        // Copy shortened URL to clipboard
                        if (OpenClipboard(NULL))
                        {
                            EmptyClipboard();
                            HGLOBAL h_global = GlobalAlloc(GMEM_MOVEABLE, short_url.size() + 1);
                            if (h_global)
                            {
                                char* p_global = (char*)GlobalLock(h_global);
                                memcpy(p_global, short_url.c_str(), short_url.size() + 1);
                                GlobalUnlock(h_global);
                                SetClipboardData(CF_TEXT, h_global);
                            }
                            CloseClipboard();

                            // Show notification
                            g_nid.uFlags = NIF_INFO;
                            strcpy_s(g_nid.szInfoTitle, "URL Shortened");
                            sprintf_s(g_nid.szInfo, "URL shortened and copied to clipboard:\n%s", short_url.c_str());
                            g_nid.dwInfoFlags = NIIF_INFO;
                            Shell_NotifyIcon(NIM_MODIFY, &g_nid);
                        }
                    }
                    else
                    {
                        // Show error notification
                        g_nid.uFlags = NIF_INFO;
                        strcpy_s(g_nid.szInfoTitle, "Error");
                        strcpy_s(g_nid.szInfo, "Failed to shorten URL. Check your connection.");
                        g_nid.dwInfoFlags = NIIF_ERROR;
                        Shell_NotifyIcon(NIM_MODIFY, &g_nid);
                    }
                });
            shortener_thread.detach();
        }
        else
        {
            // Show warning notification
            g_nid.uFlags = NIF_INFO;
            strcpy_s(g_nid.szInfoTitle, "Warning");
            strcpy_s(g_nid.szInfo, "No valid URL found in clipboard.");
            g_nid.dwInfoFlags = NIIF_WARNING;
            Shell_NotifyIcon(NIM_MODIFY, &g_nid);
        }
    }
}

// Tray
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID)
        {
            big::handle_clipboard_url();
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);

            HMENU h_length_menu = CreatePopupMenu();
            AppendMenu(h_length_menu, g_id_length == 5 ? MF_CHECKED : MF_UNCHECKED, IDM_LENGTH_5, "5 characters");
            AppendMenu(h_length_menu, g_id_length == 6 ? MF_CHECKED : MF_UNCHECKED, IDM_LENGTH_6, "6 characters");
            AppendMenu(h_length_menu, g_id_length == 7 ? MF_CHECKED : MF_UNCHECKED, IDM_LENGTH_7, "7 characters");
            AppendMenu(h_length_menu, g_id_length == 8 ? MF_CHECKED : MF_UNCHECKED, IDM_LENGTH_8, "8 characters");

            HMENU h_menu = CreatePopupMenu();
            AppendMenu(h_menu, g_autostart ? MF_CHECKED : MF_UNCHECKED, IDM_AUTOSTART, "Start with Windows");
            AppendMenu(h_menu, MF_POPUP, (UINT_PTR)h_length_menu, "ID Length");
            AppendMenu(h_menu, MF_SEPARATOR, 0, NULL);
            AppendMenu(h_menu, MF_STRING, IDM_ABOUT, "About");
            AppendMenu(h_menu, MF_SEPARATOR, 0, NULL);
            AppendMenu(h_menu, MF_STRING, IDM_EXIT, "Exit");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(h_menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
            PostMessage(hwnd, WM_NULL, 0, 0);

            DestroyMenu(h_length_menu);
            DestroyMenu(h_menu);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_EXIT:
            PostQuitMessage(0);
            break;

        case IDM_AUTOSTART:
            g_autostart = !g_autostart;
            set_autostart(g_autostart);
            break;

        case IDM_LENGTH_5:
        case IDM_LENGTH_6:
        case IDM_LENGTH_7:
        case IDM_LENGTH_8:
            g_id_length = LOWORD(wParam) - IDM_LENGTH_5 + 5;
            save_settings();
            break;
        case IDM_ABOUT:
            show_about_dialog(hwnd);
            break;
        }
        break;

    case WM_DESTROY:
        if (g_nid.hIcon && g_nid.hIcon != LoadIcon(NULL, IDI_APPLICATION))
            DestroyIcon(g_nid.hIcon);

        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return 0;
}

void show_about_dialog(HWND hwnd)
{
    char aboutMessage[512];
    sprintf_s(aboutMessage,
        "%s v%s\n\n"
        "Author: %s\n"
        "Website: %s\n"
        "Build Date: %s\n\n"
        "A lightweight URL shortener utility.\n"
        "Use Ctrl+Alt+U to shorten URLs from clipboard.",
        APP_NAME, APP_VERSION, APP_AUTHOR, APP_WEBSITE, APP_BUILD_DATE);

    MessageBox(hwnd, aboutMessage, "About RetardLink", MB_ICONINFORMATION | MB_OK);
}

void create_tray_icon(HWND hwnd)
{
    // Load icon from resources
    HICON h_icon = NULL;

    // Try loading from resources first
    h_icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MYICON));

    // If that fails, try loading from file
    if (!h_icon)
    {
        char exePath[MAX_PATH];
        GetModuleFileName(NULL, exePath, MAX_PATH);

        // Try to extract icon from the exe itself
        UINT extracted = ExtractIconEx(exePath, 0, &h_icon, NULL, 1);
        if (extracted == 0)
            h_icon = LoadIcon(NULL, IDI_APPLICATION); // Fall back to system icon
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
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)h_icon);
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)h_icon);
}

void update_tray_icon(HWND hwnd)
{
    Shell_NotifyIcon(NIM_MODIFY, &g_nid);
}

void remove_tray_icon()
{
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

// API related
std::string get_clipboard_text()
{
    std::string clipboard_text;

    if (OpenClipboard(NULL))
    {
        HANDLE h_data = GetClipboardData(CF_TEXT);
        if (h_data)
        {
            char* psz_text = static_cast<char*>(GlobalLock(h_data));
            if (psz_text)
            {
                clipboard_text = psz_text;
                GlobalUnlock(h_data);
            }
        }
        CloseClipboard();
    }

    return clipboard_text;
}

bool is_valid_url(const std::string& text)
{
    std::regex url_regex("https?://\\S+");
    std::smatch match;

    return std::regex_search(text, match, url_regex);
}

std::string shorten_url(const std::string& url, int length)
{
    HINTERNET h_internet = InternetOpen("URL Shortener Client", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!h_internet)
    {
        return "";
    }

    // Extract first URL from text
    std::regex url_regex("https?://\\S+");
    std::smatch match;
    std::string original_url;

    if (std::regex_search(url, match, url_regex))
    {
        original_url = match[0];
    }
    else
    {
        InternetCloseHandle(h_internet);
        return "";
    }

    // Build the API request URL
    std::string api_request_url = std::string(API_URL) +
        "?action=create" +
        "&url=" + url_escape(original_url) +
        "&length=" + std::to_string(length) +
        "&password=";

    HINTERNET h_connect = InternetOpenUrl(h_internet, api_request_url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!h_connect)
    {
        InternetCloseHandle(h_internet);
        return "";
    }

    // Read the response
    char buffer[4096];
    DWORD bytes_read;
    std::string response;

    while (InternetReadFile(h_connect, buffer, sizeof(buffer) - 1, &bytes_read) && bytes_read > 0)
    {
        buffer[bytes_read] = 0;
        response += buffer;
    }

    InternetCloseHandle(h_connect);
    InternetCloseHandle(h_internet);

    // Parse the JSON response (simplified parsing)
    std::string short_url;
    std::regex short_url_regex("\"short_url\"\\s*:\\s*\"([^\"]+)\"");

    if (std::regex_search(response, match, short_url_regex))
    {
        short_url = match[1];

        // Convert to the new format
        std::regex code_regex("[?&]code=([^&]+)");
        if (std::regex_search(short_url, match, code_regex))
        {
            std::string code = match[1];
            short_url = "https://nlog.us/r/" + code;
        }
    }

    return short_url;
}

std::string url_escape(const std::string& url)
{
    std::string result;
    for (char c : url)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            result += c;
        }
        else
        {
            char buffer[4];
            sprintf_s(buffer, "%%%02X", (unsigned char)c);
            result += buffer;
        }
    }
    return result;
}

// Windows auto start option
void set_autostart(bool enable)
{
    HKEY h_key;
    RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &h_key);

    if (enable)
    {
        char path[MAX_PATH];
        GetModuleFileName(NULL, path, MAX_PATH);
        RegSetValueEx(h_key, APP_NAME, 0, REG_SZ, (BYTE*)path, strlen(path) + 1);
    }
    else
    {
        RegDeleteValue(h_key, APP_NAME);
    }

    RegCloseKey(h_key);
}

bool is_autostart_enabled()
{
    HKEY h_key;
    bool enabled = false;

    if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &h_key) == ERROR_SUCCESS)
    {
        char value[MAX_PATH];
        DWORD value_size = sizeof(value);

        if (RegQueryValueEx(h_key, APP_NAME, NULL, NULL, (BYTE*)value, &value_size) == ERROR_SUCCESS)
        {
            enabled = true;
        }

        RegCloseKey(h_key);
    }

    return enabled;
}

// Settings
void save_settings()
{
    HKEY h_key;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\URLShortener", 0, NULL, 0, KEY_WRITE, NULL, &h_key, NULL) == ERROR_SUCCESS)
    {
        RegSetValueEx(h_key, "IDLength", 0, REG_DWORD, (BYTE*)&g_id_length, sizeof(g_id_length));
        RegCloseKey(h_key);
    }
}

void load_settings()
{
    HKEY h_key;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\URLShortener", 0, KEY_READ, &h_key) == ERROR_SUCCESS)
    {
        DWORD value_size = sizeof(g_id_length);
        DWORD type;
        RegQueryValueEx(h_key, "IDLength", NULL, &type, (BYTE*)&g_id_length, &value_size);
        RegCloseKey(h_key);
    }
}

// Main
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    return big::start_url_shortener(hInstance);
}