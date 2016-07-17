/* 
Copyright (C) 2016 Robert DeSantis
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

#include "Threadable.h"

class SpotifyEngine;

class TrackTimer : public Threadable
{
    SpotifyEngine*      m_engine;
    CCriticalSection    m_lock;

    bool                m_tracking;                 // Currently tracking time
    bool                m_paused;                   // Track is paused;
    ULONG               m_last_time;
    ULONG               m_next_notify;

    CString             m_track_link;               // Track we are tracking time for
    ULONG               m_track_length;             // Track total length
    ULONG               m_track_position;           // Current track position

    virtual UINT run();

public:
    TrackTimer( SpotifyEngine*  engine );
    ~TrackTimer();

    inline ULONG getTrackPosition() const {
        return m_tracking ? m_track_position : 0L;
    }

    inline ULONG getTrackRemaining() const {
        return m_tracking ? m_track_length - m_track_position : 0L;
    }

    inline bool isPaused() const {
        return m_paused;
    }

    void start( ULONG track_length, ULONG track_seek, LPCSTR track_link );
    void stop();
    void resume();
    void pause();
};

