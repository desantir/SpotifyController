/* 
Copyright (C) 2011-2016 Robert DeSantis
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
#include "Threadable.h"
#include "AudioOutputStream.h"
#include "MusicPlayerApi.h"
#include "TrackAnalyzer.h"
#include "TrackTimer.h"

#define ENGINE_TRACK_EVENT_NAME "DMXStudioEngineTrackEvent"

#define SPOTIFY_TOKEN_FILE		"spotify.tokens"
#define SPOTIFY_TRACK_PREFIX	"spotify:track:"
#define LOCAL_TRACK_PREFIX		"spotify:local:"
#define SPOTIFY_ALBUM_PREFIX	"spotify:album:"

typedef std::vector<CString> TrackLinkList;

struct TrackQueueEntry {

    CString		m_track_link;
    DWORD       m_seek_ms;

    TrackQueueEntry( LPCSTR track_link, DWORD seek_ms ) :
		m_track_link( track_link ),
        m_seek_ms( seek_ms )
    {}
};

typedef std::vector<sp_playlist *> PlaylistArray;
typedef std::list<TrackQueueEntry> TrackQueue;
typedef std::map<CString, AnalyzeInfo *> TrackAnalysisCache;

typedef enum {
    NOT_LOGGED_IN = 0,
    LOGIN_WAIT = 1,
    LOGIN_SUCCESS = 2,
    LOGIN_FAILED = 3
} LoginState;

typedef enum {
    CMD_NONE = 0,
    CMD_STOP_TRACK = 1,
    CMD_PAUSE_TRACK = 2,
    CMD_RESUME_TRACK = 3,
    CMD_NEXT_TRACK = 4,
    CMD_CHECK_PLAYING = 5
} SpotifyCommand;

typedef enum {
    TRACK_STREAM_PENDING = 0,
    TRACK_STREAMING = 1,
    TRACK_STREAM_COMPLETE = 2,
    TRACK_PLAYING = 3,
    TRACK_ENDED
} TrackState;

typedef std::vector<IPlayerEventCallback *> EventListeners;

class SpotifyEngine : public Threadable
{
    WAVEFORMATEX            m_waveFormat;              // Current input audio format (from Spotify)
    CString                 m_trackAnalysisContainer;   

    sp_session_config               spconfig;
    sp_session_callbacks            session_callbacks;  // The session callbacks
    sp_playlist_callbacks           pl_callbacks;       // The callbacks we are interested in for individual playlists
    sp_playlistcontainer_callbacks  pc_callbacks;       // The playlist container callbacks

    sp_session*             m_spotify_session;          // The global session handle
    AudioOutputStream*      m_audio_out;

    TrackState              m_track_state;              // track state
    sp_track*               m_current_track;            // Handle to the current track
    CString                 m_current_track_link;       // Link of curently playing track (empty if none)

    TrackTimer              m_track_timer;              // Marks time for the current track
    ULONG                   m_track_seek_ms;
    ULONG                   m_track_length_ms;
    CEvent                  m_track_event;              // Event pulsed when a track is changed (start/stop/pause)

    LoginState              m_login_state;
    CString                 m_spotify_error;
    CEvent                  m_spotify_notify;           // Coordinates spotify main loop with spotify APIs and UI

    SpotifyCommand          m_spotify_command;          // Next command from the UI
    TrackQueue              m_track_queue;
    TrackQueue              m_track_played_queue;
    CCriticalSection        m_mutex;                    // Mutex used when controlling playing tracks

    TrackAnalyzer*          m_analyzer;                 // Analyzer to use with the currently playing track
    TrackAnalysisCache      m_track_analysis_cache;     // Cache of loaded and created track analysis

    CCriticalSection        m_event_lock;               // Event handling mutex
    EventListeners          m_event_listeners;          // Track event listeners

    virtual UINT run();

public:
    SpotifyEngine(void);
    ~SpotifyEngine(void);

    bool isReady(void) {
        return isRunning() && m_audio_out != NULL && m_audio_out->isRunning();
    }

    bool connect( LPCSTR username, LPCSTR password, LPCSTR credentials_blob=NULL );
    bool connect( void );
    bool disconnect( void );
    bool registerEventListener( IPlayerEventCallback* listener );
    bool unregisterEventListener( IPlayerEventCallback* listener );

    inline CEvent& getTrackEvent() {
        return m_track_event;
    }

    LoginState getLoginState(void) const {
        return m_login_state;
    }

    LPCSTR getSpotifyError(void) {
        return m_spotify_error;
    }

    LPCSTR getCurrentTrack() const {
        return (LPCSTR)m_current_track_link;
    }

    void playTrack( LPCSTR track_link, DWORD seek_ms );
    void nextTrack();
    void previousTrack();
    void queueTrack( LPCSTR track_link );
    void stopTrack(void);
    void pauseTrack( bool pause );
    void playTracks( TrackLinkList& playlist );
    void queueTracks( TrackLinkList& playlist );
    void clearTrackQueue( );
    AnalyzeInfo* getTrackAnalysis( LPCSTR track_link );

    bool isTrackStarred( sp_track* track ) {
        return sp_track_is_starred ( m_spotify_session, track ) != 0 ? true : false;
    }

    bool isTrackPaused( ) {
        return m_current_track != NULL && m_track_timer.isPaused();
    }

    inline DWORD getTrackLength() const {
        return m_track_length_ms;
    }

    inline DWORD getTrackRemainingTime() const  {
        if ( m_current_track == NULL )
            return 0;

        return m_track_timer.getTrackRemaining();
    }

    inline DWORD getTrackPosition() const  {
        if ( m_current_track == NULL )
            return 0;

        return m_track_timer.getTrackPosition();
    }

    inline DWORD getTrackPlayTime() const {
        if ( m_current_track == NULL )
            return 0;

        return m_track_timer.getTrackPosition();
    }

    LPCSTR getPlayingTrackLink( ) {
        LPCSTR link = m_current_track_link.IsEmpty() ? NULL : (LPCSTR)m_current_track_link;
		return link;
    }

	TrackLinkList getQueuedTracks() {
		TrackLinkList tracks;
        for ( TrackQueueEntry entry : m_track_queue )
            tracks.push_back( entry.m_track_link );
        return tracks;
    }

    size_t getNumQueuedTracks() const {
        return m_track_queue.size();
    }

    size_t getNumPlayedTracks() const {
        return m_track_played_queue.size();
    }

	TrackLinkList getPlayedTracks() {
		TrackLinkList tracks;
        for ( TrackQueueEntry entry : m_track_played_queue )
            tracks.push_back( entry.m_track_link );
        return tracks;
    }

    inline bool isAnalyzing() const {
        return m_analyzer != NULL;
    }

    sp_track* linkToTrack( LPCSTR track_link )
    {
        sp_link* link = sp_link_create_from_string ( track_link );
        if ( link == NULL )
            return NULL;

        sp_track* track = sp_link_as_track ( link );
        sp_link_release( link );
        return track;
    }

    void sendEvent( PlayerEvent event, ULONG event_ms, LPCSTR track_link );
    void sendTrackQueueEvent();
    void sendPlaylistEvent( PlayerEvent event, sp_playlist *playlist );

private:
    void _stopTrack(void);
    void _startTrack(void);
    void _resume(void);
    void _pause(void);
    bool _readCredentials( CString& username, CString& credentials );
    void _writeCredentials( LPCSTR username, LPCSTR credentials );
    void _processTrackAnalysis();

    EventListeners::iterator findListener( IPlayerEventCallback* listener );

    void sendCommand( SpotifyCommand cmd ) {
        m_spotify_command = cmd;
        m_spotify_notify.SetEvent();
    }

    void removeTrackAnalyzer(void);
    bool haveTrackAnalysis( LPCSTR spotify_link );
    bool saveTrackAnalysis( AnalyzeInfo* info );
    AnalyzeInfo* loadTrackAnalysis( LPCSTR spotify_id );
    void freeTrackAnalysisCache(void);

    void inititializeSpotifyCallbacks(void);
    void play_token_lost(sp_session *sess);
    void log_message(sp_session *session, const char *data);
    void metadata_updated(sp_session *sess);
    void end_of_track(sp_session *sess);
    int music_delivery(sp_session *sess, const sp_audioformat *format, const void *frames, int num_frames);
    void notify_main_thread(sp_session *sess);
    void logged_in(sp_session *sess, sp_error error);
    void message_to_user(sp_session *session, const char *message);
    void offline_status_updated(sp_session *session);
    void offline_error(sp_session *session, sp_error error);
    void start_playback(sp_session *session);
    void stop_playback(sp_session *session);
    void logged_out(sp_session *session);
    void get_audio_buffer_stats(sp_session *session, sp_audio_buffer_stats *stats);

    void playlist_added(sp_playlistcontainer *pc, sp_playlist *playlist, int position, void *userdata);
    void playlist_removed(sp_playlistcontainer *pc, sp_playlist *playlist, int position, void *userdata);
    void playlist_moved(sp_playlistcontainer *pc, sp_playlist *playlist, int position, int new_position, void *userdata);
    void container_loaded(sp_playlistcontainer *pc, void *userdata);

    void tracks_added(sp_playlist *pl, sp_track * const *tracks, int num_tracks, int position, void *userdata);
    void tracks_removed(sp_playlist *pl, const int *tracks, int num_tracks, void *userdata);
    void tracks_moved(sp_playlist *pl, const int *tracks, int num_tracks, int new_position, void *userdata);
    void playlist_renamed(sp_playlist *pl, void *userdata);
    void playlist_state_changed(sp_playlist *pl, void *userdata);
    void playlist_update_in_progress(sp_playlist *pl, bool done, void *userdata);
    void playlist_metadata_updated(sp_playlist *pl, void *userdata);
    void track_created_changed(sp_playlist *pl, int position, sp_user *user, int when, void *userdata);
    void track_seen_changed(sp_playlist *pl, int position, bool seen, void *userdata);
    void description_changed(sp_playlist *pl, const char *desc, void *userdata);
    void image_changed(sp_playlist *pl, const byte *image, void *userdata);
    void track_message_changed(sp_playlist *pl, int position, const char *message, void *userdata);
    void subscribers_changed(sp_playlist *pl, void *userdata);
    void credentials_blob_updated(sp_session *session, const char *blob);

    static void SP_CALLCONV ftor_credentials_blob_updated(sp_session *session, const char *blob) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->credentials_blob_updated( session, blob );
    }
    static void SP_CALLCONV ftor_play_token_lost(sp_session *session) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->play_token_lost( session );
    }
    static void SP_CALLCONV ftor_log_message(sp_session *session, const char *data) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->log_message( session, data );
    }
    static void SP_CALLCONV ftor_metadata_updated(sp_session *session) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->metadata_updated( session );
    }
    static void SP_CALLCONV ftor_end_of_track(sp_session *session) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->end_of_track( session );
    }
    static int SP_CALLCONV ftor_music_delivery(sp_session *session, const sp_audioformat *format, const void *frames, int num_frames) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        return st->music_delivery( session, format, frames, num_frames );
    }
    static void SP_CALLCONV ftor_notify_main_thread(sp_session *session) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->notify_main_thread( session );
    }
    static void SP_CALLCONV ftor_logged_in(sp_session *session, sp_error error) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->logged_in( session, error );
    }
    static void SP_CALLCONV ftor_message_to_user(sp_session *session, const char *message) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->message_to_user( session, message );
    }
    static void SP_CALLCONV ftor_offline_status_updated(sp_session *session) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->offline_status_updated( session );
    }
    static void SP_CALLCONV ftor_offline_error(sp_session *session, sp_error error) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->offline_error( session, error );
    }
    static void SP_CALLCONV ftor_start_playback(sp_session *session) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->start_playback( session );
    }
    static void SP_CALLCONV ftor_stop_playback(sp_session *session) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->stop_playback( session );
    }
    static void SP_CALLCONV ftor_logged_out(sp_session *session) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->logged_out( session );
    }
    static void SP_CALLCONV ftor_get_audio_buffer_stats(sp_session *session, sp_audio_buffer_stats *stats) {
        SpotifyEngine* st = (SpotifyEngine*)sp_session_userdata( session );
        st->get_audio_buffer_stats( session, stats );
    }
    static void SP_CALLCONV ftor_playlist_added(sp_playlistcontainer *pc, sp_playlist *playlist, int position, void *userdata) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->playlist_added( pc, playlist, position, userdata );
    }
    static void SP_CALLCONV ftor_playlist_removed(sp_playlistcontainer *pc, sp_playlist *playlist, int position, void *userdata) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->playlist_removed( pc, playlist, position, userdata );
    }
    static void SP_CALLCONV ftor_playlist_moved(sp_playlistcontainer *pc, sp_playlist *playlist, int position, int new_position, void *userdata) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->playlist_moved( pc, playlist, position, new_position, userdata );
    }
    static void SP_CALLCONV ftor_container_loaded(sp_playlistcontainer *pc, void *userdata) {
        if ( userdata == NULL )
            return;
            
        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->container_loaded( pc, userdata );
    }

    static void SP_CALLCONV ftor_tracks_added(sp_playlist *pl, sp_track * const *tracks, int num_tracks, int position, void *userdata) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->tracks_added( pl, tracks, num_tracks, position, userdata );
    }
    static void SP_CALLCONV ftor_tracks_removed(sp_playlist *pl, const int *tracks, int num_tracks, void *userdata) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->tracks_removed( pl, tracks, num_tracks, userdata );
    }
    static void SP_CALLCONV ftor_tracks_moved(sp_playlist *pl, const int *tracks, int num_tracks, int new_position, void *userdata ) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->tracks_moved( pl, tracks, num_tracks, new_position, userdata );
    }
    static void SP_CALLCONV ftor_playlist_renamed(sp_playlist *pl, void *userdata ) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->playlist_renamed( pl, userdata );
    }
    static void SP_CALLCONV ftor_playlist_state_changed(sp_playlist *pl, void *userdata ) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->playlist_state_changed( pl, userdata );
    }
    static void SP_CALLCONV ftor_playlist_update_in_progress(sp_playlist *pl, bool done, void *userdata) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->playlist_update_in_progress( pl, done, userdata );
    }
    static void SP_CALLCONV ftor_playlist_metadata_updated(sp_playlist *pl, void *userdata) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->playlist_metadata_updated( pl, userdata );
    }
    static void SP_CALLCONV ftor_track_created_changed(sp_playlist *pl, int position, sp_user *user, int when, void *userdata ) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->track_created_changed( pl, position, user, when, userdata );
    }
    static void SP_CALLCONV ftor_track_seen_changed(sp_playlist *pl, int position, bool seen, void *userdata ) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->track_seen_changed( pl, position, seen, userdata );
    }
    static void SP_CALLCONV ftor_description_changed(sp_playlist *pl, const char *desc, void *userdata ) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->description_changed( pl, desc, userdata );
    }
    static void SP_CALLCONV ftor_image_changed(sp_playlist *pl, const byte *image, void *userdata ) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->image_changed( pl, image, userdata );
    }
    static void SP_CALLCONV ftor_track_message_changed(sp_playlist *pl, int position, const char *message, void *userdata ) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->track_message_changed( pl, position, message, userdata );
    }
    static void SP_CALLCONV ftor_subscribers_changed(sp_playlist *pl, void *userdata ) {
        if ( userdata == NULL )
            return;

        SpotifyEngine* st = (SpotifyEngine*)userdata;
        st->subscribers_changed( pl, userdata );
    }
};

extern const uint8_t g_appkey[];
extern const size_t g_appkey_size;
extern const char * g_EchoNestKey;

