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

#pragma once

#include "stdafx.h"

class AudioFrameBuffer
{
    UINT    m_frame_capacity;                       // Numer of frame that can be stored
    UINT    m_frame_size;                           // Size of a frame in bytes

    UINT    m_write_ptr;                            // New frames go here
    UINT    m_read_ptr;                             // Next frame to read is here
    UINT    m_frame_count;                          // Number of frames available to read

    BYTE*   m_buffer;                               // Frame data

public:
    AudioFrameBuffer( UINT frames=44100*10, UINT channels=2, UINT sample_size=sizeof(int16_t) ) :
        m_frame_capacity( frames ),
        m_frame_size( channels * sample_size ),
        m_write_ptr( 0 ),
        m_read_ptr( 0 ),
        m_frame_count( 0 )
    {
        m_buffer = (BYTE *)malloc( m_frame_capacity * m_frame_size );
    }

    ~AudioFrameBuffer()
    {
        free( m_buffer );
    }

    inline UINT availableSpace() const {
        return m_frame_capacity-m_frame_count;
    }

    inline UINT size( void ) const {
        return m_frame_count;
    }

    inline void reset( void ) {
        m_write_ptr = m_read_ptr = m_frame_count = 0;
    }

    UINT read( UINT32 frames, LPBYTE pData );
    bool write( UINT32 frames, LPBYTE pData );

    LPBYTE getFramePointer( UINT frame ) {
        return &m_buffer[ frame * m_frame_size ];
    }

    inline UINT getFrameSize() const {
        return m_frame_size;
    }
};

