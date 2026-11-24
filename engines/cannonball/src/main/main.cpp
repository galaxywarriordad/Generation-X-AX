/***************************************************************************
    Cannonball Main Entry Point.
    
    Copyright Chris White.
    See license.txt for more details.
***************************************************************************/

#include <cstring>
#include <iostream>

// SDL Library
#include <SDL.h>

// SDL Specific Code
#include "sdl2/timer.hpp"
#include "sdl2/input.hpp"

#include "video.hpp"

#include "romloader.hpp"
#include "trackloader.hpp"
#include "stdint.hpp"
#include "main.hpp"
#include "engine/outrun.hpp"
#include "frontend/config.hpp"
#include "frontend/menu.hpp"

#include "engine/oinputs.hpp"
#include "engine/ooutputs.hpp"
#include "engine/omusic.hpp"

// Direct X Haptic Support.
// Fine to include on non-windows builds as dummy functions used.
#include "directx/ffeedback.hpp"

#if defined(__has_include)
#if __has_include("ax/topology.hpp")
#include "ax/topology.hpp"
#define CANNONBALL_HAS_AX_TOPOLOGY 1
#endif
#endif

// ------------------------------------------------------------------------------------------------
// Initialize Shared Variables
// ------------------------------------------------------------------------------------------------
using namespace cannonball;

int    cannonball::state       = STATE_BOOT;
double cannonball::frame_ms    = 0;
int    cannonball::frame       = 0;
bool   cannonball::tick_frame  = true;
int    cannonball::fps_counter = 0;

// ------------------------------------------------------------------------------------------------
// Main Variables and Pointers
// ------------------------------------------------------------------------------------------------
Audio cannonball::audio;
Menu* menu = nullptr;
bool pause_engine = false;

// ------------------------------------------------------------------------------------------------

static void apply_ax_topology_hints()
{
#if defined(CANNONBALL_HAS_AX_TOPOLOGY)
    // Align foreground emulation / render work with [AX] physical core policy
    ax::topology::apply_default_emulation_affinity();
#endif
}

static void quit_func(int code)
{
    audio.stop_audio();
    input.close_joy();
    forcefeedback::close();
    delete menu;
    menu = nullptr;
    SDL_Quit();
    exit(code);
}

static void process_events(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE)
                    state = STATE_QUIT;
                else
                    input.handle_key_down(&event.key.keysym);
                break;

            case SDL_KEYUP:
                input.handle_key_up(&event.key.keysym);
                break;

            case SDL_JOYAXISMOTION:
                input.handle_joy_axis(&event.jaxis);
                break;

            case SDL_JOYBUTTONDOWN:
                input.handle_joy_down(&event.jbutton);
                break;

            case SDL_JOYBUTTONUP:
                input.handle_joy_up(&event.jbutton);
                break;

            case SDL_CONTROLLERAXISMOTION:
                input.handle_controller_axis(&event.caxis);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                input.handle_controller_down(&event.cbutton);
                break;

            case SDL_CONTROLLERBUTTONUP:
                input.handle_controller_up(&event.cbutton);
                break;

            case SDL_JOYHATMOTION:
                input.handle_joy_hat(&event.jhat);
                break;

            case SDL_JOYDEVICEADDED:
                input.open_joy();
                break;

            case SDL_JOYDEVICEREMOVED:
                input.close_joy();
                break;

            case SDL_QUIT:
                state = STATE_QUIT;
                break;
        }
    }
}

static void tick()
{
    frame++;

    if (config.fps == 60)
        tick_frame = (frame & 1) != 0;
    else if (config.fps == 120)
        tick_frame = (frame & 3) == 1;

    process_events();

    if (tick_frame)
    {
        oinputs.tick();
        oinputs.do_gear();
    }

    switch (state)
    {
        case STATE_GAME:
        {
            if (tick_frame)
            {
                if (input.has_pressed(Input::TIMER)) outrun.freeze_timer = !outrun.freeze_timer;
                if (input.has_pressed(Input::PAUSE)) pause_engine = !pause_engine;
                if (input.has_pressed(Input::MENU))  state = STATE_INIT_MENU;
            }

            if (!pause_engine || input.has_pressed(Input::STEP))
            {
                outrun.tick(tick_frame);
                if (tick_frame) input.frame_done();
                osoundint.tick();
            }
            else
            {
                if (tick_frame) input.frame_done();
            }
        }
        break;

        case STATE_INIT_GAME:
            if (config.engine.jap && !roms.load_japanese_roms())
            {
                state = STATE_QUIT;
            }
            else
            {
                tick_frame = true;
                pause_engine = false;
                outrun.init();
                state = STATE_GAME;
            }
            break;

        case STATE_MENU:
            menu->tick();
            input.frame_done();
            osoundint.tick();
            break;

        case STATE_INIT_MENU:
            oinputs.init();
            outrun.outputs->init();
            menu->init();
            state = STATE_MENU;
            break;
    }

    outrun.outputs->writeDigitalToConsole();
    if (tick_frame)
    {
        input.set_rumble(outrun.outputs->is_set(OOutputs::D_MOTOR), config.controls.rumble);
    }
}

static void main_loop()
{
    Timer fps_count;
    int local_frame = 0;
    fps_count.start();

    const bool vsync = config.video.vsync == 1 && video.supports_vsync();
    Timer frame_time;
    double deltatime = 0.0;

    while (state != STATE_QUIT)
    {
        frame_time.start();
        tick();

        video.prepare_frame();
        video.render_frame();

        audio.tick();

        if (!vsync)
        {
            deltatime += (frame_ms * audio.adjust_speed());
            const int deltaintegral = static_cast<int>(deltatime);
            const int t = frame_time.get_ticks();

            if (static_cast<double>(t) < deltatime)
            {
                const double remaining = deltatime - static_cast<double>(t);
                if (remaining >= 1.0)
                {
                    SDL_Delay(static_cast<Uint32>(remaining));
                }
            }

            deltatime -= static_cast<double>(deltaintegral);
        }

        if (config.video.fps_count)
        {
            local_frame++;
            if (fps_count.get_ticks() >= 1000)
            {
                fps_counter = local_frame;
                local_frame = 0;
                fps_count.start();
            }
        }
    }

    quit_func(0);
}

static bool parse_command_line(int argc, char* argv[])
{
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-cfgfile") == 0 && i + 1 < argc)
        {
            config.set_config_file(argv[i + 1]);
        }
        else if (strcmp(argv[i], "-file") == 0 && i + 1 < argc)
        {
            if (!trackloader.set_layout_track(argv[i + 1]))
                return false;
        }
        else if (strcmp(argv[i], "-help") == 0)
        {
            std::cout << "Command Line Options:\n\n" <<
                         "-cfgfile: Location and name of config.xml\n" <<
                         "-file   : LayOut Editor track data to load\n" << std::endl;
            return false;
        }
    }
    return true;
}

int main(int argc, char* argv[])
{
    apply_ax_topology_hints();

    bool ok = parse_command_line(argc, argv);

    if (ok)
    {
        config.load();
        ok = roms.load_revb_roms(config.sound.fix_samples);
    }
    if (!ok)
    {
        quit_func(1);
        return 0;
    }

    if (SDL_GameControllerAddMappingsFromFile((config.data.res_path + "gamecontrollerdb.txt").c_str()) == -1)
        std::cout << "Unable to load controller mapping" << std::endl;

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) == -1)
    {
        std::cerr << "SDL Initialization Failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    if (!omusic.load_widescreen_map(config.data.res_path))
        std::cout << "Unable to load widescreen tilemaps" << std::endl;

    config.set_fps(config.video.fps);
    if (!video.init(&roms, &config.video))
        quit_func(1);

    audio.init();

    state = config.menu.enabled ? STATE_INIT_MENU : STATE_INIT_GAME;

    input.init(config.controls.pad_id,
               config.controls.keyconfig, config.controls.padconfig,
               config.controls.analog,    config.controls.axis, config.controls.invert, config.controls.asettings);

    if (config.controls.haptic)
        config.controls.haptic = forcefeedback::init(config.controls.max_force, config.controls.min_force, config.controls.force_duration);

    menu = new Menu();
    menu->populate();
    main_loop();

    return 0;
}
