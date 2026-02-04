#pragma once
#include "shared_data.h"

// Diese Funktion wird den Hintergrund-Thread starten
void run_trainer_thread(SharedData* data);

// Hier laden wir später die ID (aus mk.cpp Logik)
unsigned short load_trainer_id_from_file();
