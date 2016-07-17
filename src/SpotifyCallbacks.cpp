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
void SpotifyEngine::inititializeSpotifyCallbacks( void )
{
    memset( &session_callbacks, 0, sizeof(sp_session_callbacks) );
    memset( &pl_callbacks, 0, sizeof(sp_playlist_callbacks) );
    memset( &pc_callbacks, 0, sizeof(sp_playlistcontainer_callbacks) );

    session_callbacks.logged_in = &ftor_logged_in;
    session_callbacks.notify_main_thread = &ftor_notify_main_thread;
    session_callbacks.metadata_updated = &ftor_metadata_updated;
    session_callbacks.play_token_lost = &ftor_play_token_lost;
    session_callbacks.log_message = &ftor_log_message;
    session_callbacks.end_of_track = &ftor_end_of_track;
    session_callbacks.logged_out = &ftor_logged_out;
    session_callbacks.connection_error = NULL;
    session_callbacks.message_to_user = &ftor_message_to_user;
    session_callbacks.streaming_error = NULL;
    session_callbacks.userinfo_updated = NULL;
    session_callbacks.start_playback = &ftor_start_playback;
    session_callbacks.stop_playback = &ftor_stop_playback;
    session_callbacks.get_audio_buffer_stats = &ftor_get_audio_buffer_stats;
    session_callbacks.offline_status_updated = &ftor_offline_status_updated;
    session_callbacks.offline_error = &ftor_offline_error;
    session_callbacks.credentials_blob_updated = &ftor_credentials_blob_updated;
    session_callbacks.connectionstate_updated = NULL;
    session_callbacks.scrobble_error = NULL;
    session_callbacks.private_session_mode_changed = NULL;
    session_callbacks.music_delivery = &ftor_music_delivery;

    pc_callbacks.playlist_added = &ftor_playlist_added;
    pc_callbacks.playlist_removed = &ftor_playlist_removed;
    pc_callbacks.playlist_moved = &ftor_playlist_moved;
    pc_callbacks.container_loaded = &ftor_container_loaded;

    pl_callbacks.tracks_added = &ftor_tracks_added;
    pl_callbacks.tracks_removed = &ftor_tracks_removed;
    pl_callbacks.tracks_moved = &ftor_tracks_moved;
    pl_callbacks.playlist_renamed = &ftor_playlist_renamed;
    pl_callbacks.playlist_state_changed = &ftor_playlist_state_changed;
    pl_callbacks.playlist_update_in_progress = &ftor_playlist_update_in_progress;
    pl_callbacks.playlist_metadata_updated = &ftor_playlist_metadata_updated;
    pl_callbacks.track_created_changed = &ftor_track_created_changed;
    pl_callbacks.track_seen_changed = &ftor_track_seen_changed;
    pl_callbacks.description_changed = &ftor_description_changed;
    pl_callbacks.image_changed = &ftor_image_changed;
    pl_callbacks.track_message_changed = &ftor_track_message_changed;
    pl_callbacks.subscribers_changed = &ftor_subscribers_changed;
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::credentials_blob_updated(sp_session *session, const char *blob)
{
    SPOTIFY_API_CALLED( "credentials_blob_updated" );

    _writeCredentials( sp_session_user_name( m_spotify_session ), blob );
}

/**
 * Notification that some other connection has started playing on this account.
 * Playback has been stopped.
 *
 * @sa sp_session_callbacks#play_token_lost
 */
void SpotifyEngine::play_token_lost(sp_session *sess)
{
    SPOTIFY_API_CALLED( "play_token_lost" );

    m_login_state = NOT_LOGGED_IN;

    _stopTrack();
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::log_message(sp_session *session, const char *data) {
    CString message = data;
    message.Replace( '\n', ' ' );
    log( message );
}

/**
 * Callback called when libspotify has new metadata available
 *
 * @sa sp_session_callbacks#metadata_updated
 */
void SpotifyEngine::metadata_updated(sp_session *sess)
{
    SPOTIFY_API_CALLED( "metadata_updated" );
}

/**
 * This callback is used from libspotify when the current track has ended
 *
 * @sa sp_session_callbacks#end_of_track
 */
void SpotifyEngine::end_of_track(sp_session *sess)
{
    SPOTIFY_API_CALLED( "end_of_track" );

    if ( isAnalyzing() )
        m_analyzer->finishData();

    m_track_state = TRACK_STREAM_COMPLETE;
    //g_notify_do = 1;
    m_spotify_notify.SetEvent();
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::get_audio_buffer_stats(sp_session *session, sp_audio_buffer_stats *stats)
{
    SPOTIFY_API_CALLED( "get_audio_buffer_stats" );

    stats->samples = m_audio_out->getCachedSamples();
    stats->stutter = 0;
}

/**
 * This callback is used from libspotify whenever there is PCM data available.
 *
 * @sa sp_session_callbacks#music_delivery
 */
int SpotifyEngine::music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames)
{
    if (num_frames == 0) {                      // Audio discontinuity
        m_audio_out->cancel();
        return 0; 
    }

    SPOTIFY_API_CALLED( "music_delivery frames=%d rate=%d", num_frames, format->sample_rate );

    // Fill buffer till it complains
    if ( !m_audio_out->addSamples( num_frames, format->channels, format->sample_rate, (LPBYTE)frames ) ) {
        SPOTIFY_API_CALLED( "music_delivery wait" );
        return 0;
    }

    if ( m_track_state == TRACK_STREAM_PENDING ) {
        m_track_state = TRACK_STREAMING;

        m_track_timer.start( m_track_length_ms, m_track_seek_ms, m_current_track_link );
    }

    m_spotify_notify.SetEvent();

    if ( isAnalyzing() ) {
        m_analyzer->addData( num_frames, (LPBYTE)frames );
    }

    return num_frames;
}

/**
 * This callback is called from an internal libspotify thread to ask us to
 * reiterate the main loop.
 *
 * We notify the main thread using a condition variable and a protected variable.
 *
 * @sa sp_session_callbacks#notify_main_thread
 */
void SpotifyEngine::notify_main_thread(sp_session *sess)
{
    SPOTIFY_API_CALLED( "notify_main_thread" );
    
    m_spotify_notify.SetEvent();
}

void SpotifyEngine::logged_in(sp_session *sess, sp_error error)
{
    SPOTIFY_API_CALLED( "logged_in" );

    if ( SP_ERROR_OK != error ) {
        log_status( "Login failed: %s", sp_error_message(error) );
        m_spotify_error = sp_error_message(error);
        m_login_state = LOGIN_FAILED;
        return;
    }

    sp_playlistcontainer *pc = sp_session_playlistcontainer(sess);
    sp_playlistcontainer_add_callbacks( pc, &pc_callbacks, this);

    for (int i=0; i < sp_playlistcontainer_num_playlists(pc); ++i) {
        sp_playlist *pl = sp_playlistcontainer_playlist(pc, i);
        sp_playlist_add_callbacks( pl, &pl_callbacks, this );
    }

    m_login_state = LOGIN_SUCCESS;
    m_spotify_error = "";
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::message_to_user(sp_session *session, const char *message)
{
    log_status( "Message to user: %s", message );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::offline_status_updated(sp_session *session)
{
    SPOTIFY_API_CALLED( "offline_status_updated" );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::offline_error(sp_session *session, sp_error error)
{
    SPOTIFY_API_CALLED( "offline_error" );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::start_playback(sp_session *session)
{
    SPOTIFY_API_CALLED( "start_playback" );
}

void SpotifyEngine::stop_playback(sp_session *session)
{
    SPOTIFY_API_CALLED( "stop_playback" );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::logged_out(sp_session *session)
{
    SPOTIFY_API_CALLED( "logged_out" );

    m_login_state = NOT_LOGGED_IN;
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::playlist_added(sp_playlistcontainer *pc, sp_playlist *playlist, int position, void *userdata)
{
    LPCSTR name = sp_playlist_name( playlist );
    SPOTIFY_API_CALLED( "playlist_added @%d named %s", position, name );

    sp_playlist_add_callbacks( playlist, &pl_callbacks, NULL);

    sendPlaylistEvent( PlayerEvent::PLAYLIST_ADDED, playlist );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::playlist_removed(sp_playlistcontainer *pc, sp_playlist *playlist, int position, void *userdata)
{
    SPOTIFY_API_CALLED( "playlist_removed" );

    sendPlaylistEvent( PlayerEvent::PLAYLIST_REMOVED, playlist );
}

void SpotifyEngine::playlist_moved(sp_playlistcontainer *pc, sp_playlist *playlist, int position, int new_position, void *userdata)
{
    SPOTIFY_API_CALLED( "playlist_moved" );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::container_loaded(sp_playlistcontainer *pc, void *userdata)
{
    SPOTIFY_API_CALLED( "container_loaded" );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::tracks_added(sp_playlist *pl, sp_track * const *tracks, int num_tracks, int position, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );
    LPCSTR tr_name = (num_tracks > 0 ) ? sp_track_name( tracks[0] ) : "NONE";

    SPOTIFY_API_CALLED( "%d tracks_added to %s: %s", num_tracks, pl_name, tr_name );

    sendPlaylistEvent( PlayerEvent::PLAYLIST_CHANGED, pl );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::tracks_removed(sp_playlist *pl, const int *tracks, int num_tracks, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "%d tracks_removed from %s", num_tracks, pl_name );

    sendPlaylistEvent( PlayerEvent::PLAYLIST_CHANGED, pl );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::tracks_moved(sp_playlist *pl, const int *tracks, int num_tracks, int new_position, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "%d tracks_moved in %s", num_tracks, pl_name );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::playlist_renamed(sp_playlist *pl, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "playlist_renamed %s", pl_name );

    sendPlaylistEvent( PlayerEvent::PLAYLIST_CHANGED, pl );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::playlist_state_changed(sp_playlist *pl, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "playlist_state_changed %s", pl_name );

    sendPlaylistEvent( PlayerEvent::PLAYLIST_CHANGED, pl );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::playlist_update_in_progress(sp_playlist *pl, bool done, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "playlist_update_in_progress %s", pl_name );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::playlist_metadata_updated(sp_playlist *pl, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "playlist_metadata_updated %s", pl_name );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::track_created_changed(sp_playlist *pl, int position, sp_user *user, int when, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "track_created_changed %s", pl_name );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::track_seen_changed(sp_playlist *pl, int position, bool seen, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "track_seen_changed %s", pl_name );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::description_changed(sp_playlist *pl, const char *desc, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "description_changed %s", pl_name );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::image_changed(sp_playlist *pl, const byte *image, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "image_changed %s", pl_name );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::track_message_changed(sp_playlist *pl, int position, const char *message, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "track_message_changed %s", pl_name );
}

// ----------------------------------------------------------------------------
//
void SpotifyEngine::subscribers_changed(sp_playlist *pl, void *userdata)
{
    LPCSTR pl_name = sp_playlist_name( pl );

    SPOTIFY_API_CALLED( "subscribers_changed %s", pl_name );
}