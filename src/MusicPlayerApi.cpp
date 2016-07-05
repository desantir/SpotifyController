/* 
Copyright (C) 2011-16 Robert DeSantis
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

static size_t getTrackLinks( TrackArray& tracks, LPSTR buffer, size_t buffer_length );

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
bool DMX_PLAYER_API Connect( void )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    return theApp.m_spotify.connect();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API Disconnect( void )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    theApp.m_echonest.stop();

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
bool DMX_PLAYER_API WaitOnTrackEvent( DWORD wait_ms, LPSTR track_link, bool* paused )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    bool result = ::WaitForSingleObject( theApp.m_spotify.getTrackEvent(), wait_ms ) == WAIT_OBJECT_0;

    if ( track_link ) {
        sp_track* track = theApp.m_spotify.getPlayingTrack();
        if ( track != NULL ) {
            sp_link * link = sp_link_create_from_track( track, 0 );

            sp_link_as_string ( link, track_link, MAX_LINK_SIZE );
            sp_link_release( link );
        }
        else
            *track_link = '\0';
    }

    if ( paused )
        *paused = theApp.m_spotify.isTrackPaused();

    return result;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlaylists( UINT* num_lists, LPSTR playlist_links, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    PlaylistArray playlists = theApp.m_spotify.getPlaylists( );

    UINT link_counter = 0;
    UINT index = 0;
    playlist_links[0] = '\0';
    buffer_length--;
    CString spotifyLink;

    for ( sp_playlist* pl : playlists ) {
        sp_link * link = sp_link_create_from_playlist( pl );

        int size = sp_link_as_string ( link, spotifyLink.GetBuffer( MAX_LINK_SIZE ), MAX_LINK_SIZE );
        sp_link_release( link );
        spotifyLink.ReleaseBuffer();

        if ( (size_t)spotifyLink.GetLength()+1 > buffer_length )
            break;

        strcpy_s( playlist_links, buffer_length, spotifyLink );
        playlist_links = &playlist_links[size+1];
        *playlist_links = '\0';

        buffer_length -= (size+1);

        link_counter++;
    }

    *num_lists = link_counter;

    return *num_lists == playlists.size();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlaylistName( LPCSTR playlist_link, LPSTR buffer, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    sp_playlist* playlist = theApp.m_spotify.linkToPlaylist( playlist_link );
    if ( playlist == NULL )
        return false;

    LPCSTR name = sp_playlist_name( playlist );
    if ( name == NULL || strlen(name)+1 > buffer_length )
        return false;

    errno_t err = strncpy_s( buffer, buffer_length, name, strlen(name) );
    return err == 0;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetTracks( LPCSTR playlist_link, UINT* num_tracks, LPSTR track_links, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    TrackArray tracks;

    if ( !strcmp( PLAYED_PLAYLIST_LINK, playlist_link ) ) {
        tracks = theApp.m_spotify.getPlayedTracks();
    }
    else if ( !strcmp( QUEUED_PLAYLIST_LINK, playlist_link ) ) {
        tracks = theApp.m_spotify.getQueuedTracks();
    }
    else {
        sp_playlist* playlist = theApp.m_spotify.linkToPlaylist( playlist_link );
        if ( playlist == NULL )
            return false;

        tracks = theApp.m_spotify.getTracks( playlist );
    }

    *num_tracks = getTrackLinks( tracks, track_links, buffer_length );

    return *num_tracks == tracks.size();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API QueueTrack( LPCSTR track_link )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    sp_track* track = theApp.m_spotify.linkToTrack( track_link );
    if ( track == NULL )
        return false;

    theApp.m_spotify.queueTrack( track );

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API PlayTrack( LPCSTR track_link, DWORD seek_ms )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    sp_track* track = theApp.m_spotify.linkToTrack( track_link );
    if ( track == NULL )
        return false;

    theApp.m_spotify.playTrack( track, seek_ms );

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API PlayAllTracks( LPCSTR playlist_link, bool queue )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    sp_playlist* playlist = theApp.m_spotify.linkToPlaylist( playlist_link );
    if ( playlist == NULL )
        return false;

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
bool DMX_PLAYER_API GetPlayingTrack( LPSTR track_link, DWORD* track_length, 
                                     DWORD* time_remaining, UINT* queued_tracks, 
                                     UINT* previous_tracks )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if ( track_link ) {
        if ( theApp.m_spotify.getPlayingTrack() != NULL )
            strcpy_s( track_link, MAX_LINK_SIZE, theApp.m_spotify.getPlayingTrackLink() );
        else
            track_link[0] = '\0';
    }

    if ( track_length )
        *track_length =  theApp.m_spotify.getTrackLength();
    if ( time_remaining )
        *time_remaining = theApp.m_spotify.getTrackRemainingTime();
    if ( queued_tracks )
        *queued_tracks = theApp.m_spotify.getNumQueuedTracks();
    if ( previous_tracks )
        *previous_tracks = theApp.m_spotify.getNumPlayedTracks();

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetTrackInfo( LPCSTR track_link, 
                                  LPSTR track_name, size_t track_name_size,
                                  LPSTR artist_name, size_t artist_name_size, 
                                  LPSTR album_name, size_t album_name_size,
                                  DWORD* track_duration_ms, bool* starred )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    sp_track* track = theApp.m_spotify.linkToTrack( track_link );
    if ( !track )
        return false;

    if ( track_name ) {
        LPCSTR title = sp_track_name( track );
        if ( title == NULL )
            return false;
        errno_t err = strncpy_s( track_name, track_name_size, title, strlen(title) );
        if ( err != 0 )
            return false;
    }

    if ( artist_name ) {
        if ( sp_track_num_artists( track ) == 0 )
            *artist_name = '\0';
        else {
            sp_artist *artist = sp_track_artist( track, 0 );
            if ( artist == NULL )
                return false;
            LPCSTR title = sp_artist_name( artist );
            errno_t err = strncpy_s( artist_name, artist_name_size, title, strlen(title) );
            if ( err != 0 )
                return false;
        }
    }

    if ( album_name ) {
        sp_album *album = sp_track_album( track );
        if ( album == NULL )
            return false;
        LPCSTR title = sp_album_name( album );
        errno_t err = strncpy_s( album_name, album_name_size, title, strlen(title) );
        if ( err != 0 )
            return false;
    }

    if ( track_duration_ms )
        *track_duration_ms =  theApp.m_spotify.getTrackLength( track );
    if ( starred )
        *starred = theApp.m_spotify.isTrackStarred( track );

    return true;
}

// ----------------------------------------------------------------------------
//
AudioStatus DMX_PLAYER_API GetTrackAudioInfo( LPCSTR track_link, AudioInfo* audio_info, DWORD wait_ms ) 
{
    CString spotify_link;
    memset( audio_info, 0, sizeof(AudioInfo) );

    sp_track* track = theApp.m_spotify.linkToTrack( track_link );
    if ( track == NULL )
        return FAILED;

    sp_linktype link_type = theApp.m_spotify.getTrackLink( track, spotify_link );

    if ( link_type == SP_LINKTYPE_TRACK  )
        return theApp.m_echonest.getTrackAudioInfo( spotify_link, audio_info, wait_ms );

    if ( link_type == SP_LINKTYPE_LOCALTRACK ) {
        LPCSTR track_name = sp_track_name( track );
        if ( track_name == NULL )
            return FAILED;

        sp_artist *artist = sp_track_artist( track, 0 );
        if ( artist == NULL )
            return FAILED;

        LPCSTR artist_name = sp_artist_name( artist );
        if ( artist_name == NULL )
            return FAILED;

       return theApp.m_echonest.lookupTrackAudioInfo( track_name, artist_name, audio_info, wait_ms );
    }

    return FAILED;
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
bool DMX_PLAYER_API GetQueuedTracks( UINT* num_tracks, LPSTR track_links, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    TrackArray tracks = theApp.m_spotify.getQueuedTracks();

    *num_tracks = getTrackLinks( tracks, track_links, buffer_length );

    return *num_tracks == tracks.size();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlayedTracks( UINT* num_tracks, LPSTR track_links, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    TrackArray tracks = theApp.m_spotify.getPlayedTracks();

    *num_tracks = getTrackLinks( tracks, track_links, buffer_length );

    return *num_tracks == tracks.size();
}

// ----------------------------------------------------------------------------
//
void DMX_PLAYER_API GetLastPlayerError( LPSTR buffer, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    strncpy_s( buffer, buffer_length, theApp.m_spotify.getSpotifyError(), _TRUNCATE  );
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetTrackAnalysis( LPCSTR track_link, AnalyzeInfo** analysis_info )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    AnalyzeInfo* info = theApp.m_spotify.getTrackAnalysis( track_link );
    if ( info == NULL )
        return false;

    *analysis_info = info;
    
    return true;
}

// ---------------------------------------------------------------------------
//
static size_t getTrackLinks( TrackArray& tracks, LPSTR buffer, size_t buffer_length ) {
    UINT link_counter = 0;
    UINT index = 0;

    buffer[0] = '\0';
    buffer_length--;

    CString spotifyLink;

    for ( sp_track* track : tracks ) {
        sp_link * link = sp_link_create_from_track( track, 0 );

        UINT size = sp_link_as_string ( link, spotifyLink.GetBuffer(MAX_LINK_SIZE), MAX_LINK_SIZE );

        sp_link_release( link );
        spotifyLink.ReleaseBuffer();

        if ( size >= buffer_length )
            break;

        strcpy_s( buffer, buffer_length, spotifyLink );
        buffer = &buffer[size+1];
        *buffer = '\0';

        buffer_length -= (size+1);

        link_counter++;
    }

    return link_counter;
}
