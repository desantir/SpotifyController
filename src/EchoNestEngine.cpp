/* 
Copyright (C) 2014 Robert DeSantis
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
#include <afxinet.h>

// ----------------------------------------------------------------------------
//
EchoNestEngine::EchoNestEngine()
{
}

// ----------------------------------------------------------------------------
//
EchoNestEngine::~EchoNestEngine()
{
}

// ----------------------------------------------------------------------------
//
bool EchoNestEngine::httpGet( LPCTSTR pszServerName, LPCTSTR url, CString& json_data )
{
    static LPCSTR accept_types[] = { "text/html", "application/xhtml+xml", "*/*", NULL };

    CInternetSession session( _T("EchoNest Session") );
    CHttpConnection* pServer = NULL;
    CHttpFile* pFile = NULL;
    bool result = FALSE;

    json_data.Empty();

    try {
        DWORD dwRet = 0;

        pServer = session.GetHttpConnection( pszServerName, (INTERNET_PORT)80 );

        pFile = pServer->OpenRequest(CHttpConnection::HTTP_VERB_GET, url, NULL, 1, accept_types );
        pFile->SendRequest();
        pFile->QueryInfoStatusCode(dwRet);

        if (dwRet == HTTP_STATUS_OK) {
            char buffer[ 1024 ];
            UINT len;

            while ( (len = pFile->Read( buffer, sizeof(buffer)-1 )) > 0 ) {
                buffer[ len ] = '\0';
                json_data.Append( buffer );
            }

            result = TRUE;
        }
    }
    catch ( std::exception& ex ) {
        log( ex );
    }
    catch ( ... ) {
        log( "Unknown error connecting to %s with URL %s", pszServerName, url );
    }

    if ( pFile ) {
        pFile->Close();
        delete pFile;
    }

    if ( pServer ) {
        pServer->Close();
        delete pServer;
    }

    session.Close();

    return result;
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
bool EchoNestEngine::getTrackAudioInfo( LPCSTR spotify_track_link, AudioInfo* audio_info )
{
    CString echonest_url;
    
    echonest_url.Format( "/api/v4/song/profile?api_key=%s&format=json&track_id=%s&bucket=audio_summary&bucket=song_type&bucket=id:spotify", 
        g_EchoNestKey, spotify_track_link );

    return fetchSongData( echonest_url, audio_info );
}

// ----------------------------------------------------------------------------
//
bool EchoNestEngine::lookupTrackAudioInfo( LPCSTR track_name, LPCSTR artist_name, AudioInfo*audio_info )
{
    CString echonest_url;
    
    echonest_url.Format( "/api/v4/song/search?api_key=%s&format=json&artist=%s&title=%s&bucket=audio_summary&bucket=song_type&results=1", 
        g_EchoNestKey, encodeString( artist_name ), encodeString( track_name ) );

    return fetchSongData( echonest_url, audio_info );
}

// ----------------------------------------------------------------------------
//
bool EchoNestEngine::fetchSongData( LPCSTR echonest_url, AudioInfo*audio_info )
{
    CString buffer;

    try {
        if ( !httpGet( "developer.echonest.com", echonest_url, buffer ) )
            return false;

        SimpleJsonParser parser( buffer );

        if ( !parser.has_key( "response" ) ) {
            log( "EchoNest response missing 'response' tag (%s)", echonest_url );
            return false;
        }

        SimpleJsonParser response_parser = parser.get<SimpleJsonParser>( "response" );

        if ( !response_parser.has_key( "status" ) || !response_parser.has_key( "songs" ) ) {
            log( "EchoNest response missing 'status' or 'songs' tag (%s)", echonest_url );
            return false;
        }

        int code = response_parser.get<SimpleJsonParser>( "status" ).get<int>( "code" );
        if ( code != 0 ) {
            log( "EchoNest response code %d != 0 (%s)", code, echonest_url );
            return false;
        }

        PARSER_LIST songs = response_parser.get<PARSER_LIST>( "songs" );
        if ( songs.size() != 1 ) {
            log( "EchoNest response missing 'songs' array count = %d (%s)", songs.size(), echonest_url );
            return false;
        }

        if ( !songs[0].has_key( "song_type" ) || !songs[0].has_key( "audio_summary" ) ) {
            log( "EchoNest response missing 'song_type' or 'audio_summary' tag (%s)", echonest_url );
            return false;
        }

        CString song_type = songs[0].get<CString>( "song_type" );
        CString id = songs[0].get<CString>( "id" );
        strncpy_s( audio_info->song_type, song_type, sizeof(audio_info->song_type) );
        strncpy_s( audio_info->id, id, sizeof(audio_info->id) );

        SimpleJsonParser audio_summary = songs[0].get<SimpleJsonParser>( "audio_summary" );
        audio_info->key = audio_summary.get<int>( "key" );
        audio_info->energy = audio_summary.get<double>( "energy" );
        audio_info->liveness = audio_summary.get<double>( "liveness" );
        audio_info->tempo = audio_summary.get<double>( "tempo" );
        audio_info->speechiness = audio_summary.get<double>( "speechiness" );
        audio_info->acousticness = audio_summary.get<double>( "acousticness" );
        audio_info->instrumentalness = audio_summary.get<double>( "instrumentalness" );
        audio_info->duration = audio_summary.get<double>( "duration" );
        audio_info->mode = audio_summary.get<int>( "mode" );
        audio_info->time_signature = audio_summary.get<int>( "time_signature" );
        audio_info->loudness = audio_summary.get<double>( "loudness" );
        audio_info->valence = audio_summary.get<double>( "valence" );
        audio_info->danceability = audio_summary.get<double>( "danceability" );

        return true;
    }
    catch ( std::exception& ex ) {
        log( ex );
        return false;
    }
}
