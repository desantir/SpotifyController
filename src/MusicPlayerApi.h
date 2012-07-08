/* 
Copyright (C) 2011,2012 Robert DeSantis
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

extern "C" {

DWORD DMX_PLAYER_API GetPlayerApiVersion( void );
void DMX_PLAYER_API GetPlayerName( LPSTR buffer, size_t buffer_length );
bool DMX_PLAYER_API Connect( void );
bool DMX_PLAYER_API Disconnect( void );
bool DMX_PLAYER_API Signon( LPCSTR username, LPCSTR password );
bool DMX_PLAYER_API GetPlaylists( UINT* num_lists, DWORD* playlist_ids, size_t playlist_ids_capacity );
bool DMX_PLAYER_API GetPlaylistName( DWORD playlist_id, LPSTR buffer, size_t buffer_length );
bool DMX_PLAYER_API GetTracks( DWORD playlist_id, UINT* num_tracks, DWORD* track_ids, size_t track_ids_capacity );
bool DMX_PLAYER_API GetTrackName( DWORD track_id, LPSTR buffer, size_t buffer_length );
bool DMX_PLAYER_API PlayTrack( DWORD track_id, bool queue );
bool DMX_PLAYER_API PlayAllTracks( DWORD playlist_id, bool queue );
bool DMX_PLAYER_API ForwardTrack( void );
bool DMX_PLAYER_API BackTrack( void );
bool DMX_PLAYER_API StopTrack( void );
bool DMX_PLAYER_API PauseTrack( bool pause );
bool DMX_PLAYER_API GetPlayingTrack( DWORD* track_id, DWORD* track_length, DWORD* time_remaining, UINT* queued_tracks );
bool DMX_PLAYER_API IsTrackPaused( void );
bool DMX_PLAYER_API IsLoggedIn( void );
bool DMX_PLAYER_API GetQueuedTracks( UINT* num_tracks, DWORD* track_ids, size_t track_ids_capacity );
void DMX_PLAYER_API GetLastPlayerError( LPSTR buffer, size_t buffer_length );
bool DMX_PLAYER_API WaitOnTrackEvent( DWORD wait_ms, DWORD* track_id, bool* paused );

};

