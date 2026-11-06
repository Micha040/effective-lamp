// DeskCats – Pausen-Gründe (siehe pause.h)
#include "pause.h"

#include <shellapi.h>
#include <tlhelp32.h>
#include <wtsapi32.h>

#include <cwchar>

namespace app {

namespace {
// {6FE69556-704A-47A0-8F24-C28D936FDA47}
const GUID kConsoleDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};
}  // namespace

void PauseWatch::init(HWND host) {
    host_ = host;
    sessionRegistered_ = WTSRegisterSessionNotification(host, NOTIFY_FOR_THIS_SESSION) != FALSE;
    power_ = RegisterPowerSettingNotification(host, &kConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!sessionRegistered_) logLine(L"Sperrbildschirm-Benachrichtigung nicht verfügbar (%lu)", GetLastError());
}

void PauseWatch::shutdown() {
    if (sessionRegistered_) WTSUnRegisterSessionNotification(host_);
    sessionRegistered_ = false;
    if (power_) UnregisterPowerSettingNotification(power_);
    power_ = nullptr;
}

void PauseWatch::onSessionChange(WPARAM code) {
    if (code == WTS_SESSION_LOCK) locked_ = true;
    else if (code == WTS_SESSION_UNLOCK) locked_ = false;
}

void PauseWatch::onPowerBroadcast(WPARAM wp, LPARAM lp) {
    if (wp != PBT_POWERSETTINGCHANGE || !lp) return;
    auto* s = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
    if (IsEqualGUID(s->PowerSetting, kConsoleDisplayState) && s->DataLength >= sizeof(DWORD)) {
        DWORD state = *reinterpret_cast<const DWORD*>(s->Data);
        displayOff_ = state == 0;   // 0 = aus, 1 = an, 2 = gedimmt
    }
}

bool PauseWatch::gameRunning(const std::vector<std::wstring>& games, std::wstring* which) {
    if (games.empty()) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            for (const std::wstring& g : games) {
                if (_wcsicmp(pe.szExeFile, g.c_str()) == 0) {
                    found = true;
                    if (which) *which = g;
                    break;
                }
            }
        } while (!found && Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

bool PauseWatch::systemBusy(std::wstring* why) {
    QUERY_USER_NOTIFICATION_STATE st;
    if (FAILED(SHQueryUserNotificationState(&st))) return false;
    switch (st) {
        case QUNS_RUNNING_D3D_FULL_SCREEN:
            if (why) *why = L"Vollbild-Anwendung (exklusiv)";
            return true;
        case QUNS_PRESENTATION_MODE:
            if (why) *why = L"Präsentationsmodus";
            return true;
        case QUNS_NOT_PRESENT:
            if (why) *why = L"Bildschirmschoner oder anderer Benutzer";
            return true;
        default:
            return false;   // QUNS_BUSY (randloses Vollbild) regelt die Verdeckungs-Prüfung je Monitor
    }
}

}  // namespace app
