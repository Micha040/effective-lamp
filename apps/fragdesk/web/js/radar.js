// FragDesk Handy-App – Radarbilder und Umrechnung von Spielkoordinaten (wie fd::radarUV in src/core/radar.h) für
// Heatmap und 2D-Ansicht. r: Radar-Angaben aus der Schnittstelle (writeRadar in src/api/view_common.cpp):
// {name, image, lowerImage?, posX, posY, span, lowerZ?} – null, wenn die Karte kein Radarbild hat.
const images = new Map();

// Bild laden (einmal je Adresse, bleibt im Speicher); null, wenn es nicht geht
export function radarImage(url) {
  if (!url) return Promise.resolve(null);
  if (!images.has(url)) {
    images.set(url, new Promise(resolve => {
      const img = new Image();
      img.decoding = 'async';
      img.onload = () => resolve(img);
      img.onerror = () => { images.delete(url); resolve(null); };
      img.src = url;
    }));
  }
  return images.get(url);
}

// Spielkoordinaten -> 0..1 im Radarbild (x nach rechts, y nach unten)
export function radarUV(r, x, y) {
  return [(x - r.posX) / r.span, (r.posY - y) / r.span];
}

// liegt die Höhe z auf der unteren Ebene?
export function radarLower(r, z) {
  return !!r.lowerImage && z < r.lowerZ;
}
