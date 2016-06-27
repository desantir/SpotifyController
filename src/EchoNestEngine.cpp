/* 
Copyright (C) 2014-2016 Robert DeSantis
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
#include "EchoNestEngine.h"
#include "SimpleJsonParser.h"
#include "SimpleJsonBuilder.h"
#include <Winhttp.h>

static LPCWSTR accept_types[] = { L"*/*", NULL };
static LPCWSTR gAgentName = L"Spotify Audio Info";

BOOL readBuffer( HINTERNET hRequest, CString& data );

// ----------------------------------------------------------------------------
//
EchoNestEngine::EchoNestEngine() :
    Threadable( "EchoNestEngine" )
{
    m_trackInfoContainer.Format( "%s\\DMXStudio\\SpotifyTrackInfoCache", getUserDocumentDirectory() );
    CreateDirectory( m_trackInfoContainer, NULL );
}

// ----------------------------------------------------------------------------
//
EchoNestEngine::~EchoNestEngine()
{
    stop();
}

// ----------------------------------------------------------------------------
//
void EchoNestEngine::start()
{
    if ( !isRunning() )
        startThread();
}

// ----------------------------------------------------------------------------
//
void EchoNestEngine::stop()
{
    if ( isRunning() ) {
        stopThread();

        // Wake up the request processor if sleeping
        m_wake.SetEvent();
    }
}

// ----------------------------------------------------------------------------
//
UINT EchoNestEngine::run(void)
{
    log_status( "EchoNest engine started" );

    DWORD end_of_wait_period = 0L;                                  // Next period start tick count for throttling

    InfoRequestList work_queue;                                     // The list of tracks obtained in a single echjonest request

    AccessToken accessToken;                                        // Spotify web API access token

    while ( isRunning() ) {
        ::WaitForSingleObject(m_wake.m_hObject, 10 * 1000 );

        // Process pending info request with throttling
        while ( m_requests.size() > 0 ) {
            DWORD time = GetTickCount();

            if ( time < end_of_wait_period ) {
                log_status( "EchoNest throttled - waiting %ld seconds (%d pending)", (end_of_wait_period-time)/1000, m_requests.size() );
                break;
            }

            work_queue.clear();
            
            CSingleLock request_lock( &m_request_mutex, TRUE );

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
                    strncpy_s( audio_info.link, request.getKey(), sizeof(audio_info.link) );

                    saveAudioInfo( audio_info );
                    continue;
                }

                work_queue.push_back( request );
            }

            request_lock.Unlock();

            if ( work_queue.size() == 0 )       // All were duplicates or eliminated
                continue;

            if ( GetTickCount() > accessToken.expires ) {
                if ( !getCredentials( accessToken ) ) {
                    log_status( "Unable to obtain web API access token" );
                    return FALSE;
                }
            }

            // Make the echonest URL request
            CString echonest_url;

            echonest_url.Format( "/v1/audio-features/?ids=" );

            bool first = true;
            for ( auto const & request : work_queue ) {
                if ( !first )
                    echonest_url.AppendChar( ',' );

                LPCSTR link = request.getSpotifyLink();
                if ( strstr( link, SPOTIFY_URI_PREFIX ) == link )
                    link = &link[ strlen(SPOTIFY_URI_PREFIX ) ];

                echonest_url.Append( link );

                first = false;
            }

            try {
                CString buffer;
                UINT wait_seconds;

                if ( !httpGet( accessToken, echonest_url, buffer, wait_seconds ) ) { // Hit a limit - wait_seconds contains penatly
                    CSingleLock lock( &m_request_mutex, TRUE );
                    for ( auto const & request : work_queue )
                        m_requests.push_back( request );

                    end_of_wait_period =  GetTickCount() + (wait_seconds*1000); 
                    log_status( "Exceeded EchoNest request limit" );
                    break;
                }

                log_status( "Query %d track(s), %d track(s) in queue", work_queue.size(), m_requests.size() );

                fetchSongData( work_queue, buffer, echonest_url );

                // Mark any remaining tracks in work queue as unavailable (for this session only)
                for ( auto const & request : work_queue ) {
                    log_status( "Audio information for '%s' is unavailable", request.getKey() );

                    AudioInfo audio_info;
                    strcpy_s( audio_info.id, UNAVAILABLE_ID );
                    strncpy_s( audio_info.link, request.getKey(), sizeof(audio_info.link) );

                    saveAudioInfo( audio_info );
                }
            }
            catch ( std::exception& ex ) {
                log( ex );
            }
        }
    }

    log_status( "EchoNest engine stopped" );

    return 0;
}

// ----------------------------------------------------------------------------
//
AudioStatus EchoNestEngine::getTrackAudioInfo( LPCSTR spotify_track_link, AudioInfo* audio_info, DWORD wait_ms )
{
    return getAudioInfo( InfoRequest( spotify_track_link ), audio_info, wait_ms );
}

// ----------------------------------------------------------------------------
//
AudioStatus EchoNestEngine::lookupTrackAudioInfo( LPCSTR track_name, LPCSTR artist_name, AudioInfo* audio_info, DWORD wait_ms )
{
    return getAudioInfo( InfoRequest( artist_name, track_name ), audio_info, wait_ms );
}

// ----------------------------------------------------------------------------
//
AudioStatus EchoNestEngine::getAudioInfo( InfoRequest& request, AudioInfo* audio_info, DWORD wait_ms )
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
void EchoNestEngine::queueRequest( InfoRequest& request )
{
    CSingleLock lock( &m_request_mutex, TRUE ); 

    // If already in the list, don't re-queue it
    for ( InfoRequestList::iterator it=m_requests.begin(); it != m_requests.end(); it++ )
        if ( !strcmp( it->getSpotifyLink(), request.getSpotifyLink() ) )
            return;

    log_status( "Queue EchoNest request for %s", request.getSpotifyLink() );

    // Make sure the engine is started
    theApp.m_echonest.start();

    m_requests.push_back( request );

    // Wake up the request processor if sleeping
    m_wake.SetEvent();
}

// ----------------------------------------------------------------------------
//
bool EchoNestEngine::getCredentials( AccessToken& accessToken )
{
    bool bResults = false;

    HINTERNET  hSession = NULL, 
        hConnect = NULL,
        hRequest = NULL;

    CStringW wide_headers( "Content-Type: application/x-www-form-urlencoded\r\n" );
    CString body = "grant_type=client_credentials";

    try {
        hSession = WinHttpOpen( gAgentName, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
        if ( !hSession )
            throw std::exception( "Unable to open internet session" );

        hConnect = WinHttpConnect( hSession, L"accounts.spotify.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if ( !hConnect )
            throw std::exception( "Unable to connect session" );

        hRequest = WinHttpOpenRequest( hConnect, L"POST", L"/api/token", NULL, WINHTTP_NO_REFERER, accept_types, WINHTTP_FLAG_SECURE );
        if ( !hRequest )
            throw std::exception( "Unable to open request" );

        CStringW username( g_webapi_client_id );
        CStringW password( g_webapi_client_secret );

        if ( !WinHttpSetCredentials( hRequest, WINHTTP_AUTH_TARGET_SERVER, WINHTTP_AUTH_SCHEME_BASIC, (LPCWSTR)username, (LPCWSTR)password, NULL ) )
            throw std::exception( "Unable to set HTTP credentials" );

        if ( !WinHttpSendRequest( hRequest, (LPCWSTR)wide_headers, -1L, (LPVOID)(LPCSTR)body, body.GetLength(), body.GetLength(), 0 ) )
            throw std::exception( "Unable to send HTTP request" );
    
        if ( !WinHttpReceiveResponse( hRequest, NULL ) )
            throw std::exception( "Error waiting for HTTP response" );

        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);

        WinHttpQueryHeaders( hRequest, 
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
            WINHTTP_HEADER_NAME_BY_INDEX, 
            &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX );

        if ( dwStatusCode != HTTP_STATUS_OK ) {
            WinHttpCloseHandle(hSession);
            return FALSE;
        }

        CString json_data;

        if ( !readBuffer( hRequest, json_data ) )
            throw std::exception( "Unable to read JSON data" );

        SimpleJsonParser parser;

        parser.parse( json_data );

        accessToken.token = parser.get<CString>( "access_token" );
        accessToken.expires = GetTickCount() + (parser.get<DWORD>( "expires_in" ) * 1000);
        accessToken.token_type = parser.get<CString>( "token_type" );

        bResults = true;
    }
    catch ( std::exception& ex ) {
        log( "Error collecting Spotify access token: %s", ex.what() );
    }
    catch ( ... ) {
        log( "Unknown collecting Spotify access token" );
    }

    if ( hSession )
        WinHttpCloseHandle( hSession );

    return bResults;
}

// ----------------------------------------------------------------------------
//
bool EchoNestEngine::httpGet( AccessToken& accessToken, LPCTSTR echonest_url, CString& json_data, UINT& wait_seconds )
{
    bool  bResults = false;

    HINTERNET  hSession = NULL, 
        hConnect = NULL,
        hRequest = NULL;

    wait_seconds = 0L;
     
    CString headers;
    headers.Format( "Authorization: %s %s\r\n", accessToken.token_type, accessToken.token );

    CStringW urlW( echonest_url);
    CStringW wide_headers( headers );

    try {
        hSession = WinHttpOpen( gAgentName, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
        if ( !hSession )
            throw std::exception( "Unable to open internet session" );

        hConnect = WinHttpConnect( hSession, L"api.spotify.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if ( !hConnect )
            throw std::exception( "Unable to connect session" );

        hRequest = WinHttpOpenRequest( hConnect, L"GET", urlW, NULL, WINHTTP_NO_REFERER, accept_types, WINHTTP_FLAG_SECURE );
        if ( !hRequest )
            throw std::exception( "Unable to open request" );

        DWORD dwOption = WINHTTP_DISABLE_AUTHENTICATION;

        if ( !WinHttpSetOption( hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &dwOption, sizeof(dwOption) ) )
            throw std::exception( "Error disabling automatic asutghentication" );

        if ( !WinHttpSendRequest( hRequest, (LPCWSTR)wide_headers, -1L, NULL, 0, 0, 0 ) ) 
            throw std::exception( "Error sending HTTP request" );

        if ( !WinHttpReceiveResponse( hRequest, NULL ) )
            throw std::exception( "Error waiting for HTTP response" );

        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);

        WinHttpQueryHeaders( hRequest, 
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
            WINHTTP_HEADER_NAME_BY_INDEX, 
            &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX );

        if ( dwStatusCode != HTTP_STATUS_OK ) {
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
               
            CString error;
            error.Format( "Received HTTP status code %lu", dwStatusCode );
            throw std::exception( (LPCSTR)error );
        }

        if ( !readBuffer( hRequest, json_data ) )
            throw std::exception( "Unable to read JSON data" );

        bResults = true;
    }
    catch ( std::exception& ex ) {
        log( "Error connecting to %s with URL %s: %s", "api.spotify.com", echonest_url, ex.what() );
    }
    catch ( ... ) {
        log( "Unknown error connecting to %s with URL %s", "api.spotify.com", echonest_url );
    }

    if ( hSession )
        WinHttpCloseHandle( hSession );

    return bResults;
}

// ----------------------------------------------------------------------------
//
BOOL readBuffer( HINTERNET hRequest, CString& data ) {
    while ( true ) {
        char buffer[1000];
        DWORD read;

        BOOL bResults = WinHttpReadData( hRequest, buffer, sizeof(buffer)-1, &read );
        if ( !bResults ) {
            return FALSE;
        }

        if ( read == 0 )
            break;

        buffer[ read ] = '\0';
        data.Append( buffer );
    }

    return TRUE;    
}

// ----------------------------------------------------------------------------
//
CString EchoNestEngine::encodeString( LPCSTR source )
{
    CString results;
    char c;

    while ( c = *source++ )
        if ( c > ' ' )
            results += c;
        else
            results.AppendFormat( "%%%02x", (int)c );

    return results;
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
bool EchoNestEngine::fetchSongData( InfoRequestList& requests, LPCSTR buffer, LPCSTR echonest_url )
{
    try {
        SimpleJsonParser parser;

        parser.parse( buffer );

        if ( !parser.has_key( "audio_features" ) ) {
            log( "EchoNest response missing 'audio_features' tag (%s)", echonest_url );
            return false;
        }

        PARSER_LIST songs = parser.get<PARSER_LIST>( "audio_features" );

        log_status( "Captured audio information for %d of %d track(s)", songs.size(), requests.size() );

        for ( auto & song : songs ) {
            if ( song.is_null() )   // If song is not found the value will be null
                continue;
            
            try {
                CString spotify_uri = song.get<CString>( "uri" );

                if ( !findAndRemoveTrackLink( requests, spotify_uri ) )
                    continue;

                AudioInfo audio_info;

                CString song_type = "";
                CString id = song.get<CString>( "id" );
                strncpy_s( audio_info.song_type, song_type, sizeof(audio_info.song_type) );
                strncpy_s( audio_info.id, id, sizeof(audio_info.id) );
                strncpy_s( audio_info.link, spotify_uri, sizeof(audio_info.link) );

                audio_info.key = song.get<int>( "key" );
                audio_info.energy = song.get<double>( "energy" );
                audio_info.liveness = song.get<double>( "liveness" );
                audio_info.tempo = song.get<double>( "tempo" );
                audio_info.speechiness = song.get<double>( "speechiness" );
                audio_info.acousticness = song.get<double>( "acousticness" );
                audio_info.instrumentalness = song.get<double>( "instrumentalness" );
                audio_info.duration = song.get<double>( "duration_ms" );
                audio_info.mode = song.get<int>( "mode" );
                audio_info.time_signature = song.get<int>( "time_signature" );
                audio_info.loudness = song.get<double>( "loudness" );
                audio_info.valence = song.get<double>( "valence" );
                audio_info.danceability = song.get<double>( "danceability" );

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
bool EchoNestEngine::saveAudioInfo( AudioInfo& audio_info )
{
    CSingleLock cache_lock( &m_track_cache_mutex, TRUE );
    m_track_audio_info_cache[ audio_info.link ] = audio_info;
    cache_lock.Unlock();

    // Don't write out the entry if the track is unavailable - want to look for it in the future
    if ( !strcmp( UNAVAILABLE_ID, audio_info.id ) )
        return true;

    CString contents;

    JsonBuilder json( contents );

    json.startObject();
    json.add( "link", audio_info.link );
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
    CString filename = makeTrackInfoFileName( m_trackInfoContainer, audio_info.link );

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
AudioInfo* EchoNestEngine::loadAudioInfo( LPCSTR spotify_link )
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

        SimpleJsonParser audio_summary = parser.get<SimpleJsonParser>( "audio_summary" );
        audio_info.key = audio_summary.get<int>( "key" );
        audio_info.energy = audio_summary.get<double>( "energy" );
        audio_info.liveness = audio_summary.get<double>( "liveness" );
        audio_info.tempo = audio_summary.get<double>( "tempo" );
        audio_info.speechiness = audio_summary.get<double>( "speechiness" );
        audio_info.acousticness = audio_summary.get<double>( "acousticness" );
        audio_info.instrumentalness = audio_summary.get<double>( "instrumentalness" );
        audio_info.duration = audio_summary.get<double>( "duration" );
        audio_info.mode = audio_summary.get<int>( "mode" );
        audio_info.time_signature = audio_summary.get<int>( "time_signature" );
        audio_info.loudness = audio_summary.get<double>( "loudness" );
        audio_info.valence = audio_summary.get<double>( "valence" );
        audio_info.danceability = audio_summary.get<double>( "danceability" );

        lock.Lock();

        m_track_audio_info_cache[audio_info.link] = audio_info;

        return &m_track_audio_info_cache[audio_info.link];
    }
    catch ( std::exception& e ) {
        log( StudioException( "JSON parser error (%s) data (%s)", e.what(), data ) );
        return NULL;
    }
}