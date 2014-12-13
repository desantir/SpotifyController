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

#pragma once

#include "stdafx.h"
#include "MusicPlayerApi.h"

class EchoNestEngine
{

public:
    EchoNestEngine();
    ~EchoNestEngine();

    bool getTrackAudioInfo( LPCSTR spotify_track_link, AudioInfo* audio_info );
    bool lookupTrackAudioInfo( LPCSTR track_name, LPCSTR artist_name, AudioInfo*audio_info );

private:
    bool httpGet( LPCTSTR pszServerName, LPCTSTR pszFileName, CString& json_data );
    CString encodeString( LPCSTR source );
    bool fetchSongData( LPCSTR echonest_url, AudioInfo*audio_info );
};