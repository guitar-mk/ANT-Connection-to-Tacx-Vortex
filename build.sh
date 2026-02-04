#!/bin/bash
echo "Kompiliere TacxTrainer..."

# WICHTIG: -lusb-1.0 muss dabei sein!
# Wir kompilieren alle .cpp Dateien in src/ und alle ImGui Dateien

g++ src/main.cpp src/gui.cpp src/trainer.cpp \
    include/imgui/imgui*.cpp \
    include/imgui/backends/imgui_impl_sdl2.cpp \
    include/imgui/backends/imgui_impl_opengl3.cpp \
    -o tacx_trainer \
    -Iinclude/imgui \
    -Isrc \
    -I/usr/include/SDL2 \
    -I/usr/include/libusb-1.0 \
    -lSDL2 -lGL -ldl -lusb-1.0 -pthread

echo "Fertig. Starte mit ./tacx_trainer"
