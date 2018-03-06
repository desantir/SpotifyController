/* 
Copyright (C) 2017 Robert DeSantis
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
#include "HttpUtils.h"

#include <atlutil.h>

static LPCWSTR accept_types[] = { L"*/*", NULL };
static LPCWSTR gAgentName = L"DMXSTUDIO";

// ----------------------------------------------------------------------------
//
DWORD httpGet( LPCWSTR server_name, LPCSTR url, LPCWSTR headers, BYTE **buffer, ULONG * buffer_size )
{
	bool  bResults = false;

	HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

	CStringW urlW( url );

	try {
		hSession = WinHttpOpen( gAgentName, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
		if ( !hSession )
			throw std::exception( "Unable to open internet session" );

		hConnect = WinHttpConnect( hSession, server_name, INTERNET_DEFAULT_HTTPS_PORT, 0);
		if ( !hConnect )
			throw std::exception( "Unable to connect session" );

		hRequest = WinHttpOpenRequest( hConnect, L"GET", urlW, NULL, WINHTTP_NO_REFERER, accept_types, WINHTTP_FLAG_SECURE );
		if ( !hRequest )
			throw std::exception( "Unable to open request" );

		DWORD dwOption = WINHTTP_DISABLE_AUTHENTICATION;

		if ( !WinHttpSetOption( hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &dwOption, sizeof(dwOption) ) )
			throw std::exception( "Error disabling automatic authentication" );

		if ( !WinHttpSendRequest( hRequest, headers, -1L, NULL, 0, 0, 0 ) ) 
			throw std::exception( "Error sending HTTP request" );

		if ( !WinHttpReceiveResponse( hRequest, NULL ) )
			throw std::exception( "Error waiting for HTTP response" );

		DWORD dwStatusCode = 0;
		DWORD dwSize = sizeof(dwStatusCode);

		WinHttpQueryHeaders( hRequest, 
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
			WINHTTP_HEADER_NAME_BY_INDEX, 
			&dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX );

		if ( dwStatusCode == HTTP_STATUS_OK ) {
			if ( !readBuffer( hRequest, buffer, buffer_size ) )
				throw std::exception( "Unable to read HTTP data" );
		}
		else
			buffer_size = 0L;

		if ( hSession )
			WinHttpCloseHandle( hSession );

		return dwStatusCode;
	}
	catch ( std::exception& ex ) {
		if ( hSession )
			WinHttpCloseHandle( hSession );

		throw StudioException( "%s (%ls %s CODE=%lu)", ex.what(), server_name, url, GetLastError() );
	}
	catch ( ... ) {
		if ( hSession )
			WinHttpCloseHandle( hSession );

		throw StudioException( "Unknown error connecting to %ls with URL %s", server_name, url );
	}
}

// ----------------------------------------------------------------------------
//
DWORD httpPost( LPCWSTR server_name, LPCSTR url, CString& body, LPCWSTR headers, BYTE **buffer, ULONG * buffer_size  )
{
	bool  bResults = false;

	HINTERNET  hSession = NULL, 
		hConnect = NULL,
		hRequest = NULL;

	CStringW urlW( url );

	try {
		hSession = WinHttpOpen( gAgentName, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
		if ( !hSession )
			throw std::exception( "Unable to open internet session" );

		hConnect = WinHttpConnect( hSession, server_name, INTERNET_DEFAULT_HTTPS_PORT, 0);
		if ( !hConnect )
			throw std::exception( "Unable to connect session" );

		hRequest = WinHttpOpenRequest( hConnect, L"POST", urlW, NULL, WINHTTP_NO_REFERER, accept_types, WINHTTP_FLAG_SECURE );
		if ( !hRequest )
			throw std::exception( "Unable to open request" );

		DWORD dwOption = WINHTTP_DISABLE_AUTHENTICATION;

		if ( !WinHttpSetOption( hRequest, WINHTTP_OPTION_DISABLE_FEATURE, &dwOption, sizeof(dwOption) ) )
			throw std::exception( "Error disabling automatic authentication" );

		if ( !WinHttpSendRequest(hRequest, headers, -1L, (LPVOID)(LPCSTR)body, body.GetLength(), body.GetLength(), 0) )
			throw std::exception( "Error sending HTTP request" );

		if ( !WinHttpReceiveResponse( hRequest, NULL ) )
			throw std::exception( "Error waiting for HTTP response" );

		DWORD dwStatusCode = 0;
		DWORD dwSize = sizeof(dwStatusCode);

		WinHttpQueryHeaders( hRequest, 
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
			WINHTTP_HEADER_NAME_BY_INDEX, 
			&dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX );

		if ( dwStatusCode == HTTP_STATUS_OK ) {
			if ( !readBuffer( hRequest, buffer, buffer_size ) )
				throw std::exception( "Unable to read HTTP data" );
		}
		else
			buffer_size = 0L;

		if ( hSession )
			WinHttpCloseHandle( hSession );

		return dwStatusCode;
	}
	catch ( std::exception& ex ) {
		if ( hSession )
			WinHttpCloseHandle( hSession );

		throw StudioException( "%s (%ls %s CODE=%lu)", ex.what(), server_name, url, GetLastError() );
	}
	catch ( ... ) {
		if ( hSession )
			WinHttpCloseHandle( hSession );

		throw StudioException( "Unknown error connecting to %ls with URL %s", server_name, url );
	}
}

// ----------------------------------------------------------------------------
//
CString encodeString( LPCSTR source )
{
	static LPCSTR hex = "0123456789ABCDEF";

	CString results;

	if ( source != NULL ) {
		while (*source ) {
			char c = *source++;

			if ( isalnum( (BYTE)c ) || c == '-' )
				results.AppendChar( c );
			else
				results.AppendFormat( "%%%c%c", hex[c >> 4 & 0x0F], hex[c & 0x0F] );
		}
	}

	return results;
}

// ----------------------------------------------------------------------------
//
CString unencodeString( LPCSTR source ) {
	CString result;

	DWORD len = strlen( source );

	LPSTR buffer = result.GetBufferSetLength( len*2 );
	AtlUnescapeUrl( source, buffer, &len, len*2 );

	result.ReleaseBufferSetLength( len );

	result.Replace( '+', ' ' );

	return result;
}

// ----------------------------------------------------------------------------
//
BOOL readBuffer( HINTERNET hRequest, BYTE **buffer, ULONG * buffer_size ) {

#define BUFFER_CHUNK 5000

	BYTE *data = (BYTE *)malloc( BUFFER_CHUNK+1 );
	ULONG data_size = 0L;

	while ( true ) {
		DWORD read;

		BOOL bResults = WinHttpReadData( hRequest, data+data_size, BUFFER_CHUNK, &read );
		if ( !bResults ) {
			free( data );
			return FALSE;
		}

		if ( read == 0 )
			break;

		data_size += read;

		data = (BYTE *)realloc( data, data_size + BUFFER_CHUNK+1 );
	}

	data = (BYTE *)realloc( data, data_size );

	*buffer = data;
	*buffer_size = data_size;

	return TRUE;    
}

// ----------------------------------------------------------------------------
//
size_t parseQuery( std::map<CString,CString>& parameters, LPCSTR raw_query ) {
	if ( raw_query == NULL )
		return 0;
	if ( raw_query[0] == '?' )		// Skip query marker if present
		raw_query++;

	CString query( raw_query );

	int position = 0;
	CString name = query.Tokenize( "=", position );
	char unescaped[4096];
	DWORD unescaped_size;

	while ( !name.IsEmpty( ) ) {
		CString value = query.Tokenize( "&", position );
		if ( !value.IsEmpty() && AtlUnescapeUrl( (LPCSTR)value, unescaped, &unescaped_size, sizeof(unescaped) ) )
			parameters[ name ] = unescaped;	

		name = query.Tokenize( "=", position );
	}

	return parameters.size();
}

// ----------------------------------------------------------------------------
//
BOOL encodeBase64( LPCSTR source, LPSTR target, LPINT target_len ) {
	BOOL result = Base64Encode( (BYTE *)source, strlen(source), target, target_len, ATL_BASE64_FLAG_NOCRLF );
	target[*target_len] = '\0';
	return result;
}
