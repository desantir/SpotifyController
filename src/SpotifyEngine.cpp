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
#include "SpotifyEngine.h"

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
    m_paused( false ),
    m_audio_out( NULL ),
    m_track_length_ms( 0 ),
    m_track_start_time( 0 )
    // m_track_event( 0, 0, ENGINE_TRACK_EVENT_NAME, NULL )
{
    memset( &spconfig, 0, sizeof(sp_session_config) );

    inititializeSpotifyCallbacks();

    spconfig.api_version = SPOTIFY_API_VERSION;
    spconfig.cache_location = "/tmp";
    spconfig.settings_location = "/tmp";
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

    ULONG future = GetCurrentTime() + (60 * 1000);

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

    WAVEFORMATEX waveFormat;
    waveFormat.wFormatTag  = WAVE_FORMAT_PCM;
    waveFormat.nChannels = 2;
    waveFormat.nSamplesPerSec = 44100;
    waveFormat.wBitsPerSample = 16;
    waveFormat.cbSize = 0;
    waveFormat.nBlockAlign = (waveFormat.wBitsPerSample * waveFormat.nChannels) / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
    
    try {
        m_audio_out = AudioOutputStream::createAudioStream();
        m_audio_out->openAudioStream( &waveFormat );
    }
    catch ( std::exception& ex ) {
        log( ex );
        m_spotify_error = ex.what();
        disconnect();
        return false;
    }

    return startThread();
}

// ----------------------------------------------------------------------------
//
bool SpotifyEngine::disconnect( void )
{
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

    return true;
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
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::previousTrack()
{
    CSingleLock lock( &m_mutex, TRUE );

    if ( m_track_played_queue.size() ) {
        if ( m_current_track ) {                    // Skip over currently playing track (re-queue it)
            sp_track* track = m_track_played_queue.back();
            m_track_played_queue.pop_back();
            m_track_queue.push_front( track );
        }

        if ( m_track_played_queue.size() ) {
            sp_track* track = m_track_played_queue.back();
            m_track_played_queue.pop_back();
            playTrack( track ); 
        }
        else                                    // Current track was the only track in the played queue
            stopTrack();
    }
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::playTrack( sp_track* track )
{
    CSingleLock lock( &m_mutex, TRUE );

    // See if this track is already in the queue list - and move to that position
    TrackQueue::iterator it_position = std::find( m_track_queue.begin(), m_track_queue.end(), track );
    if ( it_position != m_track_queue.end() ) {
        std::copy( m_track_queue.begin(), it_position, back_inserter(m_track_played_queue) );
        m_track_queue.erase( m_track_queue.begin(), it_position );
    }
    else 
        m_track_queue.push_front( track );

    sendCommand( CMD_NEXT_TRACK );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::playTracks( sp_playlist* pl ) 
{
    CSingleLock lock( &m_mutex, TRUE );
    m_track_queue.clear();

    int num_tracks = sp_playlist_num_tracks( pl );

    for (int i=0; i < num_tracks; i++ ) {
        m_track_queue.push_back( sp_playlist_track( pl, i ) );
    }

    sendCommand( CMD_NEXT_TRACK );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::queueTracks( sp_playlist* pl ) 
{
    CSingleLock lock( &m_mutex, TRUE );

    int num_tracks = sp_playlist_num_tracks( pl );

    for (int i=0; i < num_tracks; i++ ) {
        m_track_queue.push_back( sp_playlist_track( pl, i ) );
    }

    sendCommand( CMD_CHECK_PLAYING );
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
    m_track_queue.push_back( track );

    sendCommand( CMD_CHECK_PLAYING );
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
        tracks.push_back( sp_playlist_track( pl, i ) );
    }

    return tracks;
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
                                m_track_state = TRACK_ENDED;
                                m_current_track = NULL;
                                wait_time = 0;
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
        m_track_state = TRACK_ENDED;
        m_track_start_time = m_track_length_ms = 0L;
        m_pause_started = m_pause_accumulator = 0L;

        m_track_event.SetEvent();                            // Track moved to stopped state
    }
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::_startTrack( )
{
    if ( m_track_queue.size() ) {
        CSingleLock lock( &m_mutex, TRUE );
        m_current_track = m_track_queue.front();
        m_track_queue.pop_front();
        m_track_played_queue.push_back( m_current_track );

        sp_session_player_load( m_spotify_session, m_current_track );

        if ( !m_paused )
            sp_session_player_play( m_spotify_session, true );

        m_track_state = TRACK_STREAM_PENDING;
        m_track_length_ms = sp_track_duration( m_current_track );
        m_track_start_time = 0L;
        m_pause_started = m_pause_accumulator = 0L;
    }
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::_resume( )
{
    if ( m_paused ) {
        m_paused = false;
        sp_session_player_play( m_spotify_session, true );

        if ( m_audio_out->isPaused() )
            m_audio_out->setPaused( false );

        if ( m_pause_started )
            m_pause_accumulator += (GetCurrentTime()-m_pause_started);

        m_track_event.SetEvent();                                   // Track moved to play state
    }
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::_pause( )
{
    if ( !m_paused ) {
        m_paused = true;
        m_audio_out->setPaused( true );
        sp_session_player_play( m_spotify_session, false );
        m_pause_started = GetCurrentTime();

        m_track_event.SetEvent();                                   // Track moved to pause state
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

    size_t read = fscanf( hFile, "%s %s", lpUsername, lpCredentials );

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
