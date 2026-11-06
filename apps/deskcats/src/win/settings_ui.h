// DeskCats – Einstellungsfenster
#pragma once
#include "settings.h"

#include <functional>

namespace app {

// Öffnet das Fenster (oder holt es nach vorne). onApply wird bei "Übernehmen"/"OK" mit den neuen Werten aufgerufen.
void showSettingsWindow(HINSTANCE inst, const Settings& current, std::function<void(const Settings&, bool autostart)> onApply);
bool settingsWindowOpen();
// Tastatur (Tab, Enter, Esc) im Einstellungsfenster: aus der Nachrichtenschleife aufrufen
bool settingsDialogMessage(MSG* m);

}  // namespace app
