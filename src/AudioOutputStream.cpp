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

#include "stdafx.h"
#include "AudioOutputStream.h"
#include "Functiondiscoverykeys_devpkey.h"

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

AudioRenderDeviceArray AudioOutputStream::audioRenderDevices;

#define AUDIO_OUTPUT_ASSERT( hr, ... ) \
    if ( !SUCCEEDED(hr) ) { \
        CString message( "Audio output stream " ); \
        message.AppendFormat( __VA_ARGS__ ); \
        message.AppendFormat( " (0x%lx)", hr ); \
        throw StudioException( __FILE__, __LINE__, (LPCSTR)message ); \
    }

// ----------------------------------------------------------------------------
//
AudioOutputStream* AudioOutputStream::createAudioStream( LPCSTR render_device )
{
    AudioOutputStream* audio_stream;

    LPCWSTR endpoint_id = NULL;
    bool isDefault = ( render_device == NULL || strlen(render_device) == 0 || StrCmpI( render_device, "default" ) == 0 );

    for ( AudioRenderDeviceArray::iterator it=audioRenderDevices.begin();
            it != audioRenderDevices.end(); it++ ) {

        if ( (isDefault && (*it).m_isDefault) ||
                (!isDefault && _stricmp( render_device, (*it).m_friendly_name ) == 0) ) {
            endpoint_id = (LPCWSTR)((*it).m_id);
            break;
        }
    }

    STUDIO_ASSERT( endpoint_id != NULL, "Cannot start unknown audio render device [%s]", render_device );

    audio_stream = new AudioOutputStream( endpoint_id );

    return audio_stream;
}

// ----------------------------------------------------------------------------
//
void AudioOutputStream::releaseAudioStream( AudioOutputStream* audio_stream )
{
    if ( audio_stream ) {
        audio_stream->closeAudioStream();
        delete audio_stream;
    }
}

// ----------------------------------------------------------------------------
//
AudioOutputStream::AudioOutputStream( LPCWSTR endpoint_id ) :
    m_endpoint_id( endpoint_id ),
    m_pEnumerator( NULL ),
    m_pDevice( NULL ),
    m_pAudioClient( NULL ),
    m_pRenderClient( NULL ),
    m_pwfx( NULL )
{
    memset( &m_format, 0, sizeof(WAVEFORMAT) );
}

// ----------------------------------------------------------------------------
//
AudioOutputStream::~AudioOutputStream(void)
{
    closeAudioStream();
}

//-----------------------------------------------------------
//
HRESULT AudioOutputStream::openAudioStream( WAVEFORMATEX* format )
{
    HRESULT hr;
    REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
    LPCSTR audio_render_device = getEndpointDeviceName( m_endpoint_id );
    
    memcpy( &m_format, format, sizeof(WAVEFORMATEX) );

    hr = CoCreateInstance(
           CLSID_MMDeviceEnumerator, NULL,
           CLSCTX_ALL, IID_IMMDeviceEnumerator,
           (void**)&m_pEnumerator);
    AUDIO_OUTPUT_ASSERT( hr, "Cannot create COM device enumerator instance" );

    hr = m_pEnumerator->GetDevice( m_endpoint_id, &m_pDevice );
    AUDIO_OUTPUT_ASSERT( hr, "GetDevice %S failed", m_endpoint_id );

    hr = m_pDevice->Activate(
                    IID_IAudioClient, CLSCTX_ALL,
                    NULL, (void**)&m_pAudioClient);
    AUDIO_OUTPUT_ASSERT( hr, "Activate failed" );

    hr = m_pAudioClient->GetMixFormat(&m_pwfx);                 // This is what the render device can do
    AUDIO_OUTPUT_ASSERT( hr, "GetMixFormat failed" );

    log_status( "Audio output stream %s internal format [%d %d-bit channel(s) @ %dHz, format %X]", 
        audio_render_device, m_pwfx->nChannels,  m_pwfx->wBitsPerSample, m_pwfx->nSamplesPerSec, m_pwfx->wFormatTag );

    hr = m_pAudioClient->Initialize(
                         AUDCLNT_SHAREMODE_SHARED,
                         0,
                         hnsRequestedDuration,
                         0,
                         &m_format,
                         NULL);

    if ( AUDCLNT_E_UNSUPPORTED_FORMAT == hr ) {
        AUDIO_OUTPUT_ASSERT( hr, "%s does not support [%d %d-bit channel(s) @ %dHz, format %X]", 
                                 audio_render_device, m_format.nChannels, m_format.wBitsPerSample, 
                                 m_format.nSamplesPerSec, m_format.wFormatTag );
    }
    AUDIO_OUTPUT_ASSERT( hr, "Initialize failed" );

    // Get the size of the allocated buffer.
    hr = m_pAudioClient->GetBufferSize( &m_bufferFrameCount );
    AUDIO_OUTPUT_ASSERT( hr, "GetBufferSize failed" );

    hr = m_pAudioClient->GetService(
                         IID_IAudioRenderClient,
                         (void**)&m_pRenderClient);

    AUDIO_OUTPUT_ASSERT( hr, "GetService failed" );

    bool started = startThread();

    STUDIO_ASSERT( started, "Audio output stream cannot start thread" );

    return 0;
}

// ----------------------------------------------------------------------------
//
HRESULT AudioOutputStream::closeAudioStream()
{
    stopThread();

    if ( m_pwfx ) {
        CoTaskMemFree(m_pwfx);
        m_pwfx = NULL;
    }

    SAFE_RELEASE(m_pEnumerator)
    SAFE_RELEASE(m_pDevice)
    SAFE_RELEASE(m_pAudioClient)
    SAFE_RELEASE(m_pRenderClient)

    return 0;
}

// ----------------------------------------------------------------------------
//
LPCSTR AudioOutputStream::getEndpointDeviceName( LPCWSTR endpoint_id )
{
    for ( AudioRenderDeviceArray::iterator it=audioRenderDevices.begin();
            it != audioRenderDevices.end(); it++ ) {
        if ( wcscmp( endpoint_id, (*it).m_id ) == 0 )
            return (*it).m_friendly_name;
    }

    return "UNKNOWN";
}

// ----------------------------------------------------------------------------
//
UINT AudioOutputStream::run(void) 
{
    LPCSTR audio_render_device = getEndpointDeviceName( m_endpoint_id );

    log_status( "Audio stream started [%s, %d %d-bit channel(s) @ %dHz, format %X]", 
        audio_render_device, m_format.nChannels, m_format.wBitsPerSample, m_format.nSamplesPerSec, m_format.wFormatTag );

    try {
        while ( isRunning() ) {
            if ( m_ring_buffer.size() > 0 || ::WaitForSingleObject( m_play_event, 100 ) == WAIT_OBJECT_0 ) {
                playAudioStream( );
                m_play_event.ResetEvent();
            }
        }
    }
    catch ( std::exception& ex ) {
        log( ex );
        return -1;
    }

    log_status( "Audio stream stopped" );

    return 0;
}

// ----------------------------------------------------------------------------
//
HRESULT AudioOutputStream::playAudioStream( )
{
    HRESULT hr;
    REFERENCE_TIME hnsActualDuration;

    // Calculate the actual duration of the allocated buffer.
    hnsActualDuration = (REFERENCE_TIME)((double)REFTIMES_PER_SEC *
                                            m_bufferFrameCount / m_pwfx->nSamplesPerSec );

    hr = m_pAudioClient->Start(); 
    AUDIO_OUTPUT_ASSERT( hr, "Start failed" );

    m_playing = true;
    m_paused = false;

    // Each loop fills about half of the shared buffer.
    while ( isRunning() && m_playing )
    {
        UINT32 framesCopied;

        if ( !m_paused ) {
            UINT32 numFramesPadding;
            BYTE *pData;

            // See how much buffer space is available.
            hr = m_pAudioClient->GetCurrentPadding( &numFramesPadding );
            AUDIO_OUTPUT_ASSERT( hr, "GetCurrentPadding failed" );

            UINT32 numFramesAvailable = m_bufferFrameCount - numFramesPadding;

            // Grab all the available space in the shared buffer.
            hr = m_pRenderClient->GetBuffer( numFramesAvailable, &pData );
            AUDIO_OUTPUT_ASSERT( hr, "GetBuffer failed" );

            framesCopied = fillBuffer( numFramesAvailable, pData );

            hr = m_pRenderClient->ReleaseBuffer( framesCopied, 0 );
            AUDIO_OUTPUT_ASSERT( hr, "ReleaseBuffer failed" );
        }
        else
            framesCopied = 1;       // To stop the exit

        // Sleep for half the buffer duration.
        ::WaitForSingleObject( m_play_wait, (DWORD)(hnsActualDuration/REFTIMES_PER_MILLISEC/2) );

        if ( framesCopied == 0 )        // We are done
            break;
    }
    
    if ( !m_paused ) {
        hr = m_pAudioClient->Stop();
        AUDIO_OUTPUT_ASSERT( hr, "Stop failed" );
    }

    hr = m_pAudioClient->Reset();  // Reset stream's clock & buffers
    AUDIO_OUTPUT_ASSERT( hr, "Reset failed" );

    m_paused = m_playing = false;

    return hr;
}

// ----------------------------------------------------------------------------
//
void AudioOutputStream::cancel() 
{
    CSingleLock lock( &m_buffer_mutex, TRUE );

    m_playing = false;
    m_ring_buffer.reset();
    m_play_wait.SetEvent();
}

// ----------------------------------------------------------------------------
//
UINT32 AudioOutputStream::fillBuffer( UINT32 numFramesAvailable, LPBYTE pData )
{
    CSingleLock lock( &m_buffer_mutex, TRUE );

    UINT32 framesCopied = m_ring_buffer.read( numFramesAvailable, pData );

    // printf( "framesCopied=%d\n", framesCopied );

    return framesCopied;
}

// ----------------------------------------------------------------------------
//
bool AudioOutputStream::addSamples( UINT32 frames, UINT32 channels, UINT32 sample_rate, LPBYTE pData )
{
    CSingleLock lock( &m_buffer_mutex, TRUE );
    
    // TODO VERIFY SAMPLE FORMAT

    bool success = m_ring_buffer.write( frames, pData );

    if ( success ) 
        m_play_event.SetEvent();
    return success;
}

// ----------------------------------------------------------------------------
// Collect audio render devices
//
void AudioOutputStream::collectAudioRenderDevices( )
{
    HRESULT hr;
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDeviceCollection *pDevices = NULL;
    IMMDevice *pDevice = NULL;
    IPropertyStore *pProperties = NULL;
    LPWSTR pstrId = NULL;
    LPWSTR pstrDefaultId = NULL;
    UINT cDevices = 0;

    audioRenderDevices.clear();

    try {
        hr = CoCreateInstance(
               CLSID_MMDeviceEnumerator, NULL,
               CLSCTX_ALL, IID_IMMDeviceEnumerator,
               (void**)&pEnumerator);
        AUDIO_OUTPUT_ASSERT( hr, "Cannot create COM device enumerator instance" );

        hr = pEnumerator->EnumAudioEndpoints( eRender, DEVICE_STATE_ACTIVE, &pDevices );
        AUDIO_OUTPUT_ASSERT( hr, "Cannot enumerate audio devices" );

        // Get the default audio endpoint (if we don't get one its not an error)
        hr = pEnumerator->GetDefaultAudioEndpoint( eRender, eConsole, &pDevice );
        if ( SUCCEEDED(hr) ) {
            pDevice->GetId( &pstrDefaultId );
            SAFE_RELEASE( pDevice );
        }

        // Get count of audio capture devices
        hr = pDevices->GetCount( &cDevices );
        AUDIO_OUTPUT_ASSERT( hr, "Cannot get audio device count" );

        log_status( "Found %d audio capture devices", cDevices );

        // Suck up the render device names and default
        for ( UINT i=0; i < cDevices; i++ ) {
            hr = pDevices->Item( i, &pDevice );
            AUDIO_OUTPUT_ASSERT( hr, "Cannot get IMMDevice" );

            hr = pDevice->GetId( &pstrId );
            AUDIO_OUTPUT_ASSERT( hr, "Cannot get IMMDevice Id" );

            hr = pDevice->OpenPropertyStore( STGM_READ, &pProperties );
            AUDIO_OUTPUT_ASSERT( hr, "Cannot open IMMDevice property store" );

            PROPVARIANT varName;
            // Initialize container for property value.
            PropVariantInit(&varName);

            // Get the endpoint's friendly-name property.
            hr = pProperties->GetValue( PKEY_Device_DeviceDesc , &varName);
            AUDIO_OUTPUT_ASSERT( hr, "Cannot open IMMDevice name property" );

            bool isDefault = pstrDefaultId != NULL && wcscmp( pstrId, pstrDefaultId ) == 0;
            CW2A friendly_name( varName.pwszVal );

            audioRenderDevices.push_back( AudioRenderDevice( pstrId, friendly_name.m_psz, isDefault) ); 

            log_status( "Registering audio render device '%s'%s", 
                        friendly_name.m_psz, isDefault ? " [Default]" : "" );

            CoTaskMemFree( pstrId );
            pstrId = NULL;

            PropVariantClear(&varName);

            SAFE_RELEASE( pProperties );
            SAFE_RELEASE( pDevice );
        }

        SAFE_RELEASE( pDevices );
        SAFE_RELEASE( pEnumerator );

        CoTaskMemFree( pstrDefaultId );
    }
    catch ( ... ) {
        CoTaskMemFree( pstrDefaultId );
        CoTaskMemFree( pstrId );

        SAFE_RELEASE( pProperties );
        SAFE_RELEASE( pDevice );
        SAFE_RELEASE( pDevices );
        SAFE_RELEASE( pEnumerator );

        throw;
    }
}
