// FragDesk – größere Bausteine: Tabellen, Diagramme, Kennzahl-Karten, Rundenleiste, Trefferbild
#pragma once
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "../core/stats.h"
#include "gfx.h"

namespace ui {

// ---- Tabelle --------------------------------------------------------------------------------------------------
struct Col {
    std::wstring title;
    float width;   // > 0: feste Breite in DIP, < 0: Anteil am Rest (z. B. -1, -2)
    Align align = Align::Left;
    std::wstring tip = {};
};

struct Cell {
    std::wstring text;
    Color color = Color(0, 0, 0, 0);   // A = 0: Standardfarbe
    F font = F::Body;
    Color dot = Color(0, 0, 0, 0);     // farbiges Kästchen vor dem Text
    Color pill = Color(0, 0, 0, 0);    // Text als Pille
    Icon icon = Icon::None;
    Color iconColor = Color(0, 0, 0, 0);
    std::wstring sub;                  // zweite Zeile, klein
    std::wstring tip;
    double sort = NAN;                 // Sortierwert (sonst Text)

    Cell() = default;
    Cell(std::wstring t) : text(std::move(t)) {}
    Cell(std::wstring t, double s) : text(std::move(t)), sort(s) {}
};

struct Row {
    std::vector<Cell> cells;
    std::function<void()> click;
    std::string key;
    bool highlight = false;
    Color accentBar = Color(0, 0, 0, 0);   // farbiger Streifen links (z. B. Sieg/Niederlage)
};

struct TableOpts {
    std::string key;
    float rowH = 40, headH = 34;
    bool header = true;
    int sortCol = -1;
    bool sortDesc = true;
    std::function<void(int)> onSort;   // nullptr = nicht sortierbar
};

void sortRows(std::vector<Row>& rows, int col, bool desc);
float table(Canvas& c, float x, float y, float w, const std::vector<Col>& cols, const std::vector<Row>& rows, const TableOpts& o);

// ---- Kennzahlen -----------------------------------------------------------------------------------------------
void statCard(Canvas& c, const RectF& r, const std::wstring& label, const std::wstring& value, const std::wstring& sub,
              const std::wstring& tip, Color valueColor = Color(0, 0, 0, 0), Icon icon = Icon::None);

// Liste von Kennzahlen mit Gesamt, T und CT; liefert die Höhe
float statTable(Canvas& c, float x, float y, float w, const std::vector<const char*>& ids, const fd::Counters& all,
                const fd::Counters& t, const fd::Counters& ct, bool sides = true, const std::string& key = "st");

// Farbe für Ratings (grün gut, rot schwach)
Color ratingColor(const Theme& th, double rating);

// ---- Diagramme ------------------------------------------------------------------------------------------------
struct ChartPoint {
    double y = 0;
    std::wstring tip;
    std::wstring label;   // Achsenbeschriftung (wird ausgedünnt)
    std::function<void()> click;
};

struct LineChartOpts {
    std::string key;
    std::function<std::wstring(double)> fmtY;
    Color line = Color(0, 0, 0, 0);
    std::function<Color(double)> pointColor;   // z. B. Premier-Farben
    std::vector<std::pair<double, Color>> bands;   // ab Wert -> Farbe (Hintergrundstreifen)
    double ref = NAN;                               // Bezugslinie (z. B. 1,00)
    double yMin = NAN, yMax = NAN;                  // diese Werte immer mit anzeigen
    double yCap = NAN;                              // Achse endet höchstens hier (z. B. 100 bei Prozent)
    bool area = true;
    int movingAvg = 0;   // gleitender Durchschnitt über n Punkte zusätzlich
};
void lineChart(Canvas& c, const RectF& r, const std::vector<ChartPoint>& pts, const LineChartOpts& o);

struct Bar {
    std::wstring label, value, sub, tip;
    double v = 0;
    Color color = Color(0, 0, 0, 0);
    std::function<void()> click;
};
float barList(Canvas& c, float x, float y, float w, const std::vector<Bar>& bars, double maxV, float rowH = 34, float labelW = 130);

// Treffer nach Körperzonen (Anteile in %)
void hitFigure(Canvas& c, const RectF& r, double head, double neck, double chest, double stomach, double arms, double legs);

// Rundenleiste eines Matches
struct RoundMark {
    int n = 0;
    bool won = false, dead = false, clutchWon = false, half = false;
    int kills = 0, clutch = 0;
    std::wstring open;   // K/D
    std::wstring tip;
    std::string side;    // T/CT
    std::function<void()> click;
};
void roundStrip(Canvas& c, const RectF& r, const std::vector<RoundMark>& rounds, const std::string& key);

// Leerer Zustand mit Symbol, Titel und Erklärung
float emptyState(Canvas& c, float x, float y, float w, Icon icon, const std::wstring& title, const std::wstring& text);

// Abschnittstitel; liefert die Höhe
float sectionTitle(Canvas& c, float x, float y, float w, const std::wstring& title, const std::wstring& sub = L"");

}  // namespace ui
