/* 
Copyright (C) 2011-14 Robert DeSantis
hopluvr at gmail dot com

This file is part of DMX Studio.
 
DMX Studio is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at your
option) any later version.
 
DMX Studio is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
License for more details.
 
You should have received a copy of the GNU General Public License
along with DMX Studio; see the file _COPYING.txt.  If not, write to
the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
MA 02111-1307, USA.
*/

#pragma once

#include "stdafx.h"

#define DMX_PLAYER_API __declspec(dllexport) __cdecl 

struct CachedTrack {
    ULONG    data_size;
    UINT     channels;
    UINT     bit_rate;
    UINT     length_ms;
    UINT     frames;
    bool     ready;
    BYTE     data[1];

    inline UINT getFrameSize() const {
        return channels * sizeof(int16_t);
    }
};

struct AudioInfo {
    char        id[512];
    char        song_type[512];                 // Comman separated list of 'christmas', 'live', 'studio', 'acoustic' and 'electric'
    int         key;                            // Key of song C,C#,D,D#,E,F,F#,G,B#,B,B#,B (0-11)
    int         mode;                           // 0=minor, 1=major
    int         time_signature;                 // beats per measure
    double      energy;                         // 0.0 < energy < 1.0
    double      liveness;                       // 0.0 < liveness < 1.0
    double      tempo;                          // 0.0 < tempo < 500.0 (BPM)
    double      speechiness;                    // 0.0 < speechiness < 1.0
    double      acousticness;                   // 0.0 < acousticness < 1.0
    double      instrumentalness;
    double      duration;                       // Duration of the track
    double      loudness;                       // -100.0 < loudness < 100.0 (dB)
    double      valence;                        // Emotion 0=negative to 1=positive
    double      danceability;                   // 0.0 < danceability < 1.0
};

extern "C" {

DWORD DMX_PLAYER_API GetPlayerApiVersion( void );
void DMX_PLAYER_API GetPlayerName( LPSTR buffer, size_t buffer_length );
bool DMX_PLAYER_API Connect( void );
bool DMX_PLAYER_API Disconnect( void );
bool DMX_PLAYER_API Signon( LPCSTR username, LPCSTR password );
bool DMX_PLAYER_API GetPlaylists( UINT* num_lists, DWORD* playlist_ids, size_t playlist_ids_capacity );
bool DMX_PLAYER_API GetPlaylistName( DWORD playlist_id, LPSTR buffer, size_t buffer_length );
bool DMX_PLAYER_API GetTracks( DWORD playlist_id, UINT* num_tracks, DWORD* track_ids, size_t track_ids_capacity );
bool DMX_PLAYER_API PlayTrack( DWORD track_id, bool queue );
bool DMX_PLAYER_API CacheTrack( DWORD track_idk );
bool DMX_PLAYER_API GetCachedTrack( CachedTrack** cached_track );
bool DMX_PLAYER_API PlayAllTracks( DWORD playlist_id, bool queue );
bool DMX_PLAYER_API ForwardTrack( void );
bool DMX_PLAYER_API BackTrack( void );
bool DMX_PLAYER_API StopTrack( void );
bool DMX_PLAYER_API PauseTrack( bool pause );
bool DMX_PLAYER_API GetPlayingTrack( DWORD* track_id, DWORD* track_length, DWORD* time_remaining, UINT* queued_tracks, UINT* previous_tracks );
bool DMX_PLAYER_API IsTrackPaused( void );
bool DMX_PLAYER_API IsLoggedIn( void );
bool DMX_PLAYER_API GetQueuedTracks( UINT* num_tracks, DWORD* track_ids, size_t track_ids_capacity );
bool DMX_PLAYER_API GetPlayedTracks( UINT* num_tracks, DWORD* track_ids, size_t track_ids_capacity );
void DMX_PLAYER_API GetLastPlayerError( LPSTR buffer, size_t buffer_length );
bool DMX_PLAYER_API WaitOnTrackEvent( DWORD wait_ms, DWORD* track_id, bool* paused );
bool DMX_PLAYER_API GetTrackInfo( DWORD track_id, 
                                  LPSTR track_name, size_t track_name_size,
                                  LPSTR artist_name, size_t artist_name_size,
                                  LPSTR album_name, size_t album_name_size,
                                  DWORD* track_duration_ms, 
                                  bool* starred,
                                  LPSTR track_link, size_t track_link_size );
bool DMX_PLAYER_API GetTrackAudioInfo( DWORD track_id, AudioInfo* audio_info );
};

