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
#include "Threadable.h"
#include "AudioFrameBuffer.h"

#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

struct AudioRenderDevice {
    CStringW	m_id;
    CString		m_friendly_name;
    bool		m_isDefault;

    AudioRenderDevice( LPWSTR id, LPSTR friendly_name, bool isDefault ) :
        m_id( id ),
        m_friendly_name( friendly_name ),
        m_isDefault( isDefault )
    {}
};

typedef std::vector<AudioRenderDevice> AudioRenderDeviceArray;

class AudioOutputStream : public Threadable
{
    CStringW					m_endpoint_id;				// Device endpoint ID
    WAVEFORMATEX          		m_format;

    UINT32						m_bufferFrameCount;
    IMMDeviceEnumerator *		m_pEnumerator;
    IMMDevice *					m_pDevice;
    IAudioClient *				m_pAudioClient;
    IAudioRenderClient *		m_pRenderClient;
    WAVEFORMATEX *				m_pwfx;

    CMutex						m_buffer_mutex;
    CEvent                      m_play_event;
    CEvent                      m_play_wait;
    AudioFrameBuffer            m_ring_buffer;
    bool                        m_playing;
    bool                        m_paused;

    UINT run(void);

public:
    AudioOutputStream( LPCWSTR endpoint_id=NULL );
    virtual ~AudioOutputStream(void);

    bool addSamples( UINT32 frames, UINT32 channels, UINT32 sample_rate, LPBYTE pData );

    WAVEFORMATEX getFormat( ) const {
        return m_format;
    }

    unsigned getSamplesPerSecond() const {
        return m_format.nSamplesPerSec;
    }

    UINT getCachedSamples(void) const {
        return m_ring_buffer.size();
    }

    void setPaused( bool paused ) {
        if ( m_paused != paused ) {
            m_paused = paused;
            if ( m_paused )
                m_pAudioClient->Stop();
            else
                m_pAudioClient->Start();
        }
    }

    bool isPlaying() const {
        return m_playing;
    }

    bool isPaused() const {
        return m_paused;
    }

    void cancel(void);

    virtual HRESULT openAudioStream( WAVEFORMATEX* format );
    virtual HRESULT closeAudioStream();

    static void collectAudioRenderDevices();
    static AudioRenderDeviceArray audioRenderDevices;

    static AudioOutputStream* createAudioStream( LPCSTR render_device=NULL );
    static void releaseAudioStream( AudioOutputStream* audio_stream );

private:
    HRESULT releaseResources();

    HRESULT playAudioStream();
    UINT32 fillBuffer( UINT32 numFramesAvailable, LPBYTE pData );

    LPCSTR getEndpointDeviceName( LPCWSTR endpoint_id );
};

