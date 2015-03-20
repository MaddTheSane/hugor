#include <SDL.h>
#include <SDL_mixer.h>
#include <QDebug>
#include <QFile>
#include <cstdio>

extern "C" {
#include "heheader.h"
}
#include "happlication.h"
#include "settings.h"
#include "rwopsbundle.h"
#include "hugohandlers.h"


// Current music and sample volumes. Needed to restore the volumes
// after muting them.
static int currentMusicVol = MIX_MAX_VOLUME;
static int currentSampleVol = MIX_MAX_VOLUME;
static bool isMuted = false;


void
initSoundEngine()
{
    // Initialize only the audio part of SDL.
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        qWarning("Unable to initialize sound system: %s", SDL_GetError());
        exit(1);
    }

    // This will preload the needed codecs now instead of constantly loading
    // and unloading them each time a sound is played/stopped.
    int sdlFormats = MIX_INIT_MP3 | MIX_INIT_MOD;
    if (Mix_Init((sdlFormats & sdlFormats) != sdlFormats)) {
        qWarning("Unable to load MP3 and/or MOD audio formats: %s", Mix_GetError());
        exit(1);
    }

    // Initialize the mixer. 44.1kHz, default sample format,
    // 2 channels (stereo) and a 4k chunk size.
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) != 0) {
        qWarning("Unable to initialize audio mixer: %s", Mix_GetError());
        exit(1);
    }
    Mix_AllocateChannels(8);
}


void
closeSoundEngine()
{
    // Shut down SDL and SDL_mixer.
    Mix_ChannelFinished(0);
    Mix_HookMusicFinished(0);
    // Close the audio device as many times as it was opened.
    int opened = Mix_QuerySpec(0, 0, 0);
    for (int i = 0; i < opened; ++i) {
        Mix_CloseAudio();
    }
    SDL_Quit();
}


void
muteSound( bool mute )
{
    if (mute and not isMuted) {
        Mix_VolumeMusic(0);
        Mix_Volume(-1, 0);
        isMuted = true;
    } else if (not mute and isMuted) {
        Mix_VolumeMusic(currentMusicVol);
        Mix_Volume(-1, currentSampleVol);
        isMuted = false;
    }
}


void
HugoHandlers::playmusic(HUGO_FILE infile, long reslength, char loop_flag, int* result)
{
    if (not hApp->settings()->enableMusic) {
        std::fclose(infile);
        *result = false;
        return;
    }

    // We only play one music track at a time, so it's enough
    // to make this static.
    static Mix_Music* music = 0;

    // Any currently playing music should be stopped before playing
    // a new one.
    Mix_HaltMusic();

    // Clean up any active data from a previous call.
    if (music != 0) {
        Mix_FreeMusic(music);
        music = 0;
    }

    // Create an RWops for the embedded media resource.
    SDL_RWops* rwops = RWFromMediaBundle(infile, reslength);
    if (rwops == 0) {
        qWarning() << "ERROR:" << SDL_GetError();
        std::fclose(infile);
        *result = false;
        return;
    }

    // SDL_mixer's auto-detection doesn't always work reliably. It's very
    // common for example to have broken headers in MP3s that otherwise play
    // just fine. So we use Mix_LoadMUSType_RW() without auto-detection.
    Mix_MusicType musType;
    switch (resource_type) {
    case MIDI_R:
        musType = MUS_MID;
        break;
    case XM_R:
    case S3M_R:
    case MOD_R:
        musType = MUS_MOD;
        break;
    case MP3_R:
        musType = MUS_MP3;
        break;
    default:
        qWarning() << "ERROR: Unknown music resource type";
        *result = false;
        return;
    }

    // Create a Mix_Music* from the RWops. Let SDL_mixer take ownership of
    // the rwops and free it automatically as needed.
    music = Mix_LoadMUSType_RW(rwops, musType, true);
    if (music == 0) {
        qWarning() << "ERROR:" << Mix_GetError();
        *result = false;
        return;
    }

    // Start playing the music. Loop forever if 'loop_flag' is true.
    // Otherwise, just play it once.
    if (Mix_PlayMusic(music, loop_flag ? -1 : 1) != 0) {
        qWarning() << "ERROR:" << Mix_GetError();
        Mix_FreeMusic(music);
        *result = false;
        return;
    }
    *result = true;
}


void
HugoHandlers::musicvolume(int vol)
{
    if (vol < 0)
        vol = 0;
    else if (vol > 100)
        vol = 100;

    // Convert the Hugo volume range [0..100] to the SDL volume
    // range [0..MIX_MAX_VOLUME].
    vol = (vol * MIX_MAX_VOLUME) / 100;
    if (not isMuted) {
        Mix_VolumeMusic(vol);
    }
    currentMusicVol = vol;
}


void
HugoHandlers::stopmusic()
{
    Mix_HaltMusic();
}


void
HugoHandlers::playsample(HUGO_FILE infile, long reslength, char loop_flag, int* result)
{
    if (not hApp->settings()->enableSoundEffects) {
        std::fclose(infile);
        *result = false;
        return;
    }

    // We only play one sample at a time, so it's enough to make these
    // static.
    static QFile* file = 0;
    static Mix_Chunk* chunk = 0;

    // Any currently playing sample should be stopped before playing
    // a new one.
    Mix_HaltChannel(-1);

    // If a file already exists from a previous call, delete it first.
    if (file != 0) {
        delete file;
        file = 0;
    }

    // Open 'infile' as a QFile.
    file = new QFile;
    if (not file->open(infile, QIODevice::ReadOnly)) {
        qWarning() << "ERROR: Can't open sample sound file";
        file->close();
        std::fclose(infile);
        *result = false;
        return;
    }

    // Map the data into memory and create an RWops from that data.
    SDL_RWops* rwops = SDL_RWFromConstMem(file->map(ftell(infile), reslength), reslength);
    // Done with the file.
    file->close();
    std::fclose(infile);
    if (rwops == 0) {
        qWarning() << "ERROR:" << SDL_GetError();
        *result = false;
        return;
    }

    // If a Mix_Chunk* already exists from a previous call, delete it first.
    if (chunk != 0) {
        Mix_FreeChunk(chunk);
        chunk = 0;
    }

    // Create a Mix_Chunk* from the RWops. Tell Mix_LoadWAV_RW() to take
    // ownership of the RWops so it will free it as necessary.
    chunk = Mix_LoadWAV_RW(rwops, true);
    if (chunk == 0) {
        qWarning() << "ERROR:" << Mix_GetError();
        *result = false;
        return;
    }

    // Start playing the sample. Loop forever if 'loop_flag' is true.
    // Otherwise, just play it once.
    if (Mix_PlayChannel(-1, chunk, loop_flag ? -1 : 0) < 0) {
        qWarning() << "ERROR:" << Mix_GetError();
        Mix_FreeChunk(chunk);
        *result = false;
        return;
    }
    *result = true;
}


void
HugoHandlers::samplevolume(int vol)
{
    if (vol < 0)
        vol = 0;
    else if (vol > 100)
        vol = 100;

    // Convert the Hugo volume range [0..100] to the SDL volume
    // range [0..MIX_MAX_VOLUME].
    vol = (vol * MIX_MAX_VOLUME) / 100;
    if (not isMuted) {
        Mix_Volume(-1, vol);
    }
    currentSampleVol = vol;
}


void
HugoHandlers::stopsample()
{
    Mix_HaltChannel(-1);
}
