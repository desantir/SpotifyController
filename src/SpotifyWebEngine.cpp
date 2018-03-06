/* 
Copyright (C) 2017 Robert DeSantis
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
#include "SpotifyWebEngine.h"
#include "SimpleJsonParser.h"
#include "SimpleJsonBuilder.h"
#include "HttpUtils.h"

#include <io.h>  

// ----------------------------------------------------------------------------
//
SpotifyWebEngine::SpotifyWebEngine( ) :
	Threadable( "EchoNestEngine" )
{
	m_trackInfoContainer.Format( "%s\\DMXStudio\\SpotifyTrackInfoCache", (LPCSTR)getUserDocumentDirectory() );
	CreateDirectory( m_trackInfoContainer, NULL );
}

// ----------------------------------------------------------------------------
//
SpotifyWebEngine::~SpotifyWebEngine( )
{
	stop();
}

// ----------------------------------------------------------------------------
//
bool SpotifyWebEngine::newAuthorization( LPBYTE authorization, DWORD authorization_len )
{
	CString filename;
	filename.Format( "%s\\DMXStudio\\%s", getUserDocumentDirectory(), SPOTIFY_TOKEN_FILE );

	if ( PathFileExists( filename ) )
		DeleteFile( filename );

	FILE* tokenFile = _fsopen( filename, "at", _SH_DENYWR );
	if ( !tokenFile )
		return false;

	fwrite( authorization, authorization_len, sizeof(char), tokenFile );
	fclose( tokenFile );

	CString auth_json;
	auth_json.Append( (LPCSTR)authorization, authorization_len );

	return parseAuthorization( auth_json );
}

// ----------------------------------------------------------------------------
//
bool SpotifyWebEngine::loadAuthorization()
{
	CString filename;
	filename.Format( "%s\\DMXStudio\\%s", getUserDocumentDirectory(), SPOTIFY_TOKEN_FILE );

	m_auth_token.Empty();
	m_auth_refresh.Empty();

	if ( !PathFileExists(filename) )
		return false;
		
	FILE* tokenFile = _fsopen( filename, "rt", _SH_DENYWR );
	if ( !tokenFile )
		return false;

	fseek( tokenFile, 0L, SEEK_END );
	size_t size = ftell( tokenFile );
	fseek( tokenFile, 0L, SEEK_SET );

	CString auth_json;

	fread_s( auth_json.GetBufferSetLength(size+1), size, 1, size, tokenFile );
	fclose( tokenFile );

	auth_json.ReleaseBufferSetLength( size );

	return parseAuthorization( auth_json );
}

// ----------------------------------------------------------------------------
//
void SpotifyWebEngine::writeAuthorization( LPCSTR access_token, LPCSTR refresh_token, unsigned expires_in )
{
	CString filename;
	filename.Format( "%s\\DMXStudio\\%s", getUserDocumentDirectory(), SPOTIFY_TOKEN_FILE );

	if ( PathFileExists( filename ) )
		DeleteFile( filename );

	CString contents;

	JsonBuilder json( contents );
	json.startObject();
	json.add( "access_token", access_token );
	json.add( "refresh_token", refresh_token );
	json.add( "expires_in", expires_in );
	json.endObject();

	FILE* tokenFile = _fsopen( filename, "at", _SH_DENYWR );
	if ( tokenFile ) {
		fwrite( (LPCSTR)contents, contents.GetLength(), sizeof(char), tokenFile );
		fclose( tokenFile );
	}
}

// ----------------------------------------------------------------------------
//
bool SpotifyWebEngine::parseAuthorization(LPCSTR auth_json)
{
	m_auth_token.Empty();
	m_auth_refresh.Empty();

	SimpleJsonParser parser;

	try {
		parser.parse( auth_json );

		m_auth_token = parser.get<CString>( "access_token" );
		m_auth_refresh = parser.get<CString>( "refresh_token" );
	}
	catch ( std::exception& e ) {
		log( StudioException( "JSON parser error (%s) data (%s)", e.what(), auth_json ) );
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------------
//
bool SpotifyWebEngine::loadUser( )
{
	if ( m_auth_token.GetLength() == 0 )
		return false;

	SimpleJsonParser parser;

	// Load all the playlist
	LPBYTE buffer = NULL;

	try {
		buffer = get( "/v1/me", false );

		parser.parse( (LPCSTR)buffer );
		free( buffer );
		buffer = NULL;

		m_user_id = parser.get<CString>( "id" );
	}
	catch ( std::exception& e ) {
		if ( buffer )
			free( buffer );

		log( e );

		return false;
	}

	return true;
}

// ----------------------------------------------------------------------------
//
bool SpotifyWebEngine::checkUserAuthorization()
{
	if ( m_auth_token.IsEmpty() && !loadAuthorization() )
		return false;

	return loadUser();
}

// ----------------------------------------------------------------------------
//
PlaylistList& SpotifyWebEngine::fetchUserPlaylists( )
{
	SimpleJsonParser parser;

	m_playlists.clear();

	// Load all the playlist
	CString api_url( "/v1/me/playlists?limit=50&offset=0" );

	while ( true ) {
		LPBYTE buffer = NULL;

		try {
			buffer = get( api_url );
			parser.parse( (LPCSTR)buffer );
			free( buffer );
			buffer = NULL;

			for ( JsonNode* playlist_node : parser.getObjects("items") ) {
				CString owner_id = playlist_node->getObject( "owner" )->get<CString>( "id" );
				// if ( owner_id == "spotify" )	// Libspotify can't play tracks not in a user playlist
				//	continue;

				CString id = playlist_node->get<CString>( "id" );
				CString name = playlist_node->get<CString>( "name" );
				CString uri = playlist_node->get<CString>( "uri" );

				JsonNode* tracks_node = playlist_node->getObject( "tracks" );

				CString tracks_href = tracks_node->get<CString>( "href" );
				unsigned tracks_count = tracks_node->get<unsigned>( "total" );

				m_playlists.emplace_back( id, name, uri, tracks_href, tracks_count, owner_id );
			}

			if ( parser.getObject("next")->isNull( ) )
				break;

			api_url = parser.get<CString>( "next" );

			int uri_start = api_url.Find( "/v1" );
			if ( uri_start > 0 )
				api_url = api_url.Mid( uri_start );
		}
		catch ( std::exception& e ) {
			if ( buffer != NULL )
				free( buffer );

			log( e );
			break;
		}
	}

	// Load all the albums
	
	api_url = "/v1/me/albums?limit=50&offset=0";

	while ( true ) {
		LPBYTE buffer = NULL;

		try {
			buffer = get( api_url );
			parser.parse( (LPCSTR)buffer );
			free( buffer );
			buffer = NULL;

			for ( JsonNode* node : parser.getObjects("items") ) {
				JsonNode* album_node = node->getObject( "album" );

				CString id = album_node->get<CString>( "id" );
				CString name = album_node->get<CString>( "name" );
				CString uri = album_node->get<CString>( "uri" );

				JsonNode* tracks_node = album_node->getObject( "tracks" );
				CString tracks_href = tracks_node->get<CString>( "href" );
				unsigned tracks_count = tracks_node->get<unsigned>( "total" );
					
				Playlist playlist( id, name, uri, tracks_href, tracks_count, "" );

				for ( JsonNode* artist_node : album_node->getObjects("artists") ) {
					CString artist_id = album_node->get<CString>( "id" );
					CString artist_name = artist_node->get<CString>( "name" );
					CString artist_uri = artist_node->get<CString>( "uri" );

					playlist.add( Artist( artist_id, artist_name, artist_uri ) );
				}

				m_playlists.push_back( playlist );
			}

			if ( parser.getObject("next")->isNull( ) )
				break;

			api_url = parser.get<CString>( "next" );

			int uri_start = api_url.Find( "/v1" );
			if ( uri_start > 0 )
				api_url = api_url.Mid( uri_start );
		}
		catch ( std::exception& e ) {
			if ( buffer != NULL )
				free( buffer );

			log( e );
			break;
		}
	}

	return m_playlists;
}

// ----------------------------------------------------------------------------
//
Playlist* SpotifyWebEngine::getPlaylist( LPCSTR playlist_uri )
{
	for ( Playlist& playlist : m_playlists ) {
		if ( playlist.m_uri == playlist_uri )
			return &playlist;
	}

	return NULL;
}

// ----------------------------------------------------------------------------
//
TrackLinkList* SpotifyWebEngine::fetchUserPlaylistTracks( LPCSTR playlist_uri )
{
	Playlist* playlist = getPlaylist( playlist_uri );
	if ( playlist == NULL ) {
		// Update our playlists - this may be the first request or may be new
		theApp.m_spotify_web.fetchUserPlaylists( );

		playlist = getPlaylist( playlist_uri );
		if ( playlist == NULL )
			return NULL;
	}

	// Return cached track list
	if ( playlist->m_tracks.size() > 0 )
		return &playlist->m_tracks;

	// Fetch track list
	CString api_url;

	if ( playlist->isAlbum() ) {
		api_url.Format( "/v1/albums/%s/tracks?offset=0", (LPCSTR)playlist->m_id );
	}
	else {
		api_url.Format( "/v1/users/%s/playlists/%s/tracks?limit=100&offset=0", (LPCSTR)playlist->m_owner_id, (LPCSTR)playlist->m_id );
	}

	while ( true ) {
		LPBYTE buffer = NULL;

		try {
			SimpleJsonParser parser;

			buffer = get( api_url );
			parser.parse( (LPCSTR)buffer );
			free( buffer );
			buffer = NULL;

			for ( JsonNode* node : parser.getObjects("items") ) {
				JsonNode* track_node = ( playlist->isAlbum() ) ? node : node->getObject( "track" );

				Track* track = loadAndCacheTrack( track_node );
				if ( track != NULL )
					playlist->add( *track );
			}

			if ( parser.getObject("next")->isNull( ) )
				break;

			api_url = parser.get<CString>( "next" );

			int uri_start = api_url.Find( "/v1" );
			if ( uri_start > 0 )
				api_url = api_url.Mid( uri_start );
		}
		catch ( std::exception& e ) {
			if ( buffer != NULL )
				free( buffer );

			log( e );
			break;
		}
	}

	return &playlist->m_tracks;
}

// ----------------------------------------------------------------------------
//
TrackPtrList SpotifyWebEngine::searchForTracks( LPCSTR search_key )
{
	CString api_url;
	api_url.Format( "/v1/search?q=%s&type=track&offset=0&limit=50", (LPCSTR)encodeString( search_key ) );

	TrackPtrList tracks;

	LPBYTE buffer = NULL;

	try {
		SimpleJsonParser parser;

		buffer = get( api_url );
		parser.parse( (LPCSTR)buffer );

		JsonNode* tracks_node = parser.getObject( "tracks" );

		for ( JsonNode* track_node : tracks_node->getObjects("items") ) {
			Track* track = loadAndCacheTrack( track_node );
			if ( track != NULL )
				tracks.push_back( track );
		}
 	}
	catch ( std::exception& e ) {
		log( e );
	}

	if ( buffer != NULL )
		free( buffer );

	return tracks;
}

// ----------------------------------------------------------------------------
//
Track* SpotifyWebEngine::fetchTrack( LPCSTR track_uri ) {
	Track* track = getTrack( track_uri );
	if ( track != NULL )
		return track;

	// Fetch track
	CString api_url;
	api_url.Format( "/v1/tracks/%s", &track_uri[strlen( SPOTIFY_TRACK_PREFIX )] );

	LPBYTE buffer = NULL;

	try {
		SimpleJsonParser parser;

		buffer = get( api_url );
		parser.parse( (LPCSTR)buffer );

		track = loadAndCacheTrack( &parser );
	}
	catch ( std::exception& e ) {
		log( e );
	}

	if ( buffer != NULL )
		free( buffer );

	return track;
}

// ----------------------------------------------------------------------------
//
Track* SpotifyWebEngine::loadAndCacheTrack( JsonNode* track_node ) 
{
	CString id = track_node->get<CString>( "id" );
	CString name = track_node->get<CString>( "name" );
	CString uri = track_node->get<CString>( "uri" );
	CString href = track_node->get<CString>( "href" );
	int duration_ms = track_node->get<int>( "duration_ms" );

	Track track( id, name, uri, href, duration_ms );

	if ( track_node->has_key( "artists" ) ) {
		for ( JsonNode* artist_node : track_node->getObjects("artists") ) {
			CString artist_id = artist_node->get<CString>( "id" );
			CString artist_name = artist_node->get<CString>( "name" );
			CString artist_uri = artist_node->get<CString>( "uri" );

			track.add( Artist( artist_id, artist_name, artist_uri ) );
		}
	}

	if ( track_node->has_key( "album" ) ) {
		JsonNode* album_node = track_node->getObject( "album" );

		CString album_id = album_node->get<CString>( "id" );
		CString album_name = album_node->get<CString>( "name" );
		CString album_uri = album_node->get<CString>( "uri" );

		track.add( Album( album_id, album_name, album_uri ) );

		for ( JsonNode* image_node : album_node->getObjects( "images" ) ) {
			track.m_album.m_images.emplace_back( 
				image_node->get<CString>( "url" ), 
				image_node->get<UINT>( "height" ), 
				image_node->get<UINT>( "width" ) );
		}
	}

	return addTrack( track );
}

// ----------------------------------------------------------------------------
//
LPBYTE SpotifyWebEngine::get( LPCSTR api_url, bool check_authorization )
{
	CSingleLock lock( &m_get_mutex, TRUE ); 

	BYTE *buffer = NULL;
	ULONG buffer_size = 0L;

	if ( check_authorization && !checkUserAuthorization() )
		throw StudioException( "User needs to authenticate with Spotify service" );

	for ( unsigned tries=2; tries--; ) {
		CStringW http_headers;
		http_headers.Format( L"Authorization: Bearer %s\r\n", (LPCWSTR)CA2W(m_auth_token) );

		DWORD dwStatusCode = httpGet( L"api.spotify.com", api_url, (LPCWSTR)http_headers, &buffer, &buffer_size );

		if ( dwStatusCode == 200 ) {				// Success
			buffer = (BYTE *)realloc( buffer, buffer_size+1 );
			buffer[buffer_size] = '\0';
			return buffer;
		}

#if 0
		if ( dwStatusCode == 429 ) {           // Over limit
			wait_seconds = 60;

			// See if they told us how long
			char seconds[80];
			DWORD size = sizeof(seconds)-1;

			if ( WinHttpQueryHeaders( hRequest, 
				WINHTTP_QUERY_CUSTOM | WINHTTP_QUERY_FLAG_REQUEST_HEADERS, 
				L"Retry-After", 
				seconds, &size, WINHTTP_NO_HEADER_INDEX ) ) {
				seconds[size] = '\0';
				sscanf_s( seconds, "%u", &wait_seconds );
			}
		}
#endif

		if ( dwStatusCode == 401 ) {				// Reauthorize
			CString auth;
			auth.Format( "%s:%s", g_webapi_client_id, g_webapi_client_secret );

			char base64[2048];
			int base64len = sizeof(base64);
			encodeBase64( auth, base64, &base64len );

			http_headers.Format( L"Authorization: Basic %s\r\nContent-Type: application/x-www-form-urlencoded\r\n", 
				(LPCWSTR)CA2W(base64) );

			CString body;
			body.Format( "grant_type=refresh_token&refresh_token=%s", (LPCSTR)m_auth_refresh );

			httpPost( L"accounts.spotify.com", "/api/token", body, (LPCWSTR)http_headers, &buffer, &buffer_size );

			buffer = (BYTE *)realloc( buffer, buffer_size+1 );
			buffer[buffer_size] = '\0';

			SimpleJsonParser parser;

			try {
				parser.parse( (LPCSTR)buffer );

				m_auth_token = parser.get<CString>( "access_token" );

				unsigned expires_in = parser.get<unsigned>( "expires_in" );

				writeAuthorization( m_auth_token, m_auth_refresh, expires_in );
			}
			catch ( std::exception& e ) {
				throw StudioException( "JSON parser error (%s) data (%s)", e.what(), (LPCSTR)buffer );
			}
		}
		else
			throw StudioException( "Received unexpected HTTP status code %lu", dwStatusCode );
	}

	throw StudioException( "Token refresh error" );
}

// ----------------------------------------------------------------------------
// TRACK AUDIO INFO QUEUEING AND FETCHING
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
//
void SpotifyWebEngine::start()
{
	if ( !isRunning() )
		startThread();
}

// ----------------------------------------------------------------------------
//
void SpotifyWebEngine::stop()
{
	if ( isRunning() ) {
		stopThread();

		// Wake up the request processor if sleeping
		m_wake.SetEvent();
	}
}
// ----------------------------------------------------------------------------
//
UINT SpotifyWebEngine::run(void)
{
	log_status( "Spotify Web API engine started" );

	DWORD end_of_wait_period = 0L;                                  // Next period start tick count for throttling

	InfoRequestList work_queue;                                     // The list of tracks obtained in a single echjonest request

	while ( isRunning() ) {
		::WaitForSingleObject(m_wake.m_hObject, 10 * 1000 );

		// Process pending info request with throttling
		while ( m_requests.size() > 0 ) {
			DWORD time = GetTickCount();

			if ( time < end_of_wait_period ) {
				log_status( "Audio info throttled - waiting %ld seconds (%d pending)", (end_of_wait_period-time)/1000, m_requests.size() );
				break;
			}

			work_queue.clear();

			CSingleLock request_lock( &m_audio_info_mutex, TRUE );

			while ( work_queue.size() <= 25 && m_requests.size() > 0 ) {
				InfoRequest request = m_requests.back();

				m_requests.pop_back();          // Remove the request

												// Duplicate request
				if ( m_track_audio_info_cache.find( request.getKey() ) != m_track_audio_info_cache.end() )
					continue;

				if ( !request.hasLink() ) {     // Currently no support for no links
					log_status( "Audio information for '%s' is unavailable", request.getKey() );

					AudioInfo audio_info;
					strcpy_s( audio_info.id, UNAVAILABLE_ID );
					strncpy_s( audio_info.track_link, request.getKey(), sizeof(audio_info.track_link) );

					saveAudioInfo( audio_info );
					continue;
				}

				work_queue.push_back( request );
			}

			request_lock.Unlock();

			if ( work_queue.size() == 0 )       // All were duplicates or eliminated
				continue;

			// Make the echonest URL request
			CString echonest_url;

			echonest_url.Format( "/v1/audio-features/?ids=" );

			bool first = true;
			for ( auto const & request : work_queue ) {
				if ( !first )
					echonest_url.AppendChar( ',' );

				LPCSTR link = request.getSpotifyLink();
				if ( strstr( link, SPOTIFY_TRACK_PREFIX ) == link )
					link = &link[ strlen(SPOTIFY_TRACK_PREFIX ) ];

				echonest_url.Append( link );

				first = false;
			}

			LPBYTE buffer = NULL;

			try {
				/*
				UINT wait_seconds;

				if ( !httpGet( accessToken, echonest_url, buffer, wait_seconds ) ) { // Hit a limit - wait_seconds contains penatly
					CSingleLock lock( &m_request_mutex, TRUE );
					for ( auto const & request : work_queue )
						m_requests.push_back( request );

					end_of_wait_period =  GetTickCount() + (wait_seconds*1000); 
					log_status( "Exceeded EchoNest request limit" );
					break;
				}
				*/

				buffer = get( echonest_url );

				log_status( "Query %d track(s), %d track(s) in queue", work_queue.size(), m_requests.size() );

				fetchSongData( work_queue, (LPCSTR)buffer, echonest_url );

				free( buffer );

				// Mark any remaining tracks in work queue as unavailable (for this session only)
				for ( auto const & request : work_queue ) {
					log_status( "Audio information for '%s' is unavailable", request.getKey() );

					AudioInfo audio_info;
					strcpy_s( audio_info.id, UNAVAILABLE_ID );
					strncpy_s( audio_info.track_link, request.getKey(), sizeof(audio_info.track_link) );

					saveAudioInfo( audio_info );
				}
			}
			catch ( std::exception& ex ) {
				if ( buffer != NULL )
					free( buffer );

				log( ex );
			}
		}
	}

	log_status( "Spotify Web API engine stopped" );

	return 0;
}

// ----------------------------------------------------------------------------
//
AudioStatus SpotifyWebEngine::getTrackAudioInfo( LPCSTR spotify_track_link, AudioInfo* audio_info, DWORD wait_ms )
{
	return getAudioInfo( InfoRequest( spotify_track_link ), audio_info, wait_ms );
}

// ----------------------------------------------------------------------------
//
AudioStatus SpotifyWebEngine::lookupTrackAudioInfo( LPCSTR track_name, LPCSTR artist_name, AudioInfo* audio_info, DWORD wait_ms )
{
	return getAudioInfo( InfoRequest( artist_name, track_name ), audio_info, wait_ms );
}

// ----------------------------------------------------------------------------
//
AudioStatus SpotifyWebEngine::getAudioInfo( InfoRequest& request, AudioInfo* audio_info, DWORD wait_ms )
{
	AudioInfo* track_info = loadAudioInfo( request.getKey() );
	if ( track_info != NULL ) {
		if ( !strcmp( UNAVAILABLE_ID, track_info->id ) )
			return NOT_AVAILABLE;

		*audio_info = *track_info;
		return OK;
	}

	// If wait time is < 0 then only try a cache hit
	if ( wait_ms < 0 )
		return FAILED;

	queueRequest( request );

	if ( wait_ms == 0 )
		return QUEUED;

	// Wait up to the specified time for the response

	DWORD end_time = GetTickCount() + wait_ms;

	do {
		Sleep( 10 );

		CSingleLock lock( &m_track_cache_mutex, TRUE );

		AudioTrackInfoCache::iterator it = m_track_audio_info_cache.find( request.getKey() );

		if ( it != m_track_audio_info_cache.end() ) {
			AudioInfo& info = (*it).second;

			if ( !strcmp( UNAVAILABLE_ID, info.id ) )
				return NOT_AVAILABLE;

			*audio_info = info;

			return OK;
		}

		lock.Unlock();
	}
	while ( end_time > GetTickCount() );

	return FAILED;
}

// ----------------------------------------------------------------------------
//
void SpotifyWebEngine::queueRequest( InfoRequest& request )
{
	CSingleLock lock( &m_audio_info_mutex, TRUE ); 

	// If already in the list, don't re-queue it
	for ( InfoRequestList::iterator it=m_requests.begin(); it != m_requests.end(); it++ )
		if ( !strcmp( it->getSpotifyLink(), request.getSpotifyLink() ) )
			return;

	log_status( "Queue audio info request for %s", request.getSpotifyLink() );

	// Make sure the engine is started
	start();

	m_requests.push_back( request );

	// Wake up the request processor if sleeping
	m_wake.SetEvent();
}

// ----------------------------------------------------------------------------
//
bool findAndRemoveTrackLink( InfoRequestList& requests, CString& spotify_uri )
{
	for ( InfoRequestList::iterator it=requests.begin(); it != requests.end(); it++ ) {
		if ( !strcmp( it->getSpotifyLink(), spotify_uri ) ) {
			requests.erase( it );
			return true;
		}
	}

	return false;
}

// ----------------------------------------------------------------------------
//
bool SpotifyWebEngine::fetchSongData( InfoRequestList& requests, LPCSTR buffer, LPCSTR echonest_url )
{
	try {
		SimpleJsonParser parser;

		parser.parse( buffer );

		if ( !parser.has_key( "audio_features" ) ) {
			log( "Response missing 'audio_features' tag (%s)", echonest_url );
			return false;
		}

		JsonNodePtrArray songs = parser.getObjects( "audio_features" );

		log_status( "Captured audio information for %d of %d track(s)", songs.size(), requests.size() );

		for ( auto & song : songs ) {
			if ( song->is_null() )   // If song is not found the value will be null
				continue;

			try {
				CString spotify_uri = song->get<CString>( "uri" );

				if ( !findAndRemoveTrackLink( requests, spotify_uri ) )
					continue;

				AudioInfo audio_info;

				CString song_type = "";
				CString id = song->get<CString>( "id" );
				strncpy_s( audio_info.song_type, song_type, sizeof(audio_info.song_type) );
				strncpy_s( audio_info.id, id, sizeof(audio_info.id) );
				strncpy_s( audio_info.track_link, spotify_uri, sizeof(audio_info.track_link) );

				audio_info.key = song->get<int>( "key" );
				audio_info.energy = song->get<double>( "energy" );
				audio_info.liveness = song->get<double>( "liveness" );
				audio_info.tempo = song->get<double>( "tempo" );
				audio_info.speechiness = song->get<double>( "speechiness" );
				audio_info.acousticness = song->get<double>( "acousticness" );
				audio_info.instrumentalness = song->get<double>( "instrumentalness" );
				audio_info.duration = song->get<double>( "duration_ms" );
				audio_info.mode = song->get<int>( "mode" );
				audio_info.time_signature = song->get<int>( "time_signature" );
				audio_info.loudness = song->get<double>( "loudness" );
				audio_info.valence = song->get<double>( "valence" );
				audio_info.danceability = song->get<double>( "danceability" );

				log_status( "Captured audio information for '%s'", (LPCSTR)spotify_uri );

				saveAudioInfo( audio_info );
			}
			catch ( std::exception& ex ) {
				log( ex );
			}
		}
	}
	catch ( std::exception& ex ) {
		log( ex );
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------------
// TRACK AUDIO INFO PERSISTANCE METHODS
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
//
CString makeTrackInfoFileName( LPCSTR directory, LPCSTR spotify_link )
{
	CString safe_id( spotify_link );
	safe_id.Replace( ':', '_' );
	safe_id.Replace( '/', '_' );
	safe_id.Replace( '\\', '_' );

	CString filename;
	filename.Format( "%s\\%s.info", directory, (LPCSTR)safe_id );

	return filename;
}

// ----------------------------------------------------------------------------
//
bool SpotifyWebEngine::saveAudioInfo( AudioInfo& audio_info )
{
	CSingleLock cache_lock( &m_track_cache_mutex, TRUE );
	m_track_audio_info_cache[ audio_info.track_link ] = audio_info;
	cache_lock.Unlock();

	// Don't write out the entry if the track is unavailable - want to look for it in the future
	if ( !strcmp( UNAVAILABLE_ID, audio_info.id ) )
		return true;

	CString contents;

	JsonBuilder json( contents );

	json.startObject();
	json.add( "link", audio_info.track_link );
	json.add( "id", audio_info.id );
	json.add( "song_type", audio_info.song_type );

	json.startObject( "audio_summary" );
	json.add( "key", audio_info.key );
	json.add( "energy", audio_info.energy );
	json.add( "liveness", audio_info.liveness );

	json.add( "tempo", audio_info.tempo );
	json.add( "speechiness", audio_info.speechiness );
	json.add( "acousticness", audio_info.acousticness );
	json.add( "instrumentalness", audio_info.instrumentalness );
	json.add( "duration", audio_info.duration );
	json.add( "mode", audio_info.mode );
	json.add( "time_signature", audio_info.time_signature );
	json.add( "loudness", audio_info.loudness );
	json.add( "valence", audio_info.valence );
	json.add( "danceability", audio_info.danceability );
	json.endObject( "audio_summary" );

	json.endObject();

	// Write the file out
	CString filename = makeTrackInfoFileName( m_trackInfoContainer, audio_info.track_link );

	FILE* hFile = _fsopen( filename, "wt", _SH_DENYWR );
	size_t written = fwrite( (LPCSTR)contents, 1, contents.GetLength(), hFile );
	fclose( hFile );

	if ( written != contents.GetLength() ) {
		log( "Unable to write track info to %s", filename );
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------------
//
AudioInfo* SpotifyWebEngine::loadAudioInfo( LPCSTR spotify_link )
{
	CSingleLock lock( &m_track_cache_mutex, TRUE );

	AudioTrackInfoCache::iterator it = m_track_audio_info_cache.find( spotify_link );
	if ( it != m_track_audio_info_cache.end() )
		return &(*it).second;

	lock.Unlock();

	// See if it existing on disk - if available, load into the cache and return
	CString filename = makeTrackInfoFileName( m_trackInfoContainer, spotify_link );

	if ( GetFileAttributes( filename ) == INVALID_FILE_ATTRIBUTES )
		return NULL;

	FILE* hFile = _fsopen( filename, "rt", _SH_DENYWR );
	if ( hFile == NULL ) {
		log( "Unable to read track info from %s", filename );
		return NULL;
	}

	// Get file size
	fseek( hFile, 0L, SEEK_END );
	size_t size = ftell( hFile );
	rewind( hFile );

	CString data;
	fread( data.GetBufferSetLength(size), 1, size, hFile );
	fclose( hFile );

	SimpleJsonParser parser;

	try {
		parser.parse( data );

		AudioInfo audio_info;

		CString spotify_link = parser.get<CString>( "link" );
		CString id = parser.get<CString>( "id" );
		CString song_type = parser.get<CString>( "song_type" );

		strncpy_s( audio_info.song_type, song_type, sizeof(audio_info.song_type) );
		strncpy_s( audio_info.id, id, sizeof(audio_info.id) );

		JsonNode* audio_summary = parser.getObject( "audio_summary" );
		audio_info.key = audio_summary->get<int>( "key" );
		audio_info.energy = audio_summary->get<double>( "energy" );
		audio_info.liveness = audio_summary->get<double>( "liveness" );
		audio_info.tempo = audio_summary->get<double>( "tempo" );
		audio_info.speechiness = audio_summary->get<double>( "speechiness" );
		audio_info.acousticness = audio_summary->get<double>( "acousticness" );
		audio_info.instrumentalness = audio_summary->get<double>( "instrumentalness" );
		audio_info.duration = audio_summary->get<double>( "duration" );
		audio_info.mode = audio_summary->get<int>( "mode" );
		audio_info.time_signature = audio_summary->get<int>( "time_signature" );
		audio_info.loudness = audio_summary->get<double>( "loudness" );
		audio_info.valence = audio_summary->get<double>( "valence" );
		audio_info.danceability = audio_summary->get<double>( "danceability" );

		lock.Lock();

		m_track_audio_info_cache[audio_info.track_link] = audio_info;

		return &m_track_audio_info_cache[audio_info.track_link];
	}
	catch ( std::exception& e ) {
		log( StudioException( "JSON parser error (%s) data (%s)", e.what(), data ) );
		return NULL;
	}
}