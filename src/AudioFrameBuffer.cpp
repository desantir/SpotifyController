/* 
Copyright (C) 2011,2012 Robert DeSantis
 
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
#include "AudioFrameBuffer.h"

// ----------------------------------------------------------------------------
//
UINT AudioFrameBuffer::read( UINT32 frames, LPBYTE pData )
{
    if ( frames > m_frame_count )
        frames = m_frame_count;

    for ( UINT32 frames_left=frames; true; ) {
        if ( m_read_ptr + frames_left < m_frame_capacity ) {
            memcpy( pData, &m_buffer[m_read_ptr*m_frame_size], frames_left*m_frame_size );
            m_read_ptr += frames_left;
            m_frame_count -= frames_left;
            break;
        }
        else {
            UINT partial_frames = m_frame_capacity-m_read_ptr;
            UINT size = partial_frames*m_frame_size;
            memcpy( pData, &m_buffer[m_read_ptr*m_frame_size], size );
            pData = &pData[ size ];
            m_read_ptr = 0;
            m_frame_count -= partial_frames;
            frames_left -= partial_frames;
        }
    }

    return frames;
}

// ----------------------------------------------------------------------------
//
bool AudioFrameBuffer::write( UINT32 frames, LPBYTE pData )
{
    if ( m_frame_count + frames > m_frame_capacity )
        return false;

    for ( UINT32 frames_left=frames; true; ) {
        if ( m_write_ptr + frames_left < m_frame_capacity ) {
            memcpy( &m_buffer[m_write_ptr*m_frame_size], pData, frames_left*m_frame_size );
            m_write_ptr += frames_left;
            m_frame_count += frames_left;
            break;
        }
        else {
            UINT partial_frames = m_frame_capacity-m_write_ptr;
            memcpy( &m_buffer[m_write_ptr*m_frame_size], pData, partial_frames*m_frame_size );
            frames_left -= partial_frames;
            m_frame_count += partial_frames;
            m_write_ptr = 0;
            pData = &pData[ partial_frames*m_frame_size ];
        }
    }

    return true;
}