#pragma once

class TrackAnalyzer
{

    UINT                        m_numChannels;

    // Audio sample storage 
    int16_t*					m_sample_left;
    int16_t*					m_sample_right;
    unsigned					m_sample_size;
    unsigned					m_data_index;

    AnalyzeInfo*                m_analyze_info;
    UINT                        m_sample_index;

public:
    TrackAnalyzer( WAVEFORMATEX* format, DWORD track_length_ms, LPCSTR spotify_link );
    ~TrackAnalyzer();

    HRESULT addData( UINT32 numFramesAvailable, BYTE *pData  );
    HRESULT finishData();

    inline AnalyzeInfo* captureAnalyzerData() {
        AnalyzeInfo* value = m_analyze_info;
        m_analyze_info = NULL;
        return value;
    }

private:
    void applyWindowFunction( unsigned m_sample_size, int16_t* sampleSet );
    HRESULT processAmplitudes( size_t sample_size, int16_t* sample_data[] );
};

