/*   m_who.c - Because s_user.c was just crazy.
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers.
 *
 *   This program is free softwmare; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* rewritten 06/02 by larne, the old one was unreadable. */

/* $Id$ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include "h.h"
#include "proto.h"

DLLFUNC int m_who(aClient *cptr, aClient *sptr, int parc, char *parv[]);

/* Place includes here */
#define MSG_WHO 	"WHO"	/* who */
#define TOK_WHO 	"\""	/* 127 4ever !;) */

#ifndef DYNAMIC_LINKING
ModuleHeader m_who_Header
#else
#define m_who_Header Mod_Header
ModuleHeader Mod_Header
#endif
  = {
	"who",	/* Name of module */
	"$Id$", /* Version */
	"command /who", /* Short description of module */
	"3.2-b8-1",
	NULL 
    };

/* This is called on module init, before Server Ready */
#ifdef DYNAMIC_LINKING
DLLFUNC int	Mod_Init(ModuleInfo *modinfo)
#else
int    m_who_Init(ModuleInfo *modinfo)
#endif
{
	/*
	 * We call our add_Command crap here
	*/
	add_Command(MSG_WHO, TOK_WHO, m_who, MAXPARA);
	return MOD_SUCCESS;
}

/* Is first run when server is 100% ready */
#ifdef DYNAMIC_LINKING
DLLFUNC int	Mod_Load(int module_load)
#else
int    m_who_Load(int module_load)
#endif
{
	return MOD_SUCCESS;
}


/* Called when module is unloaded */
#ifdef DYNAMIC_LINKING
DLLFUNC int	Mod_Unload(int module_unload)
#else
int	m_who_Unload(int module_unload)
#endif
{
	if (del_Command(MSG_WHO, TOK_WHO, m_who) < 0)
	{
		sendto_realops("Failed to delete commands when unloading %s",
				m_who_Header.name);
	}
	return MOD_SUCCESS;
}

static void do_channel_who(aClient *sptr, aChannel *channel, char *mask);
static void make_who_status(aClient *, aClient *, aChannel *, Member *, char *, int);
static void do_other_who(aClient *sptr, char *mask);
static void send_who_reply(aClient *, aClient *, char *, char *, char *);
static char *first_visible_channel(aClient *, aClient *, int *);
static int parse_who_options(aClient *, int, char**);
static void who_sendhelp(aClient *);
static int has_common_channels(aClient *, aClient *);

#define WF_OPERONLY  0x01 /* only show opers */
#define WF_ONCHANNEL 0x02 /* we're on the channel we're /who'ing */
#define WF_WILDCARD  0x04 /* a wildcard /who */
#define WF_REALHOST  0x08 /* want real hostnames */

static int who_flags;

#define WHO_CANTSEE 0x01 /* set if we can't see them */
#define WHO_CANSEE  0x02 /* set if we can */
#define WHO_OPERSEE 0x04 /* set if we only saw them because we're an oper */

#define FVC_HIDDEN  0x01

#define WHO_WANT 1
#define WHO_DONTWANT 2
#define WHO_DONTCARE 0

struct {
  int want_away;
  int want_channel;
  char *channel; /* if they want one */
  int want_gecos;
  char *gecos;
  int want_server;
  char *server;
  int want_host;
  char *host;
  int want_nick;
  char *nick;
  int want_user;
  char *user;
  int want_umode;
  int umodes_dontwant;
  int umodes_want;
  int common_channels_only;
} wfl;

DLLFUNC int m_who(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
  aChannel *target_channel;
  char *mask = parv[1];
  char star[] = "*";
  int i = 0;

  who_flags = 0;
  memset(&wfl, 0, sizeof(wfl));

  if (parc > 1)
    {
      i = parse_who_options(sptr, parc - 1, parv + 1);
      if (i < 0)
	{
	  sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, parv[0],
		     mask);
	  return 0;
	}
    }

  if (parc-i < 2 || strcmp(parv[1 + i], "0") == 0)
    mask = star;
  else
    mask = parv[1 + i];

  if (!i && parc > 2 && *parv[2] == 'o')
    who_flags |= WF_OPERONLY;

  collapse(mask);

  if (*mask == '\0')
    {
      /* no mask given */
      sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, parv[0], "*");
      return 0;
    }


  if ((target_channel = find_channel(mask, NULL)) != NULL)
    {
      do_channel_who(sptr, target_channel, mask);
      sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, parv[0], mask);
      return 0;
    }
  if (wfl.channel && wfl.want_channel == WHO_WANT && (target_channel =
      find_channel(wfl.channel, NULL)) != NULL) {
      do_channel_who(sptr, target_channel, mask);
      sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, parv[0], mask);
      return 0;
  }
  else
    {
      do_other_who(sptr, mask);
      sendto_one(sptr, getreply(RPL_ENDOFWHO), me.name, parv[0], mask);
      return 0;
    }

  return 0;
}

static void who_sendhelp(aClient *sptr)
{
  char *who_help[] = {
    "/WHO [+|-][acghmnsuCM] [args]",
    "Flags are specified like channel modes, the flags cgmnsu all have arguments",
    "Flags are set to a positive check by +, a negative check by -",
    "The flags work as follows:",
    "Flag a: user is away",
    "Flag c <channel>: user is on <channel>,",
    "                  no wildcards accepted",
    "Flag g <gcos/realname>: user has string <gcos> in their GCOS,",
    "                        wildcards accepted, oper only",
    "Flag h <host>: user has string <host> in their hostname,",
    "               wildcards accepted",
    "Flag m <usermodes>: user has <usermodes> set on them,",
    "                    only o/A/a for nonopers",
    "Flag n <nick>: user has string <nick> in their nickname,",
    "               wildcards accepted",
    "Flag s <server>: user is on server <server>,",
    "                 wildcards not accepted",
    "Flag u <user>: user has string <user> in their username,",
    "               wildcards accepted",
    "Behavior flags:",
    "Flag M: check for user in channels I am a member of",
    "Flag R: show users' real hostnames (oper only.)",
    NULL
  };
  char **s;

  for (s = who_help; *s; s++)
    sendto_one(sptr, getreply(RPL_LISTSYNTAX), me.name, sptr->name, *s);
}

#define WHO_ADD 1
#define WHO_DEL 2

static int parse_who_options(aClient *sptr, int argc, char **argv)
{
  char *s = argv[0];
  int what = WHO_ADD;
  int i = 1;

  if (*s != '-' && *s != '+')
    return 0;

  while (*s)
    {
      switch (*s)
	{
	case '+':
	  what = WHO_ADD;
	  break;

	case '-':
	  what = WHO_DEL;
	  break;

	case 'a':
	  if (what == WHO_ADD)
	    wfl.want_away = WHO_WANT;
	  else
	    wfl.want_away = WHO_DONTWANT;
	  break;

	case 'c':
	  if (i >= argc)
	    {
	      who_sendhelp(sptr);
	      return -1;
	    }

	  wfl.channel = argv[i];

	  if (what == WHO_ADD)
	    wfl.want_channel = WHO_WANT;
	  else
	    wfl.want_channel = WHO_DONTWANT;

	  i++;
	  break;
	  
	case 'g':
	  if (i >= argc)
	    {
	      who_sendhelp(sptr);
	      return -1;
	    }
	  
	  if (!IsAnOper(sptr))
	    break; /* oper-only */

	  wfl.gecos = argv[i];
	  if (what == WHO_ADD)
	    wfl.want_gecos = WHO_WANT;
	  else
	    wfl.want_gecos = WHO_DONTWANT;

	  i++;
	  break;

	case 's':
	  if (i >= argc)
	    {
	      who_sendhelp(sptr);
	      return -1;
	    }

	  wfl.server = argv[i];
	  if (what == WHO_ADD)
	    wfl.want_server = WHO_WANT;
	  else
	    wfl.want_server = WHO_DONTWANT;

	  i++;
	  break;

	case 'h':
	  if (i >= argc)
	    {
	      who_sendhelp(sptr);
	      return -1;
	    }

	  wfl.host = argv[i];
	  if (what == WHO_ADD)
	    wfl.want_host = WHO_WANT;
	  else
	    wfl.want_host = WHO_DONTWANT;

	  i++;
	  break;

	case 'n':
	  if (i >= argc)
	    {
	      who_sendhelp(sptr);
	      return -1;
	    }

	  wfl.nick = argv[i];
	  if (what == WHO_ADD)
	    wfl.want_nick = WHO_WANT;
	  else
	    wfl.want_nick = WHO_DONTWANT;

	  i++;
	  break;

	case 'u':
	  if (i >= argc)
	    {
	      who_sendhelp(sptr);
	      return -1;
	    }

	  wfl.user = argv[i];
	  if (what == WHO_ADD)
	    wfl.want_user = WHO_WANT;
	  else
	    wfl.want_user = WHO_DONTWANT;

	  i++;
	  break;

	case 'm':
	  if (i >= argc)
	    {
	      who_sendhelp(sptr);
	      return -1;
	    }

	  {
	    char *s = argv[i];
	    int *umodes;

	    if (what == WHO_ADD)
	      umodes = &wfl.umodes_want;
	    else
	      umodes = &wfl.umodes_dontwant;

	    while (*s)
	      {
		int i;

		for (i = 0; i <= Usermode_highest; i++) 
		  {
		    if (*s == Usermode_Table[i].flag)
		      {
			*umodes |= Usermode_Table[i].mode;
			break;
		      }
		  }
		s++;
	      }

	    if (!IsAnOper(sptr))
	      *umodes = *umodes & (UMODE_OPER | UMODE_LOCOP | UMODE_SADMIN |
				   UMODE_ADMIN);
	  }

	  i++;
	  break;

	case 'M':
	  if (what == WHO_ADD)
	    wfl.common_channels_only = 1;
	  else
	    wfl.common_channels_only = 0;
	  break;

	case 'R':
	  if (!IsAnOper(sptr))
	    break;

	  if (what == WHO_ADD)
	    who_flags |= WF_REALHOST;
	  else
	    who_flags &= ~WF_REALHOST;

	  break;

	default:
	  who_sendhelp(sptr);
	  return -1;
	}
      s++;
    }

  return i;
}

static int can_see(aClient *sptr, aClient *acptr, aChannel *channel)
{
  int ret = 0;

  do {
    /* can only see people */
    if (!IsPerson(acptr))
      return WHO_CANTSEE;

    /* can only see opers if thats what they want */
    if (who_flags & WF_OPERONLY)
      {
	if (!IsAnOper(acptr))
	  return ret | WHO_CANTSEE;

	if (IsHideOper(acptr)) {
	  if (IsAnOper(sptr))
	    ret |= WHO_OPERSEE;
	  else
	    return ret | WHO_CANTSEE;
	}
      }

    /* if they only want people who are away */
    if ((wfl.want_away == WHO_WANT && !acptr->user->away) ||
	(wfl.want_away == WHO_DONTWANT && acptr->user->away))
      return WHO_CANTSEE;

    /* if they only want people on a certain channel. */
    if (wfl.want_channel != WHO_DONTCARE)
      {
	aChannel *chan = find_channel(wfl.channel, NULL);

	if (!chan && wfl.want_channel == WHO_WANT)
	  return WHO_CANTSEE;

	if ((wfl.want_channel == WHO_WANT) &&
	    !IsMember(acptr, chan))
	  return WHO_CANTSEE;

	if ((wfl.want_channel == WHO_DONTWANT) &&
	    IsMember(acptr, chan))
	  return WHO_CANTSEE;
      }

    /* if they only want people with a certain gecos */
    if (wfl.want_gecos != WHO_DONTCARE)
      {
	if (((wfl.want_gecos == WHO_WANT) && match(wfl.gecos, acptr->info)) ||
	    ((wfl.want_gecos == WHO_DONTWANT) && !match(wfl.gecos, acptr->info)))
	  {
	    return WHO_CANTSEE;
	  }
      }

    /* if they only want people with a certain server */
    if (wfl.want_server != WHO_DONTCARE)
      {
	if (((wfl.want_server == WHO_WANT) && strcmp(wfl.server, acptr->user->server)) ||
	    ((wfl.want_server == WHO_DONTWANT) && !strcmp(wfl.server, acptr->user->server)))
	  {
	    return WHO_CANTSEE;
	  }
      }

    /* if they only want people with a certain host */
    if (wfl.want_host != WHO_DONTCARE)
      {
	char *host;

	if (IsAnOper(sptr))
	  host = acptr->user->realhost;
	else
	  {
	    if (IsHidden(acptr))
	      host = acptr->user->virthost;
	    else
	      host = acptr->user->realhost;
	  }

	if (((wfl.want_host == WHO_WANT) && match(wfl.host, host)) ||
	    ((wfl.want_host == WHO_DONTWANT) && !match(wfl.host, host)))
	  {
	    return WHO_CANTSEE;
	  }
      }

    /* if they only want people with a certain nick.. */
    if (wfl.want_nick != WHO_DONTCARE)
      {
	if (((wfl.want_nick == WHO_WANT) && match(wfl.nick, acptr->name)) ||
	    ((wfl.want_nick == WHO_DONTWANT) && !match(wfl.nick, acptr->name)))
	  {
	    return WHO_CANTSEE;
	  }
      }

    /* if they only want people with a certain username */
    if (wfl.want_user != WHO_DONTCARE)
      {
	if (((wfl.want_user == WHO_WANT) && match(wfl.user, acptr->user->username)) ||
	    ((wfl.want_user == WHO_DONTWANT) && !match(wfl.user, acptr->user->username)))
	  {
	    return WHO_CANTSEE;
	  }
      }

    /* if they only want people with a certain umode */
    if (wfl.umodes_want)
      {
	if (!(acptr->umodes & wfl.umodes_want))
	  return WHO_CANTSEE;
      }

    if (wfl.umodes_dontwant)
      {
	if (acptr->umodes & wfl.umodes_dontwant)
	  return WHO_CANTSEE;
      }

    /* if they only want common channels */
    if (wfl.common_channels_only)
      {
	if (!has_common_channels(sptr, acptr))
	  return WHO_CANTSEE;
      }

    if (channel)
      {
	int member = who_flags & WF_ONCHANNEL;

	if (SecretChannel(channel) ||
	    HiddenChannel(channel))
	  {
	    /* if they aren't on it.. they can't see it */
	    if (!(who_flags & WF_ONCHANNEL))
	      break;
	  }

	if (IsHiding(acptr))
	  /* if they're +I .. only show them if the other person is an oper */
	  break;

	if (IsInvisible(acptr) && !member)
	  break;

	if ((channel->mode.mode & MODE_AUDITORIUM) &&
	    !is_chan_op(acptr, channel) &&
	    !is_chan_op(sptr, channel))
	  break;
      }
    else
      {
	/* a user/mask who */
	
	if (IsInvisible(acptr))
	  {
	    /* don't show them unless it's an exact match */
	    if ((who_flags & WF_WILDCARD))
	      break;
	  }
      }

    /* phew.. show them. */
    return WHO_CANSEE;
  } while (0);

  /* if we get here, it's oper-dependant. */
  if (IsAnOper(sptr))
    return ret | WHO_OPERSEE | WHO_CANSEE;
  else
    if (sptr == acptr)
      return ret | WHO_CANSEE;
    else
      return ret | WHO_CANTSEE;
}

static void do_channel_who(aClient *sptr, aChannel *channel, char *mask)
{
  Member *cm = channel->members;
  
  if (IsMember(sptr, channel) || IsNetAdmin(sptr))
    who_flags |= WF_ONCHANNEL;

  for (cm = channel->members; cm; cm = cm->next)
    {
      aClient *acptr = cm->cptr;
      char status[20];

      int cansee;

      if ((cansee = can_see(sptr, acptr, channel)) & WHO_CANTSEE)
	continue;

      make_who_status(sptr, acptr, channel, cm, status, cansee);
      send_who_reply(sptr, acptr, channel->chname, status, "");
    }

  return;
}

static void make_who_status(aClient *sptr, aClient *acptr, aChannel *channel, 
			    Member *cm, char *status, int cansee)
{
  int i = 0;

  if (acptr->user->away)
    status[i++] = 'G';
  else
    status[i++] = 'H';
  
  if (IsARegNick(acptr))
    status[i++] = 'r';

  if (IsAnOper(acptr) &&
      (!IsHideOper(acptr) || sptr == acptr || IsAnOper(sptr)))
    status[i++] = '*';
  
  if (IsAnOper(acptr) &&
      (IsHideOper(acptr) && sptr != acptr && IsAnOper(sptr)))
    status[i++] = '!';
  
  if (cansee & WHO_OPERSEE)
    status[i++] = '&';

  if (cm)
    {
      if (cm->flags & CHFL_CHANOP)
	status[i++] = '@';
      else if (cm->flags & CHFL_HALFOP)
	status[i++] = '%';
      else if (cm->flags & CHFL_VOICE)
	status[i++] = '+';
    }

  status[i] = '\0';
}

static void do_other_who(aClient *sptr, char *mask)
{
  /* wildcard? */
  if (strchr(mask, '*') || strchr(mask, '?'))
    {
      /* go through all users.. */
      aClient *acptr;

      who_flags |= WF_WILDCARD;

      for (acptr = client; acptr; acptr = acptr->next)
	{
	  int cansee;
	  char status[20];
	  char *channel;
	  int flg;

	  if (match(mask, acptr->name))
	    continue;

	  if ((cansee = can_see(sptr, acptr, NULL)) & WHO_CANTSEE)
	    continue;

	  channel = first_visible_channel(sptr, acptr, &flg);
	  make_who_status(sptr, acptr, NULL, NULL, status, cansee);
	  send_who_reply(sptr, acptr, channel, status, (flg & FVC_HIDDEN) ? "~" : "");
	}	  
    }
  else
    {
      /* just a single client */
      aClient *acptr = find_client(mask, NULL);
      int cansee;
      char status[20];
      char *channel;
      int flg;

      if (!acptr)
	return;

      if ((cansee = can_see(sptr, acptr, NULL)) == WHO_CANTSEE)
	return;
      
      channel = first_visible_channel(sptr, acptr, &flg);
      make_who_status(sptr, acptr, NULL, NULL, status, cansee);
      send_who_reply(sptr, acptr, channel, status, (flg & FVC_HIDDEN) ? "~" : "");
    }

  return;
}

static void send_who_reply(aClient *sptr, aClient *acptr, 
			   char *channel, char *status, char *xstat)
{
  char *stat;

  stat = malloc(strlen(status) + strlen(xstat) + 1);

  sprintf(stat, "%s%s", status, xstat);

  sendto_one(sptr, getreply(RPL_WHOREPLY), me.name, sptr->name,      
	     channel,       /* channel name */
	     acptr->user->username, /* user name */
	     (IsHidden(acptr) && !(who_flags & WF_REALHOST)) ?
	     acptr->user->virthost :
	     acptr->user->realhost, /* hostname */
	     acptr->user->server,   /* server name */
	     acptr->name,           /* nick */
	     stat,                  /* status */
	     acptr->hopcount,        /* hops */ 
	     acptr->info            /* realname */
	     );
  free(stat);
}

static char *first_visible_channel(aClient *sptr, aClient *acptr, int *flg)
{
  Membership *lp;

  *flg = 0;

  /* this is a bit cack, but it seems the only way
     (and is the current behaviour) since +I is not channel-specific */
  if (IsHiding(acptr))
    return "*";

  for (lp = acptr->user->channel; lp; lp = lp->next)
    {
      aChannel *chptr = lp->chptr;
      int cansee = ShowChannel(sptr, chptr);

      if (!cansee)
	{
	  if (IsAnOper(sptr))
	    *flg |= FVC_HIDDEN;
	  else
	    continue;
	}
      
      return chptr->chname;
    }
 
  /* no channels that they can see */

  return "*";
}

static int has_common_channels(aClient *c1, aClient *c2)
{
  Membership *lp;

  if (!IsAnOper(c1) && IsHiding(c2))
    return 0;

  for (lp = c1->user->channel; lp; lp = lp->next)
    {
      if (IsMember(c2, lp->chptr))
	return 1;
    }

  return 0;
}
