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
#include "Threadable.h"

// ----------------------------------------------------------------------------
//
Threadable::Threadable(void) :
	m_running( false ),
	m_thread( NULL )
{
}

// ----------------------------------------------------------------------------
//
Threadable::~Threadable(void)
{
	if ( m_running )
		stopThread();
}

// ----------------------------------------------------------------------------
//
bool Threadable::startThread( ) {
	if ( m_running )
		return false;

	// Start thread
	m_thread = AfxBeginThread( _run, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED );
	if ( !m_thread ) {
		printf( "Thread failed to start\n" );
		m_running = false;
		return false;
	}

	m_thread->m_bAutoDelete = false;
	m_thread->ResumeThread();

	return true;
}

// ----------------------------------------------------------------------------
//
bool Threadable::stopThread() {
	if ( !m_running || !m_thread )
		return true;

	DWORD exit_code = 0;
	if ( GetExitCodeThread( m_thread->m_hThread, &exit_code ) && exit_code != STILL_ACTIVE ) {
		m_running = false;
		printf( "Thread premature exit (code %lx)\n", exit_code );
	}
	else {
		// Stop the thread
		m_running = false;

		// Wait for thread to stop
		DWORD status = ::WaitForSingleObject( m_thread->m_hThread, 5000 );

		if ( status == WAIT_FAILED )
			printf( "Thread failed to stop\n" );
		else
			delete m_thread;
	}

	m_thread = NULL;

	return true;
}

// ----------------------------------------------------------------------------
// Main thread loop
//
UINT __cdecl _run( LPVOID object )
{
	Threadable* t = reinterpret_cast<Threadable *>(object);
	t->m_running = true;
	return t->run();
}