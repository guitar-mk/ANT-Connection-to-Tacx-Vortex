# Tacx Vortex Trainer Controller (C++ / ImGui)

Ein grafisches Dashboard zur Steuerung eines **Tacx Vortex (T2180)** Rollentrainers über USB/ANT+ auf dem Raspberry Pi. Das Projekt nutzt **Dear ImGui** für die Oberfläche und **libusb** für die direkte Hardware-Kommunikation.

![Status](https://img.shields.io/badge/Status-Beta-orange) ![Platform](https://img.shields.io/badge/Platform-Raspberry_Pi_5-red)

## 🚀 Features

* **Live Telemetrie:** Anzeige von Watt, Geschwindigkeit und Trittfrequenz.
* **ERG Modus:** Der Trainer hält eine Ziel-Leistung (Watt) konstant, egal wie schnell man tritt.
* **Grade Modus:** Simulation von Steigungen (in %).
* **Daten-Logging:** Aufzeichnung des Trainings als `.csv` Datei (Zeit, Watt, Speed, Cadence, Zielwerte).
* **Kalibrierung:** Echtzeit-Anpassung der Bremsstärke (Faktor) für ERG und Steigung direkt in der GUI.
* **Touch & Keyboard:** Bedienung über Touchscreen oder Tastatur (+/- Tasten).

## 🛠️ Voraussetzungen & Installation

Das Projekt wurde auf einem Raspberry Pi 5 (Raspberry Pi OS) entwickelt.

### 1. System-Pakete installieren
Du benötigst den C++ Compiler, SDL2 (für das Fenster) und libusb (für den Trainer):

```bash
sudo apt-get update
sudo apt-get install build-essential git
sudo apt-get install libsdl2-dev       # Für Grafik/Fenster
sudo apt-get install libusb-1.0-0-dev  # Für USB Zugriff auf den Tacx