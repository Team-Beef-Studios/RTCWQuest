/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein single player GPL Source Code (RTCW SP Source Code).  

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW SP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW SP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "client.h"
#include "../../../RTCWVR/VrClientInfo.h"

extern vr_client_info_t vr;

qboolean scr_initialized;           // ready to draw

cvar_t      *cl_timegraph;
cvar_t      *cl_debuggraph;
cvar_t      *cl_graphheight;
cvar_t      *cl_graphscale;
cvar_t      *cl_graphshift;

/*
================
SCR_DrawNamedPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawNamedPic( float x, float y, float width, float height, const char *picname ) {
	qhandle_t hShader;

	assert( width != 0 );

	hShader = re.RegisterShader( picname );
	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
================
SCR_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
void SCR_AdjustFrom640( float *x, float *y, float *w, float *h ) {
	float xscale;
	float yscale;

#ifdef PANDORA
	// adjust for wide screens
	if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
//		*x += 0.5 * ( cls.glconfig.vidWidth - ( cls.glconfig.vidHeight * 640 / 480 ) );
	}
#endif

	// scale for screen sizes
	xscale = cls.glconfig.vidWidth / 640.0;
	yscale = cls.glconfig.vidHeight / 480.0;
/*	#ifdef PANDORA
	if (xscale>yscale)
		yscale=xscale;
	else
		xscale=yscale;
	#endif*/
	if ( x ) {
		*x *= xscale;
	}
	if ( y ) {
		*y *= yscale;
	}
	if ( w ) {
		*w *= xscale;
	}
	if ( h ) {
		*h *= yscale;
	}
}

/*
================
SCR_FillRect

Coordinates are 640*480 virtual values
=================
*/
void SCR_FillRect( float x, float y, float width, float height, const float *color ) {
	re.SetColor( color );

	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 0, 0, cls.whiteShader );

	re.SetColor( NULL );
}


/*
================
SCR_DrawPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawPic( float x, float y, float width, float height, qhandle_t hShader ) {
	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}



/*
** SCR_DrawChar
** chars are drawn at 640*480 virtual screen size
*/
static void SCR_DrawChar( int x, int y, float size, int ch ) {
	int row, col;
	float frow, fcol;
	float ax, ay, aw, ah;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -size ) {
		return;
	}

	ax = x;
	ay = y;
	aw = size;
	ah = size;
	SCR_AdjustFrom640( &ax, &ay, &aw, &ah );

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	size = 0.0625;

	re.DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow,
					   fcol + size, frow + size,
					   cls.charSetShader );
}

/*
** SCR_DrawSmallChar
** small chars are drawn at native screen resolution
*/
void SCR_DrawSmallChar( int x, int y, int ch ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -SMALLCHAR_HEIGHT ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	size = 0.0625;

	re.DrawStretchPic( x, y, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT,
					   fcol, frow,
					   fcol + size, frow + size,
					   cls.charSetShader );
}


/*
==================
SCR_DrawBigString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void SCR_DrawStringExt( int x, int y, float size, const char *string, float *setColor, qboolean forceColor ) {
	vec4_t color;
	const char  *s;
	int xx;

	// draw the drop shadow
	color[0] = color[1] = color[2] = 0;
	color[3] = setColor[3];
	re.SetColor( color );
	s = string;
	xx = x;
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
			continue;
		}
		SCR_DrawChar( xx + 2, y + 2, size, *s );
		xx += size;
		s++;
	}


	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				memcpy( color, g_color_table[ColorIndex( *( s + 1 ) )], sizeof( color ) );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			s += 2;
			continue;
		}
		SCR_DrawChar( xx, y, size, *s );
		xx += size;
		s++;
	}
	re.SetColor( NULL );
}


void SCR_DrawBigString( int x, int y, const char *s, float alpha ) {
	float color[4];

	color[0] = color[1] = color[2] = 1.0;
	color[3] = alpha;
	SCR_DrawStringExt( x, y, BIGCHAR_WIDTH, s, color, qfalse );
}

void SCR_DrawBigStringColor( int x, int y, const char *s, vec4_t color ) {
	SCR_DrawStringExt( x, y, BIGCHAR_WIDTH, s, color, qtrue );
}


/*
==================
SCR_DrawSmallString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void SCR_DrawSmallStringExt( int x, int y, const char *string, float *setColor, qboolean forceColor ) {
	vec4_t color;
	const char  *s;
	int xx;

	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				memcpy( color, g_color_table[ColorIndex( *( s + 1 ) )], sizeof( color ) );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			s += 2;
			continue;
		}
		SCR_DrawSmallChar( xx, y, *s );
		xx += SMALLCHAR_WIDTH;
		s++;
	}
	re.SetColor( NULL );
}



/*
** SCR_Strlen -- skips color escape codes
*/
static int SCR_Strlen( const char *str ) {
	const char *s = str;
	int count = 0;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}

	return count;
}

/*
** SCR_GetBigStringWidth
*/
int SCR_GetBigStringWidth( const char *str ) {
	return SCR_Strlen( str ) * 16;
}


//===============================================================================

/*
=================
SCR_DrawDemoRecording
=================
*/
void SCR_DrawDemoRecording( void ) {
	char string[1024];
	int pos;

	if ( !clc.demorecording ) {
		return;
	}

	pos = FS_FTell( clc.demofile );
	sprintf( string, "RECORDING %s: %ik", clc.demoName, pos / 1024 );

	SCR_DrawStringExt( 320 - strlen( string ) * 4, 20, 8, string, g_color_table[7], qtrue );
}


/*
===============================================================================

DEBUG GRAPH

===============================================================================
*/

typedef struct
{
	float value;
	int color;
} graphsamp_t;

static int current;
static graphsamp_t values[1024];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph( float value, int color ) {
	values[current & 1023].value = value;
	values[current & 1023].color = color;
	current++;
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph( void ) {
	int a, x, y, w, i, h;
	float v;
	int color;

	//
	// draw the graph
	//
	w = cls.glconfig.vidWidth;
	x = 0;
	y = cls.glconfig.vidHeight;
	re.SetColor( g_color_table[0] );
	re.DrawStretchPic( x, y - cl_graphheight->integer,
					   w, cl_graphheight->integer, 0, 0, 0, 0, cls.whiteShader );
	re.SetColor( NULL );

	for ( a = 0 ; a < w ; a++ )
	{
		i = ( current - 1 - a + 1024 ) & 1023;
		v = values[i].value;
		color = values[i].color;
		v = v * cl_graphscale->integer + cl_graphshift->integer;

		if ( v < 0 ) {
			v += cl_graphheight->integer * ( 1 + (int)( -v / cl_graphheight->integer ) );
		}
		h = (int)v % cl_graphheight->integer;
		re.DrawStretchPic( x + w - 1 - a, y - h, 1, h, 0, 0, 0, 0, cls.whiteShader );
	}
}

//=============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init( void ) {
	cl_timegraph = Cvar_Get( "timegraph", "0", CVAR_CHEAT );
	cl_debuggraph = Cvar_Get( "debuggraph", "0", CVAR_CHEAT );
	cl_graphheight = Cvar_Get( "graphheight", "32", CVAR_CHEAT );
	cl_graphscale = Cvar_Get( "graphscale", "1", CVAR_CHEAT );
	cl_graphshift = Cvar_Get( "graphshift", "0", CVAR_CHEAT );

	scr_initialized = qtrue;
}


//=======================================================

/*
==================
SCR_DrawScreenField

This will be called twice if rendering in stereo mode
==================
*/
void SCR_DrawScreenField( stereoFrame_t stereoFrame ) {
	re.BeginFrame( stereoFrame );

	// wide aspect ratio screens need to have the sides cleared
	// unless they are displaying game renderings
	// SEB: commented, since this break cinematics
	if ( cls.state != CA_ACTIVE ) {
/*		if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
			re.SetColor( g_color_table[0] );
			re.DrawStretchPic( 0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight, 0, 0, 0, 0, cls.whiteShader );
			re.SetColor( NULL );
		}*/
	}

	if ( !uivm ) {
		Com_DPrintf( "draw screen without UI loaded\n" );
		return;
	}

	// if the menu is going to cover the entire screen, we
	// don't need to render anything under it
	if ( !VM_Call( uivm, UI_IS_FULLSCREEN ) ) {
		switch ( cls.state ) {
		default:
			Com_Error( ERR_FATAL, "SCR_DrawScreenField: bad cls.state" );
			break;
		case CA_CINEMATIC:
			SCR_DrawCinematic();
			break;
		case CA_DISCONNECTED:
			// force menu up
			S_StopAllSounds();
			VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
			break;
		case CA_CONNECTING:
		case CA_CHALLENGING:
		case CA_CONNECTED:
			// connecting clients will only show the connection dialog
			// refresh to update the time
			VM_Call( uivm, UI_REFRESH, cls.realtime );
			VM_Call( uivm, UI_DRAW_CONNECT_SCREEN, qfalse );
			break;
//			// Ridah, if the cgame is valid, fall through to there
//			if (!cls.cgameStarted || !com_sv_running->integer) {
//				// connecting clients will only show the connection dialog
//				VM_Call( uivm, UI_DRAW_CONNECT_SCREEN, qfalse );
//				break;
//			}
		case CA_LOADING:
		case CA_PRIMED:
            CL_CGameSetVRClientInfo();
			// draw the game information screen and loading progress
			CL_CGameRendering( stereoFrame );

			// also draw the connection information, so it doesn't
			// flash away too briefly on local or lan games
			//if (!com_sv_running->value || Cvar_VariableIntegerValue("sv_cheats"))	// Ridah, don't draw useless text if not in dev mode
			VM_Call( uivm, UI_REFRESH, cls.realtime );
			VM_Call( uivm, UI_DRAW_CONNECT_SCREEN, qtrue );
			break;
		case CA_ACTIVE:
            CL_CGameSetVRClientInfo();
			CL_CGameRendering( stereoFrame );
			SCR_DrawDemoRecording();
			break;
		}
	}

	// the menu draws next
	if ( cls.keyCatchers & KEYCATCH_UI && uivm ) {
		VM_Call( uivm, UI_REFRESH, cls.realtime );
	}

	// console draws next
	Con_DrawConsole();

	// debug graph can be drawn on top of anything
	if ( cl_debuggraph->integer || cl_timegraph->integer || cl_debugMove->integer ) {
		SCR_DrawDebugGraph();
	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/

void RTCWVR_FrameSetup();
void RTCWVR_prepareEyeBuffer(int eye );
void RTCWVR_finishEyeBuffer(int eye );
qboolean RTCWVR_useScreenLayer();
void RTCWVR_processHaptics();
void RTCWVR_getHMDOrientation();
qboolean RTCWVR_processMessageQueue();
void RTCWVR_getTrackedRemotesOrientation();
void GPUDropSync();

static void SCR_DrawDirectStereoFrame( void ) {
	RTCWVR_prepareEyeBuffer( 0 );
	SCR_DrawScreenField( STEREO_LEFT );

	if ( com_speeds->integer ) {
		re.EndFrame( STEREO_LEFT, &time_frontend, &time_backend );
	} else {
		re.EndFrame( STEREO_LEFT, NULL, NULL );
	}

	RTCWVR_finishEyeBuffer( 0 );

	RTCWVR_prepareEyeBuffer( 1 );
	SCR_DrawScreenField( STEREO_RIGHT );

	if ( com_speeds->integer ) {
		re.EndFrame( STEREO_RIGHT, &time_frontend, &time_backend );
	} else {
		re.EndFrame( STEREO_RIGHT, NULL, NULL );
	}

	RTCWVR_finishEyeBuffer( 1 );
}

void SCR_UpdateScreen( void ) {
	static int recursive;
	static cvar_t *vr_stereoReplay;
	static qboolean replayFailureLogged;

	if ( !scr_initialized ) {
		return;             // not initialized yet
	}

	if ( ++recursive > 2 ) {
		Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
	}
	recursive = 1;

	RTCWVR_FrameSetup();

	RTCWVR_processMessageQueue();

	//Get controller state here
	RTCWVR_getHMDOrientation();
	RTCWVR_getTrackedRemotesOrientation();

	RTCWVR_processHaptics();

	if ( RTCWVR_useScreenLayer() ) {
		RTCWVR_prepareEyeBuffer( 0 );
		SCR_DrawScreenField( STEREO_CENTER );

		if ( com_speeds->integer ) {
			re.EndFrame( STEREO_CENTER, &time_frontend, &time_backend );
		} else {
			re.EndFrame( STEREO_CENTER, NULL, NULL );
		}

		RTCWVR_finishEyeBuffer( 0 );
	} else if ( ( vr.binocularsActive || vr.scopeengaged || vr.cgzoommode ) ||
				!( vr_stereoReplay ? vr_stereoReplay->integer : ( vr_stereoReplay = Cvar_Get( "vr_stereoReplay", "0", CVAR_ARCHIVE ) )->integer ) ) {
		SCR_DrawDirectStereoFrame();
	} else {
		int eye;
		qboolean replayOk = qtrue;

		if ( !re.VR_BeginStereoReplayCapture || !re.VR_ReplayStereoFrame ||
			 !re.VR_BeginStereoReplayCapture() ) {
			if ( !replayFailureLogged ) {
				Com_Printf( "VR stereo replay unavailable; falling back to direct stereo.\n" );
				replayFailureLogged = qtrue;
			}
			SCR_DrawDirectStereoFrame();
			goto submitFrame;
		}

		vr.eye = 0;
		vr.off_center_fov_x[0] = vr.off_center_fov_x[1] = 0.0f;
		vr.off_center_fov_y[0] = vr.off_center_fov_y[1] = 0.0f;

		SCR_DrawScreenField( STEREO_CENTER );

		for ( eye = 0; eye < 2; eye++ ) {
			RTCWVR_prepareEyeBuffer( eye );
			if ( !re.VR_ReplayStereoFrame( eye == 0 ? STEREO_LEFT : STEREO_RIGHT, eye == 1 ) ) {
				if ( re.VR_CancelStereoReplayCapture ) {
					re.VR_CancelStereoReplayCapture();
				}
				RTCWVR_finishEyeBuffer( eye );
				replayOk = qfalse;
				if ( !replayFailureLogged ) {
					Com_Printf( "VR stereo replay failed; falling back to direct stereo on subsequent frames.\n" );
					replayFailureLogged = qtrue;
				}
				Cvar_Set( "vr_stereoReplay", "0" );
				break;
			}
			RTCWVR_finishEyeBuffer( eye );
		}

		if ( !replayOk ) {
			goto submitFrame;
		}
	}

	//And we're done
submitFrame:
	re.SubmitStereoFrame();

	recursive = 0;
}
