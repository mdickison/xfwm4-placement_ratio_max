/*
        This program is free software; you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation; either version 2, or (at your option)
        any later version.
 
        This program is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.
 
        You should have received a copy of the GNU General Public License
        along with this program; if not, write to the Free Software
        Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 
        oroborus - (c) 2001 Ken Lynch
        xfwm4    - (c) 2002-2004 Olivier Fourdan
 
 */
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <sys/time.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <libxfce4util/libxfce4util.h> 
#include <libxfcegui4/libxfcegui4.h>

#include "screen.h"
#include "focus.h"
#include "misc.h"
#include "client.h"
#include "frame.h"
#include "stacking.h"
#include "transients.h"
#include "workspaces.h"
#include "hints.h"

typedef struct _ClientPair ClientPair;
struct _ClientPair
{
    Client *prefered;
    Client *highest;
};

static Client *client_focus  = NULL;
static Client *pending_focus = NULL;
static Client *last_ungrab   = NULL;

static ClientPair
clientGetTopMostFocusable (ScreenInfo *screen_info, int layer, Client * exclude)
{
    ClientPair top_client;
    Client *c = NULL;
    GList *index = NULL;

    TRACE ("entering clientGetTopMostFocusable");

    top_client.prefered = top_client.highest = NULL;
    for (index = screen_info->windows_stack; index; index = g_list_next (index))
    {
        c = (Client *) index->data;
        TRACE ("*** stack window \"%s\" (0x%lx), layer %i", c->name,
            c->window, (int) c->win_layer);

        if ((c->type & (WINDOW_SPLASHSCREEN | WINDOW_DOCK | WINDOW_DESKTOP))
            || ((layer != WIN_LAYER_DESKTOP) && (c->type & WINDOW_DESKTOP)))
        {
            continue;
        }
        
        if (!exclude || (c != exclude))
        {
            if ((c->win_layer <= layer) && FLAG_TEST (c->flags, CLIENT_FLAG_VISIBLE))
            {
                if (clientSelectMask (c, 0, WINDOW_NORMAL | WINDOW_DIALOG | WINDOW_MODAL_DIALOG))
                {
                    top_client.prefered = c;
                }
                top_client.highest = c;
            }
            else if (c->win_layer > layer)
            {
                break;
            }
        }
    }

    return top_client;
}

void
clientFocusTop (ScreenInfo *screen_info, int layer)
{
    ClientPair top_client;

    top_client = clientGetTopMostFocusable (screen_info, layer, NULL);
    if (top_client.prefered)
    {
        clientSetFocus (screen_info, top_client.prefered, GDK_CURRENT_TIME, NO_FOCUS_FLAG);
    }
    else
    {
        clientSetFocus (screen_info, top_client.highest, GDK_CURRENT_TIME, NO_FOCUS_FLAG);
    }
}

void
clientFocusNew(Client * c)
{
    ScreenInfo *screen_info = NULL;
    gboolean give_focus;

    g_return_if_fail (c != NULL);
    
    if (!clientAcceptFocus (c))
    {
        return;
    }
    screen_info = c->screen_info;
    give_focus = screen_info->params->focus_new;
#if 0    
    /*  Try to avoid focus stealing */
    if (client_focus)
    {
        if (FLAG_TEST(c->flags, CLIENT_FLAG_HAS_USER_TIME) &&
            FLAG_TEST(client_focus->flags, CLIENT_FLAG_HAS_USER_TIME))
        {
            TRACE ("Current %u, new %u", client_focus->user_time, c->user_time);
            if (c->user_time < client_focus->user_time)
            {
                give_focus = FALSE;
            }
        }
    }
#endif
    
    if (give_focus || FLAG_TEST(c->flags, CLIENT_FLAG_STATE_MODAL))
    {
        clientSetFocus (c->screen_info, c, GDK_CURRENT_TIME, FOCUS_IGNORE_MODAL);
        clientPassGrabButton1 (c);
    }
    else
    {
        clientPassGrabButton1 (NULL);
    }
}

gboolean
clientSelectMask (Client * c, int mask, int type)
{
    TRACE ("entering clientSelectMask");

    g_return_val_if_fail (c != NULL, FALSE);

    if ((!clientAcceptFocus (c)) && !(mask & INCLUDE_SKIP_FOCUS))
    {
        return FALSE;
    }
    if (FLAG_TEST (c->flags, CLIENT_FLAG_HIDDEN) && !(mask & INCLUDE_HIDDEN))
    {
        return FALSE;
    }
    if (FLAG_TEST (c->flags, CLIENT_FLAG_SKIP_PAGER)
        && !(mask & INCLUDE_SKIP_PAGER))
    {
        return FALSE;
    }
    if (FLAG_TEST (c->flags, CLIENT_FLAG_SKIP_TASKBAR)
        && !(mask & INCLUDE_SKIP_TASKBAR))
    {
        return FALSE;
    }
    if ((c->win_workspace != c->screen_info->current_ws) && !(mask & INCLUDE_ALL_WORKSPACES))
    {
        return FALSE;
    }
    if (c->type && type)
    {
        return TRUE;
    }
    return FALSE;
}

Client *
clientGetNext (Client * c, int mask)
{
    Client *c2 = NULL;
    unsigned int i;

    TRACE ("entering clientGetNext");

    if (c)
    {
        ScreenInfo *screen_info = c->screen_info;
        for (c2 = c->next, i = 0; (c2) && (i < screen_info->client_count - 1);
            c2 = c2->next, i++)
        {
            if (clientSelectMask (c2, mask, WINDOW_NORMAL | WINDOW_DIALOG | WINDOW_MODAL_DIALOG))
            {
                return c2;
            }
        }
    }
    return NULL;
}

Client *
clientGetPrevious (Client * c, int mask)
{
    Client *c2 = NULL;
    unsigned int i;

    TRACE ("entering clientGetPrevious");

    if (c)
    {
        ScreenInfo *screen_info = c->screen_info;
        for (c2 = c->prev, i = 0; (c2) && (i < screen_info->client_count);
            c2 = c2->prev, i++)
        {
            if (clientSelectMask (c2, mask, WINDOW_NORMAL | WINDOW_DIALOG | WINDOW_MODAL_DIALOG))
            {
                return c2;
            }
        }
    }
    return NULL;
}

void
clientPassFocus (ScreenInfo *screen_info, Client * c)
{
    Client *new_focus = NULL;
    Client *current_focus = client_focus;
    ClientPair top_most;
    Client *c2 = NULL;
    Window dr, window;
    int rx, ry, wx, wy;
    unsigned int mask;
    int look_in_layer = (c ? c->win_layer : WIN_LAYER_NORMAL);

    TRACE ("entering clientPassFocus");

    if (pending_focus)
    {
        current_focus = pending_focus;
    }

    if ((c || current_focus) && (c != current_focus))
    {
        return;
    }

    top_most = clientGetTopMostFocusable (screen_info, look_in_layer, c);
    if (screen_info->params->click_to_focus)
    {
        if (c)
        {
            if (clientIsModal (c))
	    {
		/* If the window is a modal, send focus back to its parent window
        	   Modals are transients, and we aren"t interested in modal
        	   for group, so it safe to use clientGetTransient because 
        	   it's really what we want...
        	 */

        	c2 = clientGetTransient (c);
        	if (c2 && FLAG_TEST(c2->flags, CLIENT_FLAG_VISIBLE))
        	{
                    new_focus = c2;
                    /* Usability: raise the parent, to grab user's attention */
                    clientRaise (c2);
        	}
	    }
	    else
	    {
	        new_focus = clientGetNext (c, 0);
	    }
        }
    }
    else if (XQueryPointer (myScreenGetXDisplay (screen_info), screen_info->xroot, &dr, &window, &rx, &ry, &wx, &wy, &mask))
    {
        new_focus = clientAtPosition (screen_info, rx, ry, c);
    }
    if (!new_focus)
    {
        new_focus = top_most.prefered ? top_most.prefered : top_most.highest;
    }
    clientSetFocus (screen_info, new_focus, GDK_CURRENT_TIME, FOCUS_IGNORE_MODAL | FOCUS_FORCE);
    if (new_focus == top_most.highest)
    {
        clientPassGrabButton1 (new_focus);
    }
    else if (last_ungrab == c)
    {
        clientPassGrabButton1 (NULL);
    }
}

gboolean
clientAcceptFocus (Client * c)
{
    g_return_val_if_fail (c != NULL, FALSE);

    TRACE ("entering clientAcceptFocus");

    /* Modal dialogs *always* accept focus */
    if (FLAG_TEST(c->flags, CLIENT_FLAG_STATE_MODAL))
    {
        return TRUE; 
    }
    /* First check GNOME protocol */
    if (c->win_hints & WIN_HINTS_SKIP_FOCUS)
    {
        return FALSE;
    }
    if (!FLAG_TEST (c->wm_flags, WM_FLAG_INPUT | WM_FLAG_TAKEFOCUS))
    {
        return FALSE;
    }
    
    return TRUE;
}

void
clientSortRing(Client *c)
{
    ScreenInfo *screen_info = NULL;
    
    g_return_if_fail (c != NULL);

    TRACE ("Sorting...");
    screen_info = c->screen_info;
    if (screen_info->client_count > 2 && c != screen_info->clients)
    {
        c->prev->next = c->next;
        c->next->prev = c->prev;

        c->prev = screen_info->clients->prev;
        c->next = screen_info->clients;
        screen_info->clients->prev->next = c;
        screen_info->clients->prev = c;
    }
    screen_info->clients = c;
}

void
clientUpdateFocus (ScreenInfo *screen_info, Client * c, unsigned short flags)
{
    Client *c2 = ((client_focus != c) ? client_focus : NULL);
    unsigned long data[2];

    TRACE ("entering clientUpdateFocus");

    pending_focus = NULL;
    if ((c) && !clientAcceptFocus (c))
    {
        TRACE ("SKIP_FOCUS set for client \"%s\" (0x%lx)", c->name, c->window);
        return;
    }
    if ((c) && (c == client_focus) && !(flags & FOCUS_FORCE))
    {
        TRACE ("client \"%s\" (0x%lx) is already focused, ignoring request",
            c->name, c->window);
        return;
    }
    client_focus = c;
    if (c)
    {
        clientInstallColormaps (c);
        if (flags & FOCUS_SORT)
        {
            clientSortRing(c);
        }
        data[0] = c->window;
        if ((c->legacy_fullscreen) || FLAG_TEST(c->flags, CLIENT_FLAG_FULLSCREEN))
        {
            clientSetLayer (c, WIN_LAYER_ABOVE_DOCK);
        }
        frameDraw (c, FALSE, FALSE);
    }
    else
    {
        data[0] = None;
    }
    if (c2)
    {
        TRACE ("redrawing previous focus client \"%s\" (0x%lx)", c2->name,
            c2->window);
        /* Requires a bit of explanation here... Legacy apps automatically
           switch to above layer when receiving focus, and return to
           normal layer when loosing focus.
           The following "logic" is in charge of that behaviour.
         */
        if ((c2->legacy_fullscreen) || FLAG_TEST(c2->flags, CLIENT_FLAG_FULLSCREEN))
        {
            if (FLAG_TEST(c2->flags, CLIENT_FLAG_FULLSCREEN))
            {
                clientSetLayer (c2, c2->fullscreen_old_layer);
            }
            else
            {
                clientSetLayer (c2, WIN_LAYER_NORMAL);
            }
            if (c)
            {
                clientRaise(c);
                clientPassGrabButton1 (c);
            }
        }
        frameDraw (c2, FALSE, FALSE);
    }
    data[1] = None;
    XChangeProperty (myScreenGetXDisplay (screen_info), screen_info->xroot, net_active_window, XA_WINDOW, 32,
        PropModeReplace, (unsigned char *) data, 2);
}

void
clientSetFocus (ScreenInfo *screen_info, Client * c, Time timestamp, unsigned short flags)
{
    Client *c2 = NULL;

    TRACE ("entering clientSetFocus");
    
    if ((c) && !(flags & FOCUS_IGNORE_MODAL))
    {
        c2 = clientGetModalFor (c);

        if (c2)
        {
            c = c2;
        }
    }
    c2 = ((client_focus != c) ? client_focus : NULL);
    if ((c) && FLAG_TEST (c->flags, CLIENT_FLAG_VISIBLE))
    {
        TRACE ("setting focus to client \"%s\" (0x%lx)", c->name, c->window);
        if ((c == client_focus) && !(flags & FOCUS_FORCE))
        {
            TRACE ("client \"%s\" (0x%lx) is already focused, ignoring request",
                c->name, c->window);
            return;
        }        
        if (!clientAcceptFocus (c))
        {
            TRACE ("SKIP_FOCUS set for client \"%s\" (0x%lx)", c->name, c->window);
            return;
        }
        if (FLAG_TEST (c->wm_flags, WM_FLAG_INPUT))
        {
            pending_focus = c;
            XSetInputFocus (myScreenGetXDisplay (screen_info), c->window, RevertToPointerRoot, timestamp);
        }
        if (FLAG_TEST(c->wm_flags, WM_FLAG_TAKEFOCUS))
        {
            sendClientMessage (c->screen_info, c->window, wm_protocols, wm_takefocus, timestamp);
        }
        XFlush (myScreenGetXDisplay (screen_info));
    }
    else
    {
        unsigned long data[2];
        
        TRACE ("setting focus to none");
        
        data[0] = data[1] = None;
        client_focus = NULL;
        if (c2)
        {
            frameDraw (c2, FALSE, FALSE);
            XChangeProperty (clientGetXDisplay (c2), c2->screen_info->xroot, net_active_window, XA_WINDOW, 32,
                PropModeReplace, (unsigned char *) data, 2);
        }
        XChangeProperty (myScreenGetXDisplay (screen_info), screen_info->xroot, net_active_window, XA_WINDOW, 32,
            PropModeReplace, (unsigned char *) data, 2);
        XSetInputFocus (myScreenGetXDisplay (screen_info), screen_info->gnome_win, RevertToPointerRoot, GDK_CURRENT_TIME);
        XFlush (myScreenGetXDisplay (screen_info));
    }
}

Client *
clientGetFocus (void)
{
    return (client_focus);
}

void
clientClearFocus (void)
{
    client_focus = NULL;
}

void
clientGrabButton1 (Client * c)
{
    g_return_if_fail (c != NULL);
    TRACE ("entering clientGrabButton1");
    TRACE ("grabbing buttons for client \"%s\" (0x%lx)", c->name, c->window);
    
    grabButton(clientGetXDisplay (c), Button1, 0, c->window);
}

void
clientUngrabButton1 (Client * c)
{
    g_return_if_fail (c != NULL);
    TRACE ("entering clientUngrabButton1");
    TRACE ("ungrabing buttons for client \"%s\" (0x%lx)", c->name, c->window);

    ungrabButton(clientGetXDisplay (c), Button1, 0, c->window);
}

void
clientPassGrabButton1(Client * c)
{
    TRACE ("entering clientPassGrabButton1");
    TRACE ("ungrabing buttons for client \"%s\" (0x%lx)", c->name, c->window);

    if (c == NULL)
    {
        if (last_ungrab)
        {
            clientGrabButton1 (last_ungrab);
        }
        last_ungrab = NULL;
        return;
    }
    
    if (last_ungrab == c)
    {
        return;
    }
    
    if (last_ungrab)
    {
        clientGrabButton1 (last_ungrab);
    }
    
    clientUngrabButton1 (c);
    last_ungrab = c;
}

Client *
clientGetLastGrab (void)
{
    return last_ungrab;
}

void
clientClearLastGrab (void)
{
    last_ungrab = NULL;
}
