#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <thread>

#include "shared_data.h"
#include "trainer.h"
#include "gui.h"

int main(int, char**)
{
    // 1. Hardware Init (SDL Bibliothek für Fenster & Input)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) return -1;

    // OpenGL ES 2.0 Profil für Raspberry Pi setzen (ressourcensparend)
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Fenster erstellen (1024x600 px)
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Tacx Visu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 800, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // V-Sync aktivieren (verhindert "Tearing" des Bildes)

    // 2. GUI System (ImGui) starten
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().FontGlobalScale = 2.0f; // Alles 2x größer für Lesbarkeit
    
    // Verknüpfung von ImGui mit SDL und OpenGL
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // 3. Logik starten
    SharedData data; // Unser zentraler Datenspeicher
    
    // Startet den Trainer-Code in einem PARALLELEN Thread.
    // Wichtig: 'run_trainer_thread' läuft jetzt im Hintergrund weiter.
    std::thread trainer_thread(run_trainer_thread, &data);

    ImVec4 clear_color = ImVec4(0.10f, 0.10f, 0.10f, 1.00f); // Hintergrundfarbe
    
    // 4. Die Hauptschleife (läuft bis User beendet)
    while (data.app_running)
    {
        // Eingaben verarbeiten (Maus, Tastatur, Fenster schließen)
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) data.app_running = false;
        }

        // Neuen Frame vorbereiten
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Unsere GUI zeichnen (ruft Code in gui.cpp auf)
        RenderDashboard(&data);

        // Alles auf den Bildschirm bringen (Rendern)
        ImGui::Render();
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        
        // CPU-Bremse: 30ms warten -> Ergibt ca. 30 FPS.
        // Ohne das würde der Pi 100% CPU nutzen nur um das Menü zu zeichnen.
        SDL_Delay(30); 
    }

    // 5. Aufräumen beim Beenden
    if (trainer_thread.joinable()) trainer_thread.join(); // Warten bis Trainer Thread fertig ist

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
