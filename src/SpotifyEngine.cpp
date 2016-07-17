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

#include "stdafx.h"
#include "SpotifyEngine.h"
#include "SimpleJsonBuilder.h"
#include "SimpleJsonParser.h"

#define DEBUG_SPOTIFY   false

#define SPOTIFY_API_CALLED( fmt, ... )		\
    if ( DEBUG_SPOTIFY ) { printf( fmt, __VA_ARGS__ ); printf( "\n" ); }

// ----------------------------------------------------------------------------
//
SpotifyEngine::SpotifyEngine(void) :
    m_current_track(NULL),
    m_track_state( TRACK_ENDED ),
    m_spotify_session(NULL),
    m_spotify_command( CMD_NONE ),
    m_login_state( NOT_LOGGED_IN ),
    m_audio_out( NULL ),
    m_track_length_ms( 0 ),
    m_track_seek_ms( 0 ),
    m_track_timer( this ),
    Threadable( "Engine" )
{
    memset( &spconfig, 0, sizeof(sp_session_config) );

    inititializeSpotifyCallbacks();

    spconfig.api_version = SPOTIFY_API_VERSION;
    spconfig.cache_location = "/tmp_dmxstudio_spotify";
    spconfig.settings_location = spconfig.cache_location;
    spconfig.application_key = g_appkey;
    spconfig.application_key_size = g_appkey_size;
    spconfig.user_agent = "DMXStudio-spotify-controller";

    spconfig.callbacks = &session_callbacks;
    spconfig.userdata = this;
    spconfig.compress_playlists = false;
    spconfig.dont_save_metadata_for_playlists = false;
    spconfig.initially_unload_playlists = false;

    spconfig.proxy = NULL;
    spconfig.proxy_username = NULL;
    spconfig.proxy_password = NULL;
    spconfig.tracefile = NULL;
    spconfig.device_id = NULL;

    m_trackAnalysisContainer.Format( "%s\\DMXStudio\\SpotifyTrackAnalyzeCache", getUserDocumentDirectory() );
    CreateDirectory( m_trackAnalysisContainer, NULL );
}

// ----------------------------------------------------------------------------
//
SpotifyEngine::~SpotifyEngine(void)
{
    disconnect();
}

// ----------------------------------------------------------------------------
// UI API METHODS
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
//
bool SpotifyEngine::connect()
{
    CString username;
    CString blob;

    if ( !_readCredentials( username, blob ) )
        return false;

    return connect( username, NULL, blob );
}

// ----------------------------------------------------------------------------
//
bool SpotifyEngine::connect( LPCSTR username, LPCSTR password, LPCSTR blob ) 
{
    sp_error err;

    if ( !m_spotify_session ) {                // Create a new Spoty session
        err = sp_session_create( &spconfig, &m_spotify_session);
        if ( SP_ERROR_OK != err ) {
            m_spotify_error.Format( "Unable to create session: %s", sp_error_message(err) );
            return false;
        }
    }

    m_login_state = LOGIN_WAIT;

    err = sp_session_login( m_spotify_session, username, password, 1, blob );
    if (SP_ERROR_OK != err) {
        m_spotify_error.Format( "Unable to log in: %s", sp_error_message(err) );
        return false;
    }

    // We appear to get a "successful" login callback immediately after the sp_session_login(), 
    // then, eventually, a bad login. This wait is to help combat that odd behavior by sucking up
    // both messages.  It is, obviously, highly timing dependant.

    ULONG future = GetCurrentTime() + (2 * 1000);
    while ( GetCurrentTime() < future && getLoginState() != LOGIN_FAILED ) {
        int next_timeout = 0;
        do {
            sp_session_process_events( m_spotify_session, &next_timeout );
        } while (next_timeout == 0);

        Sleep( 100 );
    }

    future = GetCurrentTime() + (60 * 1000);

    while ( getLoginState() == LOGIN_WAIT ) {
        int next_timeout = 0;
        do {
            sp_session_process_events( m_spotify_session, &next_timeout );
        } while (next_timeout == 0);

        if ( GetCurrentTime() > future ) {
            m_spotify_error = "Login timed-out";
            break;
        }

        Sleep( 100 );
    }

    if ( m_login_state != LOGIN_SUCCESS )
        return false;

    m_waveFormat.wFormatTag  = WAVE_FORMAT_PCM;
    m_waveFormat.nChannels = 2;
    m_waveFormat.nSamplesPerSec = 44100;
    m_waveFormat.wBitsPerSample = 16;
    m_waveFormat.cbSize = 0;
    m_waveFormat.nBlockAlign = (m_waveFormat.wBitsPerSample * m_waveFormat.nChannels) / 8;
    m_waveFormat.nAvgBytesPerSec = m_waveFormat.nSamplesPerSec * m_waveFormat.nBlockAlign;
    
    try {
        m_audio_out = AudioOutputStream::createAudioStream();
        m_audio_out->openAudioStream( &m_waveFormat );
    }
    catch ( std::exception& ex ) {
        log( ex );
        m_spotify_error = ex.what();
        disconnect();
        return false;
    }

    m_track_timer.startThread();

    return startThread();
}

// ----------------------------------------------------------------------------
//
bool SpotifyEngine::disconnect( void )
{
    m_track_timer.stopThread();

    if ( m_spotify_session ) {
        // Seems to be very important to stop all active tracks before killing Spotify

        clearTrackQueue( );         // Clear any pending tracks
        stopTrack();                // Stop any playing track(s)
        stopThread();               // Stop the spotify dispatcher

        ULONG future = GetCurrentTime() + (2 * 60 * 1000);

        // Logout the user        
        sp_session_logout( m_spotify_session );
        while ( getLoginState() != NOT_LOGGED_IN ) {
            int next_timeout = 0;
            do {
                sp_session_process_events( m_spotify_session, &next_timeout );
            } while (next_timeout == 0);

            if ( GetCurrentTime() > future )
                break;

            Sleep( 100 );
        }
    }

    if ( m_audio_out ) {
        AudioOutputStream::releaseAudioStream ( m_audio_out );
        m_audio_out = NULL;
    }

    removeTrackAnalyzer();

    freeTrackAnalysisCache();

    return true;
}

// ----------------------------------------------------------------------------
//
bool SpotifyEngine::registerEventListener( IPlayerEventCallback* listener )
{
    CSingleLock lock( &m_event_lock, TRUE );

    if ( findListener( listener ) != m_event_listeners.end() )
        return false;

    m_event_listeners.push_back( listener );

    return true;
}

// ----------------------------------------------------------------------------
//
bool SpotifyEngine::unregisterEventListener( IPlayerEventCallback* listener )
{
    CSingleLock lock( &m_event_lock, TRUE );

    auto it = findListener( listener );
    if ( it == m_event_listeners.end() )
        return false;

    m_event_listeners.erase( it );
    return true;
}

// ----------------------------------------------------------------------------
//
EventListeners::iterator SpotifyEngine::findListener( IPlayerEventCallback* listener ) {
    auto it=m_event_listeners.begin();

    while ( it != m_event_listeners.end() ) {
        if ( (*it) == listener )
            break;
        it++;
    }

    return it;
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::sendEvent( PlayerEvent event, ULONG event_ms, LPCSTR track_link ) 
{
    CSingleLock lock( &m_event_lock, TRUE );

    PlayerEventData trackEvent( event, event_ms, track_link );

    for ( IPlayerEventCallback* callback : m_event_listeners )
        callback->notify( &trackEvent );

    m_track_event.SetEvent();
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::sendTrackQueueEvent() 
{
    CSingleLock lock( &m_event_lock, TRUE );

    PlayerEventData trackEvent( m_track_played_queue.size(), m_track_queue.size() );

    for ( IPlayerEventCallback* callback : m_event_listeners )
        callback->notify( &trackEvent );

    m_track_event.SetEvent();
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::sendPlaylistEvent( PlayerEvent event, sp_playlist *playlist ) {
/* Playlist notification appear to be really broken - just ignore these for now
    sp_link* link = sp_link_create_from_playlist( playlist );

    if ( link != NULL ) {
        CString spotify_link;
        LPSTR spotify_link_ptr = spotify_link.GetBufferSetLength( 512 );
        sp_link_as_string ( link, spotify_link_ptr, 512 );
        sp_link_release( link );

        sendEvent( event, 0L, (LPCSTR)spotify_link );
    }
*/
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::stopTrack()
{
    sendCommand( CMD_STOP_TRACK );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::clearTrackQueue( )
{
    CSingleLock lock( &m_mutex, TRUE );

    m_track_queue.clear();

    sendTrackQueueEvent();
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::previousTrack()
{
    CSingleLock lock( &m_mutex, TRUE );

    if ( m_current_track && getTrackPlayTime() > 10*1000 ) {    // If current track > 10 seconds in, return to start
        if ( m_track_played_queue.size() )
            m_track_played_queue.pop_back();

        playTrack( m_current_track, 0L ); 
    }
    else if ( m_track_played_queue.size() ) {
        if ( m_current_track ) {                                // Skip over currently playing track (re-queue it)
            TrackQueueEntry track = m_track_played_queue.back();
            m_track_played_queue.pop_back();
            m_track_queue.push_front( track );
        }

        if ( m_track_played_queue.size() ) {
            TrackQueueEntry track = m_track_played_queue.back();
            m_track_played_queue.pop_back();
            playTrack( track.m_track, 0L ); 
        }
        else {                                                  // Current track was the only track in the played queue
            stopTrack();

            sendTrackQueueEvent();
        }
    }
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::playTrack( sp_track* track, DWORD seek_ms )
{
    CSingleLock lock( &m_mutex, TRUE );

    // See if this track is already in the played list - and move to that position
    TrackQueue::iterator it_position;
    for ( it_position=m_track_played_queue.begin(); it_position != m_track_played_queue.end() && (*it_position).m_track != track; )
        it_position++;

    if ( it_position != m_track_played_queue.end() ) {
        (*it_position).m_seek_ms = seek_ms;

        size_t count = std::distance( it_position, m_track_played_queue.end() );
        while ( count-- > 0 ) {
            m_track_queue.push_front( m_track_played_queue.back() );
            m_track_played_queue.pop_back();
        }
    }
    else {
        // See if this track is already in the queue list - and move to that position
        TrackQueue::iterator it_position;
        for ( it_position=m_track_queue.begin(); it_position != m_track_queue.end() && (*it_position).m_track != track; )
            it_position++;
    
        if ( it_position != m_track_queue.end() ) {
            (*it_position).m_seek_ms = seek_ms;
            std::copy( m_track_queue.begin(), it_position, back_inserter(m_track_played_queue) );
            m_track_queue.erase( m_track_queue.begin(), it_position );
        }
        else 
            m_track_queue.push_front( TrackQueueEntry( track, seek_ms ) );
    
    }

    sendCommand( CMD_NEXT_TRACK );

    sendTrackQueueEvent();
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::playTracks( sp_playlist* pl ) 
{
    CSingleLock lock( &m_mutex, TRUE );
    m_track_queue.clear();

    for ( auto const& track : getTracks(pl) )
        m_track_queue.push_back( TrackQueueEntry( track, 0L ) );

    sendCommand( CMD_NEXT_TRACK );

    sendTrackQueueEvent();
}

// ----------------------------------------------------------------------------
//
sp_linktype SpotifyEngine::getTrackLink( sp_track* track, CString& spotify_link ) 
{
    spotify_link.Empty();

    sp_link* link = sp_link_create_from_track( track, 0 );
    if ( link == NULL )
        return SP_LINKTYPE_INVALID;

    LPSTR spotify_link_ptr = spotify_link.GetBufferSetLength( 512 );
    sp_link_as_string ( link, spotify_link_ptr, 512 );

    sp_linktype link_type = sp_link_type( link );
    sp_link_release( link );

    return link_type;
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::queueTracks( sp_playlist* pl ) 
{
    CSingleLock lock( &m_mutex, TRUE );

    for ( auto const& track : getTracks(pl) )
        m_track_queue.push_back( TrackQueueEntry( track, 0L ) );

    sendCommand( CMD_CHECK_PLAYING );

    sendTrackQueueEvent();
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::nextTrack()
{
    sendCommand( CMD_NEXT_TRACK );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::queueTrack( sp_track* track )
{
    CSingleLock lock( &m_mutex, TRUE );
    m_track_queue.push_back( TrackQueueEntry( track, 0L ) );

    sendCommand( CMD_CHECK_PLAYING );

    sendTrackQueueEvent();
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::pauseTrack( bool pause )
{
    sendCommand( ( pause ) ? CMD_PAUSE_TRACK : CMD_RESUME_TRACK );
}

// ----------------------------------------------------------------------------
//
PlaylistArray SpotifyEngine::getPlaylists( void ) {
    PlaylistArray playlists;

    sp_playlistcontainer *pc = sp_session_playlistcontainer( m_spotify_session );

    for ( int i=0; i < sp_playlistcontainer_num_playlists(pc); i++ ) {
        sp_playlist *pl = sp_playlistcontainer_playlist(pc, i);
        if ( sp_playlist_num_tracks( pl ) > 0 )
            playlists.push_back( pl );
    }

    return playlists;
}

// ----------------------------------------------------------------------------
//
TrackArray SpotifyEngine::getTracks( sp_playlist* pl ) {
    TrackArray tracks;

    int num_tracks = sp_playlist_num_tracks( pl );

    for (int i=0; i < num_tracks; i++ ) {
        sp_track* track = sp_playlist_track( pl, i );

        sp_track_availability availability = sp_track_get_availability( m_spotify_session, track );

        if ( availability == SP_TRACK_AVAILABILITY_AVAILABLE )
            tracks.push_back( track );
    }

    return tracks;
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::removeTrackAnalyzer() {
    if ( isAnalyzing() ) {
        delete m_analyzer;
        m_analyzer = NULL;
    }
}

// ----------------------------------------------------------------------------
// ENGINE DISPATCHER AND HELPER METHODS
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
//
UINT SpotifyEngine::run()
{
    log_status( "Spotify engine started" );
    
    int next_timeout = 0;
    ULONG wait_time = 0L;

    while ( isRunning() ) {
        try {
            while ( ::WaitForSingleObject( m_spotify_notify, 100 ) != WAIT_OBJECT_0 ) {
                if ( m_current_track != NULL ) {
                    switch ( m_track_state ) {
                        case TRACK_STREAM_COMPLETE:
                            sp_session_player_unload( m_spotify_session );
                            m_track_state = TRACK_PLAYING;
                            break;

                        case TRACK_PLAYING:
                            if ( m_audio_out->getCachedSamples() == 0 ) {
                                _processTrackAnalysis();

                                m_track_state = TRACK_ENDED;
                                m_current_track = NULL;
                                wait_time = 0;

                                m_track_timer.stop();

                                _startTrack();
                            }
                            break;
                    }
                }

                if ( GetCurrentTime() > wait_time )
                    break;
            }

            // Handle any pending user command
            switch ( m_spotify_command ) {
                case CMD_STOP_TRACK:
                    _stopTrack();                
                    break;

                case CMD_NEXT_TRACK:
                    _stopTrack();
                    _startTrack();
                    break;

                case CMD_PAUSE_TRACK:
                    _pause();
                    break;

                case CMD_RESUME_TRACK:
                    _resume();
                    break;

                case CMD_CHECK_PLAYING:
                    if ( m_current_track == NULL )
                        _startTrack();
                    break;
            }

            m_spotify_command = CMD_NONE;

            // Dispatch Spotify messages
            do {
                sp_session_process_events( m_spotify_session, &next_timeout );
            } while (next_timeout == 0);

            if ( next_timeout != 0 )
                wait_time = next_timeout + GetCurrentTime();
            else
                wait_time = 0;
        }
        catch ( std::exception& ex ) {
            log( ex );
            m_spotify_error = ex.what();
        }
    }

    log_status( "Spotify engine stopped" );

    return 0;
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::_stopTrack( )
{
    if ( m_current_track != NULL ) {
        sp_session_player_unload( m_spotify_session );
        m_audio_out->cancel();
        m_current_track = NULL;
        m_current_track_link.Empty();
        m_track_state = TRACK_ENDED;
        m_track_length_ms = m_track_seek_ms = 0L;

        m_track_timer.stop( );

        removeTrackAnalyzer();
    }
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::_startTrack( )
{
    if ( m_track_queue.size() ) {
        CSingleLock lock( &m_mutex, TRUE );
        TrackQueueEntry entry = m_track_queue.front();
        m_track_queue.pop_front();

        m_current_track = entry.m_track;
        m_track_played_queue.push_back( entry );

        sp_error result = sp_session_player_load( m_spotify_session, m_current_track );
        if ( result != SP_ERROR_OK ) {
            log( "Error %d from sp_session_player_load", (INT)result );
        }

        m_track_state = TRACK_STREAM_PENDING;
        m_track_length_ms = sp_track_duration( m_current_track );
        m_track_seek_ms = entry.m_seek_ms > m_track_length_ms ? 0 : entry.m_seek_ms;

        getTrackLink( m_current_track, m_current_track_link );

        if ( m_track_seek_ms != 0L )
            sp_session_player_seek( m_spotify_session, m_track_seek_ms );
        else if ( !haveTrackAnalysis(m_current_track_link) ) {
            m_analyzer = new TrackAnalyzer( &m_waveFormat, m_track_length_ms, m_current_track_link );
        }

        if ( !isTrackPaused() ) {
            result = sp_session_player_play( m_spotify_session, true );
            if ( result != SP_ERROR_OK ) {
                log( "Error %d from sp_session_player_play", (INT)result );
            }
        }

        sendTrackQueueEvent();
    }
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::_resume( )
{
    if ( isTrackPaused() ) {
        sp_session_player_play( m_spotify_session, true );

        if ( m_audio_out->isPaused() )
            m_audio_out->setPaused( false );

        m_track_timer.resume();
    }
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::_pause( )
{
    if ( !isTrackPaused() ) {
        m_audio_out->setPaused( true );
        sp_session_player_play( m_spotify_session, false );

        m_track_timer.pause();
    }
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::_processTrackAnalysis() 
{
    if ( isAnalyzing() ) {
        saveTrackAnalysis( m_analyzer->captureAnalyzerData() );
        removeTrackAnalyzer();
    }
}

// ----------------------------------------------------------------------------
// CREDENTIALS PERSISTANCE METHODS
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
//
bool SpotifyEngine::_readCredentials( CString& username, CString& credentials )
{
    CString filename;
    filename.Format( "%s\\DMXStudio\\SpotifyCredentials", getUserDocumentDirectory() );

    username.Empty();
    credentials.Empty();

    if ( GetFileAttributes( filename ) == INVALID_FILE_ATTRIBUTES )
        return false;

    FILE* hFile = _fsopen( filename, "rt", _SH_DENYWR );
    if ( hFile == NULL )
        return false;

    LPSTR lpUsername = username.GetBufferSetLength( 256 );
    LPSTR lpCredentials = credentials.GetBufferSetLength( 2048 );

    size_t read = fscanf_s( hFile, "%s %s", lpUsername, 256, lpCredentials, 256 );

    username.ReleaseBuffer();
    credentials.ReleaseBuffer();
    fclose( hFile );

    return read == 2;
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::_writeCredentials( LPCSTR username, LPCSTR credentials )
{
    CString filename;
    filename.Format( "%s\\DMXStudio\\SpotifyCredentials", getUserDocumentDirectory() );

    FILE* hFile = _fsopen( filename, "wt", _SH_DENYWR );
    fprintf( hFile, "%s %s", username, credentials );
    fclose( hFile );
}

// ----------------------------------------------------------------------------
// TRACK ANALYSIS PERSISTANCE METHODS
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
//
AnalyzeInfo* SpotifyEngine::getTrackAnalysis( LPCSTR track_link )
{
    return loadTrackAnalysis( track_link );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::freeTrackAnalysisCache( )
{
    for ( auto const& it : m_track_analysis_cache )
        free( it.second );

    m_track_analysis_cache.clear();
}

// ----------------------------------------------------------------------------
//
CString makeTrackAnalysisFileName( LPCSTR directory, LPCSTR spotify_id )
{
    CString safe_id( spotify_id );
    safe_id.Replace( ':', '_' );
    safe_id.Replace( '/', '_' );
    safe_id.Replace( '\\', '_' );

    CString filename;
    filename.Format( "%s\\%s.analyze", directory, (LPCSTR)safe_id );

    return filename;
}

// ----------------------------------------------------------------------------
//
bool SpotifyEngine::haveTrackAnalysis( LPCSTR spotify_link ) {
    if ( m_track_analysis_cache.find( spotify_link ) != m_track_analysis_cache.end() )
        return true;

    // See if it exists but is not loaded
    CString filename = makeTrackAnalysisFileName( m_trackAnalysisContainer, spotify_link );

    DWORD dwAttrib = GetFileAttributes( filename );

    return ( dwAttrib != INVALID_FILE_ATTRIBUTES &&  !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY) );
}

// ----------------------------------------------------------------------------
//
bool SpotifyEngine::saveTrackAnalysis( AnalyzeInfo* info )
{
    CString contents;

    JsonBuilder json( contents );

    json.startObject();
    json.add( "link", info->link );

    std::vector<uint16_t> data( std::begin( info->data), &info->data[info->data_count] );

    json.startObject( "amplitude" );
    json.add( "duration_ms", info->duration_ms );
    json.add( "data_count", info->data_count );
    json.addArray< std::vector<uint16_t>>( "data", data );
    json.endObject( "amplitude" );

    json.endObject();

    // Write the file out
    CString filename = makeTrackAnalysisFileName( m_trackAnalysisContainer, info->link );

    FILE* hFile = _fsopen( filename, "wt", _SH_DENYWR );
    size_t written = fwrite( (LPCSTR)contents, 1, contents.GetLength(), hFile );
    fclose( hFile );

    if ( written != contents.GetLength() ) {
        log( "Unable to write track analysis to %s", filename );
        return false;
    }

    // Add it to the cache
    TrackAnalysisCache::iterator it = m_track_analysis_cache.find( info->link );
    if ( it != m_track_analysis_cache.end() ) {
        free( (it)->second );
        m_track_analysis_cache.erase( it );
    }

    m_track_analysis_cache[info->link] = info;

    return true;
}

// ----------------------------------------------------------------------------
//
AnalyzeInfo* SpotifyEngine::loadTrackAnalysis( LPCSTR spotify_id )
{
    TrackAnalysisCache::iterator it = m_track_analysis_cache.find( spotify_id );
    if ( it != m_track_analysis_cache.end() )
        return (it)->second;

    // See if it existing on disk - if available, load into the cache and return
    CString filename = makeTrackAnalysisFileName( m_trackAnalysisContainer, spotify_id );

    if ( GetFileAttributes( filename ) == INVALID_FILE_ATTRIBUTES )
        return NULL;

    FILE* hFile = _fsopen( filename, "rt", _SH_DENYWR );
    if ( hFile == NULL ) {
        log( "Unable to read track analysis from %s", filename );
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

        CString spotify_id = parser.get<CString>( "link" );

        SimpleJsonParser amplitute_parser = parser.get<SimpleJsonParser>( "amplitude" );

        size_t data_count = amplitute_parser.get<size_t>( "data_count" );
        UINT duration_ms = amplitute_parser.get<size_t>( "duration_ms" );
        std::vector<uint16_t> amplitude_data = amplitute_parser.getArray<uint16_t>( "data" );

        AnalyzeInfo* info = (AnalyzeInfo*)calloc( sizeof(AnalyzeInfo) + (sizeof(uint16_t) * data_count), 1 );
        for ( size_t i=0; i < data_count; i++ )
            info->data[i] = amplitude_data[i];

        strncpy_s( info->link, spotify_id, sizeof(info->link) );
        info->data_count = data_count;
        info->duration_ms = duration_ms;
        m_track_analysis_cache[info->link] = info;

        return info;
    }
    catch ( std::exception& e ) {
        log( StudioException( "JSON parser error (%s) data (%s)", e.what(), data ) );
        return NULL;
    }
}

