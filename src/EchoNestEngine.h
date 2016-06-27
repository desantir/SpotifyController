/* 
Copyright (C) 2014-16 Robert DeSantis
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
#include "MusicPlayerApi.h"

// Special ID for tracks without information
#define UNAVAILABLE_ID  "UNAVAILABLE_ID"

#define CACHE_WRITE_INTERVAL_MS (1000*60*2)

typedef std::map<CString,AudioInfo> AudioTrackInfoCache;

class InfoRequest
{
    CString     m_spotifyLink;
    CString     m_artist;
    CString     m_title;
    CString     m_key;

public:
    InfoRequest::InfoRequest( LPCSTR spotifyLink ) :
        m_spotifyLink( spotifyLink ),
        m_key( spotifyLink )
    {}

    InfoRequest::InfoRequest( LPCSTR artist, LPCSTR title ) :
        m_artist( artist ),
        m_title( title )
    {
        m_key.Empty();

        for ( int i=0; i < m_artist.GetLength() && i < 64; i++ ) {
            char ch = m_artist.GetAt( i );
            if ( isalnum( ch ) )
                m_key.AppendChar( ch );
        }

        m_key.AppendChar( '-' );

        for ( int i=0; i < m_title.GetLength() && i < 64; i++ ) {
            char ch = m_title.GetAt( i );
            if ( isalnum( ch ) )
                m_key.AppendChar( ch );
        }
    }

    LPCSTR getSpotifyLink() const {
        return (LPCSTR)m_spotifyLink;
    }

    LPCSTR getArtist() const {
        return (LPCSTR)m_artist;
    }

    LPCSTR getTitle() const {
        return (LPCSTR)m_title;
    }

    bool hasLink() const {
        return m_spotifyLink.GetLength() > 0;
    }

    LPCSTR getKey() const {
        return m_key;
    }
};

typedef std::vector<InfoRequest> InfoRequestList;

struct AccessToken {
    CString token;
    DWORD expires;
    CString token_type;

    AccessToken() : expires(0) {}
};

class EchoNestEngine : public Threadable
{
    CString                 m_trackInfoContainer;

    AudioTrackInfoCache     m_track_audio_info_cache;           // Caches track audio info to avoid http lookups
    CCriticalSection        m_track_cache_mutex;				// Protect track cache

    InfoRequestList         m_requests;                         // Track info request queue
    CCriticalSection        m_request_mutex;					// Protect request queue

    CEvent                  m_wake;								// Wake up request processor

public:
    EchoNestEngine();
    ~EchoNestEngine();

    void start();
    void stop();

    AudioStatus getTrackAudioInfo( LPCSTR spotify_track_link, AudioInfo* audio_info, DWORD wait_ms );
    AudioStatus lookupTrackAudioInfo( LPCSTR track_name, LPCSTR artist_name, AudioInfo*audio_info, DWORD wait_ms );

private:
    AudioStatus getAudioInfo( InfoRequest& request, AudioInfo* audio_info, DWORD wait_ms );

    virtual UINT run(void);

    void queueRequest( InfoRequest& request );

    bool httpGet( AccessToken& accessToken, LPCTSTR echonest_url, CString& json_data, UINT& wait_seconds );
    bool getCredentials( AccessToken& accessToken );

    CString encodeString( LPCSTR source );
    bool fetchSongData( InfoRequestList& requests, LPCSTR buffer, LPCSTR echonest_url );

    bool saveAudioInfo( AudioInfo& audio_info );
    AudioInfo* loadAudioInfo( LPCSTR spotify_link );
};

extern LPCSTR g_webapi_client_id;
extern LPCSTR g_webapi_client_secret;