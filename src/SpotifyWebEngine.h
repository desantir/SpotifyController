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

#pragma once

#include "stdafx.h"

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

struct SpotifyEntity {
	CString			m_id;
	CString			m_name;
	CString			m_uri;

	SpotifyEntity( LPCSTR id, LPCSTR name, LPCSTR uri ) :
		m_id( id ),
		m_name( name ),
		m_uri( uri )
	{}

	SpotifyEntity() {}
};

struct Artist : public SpotifyEntity {
	Artist( LPCSTR id, LPCSTR name, LPCSTR uri ) : 
		SpotifyEntity( id, name, uri )
	{}
};

struct Image {
	CString		m_href;
	UINT		m_height;
	UINT		m_width;

	Image( LPCSTR href, UINT height, UINT width ) :
		m_href( href ),
		m_height( height ),
		m_width( width )
	{}
};

typedef std::vector<Image> ImageList;

struct Album : public SpotifyEntity {
	ImageList		m_images;

	Album() {}

	Album( LPCSTR id, LPCSTR name, LPCSTR uri ) : 
		SpotifyEntity( id, name, uri )
	{}

	inline void add( Image& image ) {
		m_images.push_back( image );
	}
};

typedef std::vector<Artist> ArtistList;

struct Track : public SpotifyEntity {
	CString			m_href;
	ArtistList		m_artists;
	int				m_duration_ms;
	Album			m_album;

	Track( LPCSTR id, LPCSTR name, LPCSTR uri, LPCSTR href, int duration_ms ) :
		SpotifyEntity( id, name, uri ),
		m_href( href ),
		m_duration_ms( duration_ms )
	{}

	Track() {}

	inline void add( Artist& artist ) {
		m_artists.push_back( artist );
	}

	inline void add( Album& album ) {
		m_album = album;
	}
};

typedef std::vector<Track*> TrackPtrList;
typedef std::vector<Track> TrackList;

struct Playlist : public SpotifyEntity {
	CString			m_tracks_href;
	ArtistList		m_artists;
	TrackLinkList	m_tracks;
	CString			m_owner_id;
	unsigned		m_track_count;
	bool			m_is_album;

	Playlist( LPCSTR id, LPCSTR name, LPCSTR uri, LPCSTR tracks_href, unsigned track_count, LPCSTR owner_id ) :
		SpotifyEntity( id, name, uri ),
		m_tracks_href( tracks_href ),
		m_track_count( track_count ),
		m_owner_id( owner_id )
	{
		m_is_album = m_uri.Find( SPOTIFY_ALBUM_PREFIX ) == 0;
	}

	inline void add( Artist& artist ) {
		m_artists.push_back( artist );
	}

	inline void add( Track& track ) {
		m_tracks.push_back( track.m_uri );
	}

	inline bool isAlbum( ) const {
		return m_is_album;
	}
};

typedef std::vector<Playlist> PlaylistList;

typedef std::map<CString, Track> TrackMap;

class JsonNode;

class SpotifyWebEngine : public Threadable
{
	CString					m_auth_token;
	CString					m_auth_refresh;
	CString					m_user_id;

	PlaylistList			m_playlists;
	TrackMap				m_track_cache;

	CString                 m_trackInfoContainer;

	AudioTrackInfoCache     m_track_audio_info_cache;           // Caches track audio info to avoid http lookups
	CCriticalSection        m_track_cache_mutex;				// Protect track cache

	InfoRequestList         m_requests;                         // Track info request queue
	CCriticalSection        m_audio_info_mutex;					// Protect request queue

	CCriticalSection		m_get_mutex;

	CEvent                  m_wake;								// Wake up request processor

public:
	SpotifyWebEngine( );
	~SpotifyWebEngine( );

	bool newAuthorization( LPBYTE authorization, DWORD authorization_len );
	bool loadAuthorization();
	bool loadUser( );

	void start();
	void stop();

	PlaylistList& fetchUserPlaylists();
	Playlist* getPlaylist( LPCSTR playlist_uri );
	TrackLinkList* fetchUserPlaylistTracks( LPCSTR playlist_uri );
	Track* fetchTrack( LPCSTR track_uri );
	TrackPtrList searchForTracks( LPCSTR search_key );
	AudioStatus getTrackAudioInfo( LPCSTR spotify_track_link, AudioInfo* audio_info, DWORD wait_ms );
	AudioStatus lookupTrackAudioInfo( LPCSTR track_name, LPCSTR artist_name, AudioInfo* audio_info, DWORD wait_ms );

	inline Track* getTrack( LPCSTR track_uri ) {
		TrackMap::iterator it = m_track_cache.find( track_uri );
		return ( it == m_track_cache.end() ) ? NULL : &it->second;
	}

	inline Track* addTrack(Track& track) {
		std::pair<TrackMap::iterator, bool> result = m_track_cache.emplace( track.m_uri, track );
		return &result.first->second;
	}

private:
	bool parseAuthorization( LPCSTR auth_json );
	LPBYTE get( LPCSTR url, bool check_authorization = true );
	void writeAuthorization( LPCSTR access_token, LPCSTR refresh_token, unsigned expires_in );
	bool checkUserAuthorization();
	Track* loadAndCacheTrack( JsonNode* track_node );
	bool saveAudioInfo( AudioInfo& audio_info );
	AudioInfo* loadAudioInfo( LPCSTR spotify_link );
	UINT run(void);
	AudioStatus getAudioInfo( InfoRequest& request, AudioInfo* audio_info, DWORD wait_ms );
	void queueRequest( InfoRequest& request );
	bool fetchSongData( InfoRequestList& requests, LPCSTR buffer, LPCSTR echonest_url );
};

extern LPCSTR g_webapi_client_id;
extern LPCSTR g_webapi_client_secret;

