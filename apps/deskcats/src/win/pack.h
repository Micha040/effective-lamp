// DeskCats – App-Paket für das Windows-11-Kontextmenü bauen (DeskCats.exe --pack-menu <Ordner>)
#pragma once
#include "common.h"

namespace app {

// CLSID des Kontextmenü-Befehls in DeskCatsMenu.dll (muss zu src/menu/menu.c passen)
constexpr const wchar_t* kMenuClsid = L"9D533FE8-F14F-473D-8963-923041D1D1EF";
constexpr const wchar_t* kPackageName = L"DeskCats.Menu";
// Unsignierte Pakete nimmt Windows 11 nur mit diesem besonderen Herausgeber an
constexpr const wchar_t* kPackagePublisher = L"CN=DeskCats, OID.2.25.311729368913984317654407730594956997722=1";
// Unabhängig von der App-Version: nur erhöhen, wenn sich das Manifest ändert (Neuregistrierung braucht Adminrechte)
constexpr const wchar_t* kPackageVersion = L"1.0.0.0";

std::string buildManifest();
int packMenu(const std::wstring& outDir);   // 0 = ok

}  // namespace app
