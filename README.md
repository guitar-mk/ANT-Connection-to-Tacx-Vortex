# :tv: :bicyclist:
# Tacx Vortex Trainer Controller (C++ / ImGui)

![alt text](https://github.com/guitar_mk/ANT-Connection-to-Tacx-Vortex/blob/main/gui.png?raw=true)

Ein grafisches Dashboard zur Steuerung eines **Tacx Vortex (T2180)** Rollentrainers über USB/ANT+ auf dem Raspberry Pi. Das Projekt nutzt **Dear ImGui** für die Oberfläche und **libusb** für die direkte Hardware-Kommunikation.

## Features

* **Live Telemetrie:** Anzeige von Leistung, Geschwindigkeit und Trittfrequenz.
* **ERG Modus:** Der Trainer hält eine Ziel-Leistung (Watt) konstant, egal wie schnell man tritt.
* **Grade Modus:** Simulation von Steigungen (in %).
* **Daten-Logging:** Aufzeichnung des Trainings als `.csv` Datei (Zeit, Power, Speed, Cadence, Zielwerte).
* **Kalibrierung:** Echtzeit-Anpassung der Bremsstärke (Faktor) für ERG und Steigung direkt in der GUI.
* **Touch & Keyboard:** Bedienung über Touchscreen oder Tastatur (+/- Tasten).

## Voraussetzungen & Installation

Das Projekt wurde auf einem Raspberry Pi 5 (Raspberry Pi OS) entwickelt.

### 1. System-Pakete installieren
Du benötigst den C++ Compiler, SDL2 (für die Anzeige) und libusb (für die Trainer Verbindung):

```bash
sudo apt-get update
sudo apt-get install build-essential git
sudo apt-get install libsdl2-dev
sudo apt-get install libusb-1.0-0-dev
```

### 2. Projekt vorbereiten

Falls du das Projekt frisch startest:

```bash
# In den Projektordner wechseln
cd TacxTrainer

# ImGui (GUI Bibliothek) muss im Ordner 'include/imgui' liegen
# Falls der Ordner leer ist, führe das aus:
git clone [https://github.com/ocornut/imgui.git](https://github.com/ocornut/imgui.git) include/imgui
```

## Kompilieren

Ein Build-Skript liegt bei (`build.sh`). Es kompiliert alle C++ Dateien und linkt die Bibliotheken.

```bash
chmod +x build.sh  # Einmalig ausführbar machen
./build.sh
```

## Starten

Da wir direkt auf USB-Hardware zugreifen, werden (ohne udev-Regel) **Root-Rechte** benötigt:

```bash
sudo ./tacx_trainer
```

1.  Das Programm startet und sucht den Trainer (Status: "SUCHE...").
2.  **Tritt in die Pedale**, um den Trainer aufzuwecken (ANT+ Stick blinkt).
3.  Sobald die Verbindung steht (Status: "VERBUNDEN"), erscheinen die Werte.

## Steuerung

* **Modus wechseln:** Klick auf "ERG" oder "Grade" in der GUI.
* **Werte ändern:**
    * Schieberegler in der GUI.
    * Tasten `+` und `-` (Numpad oder normal) auf der Tastatur.
* **Aufnahme:** Button "START REC" oben rechts. CSV-Dateien landen im Programmordner.
* **Kalibrierung:** Unten im Dashboard können Faktoren für Watt und Steigung eingestellt werden ("ERG Härte").

## Projektstruktur

* `src/main.cpp`: Startpunkt, Initialisierung von SDL/OpenGL und Thread-Start.
* `src/gui.cpp`: Zeichnet das Dashboard (ImGui Code).
* `src/trainer.cpp`: Die Logik. Enthält den USB-Treiber, das Protokoll und den Hintergrund-Thread.
* `src/shared_data.h`: Datenaustausch zwischen GUI und Trainer-Logik.

```bash
TacxTrainer/
├── build.sh               # Das Kompilier-Skript
├── README.md              # Projekt-Dokumentation
├── tacx_trainer           # Das fertige Programm (nach dem Kompilieren)
├── my_trainer.txt         # Speichert die ID deines Trainers (automatisch erstellt)
├── src/
│   ├── main.cpp           # Hauptprogramm (Start, Fenster, Loop)
│   ├── gui.cpp            # Zeichnet das Dashboard (ImGui Code)
│   ├── gui.h              # Header für GUI
│   ├── trainer.cpp        # USB-Treiber & Hardware-Logik
│   ├── trainer.h          # Header für Trainer
│   └── shared_data.h      # Datenaustausch zwischen GUI & Logik
└── include/
    └── imgui/             # Die Grafik-Bibliothek (externe Abhängigkeit)
        ├── imgui.cpp
        ├── imgui_draw.cpp
        └── ...


