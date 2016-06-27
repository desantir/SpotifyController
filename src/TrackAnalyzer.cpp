#include "stdafx.h"
#include "TrackAnalyzer.h"

#define _USE_MATH_DEFINES
#include <math.h>

// ----------------------------------------------------------------------------
//
TrackAnalyzer::TrackAnalyzer( WAVEFORMATEX* format, DWORD track_length_ms, LPCSTR spotify_link ) :
    m_data_index( 0 ),
    m_sample_index( 0 )
{
    static const UINT SAMPLE_MS = 500;           // Sample every 500ms

    m_numChannels = format->nChannels;

    m_sample_size = (format->nSamplesPerSec * SAMPLE_MS) / 1000;            

    m_sample_left = new int16_t[m_sample_size];
    m_sample_right = new int16_t[m_sample_size];

    UINT amplitude_samples = (track_length_ms+SAMPLE_MS-1) / SAMPLE_MS;

    m_analyze_info = (AnalyzeInfo*)calloc( sizeof(AnalyzeInfo) + (sizeof(uint16_t) * amplitude_samples), 1 );

    m_analyze_info->data_count = amplitude_samples;
    m_analyze_info->duration_ms = SAMPLE_MS;

    strncpy_s( m_analyze_info->link, spotify_link, sizeof(m_analyze_info->link) );

    log_status( "Analyzing track '%s'", (LPCSTR)spotify_link );
}

// ----------------------------------------------------------------------------
//
TrackAnalyzer::~TrackAnalyzer()
{
    if ( m_sample_left )
        delete m_sample_left;
    if ( m_sample_right )
        delete m_sample_right;
    if ( m_analyze_info ) 
        free( m_analyze_info );

    m_sample_left = m_sample_right = NULL;
    m_analyze_info = NULL;
}

// ----------------------------------------------------------------------------
//
HRESULT TrackAnalyzer::addData(  UINT32 numFramesAvailable, BYTE *pData )
{
    int16_t* data = reinterpret_cast<int16_t *>(pData);

    for ( unsigned frame=0; frame < numFramesAvailable; frame++ ) {
        if ( data ) {
            m_sample_left[m_data_index] = *data++;

            if ( m_numChannels > 1 ) 
                m_sample_right[m_data_index] = *data++;
            else
                m_sample_right[m_data_index] = 0;

            if ( m_numChannels > 2 )				// Skip any other channels
                data = &data[m_numChannels-2];
        }
        else {
            m_sample_left[m_data_index] = m_sample_right[m_data_index] = 0;
        }

        m_data_index++;

        if ( m_data_index >= m_sample_size ) {
            applyWindowFunction( m_sample_size, m_sample_left );

            if (m_numChannels > 1 ) {
                applyWindowFunction( m_sample_size, m_sample_right );
            }

            int16_t* sample_data[] = { m_sample_left, m_sample_right };

            processAmplitudes( m_sample_size, sample_data );

            m_data_index = 0;
        }
    }

    return 0;
};

// ----------------------------------------------------------------------------
//
HRESULT TrackAnalyzer::finishData()
{
    if ( m_data_index > 0 ) {
        applyWindowFunction( m_data_index, m_sample_left );

        if ( m_numChannels > 1 ) {
            applyWindowFunction( m_data_index, m_sample_right );
        }

        int16_t* sample_data[] = { m_sample_left, m_sample_right };

        processAmplitudes( m_data_index, sample_data );

        m_data_index = 0;
    }

    return 0;
}

// ----------------------------------------------------------------------------
//
HRESULT TrackAnalyzer::processAmplitudes( size_t sample_size, int16_t* sample_data[] ) {
    // Determine peak amplitude for both channels
    int16_t amplitude = 0;
    for ( size_t i=0; i < sample_size; i++ ) {
        int16_t sample = abs(sample_data[LEFT_CHANNEL][i]);
        if ( sample > amplitude )
            amplitude = sample;

        if ( m_numChannels > 1 ) {
            int16_t sample = abs(sample_data[RIGHT_CHANNEL][i]);
            if ( sample > amplitude )
                amplitude = sample;
        }
    }

    if ( m_sample_index < m_analyze_info->data_count )
        m_analyze_info->data[ m_sample_index++ ] = amplitude;

    return 0;
}

// ----------------------------------------------------------------------------
// Apply Hann window function
//
void TrackAnalyzer::applyWindowFunction( unsigned m_sample_size, int16_t* sampleSet )
{	
    static const double TWO_PI = M_PI * 2.0;

    for ( unsigned bin=0; bin < m_sample_size; bin++ ) {
        sampleSet[bin] = (int16_t)( (float)sampleSet[bin] * 0.5f * (1.0F - (float) cos(TWO_PI * bin / (m_sample_size - 1.0F))) );
    }
}
