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

#include "stdafx.h"
#include "TrackTimer.h"

// ----------------------------------------------------------------------------
//
TrackTimer::TrackTimer( SpotifyEngine* engine ) :
    m_engine( engine ),
    Threadable( "TrackTimer" )
{
}

// ----------------------------------------------------------------------------
//
TrackTimer::~TrackTimer()
{
}

// ----------------------------------------------------------------------------
//
UINT TrackTimer::run()
{
    CSingleLock lock( &m_lock, FALSE );

    while ( isRunning() ) {
        m_lock.Lock();

        if ( m_tracking && !m_paused ) {
            ULONG time = GetCurrentTime();

            ULONG time_passed = time - m_last_time;

            m_track_position += time_passed;

            m_last_time = time;

            if ( m_next_notify < time ) {
                m_engine->sendEvent( PlayerEvent::TRACK_POSITION, m_track_position, (LPCSTR)m_track_link );
                m_next_notify = time + 1000L;
            }
        }

        m_lock.Unlock();

        Sleep(100);
    }
    
    return 0;
}

// ----------------------------------------------------------------------------
//
void TrackTimer::stop() {
    CSingleLock lock( &m_lock, TRUE );

    m_engine->sendEvent( PlayerEvent::TRACK_STOP, 0L, m_track_link );

    m_tracking = m_paused = false;
    m_track_link.Empty();
    m_last_time = m_next_notify = m_track_position = 0L;
}

// ----------------------------------------------------------------------------
//
void TrackTimer::pause() {
    m_paused = true;

    m_engine->sendEvent( PlayerEvent::TRACK_PAUSE, getTrackPosition(), m_track_link );
}

// ----------------------------------------------------------------------------
//
void TrackTimer::resume()
{
    CSingleLock lock( &m_lock, TRUE );

    if ( m_tracking ) {
        m_paused = false;
        m_next_notify = 0L;
        m_last_time = GetCurrentTime();

        m_lock.Unlock();

        m_engine->sendEvent( PlayerEvent::TRACK_RESUME, getTrackPosition(), m_track_link );
    }
}

// ----------------------------------------------------------------------------
//
void TrackTimer::start( ULONG track_length, ULONG track_seek, LPCSTR track_link )
{
    CSingleLock lock( &m_lock, TRUE );

    m_tracking = true;
    m_paused = false;
    m_track_length = track_length;
    m_track_link = track_link;
    m_next_notify = 0L;
    m_track_position = track_seek;
    m_last_time = GetCurrentTime();

    lock.Unlock();

    m_engine->sendEvent( PlayerEvent::TRACK_PLAY, getTrackPosition(), m_track_link );
}
