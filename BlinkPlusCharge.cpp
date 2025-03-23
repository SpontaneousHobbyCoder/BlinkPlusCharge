#include <windows.h>
#include <string>
#include <fstream>
#include <tlhelp32.h>
#include <thread>
#include <atomic>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdi32.lib")

struct Settings {
    int batteryThreshold;
    int breakIntervalMin, breakIntervalSec;
    int checkInterval;
    bool batteryReminder, breakReminder, batteryCustomSound, breakCustomSound;
    bool blinkReminder;
    int blinkIntervalMin, blinkIntervalSec;
    bool blinkCustomSound;
    wchar_t batterySoundPath[MAX_PATH], breakSoundPath[MAX_PATH], blinkSoundPath[MAX_PATH];
    bool autoStart;
};

const wchar_t* SETTINGS_DIR = L"%APPDATA%\\BlinkPlusCharge\\";
const wchar_t* SETTINGS_FILE = L"%APPDATA%\\BlinkPlusCharge\\settings.bin";
const wchar_t* REG_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* APP_NAME = L"BlinkPlusCharge";
const wchar_t CLASS_NAME[] = L"SettingsWindowClass";

#define IDC_BATTERY_THRESHOLD 1001
#define IDC_CHECK_INTERVAL 1002
#define IDC_BATTERY_REMINDER 1003
#define IDC_BATTERY_DEFAULT_RADIO 1004
#define IDC_BATTERY_CUSTOM_RADIO 1005
#define IDC_BATTERY_BROWSE 1006
#define IDC_BREAK_INTERVAL_MIN 1007
#define IDC_BREAK_REMINDER 1008
#define IDC_BREAK_DEFAULT_RADIO 1009
#define IDC_BREAK_CUSTOM_RADIO 1010
#define IDC_BREAK_BROWSE 1011
#define IDC_SAVE_BUTTON 1013
#define IDC_BREAK_INTERVAL_SEC 1014
#define IDC_BLINK_REMINDER 1015
#define IDC_BLINK_INTERVAL_MIN 1016
#define IDC_BLINK_INTERVAL_SEC 1017
#define IDC_BLINK_DEFAULT_RADIO 1018
#define IDC_BLINK_CUSTOM_RADIO 1019
#define IDC_BLINK_BROWSE 1020
#define IDC_SET_DEFAULTS 1021
#define IDC_KILL_PROCESS 1022
#define IDC_END_AUTORUN 1023
#define IDC_STATIC_PERCENTAGE 1024
#define IDC_STATIC_CHECK 1025
#define IDC_STATIC_BREAK_MIN 1026
#define IDC_STATIC_BREAK_SEC 1027
#define IDC_STATIC_BLINK_MIN 1028
#define IDC_STATIC_BLINK_SEC 1029
#define IDC_STATIC_BREAK_INTERVAL 1030
#define IDC_STATIC_BLINK_INTERVAL 1031
#define IDC_BATTERY_PREVIEW 1032
#define IDC_BREAK_PREVIEW 1033
#define IDC_BLINK_PREVIEW 1034

Settings settings;
HWND hBatteryEdit, hCheckEdit, hBatteryReminderCheck, hBatteryDefaultRadio, hBatteryCustomRadio, hBatterySoundEdit, hBatteryBrowse;
HWND hBreakMinEdit, hBreakSecEdit, hBreakReminderCheck, hBreakDefaultRadio, hBreakCustomRadio, hBreakSoundEdit, hBreakBrowse;
HWND hBlinkReminderCheck, hBlinkMinEdit, hBlinkSecEdit, hBlinkDefaultRadio, hBlinkCustomRadio, hBlinkSoundEdit, hBlinkBrowse;
HWND hSetDefaultsButton, hKillProcessButton, hEndAutoRunButton;
HWND hStaticPercentage, hStaticCheck, hStaticBreakMin, hStaticBreakSec, hStaticBlinkMin, hStaticBlinkSec;
HWND hStaticBreakInterval, hStaticBlinkInterval;
HWND hBatteryPreviewButton, hBreakPreviewButton, hBlinkPreviewButton;
HFONT hFont;
std::atomic<bool> keepRunning(true);

// Variables for scrolling and zooming
int fontSize = 18; // Initial font size
int scrollX = 0, scrollY = 0; // Current scroll positions
int contentWidth = 1240, contentHeight = 650; // Virtual content size

std::wstring expandPath(const wchar_t* path) {
    wchar_t buffer[MAX_PATH];
    ExpandEnvironmentStringsW(path, buffer, MAX_PATH);
    return std::wstring(buffer);
}

void loadSettings() {
    std::wstring settingsPath = expandPath(SETTINGS_FILE);
    std::ifstream file(settingsPath.c_str(), std::ios::binary);
    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(&settings), sizeof(Settings));
        file.close();
    } else {
        settings = {32, 15, 0, 61, false, false, false, false, false, 0, 12, false, L"", L"", L"", true};
        std::wstring dirPath = expandPath(SETTINGS_DIR);
        if (CreateDirectoryW(dirPath.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
            std::ofstream outFile(settingsPath.c_str(), std::ios::binary);
            if (outFile.is_open()) {
                outFile.write(reinterpret_cast<const char*>(&settings), sizeof(Settings));
                outFile.close();
            }
        }
    }
}

bool saveSettings() {
    std::wstring settingsPath = expandPath(SETTINGS_FILE);
    std::ofstream file(settingsPath.c_str(), std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(&settings), sizeof(Settings));
        file.close();
        return true;
    }
    return false;
}

void manageAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyW(HKEY_CURRENT_USER, REG_KEY, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::wstring cmd = std::wstring(exePath) + L" -background";
            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (BYTE*)cmd.c_str(), (cmd.length() + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

bool isProcessRunning() {
    HANDLE hProcess = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = {sizeof(pe)};
    DWORD currentPid = GetCurrentProcessId();
    bool running = false;
    if (Process32FirstW(hProcess, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"BlinkPlusCharge.exe") == 0 && pe.th32ProcessID != currentPid) {
                running = true;
                break;
            }
        } while (Process32NextW(hProcess, &pe));
    }
    CloseHandle(hProcess);
    return running;
}

void playSoundAsync(const wchar_t* soundPath, const wchar_t* systemSoundAlias) {
    if (soundPath && soundPath[0] != L'\0') {
        wchar_t command[512];
        wsprintfW(command, L"close customSound_%s", systemSoundAlias);
        mciSendStringW(command, NULL, 0, NULL);
        wsprintfW(command, L"open \"%s\" type waveaudio alias customSound_%s", soundPath, systemSoundAlias);
        mciSendStringW(command, NULL, 0, NULL);
        wsprintfW(command, L"play customSound_%s", systemSoundAlias);
        mciSendStringW(command, NULL, 0, NULL);
    } else {
        PlaySoundW(systemSoundAlias, NULL, SND_ALIAS | SND_ASYNC);
    }
}

void batteryReminderThread(std::atomic<bool>& running) {
    while (running) {
        Settings localSettings;
        std::wstring settingsPath = expandPath(SETTINGS_FILE);
        std::ifstream file(settingsPath.c_str(), std::ios::binary);
        if (file.is_open()) {
            file.read(reinterpret_cast<char*>(&localSettings), sizeof(Settings));
            file.close();
        }
        if (localSettings.batteryReminder) {
            SYSTEM_POWER_STATUS powerStatus;
            GetSystemPowerStatus(&powerStatus);
            if (powerStatus.ACLineStatus == 0 && powerStatus.BatteryLifePercent <= localSettings.batteryThreshold) {
                playSoundAsync(localSettings.batteryCustomSound ? localSettings.batterySoundPath : NULL, L"SystemAsterisk");
            }
        }
        Sleep(localSettings.checkInterval * 1000);
    }
}

void breakReminderThread(std::atomic<bool>& running) {
    while (running) {
        Settings localSettings;
        std::wstring settingsPath = expandPath(SETTINGS_FILE);
        std::ifstream file(settingsPath.c_str(), std::ios::binary);
        if (file.is_open()) {
            file.read(reinterpret_cast<char*>(&localSettings), sizeof(Settings));
            file.close();
        }
        if (localSettings.breakReminder) {
            DWORD breakIntervalMs = (localSettings.breakIntervalMin * 60 + localSettings.breakIntervalSec) * 1000;
            if (breakIntervalMs > 0) {
                playSoundAsync(localSettings.breakCustomSound ? localSettings.breakSoundPath : NULL, L"SystemHand");
                Sleep(breakIntervalMs);
            } else {
                Sleep(100);
            }
        } else {
            Sleep(100);
        }
    }
}

void blinkReminderThread(std::atomic<bool>& running) {
    while (running) {
        Settings localSettings;
        std::wstring settingsPath = expandPath(SETTINGS_FILE);
        std::ifstream file(settingsPath.c_str(), std::ios::binary);
        if (file.is_open()) {
            file.read(reinterpret_cast<char*>(&localSettings), sizeof(Settings));
            file.close();
        }
        if (localSettings.blinkReminder) {
            DWORD blinkIntervalMs = (localSettings.blinkIntervalMin * 60 + localSettings.blinkIntervalSec) * 1000;
            if (blinkIntervalMs > 0) {
                playSoundAsync(localSettings.blinkCustomSound ? localSettings.blinkSoundPath : NULL, L"SystemExclamation");
                Sleep(blinkIntervalMs);
            } else {
                Sleep(100);
            }
        } else {
            Sleep(100);
        }
    }
}

void runReminderLoop() {
    std::thread batteryThread(batteryReminderThread, std::ref(keepRunning));
    std::thread breakThread(breakReminderThread, std::ref(keepRunning));
    std::thread blinkThread(blinkReminderThread, std::ref(keepRunning));

    batteryThread.join();
    breakThread.join();
    blinkThread.join();
}

HWND createControl(HWND hwnd, const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, HMENU id) {
    // Adjust position based on scroll offset
    HWND ctrl = CreateWindowW(type, text, WS_VISIBLE | WS_CHILD | style, x - scrollX, y - scrollY, w, h, hwnd, id, NULL, NULL);
    SendMessage(ctrl, WM_SETFONT, (WPARAM)hFont, TRUE);
    return ctrl;
}

void enableControls(bool enable, HWND* controls, int count) {
    for (int i = 0; i < count; i++) {
        EnableWindow(controls[i], enable);
    }
}

void updateFont(HWND hwnd) {
    // Delete the old font
    if (hFont) DeleteObject(hFont);
    // Create a new font with the updated size
    hFont = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, 
                        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Times New Roman");

    // Update the font for all controls
    SendMessage(hBatteryReminderCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBatteryEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hCheckEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBatteryPreviewButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBatteryDefaultRadio, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBatteryCustomRadio, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBatterySoundEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBatteryBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBreakReminderCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBreakMinEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBreakSecEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBreakPreviewButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBreakDefaultRadio, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBreakCustomRadio, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBreakSoundEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBreakBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBlinkReminderCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBlinkMinEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBlinkSecEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBlinkPreviewButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBlinkDefaultRadio, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBlinkCustomRadio, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBlinkSoundEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hBlinkBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hSetDefaultsButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hKillProcessButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hEndAutoRunButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hStaticPercentage, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hStaticCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hStaticBreakMin, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hStaticBreakSec, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hStaticBlinkMin, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hStaticBlinkSec, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hStaticBreakInterval, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hStaticBlinkInterval, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Redraw the window
    InvalidateRect(hwnd, NULL, TRUE);
}

void updateScrollBars(HWND hwnd) {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    // Vertical scrollbar
    SCROLLINFO si = {0};
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_ALL;
    si.nMin = 0;
    si.nMax = contentHeight - 1;
    si.nPage = clientRect.bottom - clientRect.top;
    si.nPos = scrollY;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

    // Horizontal scrollbar
    si.nMax = contentWidth - 1;
    si.nPage = clientRect.right - clientRect.left;
    si.nPos = scrollX;
    SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
}

void moveControls(HWND hwnd, int dx, int dy) {
    // Move all controls by the scroll offset
    SetWindowPos(hBatteryReminderCheck, NULL, 10 - scrollX, 40 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hStaticPercentage, NULL, 20 - scrollX, 80 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBatteryEdit, NULL, 180 - scrollX, 80 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hStaticCheck, NULL, 20 - scrollX, 120 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hCheckEdit, NULL, 180 - scrollX, 120 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBatteryPreviewButton, NULL, 20 - scrollX, 160 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBatteryDefaultRadio, NULL, 20 - scrollX, 200 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBatteryCustomRadio, NULL, 20 - scrollX, 240 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBatterySoundEdit, NULL, 180 - scrollX, 240 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBatteryBrowse, NULL, 490 - scrollX, 240 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBreakReminderCheck, NULL, 620 - scrollX, 40 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hStaticBreakInterval, NULL, 630 - scrollX, 80 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hStaticBreakMin, NULL, 630 - scrollX, 120 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBreakMinEdit, NULL, 700 - scrollX, 120 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hStaticBreakSec, NULL, 780 - scrollX, 120 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBreakSecEdit, NULL, 850 - scrollX, 120 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBreakPreviewButton, NULL, 630 - scrollX, 160 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBreakDefaultRadio, NULL, 630 - scrollX, 200 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBreakCustomRadio, NULL, 630 - scrollX, 240 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBreakSoundEdit, NULL, 790 - scrollX, 240 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBreakBrowse, NULL, 1100 - scrollX, 240 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBlinkReminderCheck, NULL, 620 - scrollX, 350 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hStaticBlinkInterval, NULL, 630 - scrollX, 390 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hStaticBlinkMin, NULL, 630 - scrollX, 430 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBlinkMinEdit, NULL, 700 - scrollX, 430 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hStaticBlinkSec, NULL, 780 - scrollX, 430 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBlinkSecEdit, NULL, 850 - scrollX, 430 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBlinkPreviewButton, NULL, 630 - scrollX, 470 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBlinkDefaultRadio, NULL, 630 - scrollX, 510 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBlinkCustomRadio, NULL, 630 - scrollX, 550 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBlinkSoundEdit, NULL, 790 - scrollX, 550 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hBlinkBrowse, NULL, 1100 - scrollX, 550 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hSetDefaultsButton, NULL, 620 - scrollX, 600 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hKillProcessButton, NULL, 750 - scrollX, 600 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    SetWindowPos(hEndAutoRunButton, NULL, 880 - scrollX, 600 - scrollY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        hFont = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, 
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Times New Roman");

        // Battery Section
        createControl(hwnd, L"STATIC", L"Battery Settings:", 0, 10, 10, 300, 30, NULL);
        hBatteryReminderCheck = createControl(hwnd, L"BUTTON", L"Battery Reminder", BS_CHECKBOX, 10, 40, 200, 30, (HMENU)IDC_BATTERY_REMINDER);
        CheckDlgButton(hwnd, IDC_BATTERY_REMINDER, settings.batteryReminder ? BST_CHECKED : BST_UNCHECKED);
        hStaticPercentage = createControl(hwnd, L"STATIC", L"Percentage (%):", 0, 20, 80, 150, 30, (HMENU)IDC_STATIC_PERCENTAGE);
        hBatteryEdit = createControl(hwnd, L"EDIT", std::to_wstring(settings.batteryThreshold).c_str(), WS_BORDER, 180, 80, 60, 30, (HMENU)IDC_BATTERY_THRESHOLD);
        hStaticCheck = createControl(hwnd, L"STATIC", L"Check Battery Every (s):", 0, 20, 120, 150, 30, (HMENU)IDC_STATIC_CHECK);
        hCheckEdit = createControl(hwnd, L"EDIT", std::to_wstring(settings.checkInterval).c_str(), WS_BORDER, 180, 120, 60, 30, (HMENU)IDC_CHECK_INTERVAL);
        hBatteryPreviewButton = createControl(hwnd, L"BUTTON", L"Preview Sound", 0, 20, 160, 150, 30, (HMENU)IDC_BATTERY_PREVIEW);
        hBatteryDefaultRadio = createControl(hwnd, L"BUTTON", L"System Sound", BS_RADIOBUTTON, 20, 200, 150, 30, (HMENU)IDC_BATTERY_DEFAULT_RADIO);
        hBatteryCustomRadio = createControl(hwnd, L"BUTTON", L"Custom Sound", BS_RADIOBUTTON, 20, 240, 150, 30, (HMENU)IDC_BATTERY_CUSTOM_RADIO);
        hBatterySoundEdit = createControl(hwnd, L"EDIT", settings.batterySoundPath, WS_BORDER | ES_AUTOHSCROLL, 180, 240, 300, 30, NULL);
        hBatteryBrowse = createControl(hwnd, L"BUTTON", L"Browse...", 0, 490, 240, 100, 30, (HMENU)IDC_BATTERY_BROWSE);

        // Dividers
        createControl(hwnd, L"STATIC", L"", SS_ETCHEDVERT, 600, 10, 2, 650, NULL);
        createControl(hwnd, L"STATIC", L"", SS_ETCHEDVERT, 602, 10, 2, 650, NULL);

        // Break Section
        createControl(hwnd, L"STATIC", L"Eye Break Settings:", 0, 620, 10, 300, 30, NULL);
        hBreakReminderCheck = createControl(hwnd, L"BUTTON", L"Break Reminder", BS_CHECKBOX, 620, 40, 200, 30, (HMENU)IDC_BREAK_REMINDER);
        CheckDlgButton(hwnd, IDC_BREAK_REMINDER, settings.breakReminder ? BST_CHECKED : BST_UNCHECKED);
        hStaticBreakInterval = createControl(hwnd, L"STATIC", L"Break Interval:", 0, 630, 80, 150, 30, (HMENU)IDC_STATIC_BREAK_INTERVAL);
        hStaticBreakMin = createControl(hwnd, L"STATIC", L"(min.):", 0, 630, 120, 60, 30, (HMENU)IDC_STATIC_BREAK_MIN);
        hBreakMinEdit = createControl(hwnd, L"EDIT", std::to_wstring(settings.breakIntervalMin).c_str(), WS_BORDER, 700, 120, 60, 30, (HMENU)IDC_BREAK_INTERVAL_MIN);
        hStaticBreakSec = createControl(hwnd, L"STATIC", L"(sec.):", 0, 780, 120, 60, 30, (HMENU)IDC_STATIC_BREAK_SEC);
        hBreakSecEdit = createControl(hwnd, L"EDIT", std::to_wstring(settings.breakIntervalSec).c_str(), WS_BORDER, 850, 120, 60, 30, (HMENU)IDC_BREAK_INTERVAL_SEC);
        hBreakPreviewButton = createControl(hwnd, L"BUTTON", L"Preview Sound", 0, 630, 160, 150, 30, (HMENU)IDC_BREAK_PREVIEW);
        hBreakDefaultRadio = createControl(hwnd, L"BUTTON", L"System Sound", BS_RADIOBUTTON, 630, 200, 150, 30, (HMENU)IDC_BREAK_DEFAULT_RADIO);
        hBreakCustomRadio = createControl(hwnd, L"BUTTON", L"Custom Sound", BS_RADIOBUTTON, 630, 240, 150, 30, (HMENU)IDC_BREAK_CUSTOM_RADIO);
        hBreakSoundEdit = createControl(hwnd, L"EDIT", settings.breakSoundPath, WS_BORDER | ES_AUTOHSCROLL, 790, 240, 300, 30, NULL);
        hBreakBrowse = createControl(hwnd, L"BUTTON", L"Browse...", 0, 1100, 240, 100, 30, (HMENU)IDC_BREAK_BROWSE);

        createControl(hwnd, L"STATIC", L"", SS_ETCHEDHORZ, 620, 290, 600, 2, NULL);
        createControl(hwnd, L"STATIC", L"", SS_ETCHEDHORZ, 620, 292, 600, 2, NULL);

        // Blink Section
        createControl(hwnd, L"STATIC", L"Blink Reminder Settings:", 0, 620, 310, 300, 30, NULL);
        hBlinkReminderCheck = createControl(hwnd, L"BUTTON", L"Blink Interval", BS_CHECKBOX, 620, 350, 200, 30, (HMENU)IDC_BLINK_REMINDER);
        CheckDlgButton(hwnd, IDC_BLINK_REMINDER, settings.blinkReminder ? BST_CHECKED : BST_UNCHECKED);
        hStaticBlinkInterval = createControl(hwnd, L"STATIC", L"Blink Interval:", 0, 630, 390, 150, 30, (HMENU)IDC_STATIC_BLINK_INTERVAL);
        hStaticBlinkMin = createControl(hwnd, L"STATIC", L"(min.):", 0, 630, 430, 60, 30, (HMENU)IDC_STATIC_BLINK_MIN);
        hBlinkMinEdit = createControl(hwnd, L"EDIT", std::to_wstring(settings.blinkIntervalMin).c_str(), WS_BORDER, 700, 430, 60, 30, (HMENU)IDC_BLINK_INTERVAL_MIN);
        hStaticBlinkSec = createControl(hwnd, L"STATIC", L"(sec.):", 0, 780, 430, 60, 30, (HMENU)IDC_STATIC_BLINK_SEC);
        hBlinkSecEdit = createControl(hwnd, L"EDIT", std::to_wstring(settings.blinkIntervalSec).c_str(), WS_BORDER, 850, 430, 60, 30, (HMENU)IDC_BLINK_INTERVAL_SEC);
        hBlinkPreviewButton = createControl(hwnd, L"BUTTON", L"Preview Sound", 0, 630, 470, 150, 30, (HMENU)IDC_BLINK_PREVIEW);
        hBlinkDefaultRadio = createControl(hwnd, L"BUTTON", L"System Sound", BS_RADIOBUTTON, 630, 510, 150, 30, (HMENU)IDC_BLINK_DEFAULT_RADIO);
        hBlinkCustomRadio = createControl(hwnd, L"BUTTON", L"Custom Sound", BS_RADIOBUTTON, 630, 550, 150, 30, (HMENU)IDC_BLINK_CUSTOM_RADIO);
        hBlinkSoundEdit = createControl(hwnd, L"EDIT", settings.blinkSoundPath, WS_BORDER | ES_AUTOHSCROLL, 790, 550, 300, 30, NULL);
        hBlinkBrowse = createControl(hwnd, L"BUTTON", L"Browse...", 0, 1100, 550, 100, 30, (HMENU)IDC_BLINK_BROWSE);

        // Bottom Buttons
        hSetDefaultsButton = createControl(hwnd, L"BUTTON", L"Set Defaults", 0, 620, 600, 120, 40, (HMENU)IDC_SET_DEFAULTS);
        hKillProcessButton = createControl(hwnd, L"BUTTON", L"KillProcess", 0, 750, 600, 120, 40, (HMENU)IDC_KILL_PROCESS);
        EnableWindow(hKillProcessButton, isProcessRunning());
        hEndAutoRunButton = createControl(hwnd, L"BUTTON", L"EndAutoRun", 0, 880, 600, 120, 40, (HMENU)IDC_END_AUTORUN);
        createControl(hwnd, L"BUTTON", L"Save", 0, 1010, 600, 120, 40, (HMENU)IDC_SAVE_BUTTON);

        // Initial state
        CheckRadioButton(hwnd, IDC_BATTERY_DEFAULT_RADIO, IDC_BATTERY_CUSTOM_RADIO, settings.batteryCustomSound ? IDC_BATTERY_CUSTOM_RADIO : IDC_BATTERY_DEFAULT_RADIO);
        CheckRadioButton(hwnd, IDC_BREAK_DEFAULT_RADIO, IDC_BREAK_CUSTOM_RADIO, settings.breakCustomSound ? IDC_BREAK_CUSTOM_RADIO : IDC_BREAK_DEFAULT_RADIO);
        CheckRadioButton(hwnd, IDC_BLINK_DEFAULT_RADIO, IDC_BLINK_CUSTOM_RADIO, settings.blinkCustomSound ? IDC_BLINK_CUSTOM_RADIO : IDC_BLINK_DEFAULT_RADIO);
        
        HWND batteryControls[] = {hStaticPercentage, hBatteryEdit, hStaticCheck, hCheckEdit, hBatteryPreviewButton, hBatteryDefaultRadio, hBatteryCustomRadio, hBatterySoundEdit, hBatteryBrowse};
        HWND breakControls[] = {hStaticBreakInterval, hStaticBreakMin, hBreakMinEdit, hStaticBreakSec, hBreakSecEdit, hBreakPreviewButton, hBreakDefaultRadio, hBreakCustomRadio, hBreakSoundEdit, hBreakBrowse};
        HWND blinkControls[] = {hStaticBlinkInterval, hStaticBlinkMin, hBlinkMinEdit, hStaticBlinkSec, hBlinkSecEdit, hBlinkPreviewButton, hBlinkDefaultRadio, hBlinkCustomRadio, hBlinkSoundEdit, hBlinkBrowse};
        enableControls(settings.batteryReminder, batteryControls, 9);
        enableControls(settings.breakReminder, breakControls, 10);
        enableControls(settings.blinkReminder, blinkControls, 10);
        EnableWindow(hBatterySoundEdit, settings.batteryReminder && settings.batteryCustomSound);
        EnableWindow(hBatteryBrowse, settings.batteryReminder && settings.batteryCustomSound);
        EnableWindow(hBreakSoundEdit, settings.breakReminder && settings.breakCustomSound);
        EnableWindow(hBreakBrowse, settings.breakReminder && settings.breakCustomSound);
        EnableWindow(hBlinkSoundEdit, settings.blinkReminder && settings.blinkCustomSound);
        EnableWindow(hBlinkBrowse, settings.blinkReminder && settings.blinkCustomSound);

        // Initialize scrollbars
        updateScrollBars(hwnd);
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect(hdc, &rect, (HBRUSH)(COLOR_WINDOW + 1));
        EndPaint(hwnd, &ps);
        break;
    }

    case WM_VSCROLL: {
        SCROLLINFO si = {0};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);

        int oldPos = si.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: si.nPos -= 10; break;
        case SB_LINEDOWN: si.nPos += 10; break;
        case SB_PAGEUP: si.nPos -= si.nPage; break;
        case SB_PAGEDOWN: si.nPos += si.nPage; break;
        case SB_THUMBTRACK: si.nPos = HIWORD(wParam); break;
        }

        si.fMask = SIF_POS;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        GetScrollInfo(hwnd, SB_VERT, &si);

        if (si.nPos != oldPos) {
            scrollY = si.nPos;
            moveControls(hwnd, 0, oldPos - si.nPos);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        break;
    }

    case WM_HSCROLL: {
        SCROLLINFO si = {0};
        si.cbSize = sizeof(SCROLLINFO);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_HORZ, &si);

        int oldPos = si.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINELEFT: si.nPos -= 10; break;
        case SB_LINERIGHT: si.nPos += 10; break;
        case SB_PAGELEFT: si.nPos -= si.nPage; break;
        case SB_PAGERIGHT: si.nPos += si.nPage; break;
        case SB_THUMBTRACK: si.nPos = HIWORD(wParam); break;
        }

        si.fMask = SIF_POS;
        SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
        GetScrollInfo(hwnd, SB_HORZ, &si);

        if (si.nPos != oldPos) {
            scrollX = si.nPos;
            moveControls(hwnd, oldPos - si.nPos, 0);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        if (GetKeyState(VK_CONTROL) & 0x8000) { // Ctrl key is pressed
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0) { // Zoom in
                if (fontSize < 40) fontSize += 2; // Max font size 40
            } else if (delta < 0) { // Zoom out
                if (fontSize > 10) fontSize -= 2; // Min font size 10
            }
            updateFont(hwnd);
        } else {
            // Scroll vertically with mouse wheel
            SCROLLINFO si = {0};
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);

            int oldPos = si.nPos;
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            si.nPos -= (delta / 120) * 30; // Scroll 30 pixels per wheel notch

            si.fMask = SIF_POS;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            GetScrollInfo(hwnd, SB_VERT, &si);

            if (si.nPos != oldPos) {
                scrollY = si.nPos;
                moveControls(hwnd, 0, oldPos - si.nPos);
                InvalidateRect(hwnd, NULL, TRUE);
            }
        }
        break;
    }

    case WM_SIZE:
        updateScrollBars(hwnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BATTERY_REMINDER:
            if (HIWORD(wParam) == BN_CLICKED) {
                bool checked = IsDlgButtonChecked(hwnd, IDC_BATTERY_REMINDER) == BST_CHECKED;
                CheckDlgButton(hwnd, IDC_BATTERY_REMINDER, checked ? BST_UNCHECKED : BST_CHECKED);
                checked = !checked;
                HWND controls[] = {hStaticPercentage, hBatteryEdit, hStaticCheck, hCheckEdit, hBatteryPreviewButton, hBatteryDefaultRadio, hBatteryCustomRadio, hBatterySoundEdit, hBatteryBrowse};
                enableControls(checked, controls, 9);
                EnableWindow(hBatterySoundEdit, checked && IsDlgButtonChecked(hwnd, IDC_BATTERY_CUSTOM_RADIO));
                EnableWindow(hBatteryBrowse, checked && IsDlgButtonChecked(hwnd, IDC_BATTERY_CUSTOM_RADIO));
            }
            break;

        case IDC_BREAK_REMINDER:
            if (HIWORD(wParam) == BN_CLICKED) {
                bool checked = IsDlgButtonChecked(hwnd, IDC_BREAK_REMINDER) == BST_CHECKED;
                CheckDlgButton(hwnd, IDC_BREAK_REMINDER, checked ? BST_UNCHECKED : BST_CHECKED);
                checked = !checked;
                HWND controls[] = {hStaticBreakInterval, hStaticBreakMin, hBreakMinEdit, hStaticBreakSec, hBreakSecEdit, hBreakPreviewButton, hBreakDefaultRadio, hBreakCustomRadio, hBreakSoundEdit, hBreakBrowse};
                enableControls(checked, controls, 10);
                EnableWindow(hBreakSoundEdit, checked && IsDlgButtonChecked(hwnd, IDC_BREAK_CUSTOM_RADIO));
                EnableWindow(hBreakBrowse, checked && IsDlgButtonChecked(hwnd, IDC_BREAK_CUSTOM_RADIO));
            }
            break;

        case IDC_BLINK_REMINDER:
            if (HIWORD(wParam) == BN_CLICKED) {
                bool checked = IsDlgButtonChecked(hwnd, IDC_BLINK_REMINDER) == BST_CHECKED;
                CheckDlgButton(hwnd, IDC_BLINK_REMINDER, checked ? BST_UNCHECKED : BST_CHECKED);
                checked = !checked;
                HWND controls[] = {hStaticBlinkInterval, hStaticBlinkMin, hBlinkMinEdit, hStaticBlinkSec, hBlinkSecEdit, hBlinkPreviewButton, hBlinkDefaultRadio, hBlinkCustomRadio, hBlinkSoundEdit, hBlinkBrowse};
                enableControls(checked, controls, 10);
                EnableWindow(hBlinkSoundEdit, checked && IsDlgButtonChecked(hwnd, IDC_BLINK_CUSTOM_RADIO));
                EnableWindow(hBlinkBrowse, checked && IsDlgButtonChecked(hwnd, IDC_BLINK_CUSTOM_RADIO));
            }
            break;

        case IDC_BATTERY_DEFAULT_RADIO:
        case IDC_BATTERY_CUSTOM_RADIO:
            if (HIWORD(wParam) == BN_CLICKED) {
                CheckRadioButton(hwnd, IDC_BATTERY_DEFAULT_RADIO, IDC_BATTERY_CUSTOM_RADIO, LOWORD(wParam));
                bool custom = IsDlgButtonChecked(hwnd, IDC_BATTERY_CUSTOM_RADIO) == BST_CHECKED;
                EnableWindow(hBatterySoundEdit, custom && IsDlgButtonChecked(hwnd, IDC_BATTERY_REMINDER));
                EnableWindow(hBatteryBrowse, custom && IsDlgButtonChecked(hwnd, IDC_BATTERY_REMINDER));
            }
            break;

        case IDC_BREAK_DEFAULT_RADIO:
        case IDC_BREAK_CUSTOM_RADIO:
            if (HIWORD(wParam) == BN_CLICKED) {
                CheckRadioButton(hwnd, IDC_BREAK_DEFAULT_RADIO, IDC_BREAK_CUSTOM_RADIO, LOWORD(wParam));
                bool custom = IsDlgButtonChecked(hwnd, IDC_BREAK_CUSTOM_RADIO) == BST_CHECKED;
                EnableWindow(hBreakSoundEdit, custom && IsDlgButtonChecked(hwnd, IDC_BREAK_REMINDER));
                EnableWindow(hBreakBrowse, custom && IsDlgButtonChecked(hwnd, IDC_BREAK_REMINDER));
            }
            break;

        case IDC_BLINK_DEFAULT_RADIO:
        case IDC_BLINK_CUSTOM_RADIO:
            if (HIWORD(wParam) == BN_CLICKED) {
                CheckRadioButton(hwnd, IDC_BLINK_DEFAULT_RADIO, IDC_BLINK_CUSTOM_RADIO, LOWORD(wParam));
                bool custom = IsDlgButtonChecked(hwnd, IDC_BLINK_CUSTOM_RADIO) == BST_CHECKED;
                EnableWindow(hBlinkSoundEdit, custom && IsDlgButtonChecked(hwnd, IDC_BLINK_REMINDER));
                EnableWindow(hBlinkBrowse, custom && IsDlgButtonChecked(hwnd, IDC_BLINK_REMINDER));
            }
            break;

        case IDC_BATTERY_PREVIEW:
            if (HIWORD(wParam) == BN_CLICKED) {
                if (IsDlgButtonChecked(hwnd, IDC_BATTERY_CUSTOM_RADIO) == BST_CHECKED) {
                    wchar_t soundPath[MAX_PATH];
                    GetWindowTextW(hBatterySoundEdit, soundPath, MAX_PATH);
                    playSoundAsync(soundPath, L"SystemAsterisk");
                } else {
                    playSoundAsync(NULL, L"SystemAsterisk");
                }
            }
            break;

        case IDC_BREAK_PREVIEW:
            if (HIWORD(wParam) == BN_CLICKED) {
                if (IsDlgButtonChecked(hwnd, IDC_BREAK_CUSTOM_RADIO) == BST_CHECKED) {
                    wchar_t soundPath[MAX_PATH];
                    GetWindowTextW(hBreakSoundEdit, soundPath, MAX_PATH);
                    playSoundAsync(soundPath, L"SystemHand");
                } else {
                    playSoundAsync(NULL, L"SystemHand");
                }
            }
            break;

        case IDC_BLINK_PREVIEW:
            if (HIWORD(wParam) == BN_CLICKED) {
                if (IsDlgButtonChecked(hwnd, IDC_BLINK_CUSTOM_RADIO) == BST_CHECKED) {
                    wchar_t soundPath[MAX_PATH];
                    GetWindowTextW(hBlinkSoundEdit, soundPath, MAX_PATH);
                    playSoundAsync(soundPath, L"SystemExclamation");
                } else {
                    playSoundAsync(NULL, L"SystemExclamation");
                }
            }
            break;

        case IDC_BATTERY_BROWSE:
        case IDC_BREAK_BROWSE:
        case IDC_BLINK_BROWSE: {
            if (HIWORD(wParam) == BN_CLICKED) {
                OPENFILENAMEW ofn = {sizeof(ofn), hwnd, NULL, L"WAV Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0", NULL, 0, 0, NULL, MAX_PATH, NULL, 0, NULL, NULL, OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST};
                wchar_t fileName[MAX_PATH] = L"";
                ofn.lpstrFile = fileName;
                HWND targetEdit = (LOWORD(wParam) == IDC_BATTERY_BROWSE) ? hBatterySoundEdit : (LOWORD(wParam) == IDC_BREAK_BROWSE) ? hBreakSoundEdit : hBlinkSoundEdit;
                if (GetOpenFileNameW(&ofn)) SetWindowTextW(targetEdit, fileName);
            }
            break;
        }

        case IDC_SET_DEFAULTS:
            if (HIWORD(wParam) == BN_CLICKED) {
                settings = {32, 15, 0, 61, false, false, false, false, false, 0, 12, false, L"", L"", L"", true};
                SetWindowTextW(hBatteryEdit, L"32"); SetWindowTextW(hCheckEdit, L"61");
                SetWindowTextW(hBreakMinEdit, L"15"); SetWindowTextW(hBreakSecEdit, L"0");
                SetWindowTextW(hBlinkMinEdit, L"0"); SetWindowTextW(hBlinkSecEdit, L"12");
                CheckDlgButton(hwnd, IDC_BATTERY_REMINDER, BST_UNCHECKED);
                CheckDlgButton(hwnd, IDC_BREAK_REMINDER, BST_UNCHECKED);
                CheckDlgButton(hwnd, IDC_BLINK_REMINDER, BST_UNCHECKED);
                CheckRadioButton(hwnd, IDC_BATTERY_DEFAULT_RADIO, IDC_BATTERY_CUSTOM_RADIO, IDC_BATTERY_DEFAULT_RADIO);
                CheckRadioButton(hwnd, IDC_BREAK_DEFAULT_RADIO, IDC_BREAK_CUSTOM_RADIO, IDC_BREAK_DEFAULT_RADIO);
                CheckRadioButton(hwnd, IDC_BLINK_DEFAULT_RADIO, IDC_BLINK_CUSTOM_RADIO, IDC_BLINK_DEFAULT_RADIO);
                SetWindowTextW(hBatterySoundEdit, L""); SetWindowTextW(hBreakSoundEdit, L""); SetWindowTextW(hBlinkSoundEdit, L"");
                HWND batteryControls[] = {hStaticPercentage, hBatteryEdit, hStaticCheck, hCheckEdit, hBatteryPreviewButton, hBatteryDefaultRadio, hBatteryCustomRadio, hBatterySoundEdit, hBatteryBrowse};
                HWND breakControls[] = {hStaticBreakInterval, hStaticBreakMin, hBreakMinEdit, hStaticBreakSec, hBreakSecEdit, hBreakPreviewButton, hBreakDefaultRadio, hBreakCustomRadio, hBreakSoundEdit, hBreakBrowse};
                HWND blinkControls[] = {hStaticBlinkInterval, hStaticBlinkMin, hBlinkMinEdit, hStaticBlinkSec, hBlinkSecEdit, hBlinkPreviewButton, hBlinkDefaultRadio, hBlinkCustomRadio, hBlinkSoundEdit, hBlinkBrowse};
                enableControls(false, batteryControls, 9); enableControls(false, breakControls, 10); enableControls(false, blinkControls, 10);
                MessageBoxW(hwnd, L"Settings reset to defaults!", L"Success", MB_OK | MB_ICONINFORMATION);
            }
            break;

        case IDC_KILL_PROCESS:
            if (HIWORD(wParam) == BN_CLICKED) {
                HANDLE hProcess = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                PROCESSENTRY32W pe = {sizeof(pe)};
                DWORD currentPid = GetCurrentProcessId();
                if (Process32FirstW(hProcess, &pe)) {
                    do {
                        if (_wcsicmp(pe.szExeFile, L"BlinkPlusCharge.exe") == 0 && pe.th32ProcessID != currentPid) {
                            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                            TerminateProcess(hProc, 0);
                            CloseHandle(hProc);
                            EnableWindow(hKillProcessButton, FALSE);
                            MessageBoxW(hwnd, L"Background process terminated!", L"Success", MB_OK | MB_ICONINFORMATION);
                            break;
                        }
                    } while (Process32NextW(hProcess, &pe));
                }
                CloseHandle(hProcess);
            }
            break;

        case IDC_END_AUTORUN:
            if (HIWORD(wParam) == BN_CLICKED) {
                manageAutoStart(false);
                MessageBoxW(hwnd, L"Auto-start removed from registry!", L"Success", MB_OK | MB_ICONINFORMATION);
            }
            break;

        case IDC_SAVE_BUTTON:
            if (HIWORD(wParam) == BN_CLICKED) {
                wchar_t buffer[10];
                GetWindowTextW(hBatteryEdit, buffer, 10); settings.batteryThreshold = _wtoi(buffer);
                GetWindowTextW(hCheckEdit, buffer, 10); settings.checkInterval = _wtoi(buffer);
                GetWindowTextW(hBreakMinEdit, buffer, 10); settings.breakIntervalMin = _wtoi(buffer);
                GetWindowTextW(hBreakSecEdit, buffer, 10); settings.breakIntervalSec = _wtoi(buffer);
                GetWindowTextW(hBlinkMinEdit, buffer, 10); settings.blinkIntervalMin = _wtoi(buffer);
                GetWindowTextW(hBlinkSecEdit, buffer, 10); settings.blinkIntervalSec = _wtoi(buffer);
                if (settings.batteryThreshold < 0 || settings.batteryThreshold > 100 || settings.checkInterval < 1 || 
                    settings.breakIntervalMin < 0 || settings.breakIntervalSec < 0 || settings.blinkIntervalMin < 0 || settings.blinkIntervalSec < 0) {
                    MessageBoxW(hwnd, L"Invalid input! Percentage must be 0-100, intervals must be non-negative, and check interval must be at least 1s.", L"Error", MB_OK | MB_ICONERROR);
                    break;
                }
                if ((settings.breakIntervalMin == 0 && settings.breakIntervalSec == 0) || (settings.blinkIntervalMin == 0 && settings.blinkIntervalSec == 0)) {
                    MessageBoxW(hwnd, L"Break and Blink intervals cannot both be 0!", L"Error", MB_OK | MB_ICONERROR);
                    break;
                }
                settings.batteryReminder = IsDlgButtonChecked(hwnd, IDC_BATTERY_REMINDER) == BST_CHECKED;
                settings.breakReminder = IsDlgButtonChecked(hwnd, IDC_BREAK_REMINDER) == BST_CHECKED;
                settings.blinkReminder = IsDlgButtonChecked(hwnd, IDC_BLINK_REMINDER) == BST_CHECKED;
                settings.batteryCustomSound = IsDlgButtonChecked(hwnd, IDC_BATTERY_CUSTOM_RADIO) == BST_CHECKED;
                settings.breakCustomSound = IsDlgButtonChecked(hwnd, IDC_BREAK_CUSTOM_RADIO) == BST_CHECKED;
                settings.blinkCustomSound = IsDlgButtonChecked(hwnd, IDC_BLINK_CUSTOM_RADIO) == BST_CHECKED;
                GetWindowTextW(hBatterySoundEdit, settings.batterySoundPath, MAX_PATH);
                GetWindowTextW(hBreakSoundEdit, settings.breakSoundPath, MAX_PATH);
                GetWindowTextW(hBlinkSoundEdit, settings.blinkSoundPath, MAX_PATH);
                settings.autoStart = true;
                if (!saveSettings()) {
                    MessageBoxW(hwnd, L"Failed to save settings!", L"Error", MB_OK | MB_ICONERROR);
                    break;
                }
                manageAutoStart(true);
                HANDLE hProcess = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                PROCESSENTRY32W pe = {sizeof(pe)};
                DWORD currentPid = GetCurrentProcessId();
                if (Process32FirstW(hProcess, &pe)) {
                    do {
                        if (_wcsicmp(pe.szExeFile, L"BlinkPlusCharge.exe") == 0 && pe.th32ProcessID != currentPid) {
                            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                            TerminateProcess(hProc, 0);
                            CloseHandle(hProc);
                            break;
                        }
                    } while (Process32NextW(hProcess, &pe));
                }
                CloseHandle(hProcess);
                wchar_t exePath[MAX_PATH];
                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                ShellExecuteW(NULL, L"open", exePath, L"-background", NULL, SW_HIDE);
                EnableWindow(hKillProcessButton, TRUE);
                MessageBoxW(hwnd, L"Settings saved and process restarted!", L"Success", MB_OK | MB_ICONINFORMATION);
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            break;
        }
        break;

    case WM_DESTROY:
        mciSendStringW(L"close customSound_SystemAsterisk", NULL, 0, NULL);
        mciSendStringW(L"close customSound_SystemHand", NULL, 0, NULL);
        mciSendStringW(L"close customSound_SystemExclamation", NULL, 0, NULL);
        DeleteObject(hFont);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    FreeConsole();
    loadSettings();
    if (lpCmdLine && strcmp(lpCmdLine, "-background") == 0) {
        manageAutoStart(settings.autoStart);
        runReminderLoop();
        return 0;
    }
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW; // Redraw on resize
    RegisterClassW(&wc);
    // Height increased by 38 pixels (1 cm at 96 DPI)
    HWND hwnd = CreateWindowW(CLASS_NAME, L"BlinkPlusCharge Settings", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, 
                              CW_USEDEFAULT, CW_USEDEFAULT, 1240, 688, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}