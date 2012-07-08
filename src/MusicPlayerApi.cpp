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

#include "stdafx.h"
#include "MusicPlayerApi.h"

// ----------------------------------------------------------------------------
//
 DWORD DMX_PLAYER_API GetPlayerApiVersion( )
{
    return 1;
}

// ----------------------------------------------------------------------------
//
 void DMX_PLAYER_API GetPlayerName( LPSTR buffer, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    strncpy_s( buffer, buffer_length, "Spotify(tm) Music Controller", _TRUNCATE  );
}

// ----------------------------------------------------------------------------
//
 bool DMX_PLAYER_API Connect( )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    return theApp.m_spotify.connect();
}

// ----------------------------------------------------------------------------
//
 bool DMX_PLAYER_API Disconnect()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    return theApp.m_spotify.disconnect( );
}

// ----------------------------------------------------------------------------
//
 bool DMX_PLAYER_API Signon( LPCSTR username, LPCSTR password )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    return theApp.m_spotify.connect( username, password );
}

// ----------------------------------------------------------------------------
//
 bool DMX_PLAYER_API WaitOnTrackEvent( DWORD wait_ms, DWORD* track_id, bool* paused )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    bool result = ::WaitForSingleObject( theApp.m_spotify.getTrackEvent(), wait_ms ) == WAIT_OBJECT_0;

    if ( track_id )
        *track_id = (DWORD)theApp.m_spotify.getPlayingTrack();
    if ( paused )
        *paused = theApp.m_spotify.isTrackPaused();

    return result;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlaylists( UINT* num_lists, DWORD* playlist_ids, size_t playlist_ids_capacity )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    PlaylistArray playlists = theApp.m_spotify.getPlaylists( );

    for ( size_t i=0; i < playlists.size() && i < playlist_ids_capacity; i++ )
        playlist_ids[i] = (DWORD)playlists[i];

    *num_lists = std::min<size_t>( playlist_ids_capacity, playlists.size() );

    return *num_lists == playlists.size();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlaylistName( DWORD playlist_id, LPSTR buffer, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    LPCSTR name = sp_playlist_name(  reinterpret_cast<sp_playlist*>(playlist_id) );
    if ( name == NULL || strlen(name)+1 > buffer_length )
        return false;

    errno_t err = strncpy_s( buffer, buffer_length, name, strlen(name) );
    return err == 0;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetTracks( DWORD playlist_id, UINT* num_tracks, DWORD* track_ids, size_t track_ids_capacity )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    TrackArray tracks = theApp.m_spotify.getTracks( reinterpret_cast<sp_playlist*>(playlist_id) );

    for ( size_t i=0; i < tracks.size() && i < track_ids_capacity; i++ )
        track_ids[i] = (DWORD)tracks[i];

    *num_tracks = std::min<size_t>( track_ids_capacity, tracks.size() );

    return *num_tracks == tracks.size();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetTrackName( DWORD track_id, LPSTR buffer, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    LPCSTR title = sp_track_name( reinterpret_cast<sp_track *>(track_id) );
    if ( title == NULL )
        return false;

    sp_artist *artist = sp_track_artist( reinterpret_cast<sp_track *>(track_id), 0 );
    if ( artist == NULL )
        return false;

    CString label;
    label.Format( "%s by %s", title, sp_artist_name( artist ) );

    if ( (size_t)label.GetLength()+1 > buffer_length )
        return false;

    errno_t err = strncpy_s( buffer, buffer_length, label, label.GetLength() );
    return err == 0;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API PlayTrack( DWORD track_id, bool queue )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    sp_track* track = reinterpret_cast<sp_track *>(track_id);

    if ( !queue )
        theApp.m_spotify.playTrack( track );
    else
        theApp.m_spotify.queueTrack( track );

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API PlayAllTracks( DWORD playlist_id, bool queue )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    sp_playlist* playlist = reinterpret_cast<sp_playlist*>(playlist_id);

    if ( !queue )
        theApp.m_spotify.playTracks( playlist );
    else
        theApp.m_spotify.queueTracks( playlist );

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API ForwardTrack( void )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    theApp.m_spotify.nextTrack();

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API BackTrack( void )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    theApp.m_spotify.previousTrack();

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API StopTrack( void )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    theApp.m_spotify.stopTrack();

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API PauseTrack( bool pause )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    theApp.m_spotify.pauseTrack( pause );

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlayingTrack( DWORD* track_id, DWORD* track_length, 
                                     DWORD* time_remaining, UINT* queued_tracks )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if ( track_id )
        *track_id = (DWORD)(theApp.m_spotify.getPlayingTrack());
    if ( track_length )
        *track_length =  theApp.m_spotify.getTrackLength();
    if ( time_remaining )
        *time_remaining = theApp.m_spotify.getTrackRemainingTime();
    if ( queued_tracks )
        *queued_tracks = theApp.m_spotify.getNumQueuedTracks();
    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API IsTrackPaused( void )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    return theApp.m_spotify.isTrackPaused();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API IsLoggedIn( void )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    return theApp.m_spotify.getLoginState() == LOGIN_SUCCESS;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetQueuedTracks( UINT* num_tracks, DWORD* track_ids, size_t track_ids_capacity )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    TrackArray tracks = theApp.m_spotify.getQueuedTracks();

    for ( size_t i=0; i < tracks.size() && i < track_ids_capacity; i++ )
        track_ids[i] = (DWORD)tracks[i];

    *num_tracks = std::min<size_t>( track_ids_capacity, tracks.size() );

    return *num_tracks == tracks.size();
}

// ----------------------------------------------------------------------------
//
void DMX_PLAYER_API GetLastPlayerError( LPSTR buffer, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    strncpy_s( buffer, buffer_length, theApp.m_spotify.getSpotifyError(), _TRUNCATE  );
}