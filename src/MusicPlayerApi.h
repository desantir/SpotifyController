/* 
Copyright (C) 2011-2017 Robert DeSantis
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

#define MAX_AUDIO_TEXT_LEN      256
#define MAX_LINK_SIZE           MAX_AUDIO_TEXT_LEN

#define PLAYED_PLAYLIST_LINK    "local:playlist:played"
#define QUEUED_PLAYLIST_LINK    "local:playlist:queued"

enum AudioStatus {
    FAILED = 0,                         // Request failed
    OK = 1,                             // Item processed
    QUEUED = 2,                         // Request queued
    NOT_AVAILABLE = 3                   // Resource is not available
};

struct AnalyzeInfo {
    char        link[256];
    UINT        duration_ms;            // Duration of each data point
    size_t      data_count;             // Number of data points
    uint16_t    data[1];                // Amplitude data (0 = 32767)
};

enum PlayerEvent {
    TRACK_PLAY = 1,                     // Track play start
    TRACK_STOP = 2,                     // Track stopped
    TRACK_PAUSE = 3,                    // Track paused
    TRACK_RESUME = 4,                   // Track resumed
    TRACK_POSITION = 5,                 // Track position,
    TRACK_QUEUES = 6,                   // Track queues changed
    PLAYLIST_ADDED = 7,                 // New playlist added
    PLAYLIST_REMOVED = 8,               // Playlist removed
    PLAYLIST_CHANGED = 9                // Playlist changed (tracks, name, etc)
};

struct PlayerEventData {
    PlayerEvent m_event;
    ULONG       m_event_ms;
    LPCSTR      m_link;
    ULONG       m_played_size;
    ULONG       m_queued_size;

    PlayerEventData( PlayerEvent event, ULONG event_ms, LPCSTR track_link ) :
        m_event( event ),
        m_event_ms( event_ms ),
        m_link( track_link )
    {}

    PlayerEventData( ULONG played_size, ULONG queued_size ) :
        m_event( TRACK_QUEUES ),
        m_played_size( played_size ),
        m_queued_size( queued_size )
    {}
};

class IPlayerEventCallback
{
public:
    virtual HRESULT STDMETHODCALLTYPE notify( PlayerEventData* pNotify ) = 0;
};

struct PlayerImage {
	char		m_href[MAX_LINK_SIZE];
	UINT		m_height;
	UINT		m_width;

	PlayerImage( LPCSTR href, UINT height, UINT width ) :
		m_height( height ),
		m_width( width )
	{
		strncpy_s( m_href, MAX_LINK_SIZE, href, strlen(href) );
	}
};

struct PlayerInfo {
	char        player_type[MAX_AUDIO_TEXT_LEN];
	char        player_name[MAX_AUDIO_TEXT_LEN];
	char        player_authorization[2048];
    char        player_authorization_url[512];
};

struct PlayingInfo {
    char        track_link[MAX_LINK_SIZE];
    DWORD       track_length;
    DWORD       time_remaining;
    UINT        queued_tracks;
    UINT        previous_tracks;
};

struct TrackInfo {
    char        track_link[MAX_LINK_SIZE];
    char        track_name[MAX_AUDIO_TEXT_LEN];
    char        artist_name[MAX_AUDIO_TEXT_LEN];
    char        album_name[MAX_AUDIO_TEXT_LEN];
	PlayerImage	image[3];
    DWORD       track_duration_ms;
};

struct PlaylistInfo {
	char        playlist_link[MAX_LINK_SIZE];
	char        playlist_name[MAX_AUDIO_TEXT_LEN];
	UINT        playlist_tracks;
};

struct AudioInfo {
    char        track_link[MAX_LINK_SIZE];
    char        id[MAX_LINK_SIZE];
    char        song_type[MAX_AUDIO_TEXT_LEN];  // Comman separated list of 'christmas', 'live', 'studio', 'acoustic' and 'electric'
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
bool DMX_PLAYER_API GetPlayerInfo( PlayerInfo* player_info );
bool DMX_PLAYER_API Connect( void );
bool DMX_PLAYER_API Disconnect( void );
bool DMX_PLAYER_API RegisterEventListener( IPlayerEventCallback* listener );
bool DMX_PLAYER_API UnregisterEventListener( IPlayerEventCallback* listener );
bool DMX_PLAYER_API Signon( LPCSTR username, LPCSTR password );
bool DMX_PLAYER_API AcceptAuthorization( LPBYTE authorization_response, DWORD authorization_len );
bool DMX_PLAYER_API IsLoggedIn( void );

bool DMX_PLAYER_API GetPlaylists( UINT* num_lists, LPSTR playlist_links, size_t buffer_length );
bool DMX_PLAYER_API GetPlaylistInfo( LPCSTR playlist_link, PlaylistInfo* playlistinfo );
bool DMX_PLAYER_API GetTracks( LPCSTR playlist_link, UINT* num_tracks, LPSTR track_links, size_t buffer_length );
bool DMX_PLAYER_API SearchTracks( LPCSTR search_query, UINT* num_tracks, LPSTR track_links, size_t buffer_length );
bool DMX_PLAYER_API PlayTrack( LPCSTR track_link, DWORD seek_ms );
bool DMX_PLAYER_API QueueTrack( LPCSTR track_link );
bool DMX_PLAYER_API PlayAllTracks( LPCSTR playlist_link, bool queue );
bool DMX_PLAYER_API ForwardTrack( void );
bool DMX_PLAYER_API BackTrack( void );
bool DMX_PLAYER_API StopTrack( void );
bool DMX_PLAYER_API PauseTrack( bool pause );
bool DMX_PLAYER_API GetPlayingTrack( PlayingInfo *playing_info );
bool DMX_PLAYER_API IsTrackPaused( void );
bool DMX_PLAYER_API GetQueuedTracks( UINT* num_tracks, LPSTR track_links, size_t buffer_length );
bool DMX_PLAYER_API GetPlayedTracks( UINT* num_tracks, LPSTR track_links, size_t buffer_length );
void DMX_PLAYER_API GetLastPlayerError( LPSTR buffer, size_t buffer_length );
bool DMX_PLAYER_API WaitOnTrackEvent( DWORD wait_ms, LPSTR track_link, bool* paused );
AudioStatus DMX_PLAYER_API GetTrackAudioInfo( LPCSTR track_link, AudioInfo* audio_info, DWORD wait_ms );
bool DMX_PLAYER_API GetTrackInfo( LPCSTR track_link, TrackInfo * track_info );
bool DMX_PLAYER_API GetTrackAnalysis( LPCSTR track_link, AnalyzeInfo** analysis_info );
};

