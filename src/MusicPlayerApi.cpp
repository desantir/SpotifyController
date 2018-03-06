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

#include "stdafx.h"
#include "SpotifyEngine.h"
#include "MusicPlayerApi.h"
#include "HttpUtils.h"

static size_t getTrackLinks( TrackLinkList& tracks, LPSTR buffer, size_t buffer_length );

// ----------------------------------------------------------------------------
//
DWORD DMX_PLAYER_API GetPlayerApiVersion( )
{
    return 2;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlayerInfo( PlayerInfo* player_info )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

	CString authorization;
	authorization.Format( "%s:%s", g_webapi_client_id, g_webapi_client_secret );

    CString authorization_url;
    authorization_url.Format( "https://accounts.spotify.com/authorize?client_id=%s&response_type=code"
                              "&redirect_uri=<REDIRECT>&scope=user-read-private+user-read-email+user-library-read+user-read-birthdate+playlist-read-private&"
                              "show_dialog=true&state=<STATE>",
                              g_webapi_client_id );

	strncpy_s( player_info->player_type, sizeof(player_info->player_type), "SPOTIFY", _TRUNCATE  );
    strncpy_s( player_info->player_name, sizeof(player_info->player_name), "Spotify(tm) Music Controller", _TRUNCATE  );
	strncpy_s( player_info->player_authorization, sizeof(player_info->player_authorization), authorization, _TRUNCATE  );
    strncpy_s( player_info->player_authorization_url, sizeof(player_info->player_authorization_url), authorization_url, _TRUNCATE  );

	return true;
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

    theApp.m_spotify_web.stop();

    return theApp.m_spotify.disconnect( );
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API RegisterEventListener( IPlayerEventCallback* listener ) 
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    return theApp.m_spotify.registerEventListener( listener );
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API UnregisterEventListener( IPlayerEventCallback* listener )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    return theApp.m_spotify.unregisterEventListener( listener );
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
bool DMX_PLAYER_API IsLoggedIn( void )
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	return theApp.m_spotify.getLoginState() == LOGIN_SUCCESS;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API AcceptAuthorization( LPBYTE authorization_response, DWORD authorization_len )
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	return theApp.m_spotify_web.newAuthorization( authorization_response, authorization_len );
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API WaitOnTrackEvent( DWORD wait_ms, LPSTR track_link, bool* paused )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    bool result = ::WaitForSingleObject( theApp.m_spotify.getTrackEvent(), wait_ms ) == WAIT_OBJECT_0;

    if ( track_link )
        strcpy_s( track_link, MAX_LINK_SIZE, theApp.m_spotify.getCurrentTrack() );

    if ( paused )
        *paused = theApp.m_spotify.isTrackPaused();

    return result;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlaylists( UINT* num_lists, LPSTR playlist_links, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

	size_t link_counter = 0;

	playlist_links[0] = '\0';
	buffer_length--;

	PlaylistList& playlists = theApp.m_spotify_web.fetchUserPlaylists( );

	for ( Playlist playlist : playlists ) {
		size_t size = playlist.m_uri.GetLength()+1;

		if ( size > buffer_length )
			break;

		strcpy_s( playlist_links, buffer_length, playlist.m_uri );
		playlist_links = &playlist_links[size];
		*playlist_links = '\0';

		buffer_length -= size;

		link_counter++;
	}

	*num_lists = link_counter;

	return *num_lists == playlists.size();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlaylistInfo( LPCSTR playlist_link, PlaylistInfo* playlistinfo )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

	Playlist* playlist = theApp.m_spotify_web.getPlaylist( playlist_link );
    if ( playlist == NULL )
        return false;

	CString name;

	if ( playlist->isAlbum() ) {
		name.Format( "Album: %s", playlist->m_name );

		if ( playlist->m_artists.size( ) != 0 ) {
			name.AppendFormat( " by %s", playlist->m_artists[0].m_name );
		}
	}
	else {
		name = playlist->m_name;
	}

	errno_t err = strncpy_s( playlistinfo->playlist_link, MAX_LINK_SIZE, playlist->m_uri, playlist->m_uri.GetLength() );
	if ( err != 0 )
		return false;

	err = strncpy_s( playlistinfo->playlist_name, MAX_AUDIO_TEXT_LEN, name, name.GetLength() );
	if ( err != 0 )
		return false;

	playlistinfo->playlist_tracks = playlist->m_track_count;
	
	return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetTracks( LPCSTR playlist_uri, UINT* num_tracks, LPSTR track_links, size_t buffer_length )
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	if ( !strcmp( PLAYED_PLAYLIST_LINK, playlist_uri ) ) {
		TrackLinkList tracks = theApp.m_spotify.getPlayedTracks();
		*num_tracks = getTrackLinks( tracks, track_links, buffer_length );
		return *num_tracks == tracks.size();
	}
	else if ( !strcmp( QUEUED_PLAYLIST_LINK, playlist_uri ) ) {
		TrackLinkList tracks = theApp.m_spotify.getQueuedTracks();
		*num_tracks = getTrackLinks( tracks, track_links, buffer_length );
		return *num_tracks == tracks.size();
	}

	TrackLinkList* tracks = theApp.m_spotify_web.fetchUserPlaylistTracks( playlist_uri );
	if ( tracks == NULL )
		return false;

	*num_tracks = getTrackLinks( *tracks, track_links, buffer_length );;

	return *num_tracks == tracks->size();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API SearchTracks( LPCSTR search_query, UINT* num_tracks, LPSTR track_links, size_t buffer_length )
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	TrackPtrList tracks = theApp.m_spotify_web.searchForTracks( search_query );
	if ( tracks.size() == 0 )
		return false;

	size_t link_counter = 0;

	track_links[0] = '\0';
	buffer_length--;

	for ( Track* track : tracks ) {
		size_t size = track->m_uri.GetLength()+1;

		if ( size > buffer_length )
			break;

		strcpy_s( track_links, buffer_length, (LPCSTR)track->m_uri );
		track_links = &track_links[size];
		*track_links = '\0';

		buffer_length -= size;

		link_counter++;
	}

	*num_tracks = link_counter;

	return *num_tracks == tracks.size();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetTrackInfo( LPCSTR track_link, TrackInfo * track_info )
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	Track* track = theApp.m_spotify_web.fetchTrack( track_link );
	if ( track == NULL )
		return false;

	errno_t err;

	err = strncpy_s( track_info->track_link, MAX_LINK_SIZE, track_link, strlen(track_link) );
	if ( err != 0 )
		return false;

	err = strncpy_s( track_info->track_name, MAX_AUDIO_TEXT_LEN, track->m_name, track->m_name.GetLength() );
	if ( err != 0 )
		return false;

	if ( track->m_artists.size() == 0 )
		track_info->artist_name[0] = '\0';
	else {
		CString& artist_name = track->m_artists[0].m_name;
		err = strncpy_s( track_info->artist_name, MAX_AUDIO_TEXT_LEN, artist_name, artist_name.GetLength() );
		if ( err != 0 )
			return false;
	}

	err = strncpy_s( track_info->album_name, MAX_AUDIO_TEXT_LEN, track->m_album.m_name, track->m_album.m_name.GetLength() );
	if ( err != 0 )
		return false;

	for ( size_t index=0; index < 3; index++ ) {
		if ( track->m_album.m_images.size() > index ) {
			Image& img = track->m_album.m_images[index];

			track_info->image[index].m_height = img.m_height;
			track_info->image[index].m_width = img.m_width;
			strncpy_s( track_info->image[index].m_href, MAX_LINK_SIZE, img.m_href, img.m_href.GetLength() );
		}
		else {
			track_info->image[index].m_height = 0;
			track_info->image[index].m_width = 0;
			track_info->image[index].m_href[0] = '\0';
		}
	}

	track_info->track_duration_ms =  track->m_duration_ms;

	return true;
}

// ----------------------------------------------------------------------------
//
AudioStatus DMX_PLAYER_API GetTrackAudioInfo( LPCSTR track_link, AudioInfo* audio_info, DWORD wait_ms ) 
{
	memset( audio_info, 0, sizeof(AudioInfo) );

	if ( strncmp( track_link, SPOTIFY_TRACK_PREFIX, strlen(SPOTIFY_TRACK_PREFIX) ) == 0 )
		return theApp.m_spotify_web.getTrackAudioInfo( track_link, audio_info, wait_ms );

	if ( strncmp( track_link, LOCAL_TRACK_PREFIX, strlen(LOCAL_TRACK_PREFIX) ) == 0 ) {
		CString clean_url = unencodeString( track_link );

		TrackPtrList tracks = theApp.m_spotify_web.searchForTracks( &((LPCSTR)clean_url)[strlen(LOCAL_TRACK_PREFIX)] );
		if ( tracks.size() == 0 )
			return FAILED;

		return theApp.m_spotify_web.getTrackAudioInfo( tracks[0]->m_uri, audio_info, wait_ms );
	}

	return FAILED;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API QueueTrack( LPCSTR track_link )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    theApp.m_spotify.queueTrack( track_link );

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API PlayTrack( LPCSTR track_link, DWORD seek_ms )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    theApp.m_spotify.playTrack( track_link, seek_ms );

    return true;
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API PlayAllTracks( LPCSTR playlist_link, bool queue )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

	TrackLinkList* tracks = theApp.m_spotify_web.fetchUserPlaylistTracks( playlist_link );
	if ( tracks == NULL )
		return false;

    if ( !queue )
        theApp.m_spotify.playTracks( *tracks );
    else
        theApp.m_spotify.queueTracks( *tracks );

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
bool DMX_PLAYER_API IsTrackPaused( void )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    return theApp.m_spotify.isTrackPaused();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetQueuedTracks( UINT* num_tracks, LPSTR track_links, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

	TrackLinkList tracks = theApp.m_spotify.getQueuedTracks();

    *num_tracks = getTrackLinks( tracks, track_links, buffer_length );

    return *num_tracks == tracks.size();
}

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlayedTracks( UINT* num_tracks, LPSTR track_links, size_t buffer_length )
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

	TrackLinkList& tracks = theApp.m_spotify.getPlayedTracks();

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

// ----------------------------------------------------------------------------
//
bool DMX_PLAYER_API GetPlayingTrack( PlayingInfo *playing_info )
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());

	LPCSTR playing = theApp.m_spotify.getPlayingTrackLink();
	if ( playing == NULL )
		return false;

	strcpy_s( playing_info->track_link, MAX_LINK_SIZE, playing );

	playing_info->track_length =  theApp.m_spotify.getTrackLength();
	playing_info->time_remaining = theApp.m_spotify.getTrackRemainingTime();
	playing_info->queued_tracks = theApp.m_spotify.getNumQueuedTracks();
	playing_info->previous_tracks = theApp.m_spotify.getNumPlayedTracks();

	return theApp.m_spotify.getPlayingTrackLink() != NULL;
}

// ---------------------------------------------------------------------------
//
static size_t getTrackLinks( TrackLinkList& tracks, LPSTR buffer, size_t buffer_length ) {
	size_t link_counter = 0;

    buffer[0] = '\0';
    buffer_length--;

	for ( CString& track_uri : tracks ) {
		size_t size = track_uri.GetLength()+1;

		if ( size > buffer_length )
			break;

		strcpy_s( buffer, buffer_length, track_uri );
		buffer = &buffer[size];
		*buffer = '\0';

		buffer_length -= size;

		link_counter++;
	}

    return link_counter;
}
