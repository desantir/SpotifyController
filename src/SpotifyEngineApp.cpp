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
#include "SpotifyEngineApp.h"
#include "AudioOutputStream.h"

// CSpotifyEngineApp

BEGIN_MESSAGE_MAP(CSpotifyEngineApp, CWinApp)
END_MESSAGE_MAP()

// The one and only CSpotifyEngineApp object
CSpotifyEngineApp theApp;

// ----------------------------------------------------------------------------
//
CSpotifyEngineApp::CSpotifyEngineApp() :
    m_hLog( NULL )
{
	CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );

    openLogFile( );

    log_status( "DMXStudio Spotify Music Controller version 0.0.1 started" );
}

// ----------------------------------------------------------------------------
//
CSpotifyEngineApp::~CSpotifyEngineApp()
{
    log_status( "DMXStudio Spotify Music Controller stopped" );

    closeLogFile( );

    CoUninitialize();
}

// ----------------------------------------------------------------------------
// CSpotifyEngineApp initialization
//
BOOL CSpotifyEngineApp::InitInstance()
{
	CWinApp::InitInstance();
    
    AudioOutputStream::collectAudioRenderDevices();

	return TRUE;
}

// ----------------------------------------------------------------------------
//
CString getUserDocumentDirectory()
{
	char input_file[MAX_PATH]; 
	HRESULT result = SHGetFolderPath(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, input_file); 
	if ( result != S_OK )
		throw StudioException( "Error %d finding document directory", result );
    return CString( input_file );
}

// ----------------------------------------------------------------------------
//
void CSpotifyEngineApp::openLogFile( )
{
	CString filename;
	filename.Format( "%s\\DMXStudio\\SpotifyMusicController.log", getUserDocumentDirectory() );

    m_hLog = _fsopen( filename, "at", _SH_DENYWR );
    fputs( "\n", m_hLog );
}

// ----------------------------------------------------------------------------
//
void CSpotifyEngineApp::closeLogFile( )
{
    if ( m_hLog != NULL ) {
        fflush( m_hLog );
        fclose( m_hLog );
        m_hLog = NULL;
    }
}

// ----------------------------------------------------------------------------
//
void log( std::exception& ex ) {
	CString output;
	output.Format( "EXCEPTION: %s", ex.what() );

	log( output );
}

// ----------------------------------------------------------------------------
//
void log( StudioException& ex ) {
	CString output;

	if ( strlen( ex.getFile() ) > 0 )
		output.Format( "EXCEPTION: %s (%s:%ld)", ex.what(), ex.getFile(), ex.getLine() );
	else
		output.Format( "EXCEPTION: %s", ex.what() );

	log( output );
}

// ----------------------------------------------------------------------------
//
void log_status( const char *fmt, ... ) {
	va_list list;
	va_start( list, fmt );

	CString output( "STATUS: " );
	output.AppendFormatV( fmt, list );

	log( output );

	va_end( list );
}

// ----------------------------------------------------------------------------
//
void log( const char *fmt, ... ) {
	va_list list;
	va_start( list, fmt );

    if ( theApp.m_hLog ) {
        time_t rawtime;
        struct tm timeinfo;
        char buffer[80];

        time ( &rawtime );
        localtime_s( &timeinfo, &rawtime );

        strftime( buffer, sizeof(buffer), "[%x %X] ", &timeinfo );

	    CString output;
        output = buffer;
	    output.AppendFormatV( fmt, list );
	    output.Append( "\n" );

        // fputs should be a single atomic operation (i.e. no log collisions)
        fputs( output, theApp.m_hLog );
        fflush( theApp.m_hLog );
    }

	va_end( list );
}
