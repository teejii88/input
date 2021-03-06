/**********************************************************************
 *
 * PROJECT:		Mylly Input library
 * FILE:		Input.c
 * LICENCE:		See Licence.txt
 * PURPOSE:		A portable input hooker library.
 *
 *				(c) Tuomo Jauhiainen 2012-13
 *
 **********************************************************************/

#include "Input.h"
#include "InputSys.h"
#include "Types/List.h"
#include "Platform/Alloc.h"
#include "Platform/Window.h"
#include <assert.h>

// --------------------------------------------------

static bool		input_initialized				= false;	// Is the library properly initialized?
static bool		block_keys						= false;	// Should keyboard input be blocked
bool			show_cursor						= true;		// Display mouse cursor
int16			mouse_x							= 0;		// Current mouse x coordinate
int16			mouse_y							= 0;		// Current mouse y coordinate
static list_t*	input_hooks[NUM_INPUT_EVENTS]	= { NULL };	// A list of custom input hooks
static list_t*	char_binds						= NULL;		// Character input binds
static list_t*	key_up_binds					= NULL;		// Key up hooks
static list_t*	key_down_binds					= NULL;		// Key down hooks
static list_t*	mouse_up_binds					= NULL;		// Mouse button up binds
static list_t*	mouse_down_binds				= NULL;		// Mouse button down binds
static list_t*	mouse_move_binds				= NULL;		// Mouse move binds

// --------------------------------------------------

// Input hook functions
typedef struct {
	node_t node;
	input_handler_t handler;
} InputHookFunc;

// Keyboard bind types
typedef enum {
	BIND_KEYUP,
	BIND_KEYDOWN,
	BIND_CHAR,
} BINDTYPE_KB;

// Mouse bind types
typedef enum {
	BIND_BTNUP,
	BIND_BTNDOWN,
	BIND_MOVE
} BINDTYPE_MOUSE;

// Keybind structure
struct KeyBind {
	node_t			node;
	BINDTYPE_KB		type;
	uint32			key;
	keybind_func_t	handler;
	void*			userdata;
};

// Mousebind structure
struct MouseBind {
	node_t				node;
	BINDTYPE_MOUSE		type;
	rectangle_t			bounds;
	MOUSEBTN			button;
	mousebind_func_t	handler;
	void*				userdata;
};

// --------------------------------------------------

void input_initialize( void* window )
{
	uint32 i;

	if ( !window ) return;

	// Initialize hook lists
	for ( i = NUM_INPUT_EVENTS; i--; )
		input_hooks[i] = list_create();

	// Initialize key/mouse binds
	char_binds = list_create();
	key_up_binds = list_create();
	key_down_binds = list_create();
	mouse_up_binds = list_create();
	mouse_down_binds = list_create();
	mouse_move_binds = list_create();

	// Do window system specific initializing (event hooks etc)
	input_platform_initialize( window );

	input_initialized = true;
}

static void input_cleanup_list( list_t* list )
{
	node_t *node, *tmp;

	list_foreach_safe( list, node, tmp )
	{
		list_remove( list, node );
		mem_free( node );
	}

	list_destroy( list );
}

void input_shutdown( void )
{
	uint32 i;

	if ( !input_initialized ) return;

	// Destroy input hook lists
	for ( i = NUM_INPUT_EVENTS; i--; )
	{
		if ( input_hooks[i] != NULL )
		{
			input_cleanup_list( input_hooks[i] );
			input_hooks[i] = NULL;
		}
	}

	// Destroy key/mouse binds
	input_cleanup_list( char_binds );
	input_cleanup_list( key_up_binds );
	input_cleanup_list( key_down_binds );
	input_cleanup_list( mouse_up_binds );
	input_cleanup_list( mouse_down_binds );
	input_cleanup_list( mouse_move_binds );

	char_binds = NULL;
	key_up_binds = NULL;
	key_down_binds = NULL;
	mouse_up_binds = NULL;
	mouse_down_binds = NULL;
	mouse_move_binds = NULL;

	// Do window system specific cleanup
	input_platform_shutdown();

	input_initialized = false;
}

void input_add_hook( INPUT_EVENT event_id, input_handler_t handler )
{
	InputHookFunc* hook;

	if ( !input_initialized ) return;
	if ( event_id >= NUM_INPUT_EVENTS ) return;

	hook = mem_alloc_clean( sizeof(*hook) );
	hook->handler = handler;

	list_push( input_hooks[event_id], &hook->node );
}

void input_remove_hook( INPUT_EVENT event_id, input_handler_t handler )
{
	node_t* node;
	InputHookFunc* hook;

	if ( !input_initialized ) return;
	if ( event_id >= NUM_INPUT_EVENTS ) return;

	list_foreach( input_hooks[event_id], node )
	{
		hook = (InputHookFunc*)node;
		if ( handler == hook->handler )
		{
			list_remove( input_hooks[event_id], node );
			mem_free( hook );

			return;
		}
	}
}

static KeyBind* input_add_key_bind( uint32 key, keybind_func_t func, void* data, BINDTYPE_KB type )
{
	KeyBind* bind;
	list_t* bindlist = NULL;

	if ( !input_initialized ) return NULL;

	switch ( type )
	{
	case BIND_CHAR: bindlist = char_binds; break;
	case BIND_KEYUP: bindlist = key_up_binds; break;
	case BIND_KEYDOWN: bindlist = key_down_binds; break;
	}

	if ( bindlist == NULL ) return NULL;

	bind = mem_alloc_clean( sizeof(*bind) );
	bind->type = type;
	bind->key = key;
	bind->handler = func;
	bind->userdata = data;

	list_push( bindlist, &bind->node );

	return bind;
}

KeyBind* input_add_char_bind( uint32 key, keybind_func_t func, void* data )
{
	return input_add_key_bind( key, func, data, BIND_CHAR );
}

KeyBind* input_add_key_up_bind( uint32 key, keybind_func_t func, void* data )
{
	return input_add_key_bind( key, func, data, BIND_KEYUP );
}

KeyBind* input_add_key_down_bind( uint32 key, keybind_func_t func, void* data )
{
	return input_add_key_bind( key, func, data, BIND_KEYDOWN );
}

static MouseBind* input_add_mouse_bind( MOUSEBTN button, rectangle_t* area, mousebind_func_t func, void* data, BINDTYPE_MOUSE type )
{
	MouseBind* bind;
	list_t* bindlist = NULL;

	if ( !input_initialized ) return NULL;

	switch ( type )
	{
		case BIND_MOVE: bindlist = mouse_move_binds; break;
		case BIND_BTNUP: bindlist = mouse_up_binds; break;
		case BIND_BTNDOWN: bindlist = mouse_down_binds; break;
	}

	if ( bindlist == NULL ) return NULL;

	bind = mem_alloc_clean( sizeof(*bind) );
	bind->type = type;
	bind->bounds = *area;
	bind->button = button;
	bind->handler = func;
	bind->userdata = data;

	list_push( bindlist, &bind->node );

	return bind;
}

MouseBind* input_add_mouse_move_bind( rectangle_t* area, mousebind_func_t func, void* data )
{
	return input_add_mouse_bind( MOUSE_NONE, area, func, data, BIND_MOVE );
}

MouseBind* input_add_mousebtn_up_bind( MOUSEBTN button, rectangle_t* area, mousebind_func_t func, void* data )
{
	return input_add_mouse_bind( button, area, func, data, BIND_BTNUP );
}

MouseBind* input_add_mousebtn_down_bind( MOUSEBTN button, rectangle_t* area, mousebind_func_t func, void* data )
{
	return input_add_mouse_bind( button, area, func, data, BIND_BTNDOWN );
}

static void input_remove_key_bind_from_list( uint32 key, keybind_func_t func, BINDTYPE_KB type )
{
	KeyBind* bind;
	node_t *node, *tmp;
	list_t* bindlist = NULL;

	if ( !input_initialized ) return;

	switch ( type )
	{
		case BIND_CHAR: bindlist = char_binds; break;
		case BIND_KEYUP: bindlist = key_up_binds; break;
		case BIND_KEYDOWN: bindlist = key_down_binds; break;
	}

	if ( bindlist == NULL ) return;

	list_foreach_safe( bindlist, node, tmp )
	{
		bind = (KeyBind*)node;
		if ( bind->key == key && bind->handler == func )
		{
			list_remove( bindlist, node );
			mem_free( bind );
		}
	}
}

void input_remove_char_bind( uint32 key, keybind_func_t func )
{
	input_remove_key_bind_from_list( key, func, BIND_CHAR );
}

void input_remove_key_up_bind( uint32 key, keybind_func_t func )
{
	input_remove_key_bind_from_list( key, func, BIND_KEYUP );
}

void input_remove_key_down_bind( uint32 key, keybind_func_t func )
{
	input_remove_key_bind_from_list( key, func, BIND_KEYDOWN );
}

void input_remove_key_bind( KeyBind* bind )
{
	input_remove_key_bind_from_list( bind->key, bind->handler, bind->type );
}

static void input_remove_mouse_bind_from_list( MOUSEBTN button, mousebind_func_t func, BINDTYPE_MOUSE type )
{
	MouseBind* bind;
	node_t *node, *tmp;
	list_t* bindlist = NULL;

	if ( !input_initialized ) return;

	switch ( type )
	{
		case BIND_MOVE: bindlist = mouse_move_binds; break;
		case BIND_BTNUP: bindlist = mouse_up_binds; break;
		case BIND_BTNDOWN: bindlist = mouse_down_binds; break;
	}

	if ( bindlist == NULL ) return;

	list_foreach_safe( bindlist, node, tmp )
	{
		bind = (MouseBind*)node;
		if ( bind->button == button && bind->handler != func )
		{
			list_remove( bindlist, node );
			mem_free( bind );
		}
	}
}

void input_remove_mouse_move_bind( mousebind_func_t func )
{
	input_remove_mouse_bind_from_list( MOUSE_NONE, func, BIND_MOVE );
}

void input_remove_mousebtn_up_bind( MOUSEBTN button, mousebind_func_t func )
{
	input_remove_mouse_bind_from_list( button, func, BIND_BTNUP );
}

void input_remove_mousebtn_down_bind( MOUSEBTN button, mousebind_func_t func )
{
	input_remove_mouse_bind_from_list( button, func, BIND_BTNDOWN );
}

void input_remove_mouse_bind( MouseBind* bind )
{
	input_remove_mouse_bind_from_list( bind->button, bind->handler, bind->type );
}

void input_set_mousebind_button( MouseBind* bind, MOUSEBTN button )
{
	if ( bind == NULL ) return;
	bind->button = button;
}

void input_set_mousebind_rect( MouseBind* bind, rectangle_t* area )
{
	if ( bind == NULL ) return;
	bind->bounds = *area;
}

void input_set_mousebind_func( MouseBind* bind, mousebind_func_t func )
{
	if ( bind == NULL ) return;
	bind->handler = func;
}

void input_set_mousebind_param( MouseBind* bind, void* data )
{
	if ( bind == NULL ) return;
	bind->userdata = data;
}

void input_block_keys( bool block )
{
	block_keys = block;
}

bool input_is_cursor_showing( void )
{
	return show_cursor;
}

void input_get_cursor_pos( int16* x, int16* y )
{
	*x = mouse_x;
	*y = mouse_y;
}

bool input_handle_keyboard_event( INPUT_EVENT type, uint32 key )
{
	list_t* list;
	node_t* node;
	InputEvent event;
	InputHookFunc* hook;

	if ( !input_initialized ) return true;
	if ( type >= NUM_INPUT_EVENTS ) return true;

	list = input_hooks[type];

	if ( list_empty(list) ) return true;

	event.type = type;
	event.keyboard.key = key;

	list_foreach( list, node )
	{
		hook = (InputHookFunc*)node;

		if ( !hook->handler( &event ) )
			return false;
	}

	if ( block_keys )
		return false;

	return true;
}

bool input_handle_mouse_event( INPUT_EVENT type, int16 x, int16 y, MOUSEBTN button, MOUSEWHEEL wheel )
{
	list_t* list;
	node_t* node;
	InputEvent event;
	InputHookFunc* hook;

	if ( !input_initialized ) return true;
	if ( type >= NUM_INPUT_EVENTS ) return true;

	list = input_hooks[type];

	if ( list_empty(list) ) return true;

	event.type = type;
	event.mouse.x = x;
	event.mouse.y = y;
	event.mouse.dx = x - mouse_x;
	event.mouse.dy = y - mouse_y;
	event.mouse.button = (uint8)button;
	event.mouse.wheel = (uint8)wheel;

	mouse_x = x;
	mouse_y = y;

	list_foreach( list, node )
	{
		hook = (InputHookFunc*)node;

		if ( !hook->handler( &event ) )
			return false;
	}

	return true;
}

bool input_handle_char_bind( uint32 key )
{
	KeyBind* bind;
	node_t *node, *tmp;
	bool ret = true;

	if ( !input_initialized ) return true;

	list_foreach_safe( char_binds, node, tmp )
	{
		bind = (KeyBind*)node;
		if ( !bind->handler( key, bind->userdata ) )
		{
			ret = false;
		}
	}

	return ret;
}

bool input_handle_key_down_bind( uint32 key )
{
	KeyBind* bind;
	node_t *node, *tmp;
	bool ret = true;

	if ( !input_initialized ) return true;

	list_foreach_safe( key_down_binds, node, tmp )
	{
		bind = (KeyBind*)node;
		if ( key == bind->key )
		{
			if ( !bind->handler( key, bind->userdata ) ) ret = false;
		}
	}

	return ret;
}

bool input_handle_key_up_bind( uint32 key )
{
	KeyBind* bind;
	node_t *node, *tmp;
	bool ret = true;

	if ( !input_initialized ) return true;

	list_foreach_safe( key_up_binds, node, tmp )
	{
		bind = (KeyBind*)node;
		if ( key == bind->key )
		{
			if ( !bind->handler( key, bind->userdata ) ) ret = false;
		}
	}

	return ret;
}

bool input_handle_mouse_move_bind( int16 x, int16 y )
{
	MouseBind* bind;
	node_t *node, *tmp;
	bool ret = true;

	if ( !input_initialized ) return true;

	list_foreach_safe( mouse_move_binds, node, tmp )
	{
		bind = (MouseBind*)node;

		if ( rect_is_point_in( &bind->bounds, x, y ) )
		{
			if ( !bind->handler( MOUSE_NONE, x, y, bind->userdata ) )
			{
				ret = false;
			}
		}
	}

	return ret;
}

bool input_handle_mouse_up_bind( MOUSEBTN button, int16 x, int16 y )
{
	MouseBind* bind;
	node_t *node, *tmp;
	bool ret = true;

	if ( !input_initialized ) return true;

	list_foreach_safe( mouse_up_binds, node, tmp )
	{
		bind = (MouseBind*)node;

		if ( bind->button == button && rect_is_point_in( &bind->bounds, x, y ) )
		{
			if ( !bind->handler( button, x, y, bind->userdata ) )
			{
				ret = false;
			}
		}
	}

	return ret;
}

bool input_handle_mouse_down_bind( MOUSEBTN button, int16 x, int16 y )
{
	MouseBind* bind;
	node_t *node, *tmp;
	bool ret = true;

	if ( !input_initialized ) return true;

	list_foreach_safe( mouse_down_binds, node, tmp )
	{
		bind = (MouseBind*)node;

		if ( bind->button == button && rect_is_point_in( &bind->bounds, x, y ) )
		{
			if ( !bind->handler( button, x, y, bind->userdata ) )
			{
				ret = false;
			}
		}
	}

	return ret;
}
