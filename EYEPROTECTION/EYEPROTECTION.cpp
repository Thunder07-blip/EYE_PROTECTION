#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")


#define IDT_WORK_TIMER   1
#define IDT_BLINK_TIMER  2
#define ID_BTN_DONE      100

#define ID_TRAY_ICON     200
#define ID_TRAY_TOGGLE   201
#define ID_TRAY_EXIT     202
#define WM_TRAYICON     (WM_USER + 1)

#define REG_PATH L"Software\\EyeProtector"
#define REG_X    L"PosX"
#define REG_Y    L"PosY"
#define REG_ENABLED L"Enabled"

const int WORK_INTERVAL = 5 * 1000; // testing
const int BLINK_SECONDS = 20;

HWND hPopup = nullptr;
HWND hText = nullptr;
int  secondsLeft = BLINK_SECONDS;
bool gEnabled = true;
HFONT hUIFont = nullptr;


LRESULT CALLBACK MainProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PopupProc(HWND, UINT, WPARAM, LPARAM);

/* ---------------- Helpers ---------------- */

POINT CenterPopup(int w, int h)
{
    POINT p;
    p.x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    p.y = (GetSystemMetrics(SM_CYSCREEN) - h) / 3;
    return p;
}

bool LoadSavedPosition(int& x, int& y)
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD size = sizeof(int);
    RegQueryValueEx(hKey, REG_X, nullptr, nullptr, (LPBYTE)&x, &size);
    RegQueryValueEx(hKey, REG_Y, nullptr, nullptr, (LPBYTE)&y, &size);
    RegCloseKey(hKey);
    return true;
}

void SavePosition(HWND hwnd)
{
    RECT r;
    GetWindowRect(hwnd, &r);

    HKEY hKey;
    RegCreateKeyEx(
        HKEY_CURRENT_USER, REG_PATH,
        0, nullptr, 0, KEY_WRITE,
        nullptr, &hKey, nullptr
    );

    RegSetValueEx(hKey, REG_X, 0, REG_DWORD, (BYTE*)&r.left, sizeof(int));
    RegSetValueEx(hKey, REG_Y, 0, REG_DWORD, (BYTE*)&r.top, sizeof(int));
    RegCloseKey(hKey);
}

bool LoadEnabledState()
{
    HKEY hKey;
    DWORD enabled = 1;
    DWORD size = sizeof(DWORD);

    if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueEx(hKey, REG_ENABLED, nullptr, nullptr, (LPBYTE)&enabled, &size);
        RegCloseKey(hKey);
    }
    return enabled != 0;
}

void SaveEnabledState(bool enabled)
{
    HKEY hKey;
    RegCreateKeyEx(
        HKEY_CURRENT_USER, REG_PATH,
        0, nullptr, 0, KEY_WRITE,
        nullptr, &hKey, nullptr
    );

    DWORD val = enabled ? 1 : 0;
    RegSetValueEx(hKey, REG_ENABLED, 0, REG_DWORD, (BYTE*)&val, sizeof(DWORD));
    RegCloseKey(hKey);
}

void CreateUIFont()
{
    hUIFont = CreateFont(
        22,
        0, 0, 0,
        FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI"
    );
}

void EnableAutoStart()
{
    wchar_t path[MAX_PATH];
    GetModuleFileName(nullptr, path, MAX_PATH);

    HKEY hKey;
    if (RegOpenKeyEx(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey
    ) == ERROR_SUCCESS)
    {
        RegSetValueEx(
            hKey,
            L"EyeProtector",
            0, REG_SZ,
            (BYTE*)path,
            (DWORD)(wcslen(path) + 1) * sizeof(wchar_t)
        );
        RegCloseKey(hKey);
    }
}


/* ---------------- Tray helpers ---------------- */

void AddTrayIcon(HWND hwnd)
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(nullptr, IDI_INFORMATION);
    wcscpy_s(nid.szTip, L"Eye Protector");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND hwnd)
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_ICON;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void ShowTrayMenu(HWND hwnd)
{
    POINT p;
    GetCursorPos(&p);

    HMENU menu = CreatePopupMenu();
    AppendMenu(
        menu,
        MF_STRING | (gEnabled ? MF_CHECKED : MF_UNCHECKED),
        ID_TRAY_TOGGLE,
        L"Enable reminders"
    );
    AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

/* ---------------- Entry ---------------- */

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_ PWSTR,
    _In_ int)
{
    EnableAutoStart();
    CreateUIFont();
    WNDCLASS wc = {};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"EyeProtectorMain";

    RegisterClass(&wc);

    HWND hwnd = CreateWindow(
        wc.lpszClassName,
        L"",
        WS_OVERLAPPEDWINDOW,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr
    );

    gEnabled = LoadEnabledState();
    AddTrayIcon(hwnd);

    SetTimer(hwnd, IDT_WORK_TIMER, WORK_INTERVAL, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

/* ---------------- Hidden Controller ---------------- */

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TIMER:
        if (wParam == IDT_WORK_TIMER)
        {
            if (!gEnabled || hPopup) return 0;

            secondsLeft = BLINK_SECONDS;

            static bool popupRegistered = false;
            if (!popupRegistered)
            {
                WNDCLASS pwc = {};
                pwc.lpfnWndProc = PopupProc;
                pwc.hInstance = GetModuleHandle(nullptr);
                pwc.lpszClassName = L"EyeProtectorPopup";
                pwc.hbrBackground = NULL;
                RegisterClass(&pwc);
                popupRegistered = true;
            }

            int x, y;
            POINT p = LoadSavedPosition(x, y)
                ? POINT{ x, y }
            : CenterPopup(340, 180);

            hPopup = CreateWindowEx(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                L"EyeProtectorPopup",
                L"",
                WS_POPUP | WS_VISIBLE,
                p.x, p.y, 340, 180,
                hwnd, nullptr, GetModuleHandle(nullptr), nullptr
            );

            SetLayeredWindowAttributes(
                hPopup,
                0,
                255,
                LWA_ALPHA
            );



            SetTimer(hPopup, IDT_BLINK_TIMER, 1000, nullptr);
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
            ShowTrayMenu(hwnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_TOGGLE:
            gEnabled = !gEnabled;
            SaveEnabledState(gEnabled);
            break;

        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        break;

    case WM_DESTROY:
        RemoveTrayIcon(hwnd);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ---------------- Popup Window ---------------- */

LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(240, 240, 240));
        SetBkColor(hdc, RGB(30, 30, 30));
        static HBRUSH hBrush = CreateSolidBrush(RGB(30, 30, 30));
        return (LRESULT)hBrush;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rect;
        GetClientRect(hwnd, &rect);

        HBRUSH hBrush = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &rect, hBrush);
        DeleteObject(hBrush);

        EndPaint(hwnd, &ps);
        return 0;
    }




    case WM_CREATE:
    {
        BOOL value = TRUE;
        DwmSetWindowAttribute(
            hwnd,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &value,
            sizeof(value)
        );

        DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
        DwmSetWindowAttribute(
            hwnd,
            DWMWA_WINDOW_CORNER_PREFERENCE,
            &preference,
            sizeof(preference)
        );

        


        hText = CreateWindow(
            L"STATIC",
            L"Look far and blink\n20 seconds",
            WS_CHILD | WS_VISIBLE | SS_CENTER ,

            30, 30, 280, 60,
            hwnd, nullptr, nullptr, nullptr
        );
        SendMessage(hText, WM_SETFONT, (WPARAM)hUIFont, TRUE);


        CreateWindow(
            L"BUTTON",
            L"Done",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            120, 115, 100, 35,
            hwnd, (HMENU)ID_BTN_DONE, nullptr, nullptr
        );
        SendMessage(
            GetDlgItem(hwnd, ID_BTN_DONE),
            WM_SETFONT,
            (WPARAM)hUIFont,
            TRUE
        );

        break;
    }
    case WM_LBUTTONDOWN:
        ReleaseCapture();
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        break;

    case WM_TIMER:
        secondsLeft--;
        {
            wchar_t buf[64];
            wsprintf(buf, L"Look far and blink\n%d seconds", secondsLeft);
            SetWindowText(hText, buf);
        }
        if (secondsLeft <= 0)
            DestroyWindow(hwnd);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_DONE)
            DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        SavePosition(hwnd);
        KillTimer(hwnd, IDT_BLINK_TIMER);
        hPopup = nullptr;
        hText = nullptr;
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
