// $Id: yacli.c,v 4.0 2020/06/30 02:03:34 bbonev Exp $
//
// Copyright © 2015-2020 Boian Bonev (bbonev@ipacct.com) {{{
//
// This file is part of yacli - yet another command line interface library.
//
// yacli is free software: you can redistribute it and/or mowdify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// yacli is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with yacli.  If not, see <http://www.gnu.org/licenses/>.
// }}}

// {{{ includes

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <yacli.h>

// }}}

// {{{ definitions

#define BUFFER_STEP 1024

#define mymax(a,b) (((a)>(b))?(a):(b))
#define mymin(a,b) (((a)<(b))?(a):(b))

typedef enum {
	IN_NORM, // normal input, check for ESC or IAC
	IN_SEARCH, // incremental search in history
	IN_MORE, // more prompt, used for paging
	IN_C_X, // Ctrl-X sequence
} yacli_in_state;

typedef struct _cmnode {
	void (*cb)(yacli *cli,int ac,char **cmd); // user callback for command
	struct _cmnode *parent; // parent node in command tree
	struct _cmnode *child; // child chain in sorted order
	struct _cmnode *next; // sibling command(s) in sorted order
	struct _cmnode *dyn; // dynamically generated command list (sorted)
	yacli *cli; // pointer to owning cli (used for checks)
	char *help; // help string | <abbreviation-for-regex>
	char *cmd; // command text | @<numeric-id> | ^regex$
	uint8_t isdyn:1; // command is dynamically generated; help and cb are in parent
} cmnode;

typedef struct _cmstack {
	struct _cmstack *next;
	struct _cmstack *prev;
	cmnode *cmdt; // previous command tree
	char *mode; // current mode short name
	void *hint; // user hint for the current mode
} cmstack;

typedef struct _history {
	struct _history *next; // next item in command history
	struct _history *prev; // previous item in command history
	char *command; // command text
} history;

typedef struct _filter_inst {
	struct _filter_inst *next; // next filter instance (will receive our output)
	struct _filter *fltr; // filter class
	long private[4]; // private data, used for filter state
	char *params; // command line parameters of the filter
	char *buf; // buffer used to process output by lines
	int bufsiz; // buffer allocation size
	int buflen; // buffer content length
} filter_inst;

typedef struct _filter {
	struct _filter *next; // next defined filter
	yacli *cli; // containing cli
	char *cmd; // filter command word
	char *help; // filter help text
	int (*feed)(filter_inst *flti,const char *line,int len); // callback for text feed
	void (*done)(filter_inst *flti); // callback to flush buffered stuff
	uint8_t allownext:1; // allow chaining other filters afterwards
} filter;

struct _yacli {
	char *hostname; // hostname, used in prompt
	char *level; // access level (#/$/>)
	char *banner; // login banner
	char *buffer; // current command buffer
	char *savbuf; // saved buffer, while browsing history
	char *sbuf; // incremental search buffer
	char *rcmd; // search result pointer to command
	char *morebuf; // buffered data for more
	char *moreprompt; // more prompt text
	char **parsedcmd; // command split into words (main style)
	void *phint; // user defined hint (pointer)
	cmnode *cmdt; // command tree
	cmstack *cstack; // command stack with modes
	char *modes; // all modes from stack
	filter noopf; // noop passthrough filter
	filter_inst noopi; // noop filter instance
	filter *flts; // sorted defined filter list
	filter_inst *fcmd; // applied filters for current command
	history *hist; // command history (double linked)
	history *hist_p; // pointer in history, NULL when not in history
	yascreen *s; // screen used to render output
	void (*cmdcb)(yacli *cli,const char *cmd,int code); // callback for each executed command
	void (*listcb)(yacli *cli,void *ctx,int code); // callback for getting dynamic list items
	void (*parsedcb)(yacli *cli,int ac,char **cmd); // user callback for matching command
	void (*ctrlzcb)(yacli *cli); // user callback to notify ctrl-z
	yacli_in_state state; // input bytestream DFA state
	yacli_loop retcode; // bytestream feed return code
	int hint; // user defined hint (scalar)
	int rpos; // matching command index where (0=first matching, 1=previous, etc)
	int sx,sy; // terminal size
	int lines; // line count between prompts, used for pagination
	int bufpos; // buffer left scroll position
	int buflen; // buffer data len (may not always be 0 terminated)
	int bufsiz; // buffer size
	int cursor; // cursor position
	int morelen; // morebuf data len
	int moresiz; // morebuf alloc size
	int parsedcnt; // parsed command word count
	int parsedsiz; // parsed command array size
	uint8_t more:1; // enable paged output
	uint8_t redraw:1; // prompt needs redraw
	uint8_t wastab:1; // control for double tab
	uint8_t incmdcb:1; // flag when we are inside command callback
	uint8_t istelnet:1; // use telnet IAC codes
	uint8_t buffered:1; // buffered mode for more
	uint8_t showtsize:1; // show terminal size on change
	uint8_t clearmorel:1; // clear more prompt after line
	uint8_t clearmorep:1; // clear more prompt after page
	uint8_t clearmorec:1; // clear more prompt after cont
	uint8_t clearmoreq:1; // clear more prompt after quit
	uint8_t handlectrlz:1; // process ctrl-z shortcut
	uint8_t ctrlzexeccmd:1; // when ctrl-z is hit, execute command in buffer
};

typedef enum {
	MORE_PAGE,
	MORE_LINE,
	MORE_QUIT,
	MORE_CONT,
	MORE_CTRC,
	MORE_NONE,
} more_type;

// }}}

inline void yacli_set_hint_i(yacli *cli,int hint) { // {{{
	if (!cli)
		return;
	cli->hint=hint;
} // }}}

inline int yacli_get_hint_i(yacli *cli) { // {{{
	if (!cli)
		return 0;
	return cli->hint;
} // }}}

inline void yacli_set_hint_p(yacli *cli,void *hint) { // {{{
	if (!cli)
		return;
	cli->phint=hint;
} // }}}

inline void *yacli_get_hint_p(yacli *cli) { // {{{
	if (!cli)
		return NULL;
	return cli->phint;
} // }}}

inline yascreen *yacli_get_screen(yacli *cli) { // {{{
	if (!cli)
		return NULL;

	return cli->s;
} // }}}

static inline int yacli_regx(const char *reg,const char *str) { // {{{
	regex_t re;
	int ret;

	if (regcomp(&re,reg,REG_EXTENDED)!=0)
		return 1; // no match
	ret=regexec(&re,str,0,NULL,0);
	regfree(&re);
	return ret;
} // }}}

inline void yacli_set_showtermsize(yacli *cli,int v) { // {{{
	if (!cli)
		return;
	cli->showtsize=!!v;
} // }}}

static char myver[]="\0Yet another command line interface library (https://github.com/bbonev/yacli) $Revision: 4.0 $\n\n"; // {{{
// }}}

inline const char *yacli_ver(void) { // {{{
	return myver;
} // }}}

static inline int yacli_buf_inc(char **buf,int *siz,int *len,int add) { // {{{
	// increase buffer by len, if needed
	// return 0 on success, non-zero on error
	if (add<=0)
		return 0; // ok
	if (*siz<=*len+add) { // need to realloc
		int alloclen=*siz+(*len/BUFFER_STEP+1)*BUFFER_STEP;
		char *n=calloc(1,alloclen);

		if (!n) // no free mem, nothing more to do
			return -1;

		if (*siz&&*buf)
			memcpy(n,*buf,*siz);
		if (*buf)
			free(*buf);
		*buf=n;
		*siz=alloclen;
	}
	return 0;
} // }}}

static inline void yacli_wr_buf(yacli *cli,const char *data,size_t len) { // {{{
	if (!cli)
		return;

	if (yacli_buf_inc(&cli->morebuf,&cli->moresiz,&cli->morelen,len)) // alloc error
		return;

	if (len<=0) // noop
		return;

	memcpy(cli->morebuf+cli->morelen,data,len);
	cli->morelen+=len;
} // }}}

static inline int yacli_write_more(yacli *cli,const char *s,size_t len) { // {{{
	int i;

	if (!cli)
		return -1;

	if (cli->more&&!cli->buffered&&cli->lines+1>=cli->sy) { // if last command was exact but didn't toggle more prompt, do that now
		cli->lines=0;
		cli->redraw=1;
		cli->buffered=1;
		cli->state=IN_MORE;
	}

	if (cli->buffered) { // append to buffer in buffered mode
		yacli_wr_buf(cli,s,len);
		return len;
	}

	for (i=0;i<len;i++) { // count lines
		if (s[i]=='\n')
			cli->lines++;
		if (cli->more&&cli->lines+1>=cli->sy) { // if exceeded, do partial output and switch to buffered mode
			yascreen_write(cli->s,s,i+1);
			if (len>i+1) { // switch to more mode, only if there is more text
				cli->lines=0;
				cli->redraw=1;
				cli->buffered=1;
				cli->state=IN_MORE;
				yacli_wr_buf(cli,s+i+1,len-i-1);
			}
			return len;
		}
	}

	yascreen_write(cli->s,s,len);

	return len;
} // }}}

static inline int yacli_write_nof(yacli *cli,const char *s,size_t len) { // {{{
	int i,j=0;

	if (!cli)
		return -1;

	for (i=0;i<len;i++) {
		if (s[i]=='\n') { // process a new line
			if (i&&s[i-1]=='\r') { // just in case the sequence is already there
				yacli_write_more(cli,s+j,i-j+1);
			} else { // convert \n to \r\n
				yacli_write_more(cli,s+j,i-j);
				yacli_write_more(cli,"\r\n",2);
			}
			j=i+1;
		}
	}
	if (j<len)
		yacli_write_more(cli,s+j,len-j);
	return len;
} // }}}

static inline int yacli_filter_feed_noop(filter_inst *fltr,const char *line,int len) { // {{{
	if (!fltr)
		return -1;
	if (!fltr->fltr)
		return -1;
	if (!fltr->fltr->cli)
		return -1;

	return yacli_write_nof(fltr->fltr->cli,line,len);
} // }}}

static inline void yacli_filter_done_noop(filter_inst *fltr) { // {{{
	if (!fltr)
		return;
	if (!fltr->fltr)
		return;
	if (!fltr->fltr->cli)
		return;

	return;
} // }}}

static inline void yacli_add_fcmd_s(yacli *cli,filter_inst *f) { // {{{
	filter_inst **p;

	if (!cli)
		return;

	if (f->next) {
		#if DEBUG
		fprintf(stderr,"yacli_add_fcmd_s called with list or not properly initialized fields\n");
		#endif
		return;
	}

	p=&cli->fcmd;
	while (*p&&*p!=&cli->noopi) // try to keep the list always ending in noop node
		p=&(*p)->next;
	f->next=*p;
	*p=f;
} // }}}

static inline void yacli_add_fcmd(yacli *cli,filter *fltr,char *params) { // {{{
	filter_inst *f;

	if (!cli)
		return;
	if (!fltr)
		return;
	if (!params)
		return;

	f=calloc(1,sizeof *f);
	if (!f)
		return;

	f->next=NULL;
	f->fltr=fltr;
	f->params=strdup(params);
	if (!f->params) {
		free(f);
		return;
	}
	yacli_add_fcmd_s(cli,f);
} // }}}

static inline void yacli_free_fcmd(yacli *cli) { // {{{
	filter_inst *t;

	if (!cli)
		return;

	if (cli->fcmd&&cli->fcmd->fltr&&cli->fcmd->fltr->done)
		cli->fcmd->fltr->done(cli->fcmd);
	while (cli->fcmd) {
		t=cli->fcmd;
		cli->fcmd=cli->fcmd->next;
		if (t&&t!=&cli->noopi) {
			if (t->buf)
				free(t->buf);
			if (t->params)
				free(t->params);
			free(t);
		}
	}
	yacli_add_fcmd_s(cli,&cli->noopi);
} // }}}

static inline void *yacli_add_filter(yacli *cli,const char *cmd,const char *help,int (*feed)(filter_inst *flti,const char *line,int len),void (*done)(filter_inst *flti),int allownext) { // {{{
	filter **place,*t;

	if (!cli)
		return NULL;

	if (!cmd)
		return NULL;

	place=&cli->flts;

	while (*place) {
		int cmp=strcmp(cmd,(*place)->cmd);

		if (cmp==0) // duplicate filter
			return NULL;
		if (cmp<0) // found proper place
			break;
		place=&(*place)->next;
	}

	t=calloc(1,sizeof *t);
	if (!t)
		return NULL;

	t->cli=cli;
	t->cmd=strdup(cmd);
	t->help=strdup(help?help:"");
	t->allownext=!!allownext;
	t->feed=feed;
	t->done=done;
	t->next=*place;
	*place=t;

	return t;
} // }}}

static inline int yacli_filter_feed_include(filter_inst *fltr,const char *line,int len) { // {{{
	int i;

	if (!fltr)
		return -1;
	if (!fltr->fltr)
		return -1;
	if (!fltr->fltr->cli)
		return -1;
	if (!fltr->next)
		return -1;
	if (!fltr->next->fltr)
		return -1;
	if (!fltr->next->fltr->feed)
		return -1;

	if (yacli_buf_inc(&fltr->buf,&fltr->bufsiz,&fltr->buflen,fltr->buflen+len+1)) // +1 for zero term
		return -1; // no memory

	memcpy(fltr->buf+fltr->buflen,line,len);
	fltr->buf[fltr->buflen+len]=0;
	fltr->buflen+=len;

redo:
	for (i=0;i<fltr->buflen;i++) {
		if (fltr->buf[i]=='\n') {
			fltr->buf[i]=0;
			if (strstr(fltr->buf,fltr->params)) { // pass the line
				fltr->buf[i]='\n';
				fltr->next->fltr->feed(fltr->next,fltr->buf,i+1);
			}
			if (i+1<fltr->buflen)
				memmove(fltr->buf,fltr->buf+i+1,fltr->buflen-i-1);
			fltr->buflen-=i+1;
			goto redo;
		}
	}

	return len;
} // }}}

static inline void yacli_filter_done_include(filter_inst *fltr) { // {{{
	if (!fltr)
		return;
	if (!fltr->fltr)
		return;
	if (!fltr->fltr->cli)
		return;

	if (fltr->buf&&fltr->buflen) {
		if (strstr(fltr->buf,fltr->params)) { // pass the line
			fltr->next->fltr->feed(fltr->next,fltr->buf,fltr->buflen); // pass remaining buffer, which does not contain \n
			fltr->next->fltr->feed(fltr->next,"\n",1); // pass finishing \n
		}
		fltr->buflen=0;
	}
	fltr->next->fltr->done(fltr->next);
	return;
} // }}}

static inline int yacli_filter_feed_exclude(filter_inst *fltr,const char *line,int len) { // {{{
	int i;

	if (!fltr)
		return -1;
	if (!fltr->fltr)
		return -1;
	if (!fltr->fltr->cli)
		return -1;
	if (!fltr->next)
		return -1;
	if (!fltr->next->fltr)
		return -1;
	if (!fltr->next->fltr->feed)
		return -1;

	if (yacli_buf_inc(&fltr->buf,&fltr->bufsiz,&fltr->buflen,fltr->buflen+len+1)) // +1 for zero term
		return -1; // no memory

	memcpy(fltr->buf+fltr->buflen,line,len);
	fltr->buf[fltr->buflen+len]=0;
	fltr->buflen+=len;

redo:
	for (i=0;i<fltr->buflen;i++) {
		if (fltr->buf[i]=='\n') {
			fltr->buf[i]=0;
			if (!strstr(fltr->buf,fltr->params)) { // pass the line
				fltr->buf[i]='\n';
				fltr->next->fltr->feed(fltr->next,fltr->buf,i+1); // pass error code
			}
			if (i+1<fltr->buflen)
				memmove(fltr->buf,fltr->buf+i+1,fltr->buflen-i-1);
			fltr->buflen-=i+1;
			goto redo;
		}
	}

	return len;
} // }}}

static inline void yacli_filter_done_exclude(filter_inst *fltr) { // {{{
	if (!fltr)
		return;
	if (!fltr->fltr)
		return;
	if (!fltr->fltr->cli)
		return;

	if (fltr->buf&&fltr->buflen) {
		if (!strstr(fltr->buf,fltr->params)) { // pass the line
			fltr->next->fltr->feed(fltr->next,fltr->buf,fltr->buflen); // pass remaining buffer, which does not contain \n
			fltr->next->fltr->feed(fltr->next,"\n",1); // pass finishing \n
		}
		fltr->buflen=0;
	}
	fltr->next->fltr->done(fltr->next);
	return;
} // }}}

static inline int yacli_filter_feed_count(filter_inst *fltr,const char *line,int len) { // {{{
	int i;

	if (!fltr)
		return -1;

	for (i=0;i<len;i++)
		if (line[i]=='\n')
			fltr->private[0]++;

	return len;
} // }}}

static inline void yacli_filter_done_count(filter_inst *fltr) { // {{{
	char s[200];

	if (!fltr)
		return;
	if (!fltr->next)
		return;
	if (!fltr->next->fltr)
		return;
	if (!fltr->next->fltr->feed)
		return;
	if (!fltr->next->fltr->done)
		return;

	snprintf(s,sizeof s,"Line count: %ld\n",fltr->private[0]);

	fltr->next->fltr->feed(fltr->next,s,strlen(s));
	fltr->next->fltr->done(fltr->next);
	return;
} // }}}

inline yacli *yacli_init(yascreen *s) { // {{{
	yacli *cli=calloc(1,sizeof *cli);

	if (!cli)
		return NULL;

	if (!s) {
		free(cli);
		return NULL;
	}

	cli->s=s;

	if (myver[0]==0) { // reformat the static version string
		char *rev=strstr(myver+1,"$Revision: ");
		int vermaj,vermin;

		if (rev) {
			sscanf(rev+strlen("$Revision: "),"%d.%d",&vermaj,&vermin);
			vermaj+=vermin/100;
			vermin=vermin%100;
			memmove(myver,myver+1,strlen(myver+1)+1);
			snprintf(rev-1,sizeof myver-(rev-1-myver),"%d.%d\n\n",vermaj,vermin);
		}
	}

	cli->hostname=strdup("none");
	if (!cli->hostname)
		goto allocerror;
	cli->cstack=NULL;
	cli->modes=NULL;
	cli->moreprompt=strdup("<<< more >>> [ enter=line | space=page | c=continue | q=quit ] <<< more >>>");
	if (!cli->moreprompt)
		goto allocerror;
	cli->level=strdup("#");
	if (!cli->level)
		goto allocerror;
	cli->banner=strdup(myver);
	if (!cli->banner)
		goto allocerror;
	cli->sx=80;
	cli->sy=25;
	cli->buffer=malloc(BUFFER_STEP);
	if (!cli->buffer)
		goto allocerror;
	cli->bufsiz=BUFFER_STEP;
	cli->buflen=0;
	cli->bufpos=0;
	cli->cursor=0;
	cli->state=IN_NORM;
	cli->redraw=1;
	cli->retcode=YACLI_LOOP;
	cli->istelnet=0;
	cli->showtsize=0;
	cli->rcmd=NULL;
	cli->rpos=0;
	cli->wastab=0;
	cli->lines=0;
	cli->morebuf=strdup("");
	if (!cli->morebuf)
		goto allocerror;
	cli->morelen=0;
	cli->moresiz=0;
	cli->more=1; // use paged output by default
	cli->clearmorel=1; // remove more prompt when showing next line
	cli->clearmoreq=0; // leave more prompt when quit is pressed
	cli->clearmorec=1; // remove more prompt when continue to end of output
	cli->clearmorep=0; // leave more prompt only after whole page for clarity
	cli->parsedcmd=NULL;
	cli->parsedcnt=0;
	cli->parsedsiz=0;
	cli->handlectrlz=0;
	cli->ctrlzexeccmd=1;

	cli->noopf.next=NULL;
	cli->noopf.cli=cli;
	cli->noopf.cmd="noop";
	cli->noopf.help="";
	cli->noopf.feed=yacli_filter_feed_noop;
	cli->noopf.done=yacli_filter_done_noop;
	cli->noopf.allownext=0;
	cli->noopi.next=NULL;
	cli->noopi.fltr=&cli->noopf;
	cli->noopi.params=NULL;
	cli->noopi.buf=NULL;
	cli->noopi.bufsiz=0;
	cli->noopi.buflen=0;

	yacli_add_fcmd_s(cli,&cli->noopi);

	yacli_add_filter(cli,"include","Filter output that contains the paramter text",yacli_filter_feed_include,yacli_filter_done_include,1);
	yacli_add_filter(cli,"exclude","Filter output that contains the paramter text",yacli_filter_feed_exclude,yacli_filter_done_exclude,1);
	yacli_add_filter(cli,"count","Display output line count",yacli_filter_feed_count,yacli_filter_done_count,0);

	return cli;

allocerror:
	if (cli->morebuf)
		free(cli->morebuf);
	if (cli->buffer)
		free(cli->buffer);
	if (cli->banner)
		free(cli->banner);
	if (cli->level)
		free(cli->level);
	if (cli->moreprompt)
		free(cli->moreprompt);
	if (cli->hostname)
		free(cli->hostname);
	free(cli);
	return NULL;
} // }}}

static inline void yacli_free_parsed(yacli *cli) { // {{{
	int i;

	if (!cli)
		return;

	if (!cli->parsedcmd) {
		cli->parsedcnt=0;
		cli->parsedsiz=0;
		return;
	}

	for (i=0;i<cli->parsedcnt;i++)
		free(cli->parsedcmd[i]);
	free(cli->parsedcmd);
	cli->parsedcmd=NULL;
	cli->parsedcnt=0;
	cli->parsedsiz=0;
} // }}}

static inline void yacli_clear_parsed(yacli *cli) { // {{{
	int i;

	if (!cli)
		return;

	if (!cli->parsedcmd) {
		cli->parsedcnt=0;
		cli->parsedsiz=0;
		return;
	}

	for (i=0;i<cli->parsedcnt;i++)
		free(cli->parsedcmd[i]);
	cli->parsedcnt=0;
} // }}}

static inline void yacli_add_parsed(yacli *cli,const char *word) { // {{{
	int step=BUFFER_STEP/sizeof(char *);
	char *dw;

	if (!cli)
		return;

	dw=strdup(word);
	if (!dw)
		return;

	if (!cli->parsedcmd) {
		cli->parsedcmd=calloc(sizeof(char *),step);
		if (!cli->parsedcmd) { // memory alloc error
			free(dw);
			return;
		}
		cli->parsedcnt=0;
		cli->parsedsiz=step;
	}
	if (cli->parsedcnt>=cli->parsedsiz) {
		int cnt=cli->parsedcnt+2; // one for terminating NULL, one for the new item
		char **nc=calloc(sizeof(char *),step*(cnt/step+1));

		if (!nc) { // memory alloc error
			free(dw);
			return;
		}
		memcpy(nc,cli->parsedcmd,(cli->parsedcnt+1)*sizeof(char *));
		free(cli->parsedcmd);
		cli->parsedcmd=nc;
		cli->parsedsiz=step*(cnt/step+1);
	}

	cli->parsedcmd[cli->parsedcnt++]=dw;
	cli->parsedcmd[cli->parsedcnt]=NULL;
} // }}}

inline void yacli_set_more(yacli *cli,int on) { // {{{
	if (!cli)
		return;
	cli->more=!!on;
} // }}}

inline void yacli_set_more_clear(yacli *cli,int ln,int pg,int co,int qu) { // {{{
	if (!cli)
		return;
	cli->clearmorel=!!ln;
	cli->clearmorep=!!pg;
	cli->clearmorec=!!co;
	cli->clearmoreq=!!qu;
} // }}}

inline void yacli_set_ctrlz(yacli *cli,int on) { // {{{
	if (!cli)
		return;
	cli->handlectrlz=!!on;
} // }}}

inline void yacli_set_ctrlz_exec(yacli *cli,int on) { // {{{
	if (!cli)
		return;
	cli->ctrlzexeccmd=!!on;
} // }}}

inline void yacli_set_banner(yacli *cli,const char *banner) { // {{{
	char *t;

	if (!cli)
		return;

	t=cli->banner;
	cli->banner=strdup(banner);
	if (!cli->banner)
		cli->banner=t;
	else
		free(t);
} // }}}

inline void yacli_set_level(yacli *cli,const char *level) { // {{{
	char *t;

	if (!cli)
		return;

	t=cli->level;
	cli->level=strdup(level);
	if (!cli->level)
		cli->level=t;
	else
		free(t);
	cli->redraw=1;
} // }}}

inline void yacli_set_hostname(yacli *cli,const char *hostname) { // {{{
	char *t;

	if (!cli)
		return;

	t=cli->hostname;
	cli->hostname=strdup(hostname);
	if (!cli->hostname)
		cli->hostname=t;
	else
		free(t);
	cli->redraw=1;
} // }}}

static inline void yacli_more_prompt(yacli *cli) { // {{{
	if (!cli)
		return;

	yascreen_clearln(cli->s);
	yascreen_puts(cli->s,"\r");
	yascreen_puts(cli->s,cli->moreprompt);
} // }}}

static inline void yacli_more_clear_prompt(yacli *cli,more_type t) { // {{{
	if (!cli)
		return;

	switch (t) {
		case MORE_LINE:
			if (cli->clearmorel) {
				yascreen_clearln(cli->s);
				yascreen_puts(cli->s,"\r"); // clear more prompt
			} else
				yascreen_puts(cli->s,"\r\n"); // go next line
			break;
		case MORE_PAGE:
			if (cli->clearmorep) {
				yascreen_clearln(cli->s);
				yascreen_puts(cli->s,"\r"); // clear more prompt
			} else
				yascreen_puts(cli->s,"\r\n"); // go next line
			break;
		case MORE_QUIT:
			if (cli->clearmoreq) {
				yascreen_clearln(cli->s);
				yascreen_puts(cli->s,"\r"); // clear more prompt
			} else
				yascreen_puts(cli->s," quit\r\n"); // print the reason and go next line
			break;
		case MORE_CONT:
			if (cli->clearmorec) {
				yascreen_clearln(cli->s);
				yascreen_puts(cli->s,"\r"); // clear more prompt
			} else
				yascreen_puts(cli->s,"\r\n"); // go next line
			break;
		case MORE_CTRC:
			yascreen_puts(cli->s," ^C\r\n"); // print the reason and go next line
			break;
		case MORE_NONE:
			break;
	}
} // }}}

static inline int yacli_print_nof(yacli *cli,const char *format,...) { // {{{
	va_list ap;
	char *ns;
	int size;

	if (!cli)
		return -1;

	va_start(ap,format);
	size=vasprintf(&ns,format,ap);
	va_end(ap);

	if (size==-1) // some error, nothing more to do
		return size;

	yacli_write_nof(cli,ns,strlen(ns));

	free(ns);

	return size;
} // }}}

inline int yacli_write(yacli *cli,const char *s,size_t len) { // {{{
	if (!cli)
		return -1;
	if (!cli->fcmd)
		return -1;
	if (!cli->fcmd->fltr)
		return -1;
	if (!cli->fcmd->fltr->feed)
		return -1;

	return cli->fcmd->fltr->feed(cli->fcmd,s,len);
} // }}}

inline int yacli_print(yacli *cli,const char *format,...) { // {{{
	va_list ap;
	char *ns;
	int size;

	if (!cli)
		return -1;
	if (!cli->fcmd)
		return -1;
	if (!cli->fcmd->fltr)
		return -1;
	if (!cli->fcmd->fltr->feed)
		return -1;

	va_start(ap,format);
	size=vasprintf(&ns,format,ap);
	va_end(ap);

	if (size==-1) // some error, nothing more to do
		return size;

	cli->fcmd->fltr->feed(cli->fcmd,ns,strlen(ns));

	free(ns);

	return size;
} // }}}

static inline int yacli_promptlen(yacli *cli) { // {{{
	int promptlen;

	if (!cli)
		return 0;

	// "hostname(mode1-mode2-mode3)# "
	promptlen=strlen(cli->hostname);
	if (cli->modes&&strlen(cli->modes))
		promptlen+=strlen(cli->modes); // includes opening and closing braces
	promptlen+=1+strlen(cli->level);
	return promptlen;
} // }}}

static inline int yacli_search_promptlen(yacli *cli) { // {{{
	int promptlen;

	if (!cli)
		return 0;

	// "(i-search)'sbuf': result"
	promptlen=strlen("(i-search)");
	promptlen+=2; // quotes
	if (cli->sbuf)
		promptlen+=strlen(cli->sbuf); // search buffer
	promptlen+=2; // colon space
	return promptlen;
} // }}}

static inline int yacli_dispspace(yacli *cli) { // {{{
	if (!cli)
		return 0;

	return cli->sx-yacli_promptlen(cli)-1; // last char cannot be used
} // }}}

static inline int yacli_search_dispspace(yacli *cli) { // {{{
	if (!cli)
		return 0;

	return cli->sx-yacli_search_promptlen(cli)-1; // last char cannot be used
} // }}}

static inline int yacli_linelen(yacli *cli) { // {{{
	int linelen;

	if (!cli)
		return 0;

	linelen=cli->buflen-cli->bufpos;
	if (linelen>yacli_dispspace(cli))
		linelen=yacli_dispspace(cli)-1;

	return linelen;
} // }}}

static inline int yacli_search_linelen(yacli *cli) { // {{{
	int linelen;

	if (!cli)
		return 0;

	linelen=0;
	if (cli->rcmd)
		linelen=strlen(cli->rcmd);
	if (linelen>yacli_search_dispspace(cli))
		linelen=yacli_search_dispspace(cli)-1;

	return linelen;
} // }}}

static inline char yacli_begc(yacli *cli) { // {{{
	char begc;

	if (!cli)
		return 0;

	begc=cli->bufpos?'$':' ';

	return begc;
} // }}}

static inline char *yacli_endc(yacli *cli) { // {{{
	char *endc="";
	int linelen;

	if (!cli)
		return 0;

	linelen=cli->buflen-cli->bufpos;
	if (linelen>yacli_dispspace(cli))
		endc="$";

	return endc;
} // }}}

static inline char *yacli_search_endc(yacli *cli) { // {{{
	char *endc="";
	int linelen;

	if (!cli)
		return 0;

	linelen=0;
	if (cli->rcmd)
		linelen=strlen(cli->rcmd);
	if (linelen>yacli_search_dispspace(cli))
		endc="$";

	return endc;
} // }}}

static inline int yacli_bufpos_shiftr(yacli *cli) { // {{{
	if (!cli)
		return 0;

	if (cli->buflen-cli->bufpos<yacli_dispspace(cli)) // no right truncate or exact right end - do not shift
		return 0;

	if (cli->cursor-cli->bufpos>=yacli_dispspace(cli)-2) // shift is needed
		return 1;

	return 0;
} // }}}

static inline void yacli_search_prompt(yacli *cli) { // {{{
	int promptlen=yacli_search_promptlen(cli);
	int linelen=yacli_search_linelen(cli);
	char *endc=yacli_search_endc(cli);
	char *sbuf=cli->sbuf?cli->sbuf:"";
	char *rcmd=cli->rcmd?cli->rcmd:"";

	yascreen_print(cli->s,"%s\r(i-search)'%s': %.*s%s\r\e[%dC",yascreen_clearln_s(cli->s),sbuf,linelen,rcmd,endc,promptlen-3);
} // }}}

static inline void yacli_prompt(yacli *cli) { // {{{
	int promptlen;
	int linelen;
	char begc;
	char *endc;
	int curpos;

	if (!cli)
		return;

	promptlen=yacli_promptlen(cli);
	linelen=yacli_linelen(cli);
	begc=yacli_begc(cli);
	endc=yacli_endc(cli);

	if (cli->state==IN_SEARCH) {
		yacli_search_prompt(cli);
		return;
	} else if (cli->state==IN_MORE) {
		yacli_more_prompt(cli);
		return;
	}

	cli->lines=0;

	curpos=cli->cursor-cli->bufpos; // zero based in buffer
	curpos+=promptlen;

	yascreen_print(cli->s,"%s\r%s%s%s%c%.*s%s\r\e[%dC",yascreen_clearln_s(cli->s),cli->hostname,cli->modes?cli->modes:"",cli->level,begc,linelen,cli->buffer+cli->bufpos,endc,curpos);
	cli->redraw=0;
} // }}}

inline void yacli_message(yacli *cli,const char *line) { // {{{
	const char *p,*q;

	if (!cli)
		return;
	if (!line)
		return;

	if (!cli->incmdcb)
		yascreen_print(cli->s,"%s\r",yascreen_clearln_s(cli->s)); // clear prompt line
	p=line;
	while (*p) {
		q=p;
		while (*q&&*q!='\n')
			q++;
		if (q-p)
			yascreen_print(cli->s,"%.*s",(int)(q-p),p);
		if (*q=='\n') {
			yascreen_print(cli->s,"\r\n");
			p=q+1;
		} else {
			yascreen_print(cli->s,"\n"); // append new line, if it was missing
			p=q;
		}
	}
	if (!cli->incmdcb)
		yacli_prompt(cli);
} // }}}

static inline void yacli_eof(yacli *cli) { // {{{
	if (!cli)
		return;

	cli->retcode=YACLI_EOF;
} // }}}

inline void yacli_exit(yacli *cli) { // {{{
	if (!cli)
		return;

	yacli_eof(cli);
} // }}}

inline int yacli_add_hist(yacli *cli,const char *buf) { // {{{
	history *h;

	if (!cli)
		return 0;

	cli->hist_p=NULL;

	if (!strlen(buf)) // skip empty command
		return 0;
	if (cli->hist&&!strcmp(buf,cli->hist->prev->command)) // skip repeated command
		return 0;

	h=calloc(1,sizeof *h);

	if (!h)
		return -1;

	h->command=strdup(buf);
	if (!h->command) {
		free(h);
		return -1;
	}

	if (!cli->hist) {
		cli->hist=h;
		h->prev=h->next=h;
	} else {
		h->next=cli->hist;
		h->prev=cli->hist->prev;
		cli->hist->prev=h;
		h->prev->next=h;
	}
	return 0;
} // }}}

static inline void yacli_moveleft(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->cursor) {
		cli->cursor--;
		if (cli->cursor<cli->bufpos)
			cli->bufpos--;
		cli->redraw=1;
	}
} // }}}

static inline void yacli_moveright(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->cursor<cli->buflen) {
		int shiftr=yacli_bufpos_shiftr(cli);

		cli->cursor++;
		if (shiftr)
			cli->bufpos++;
		cli->redraw=1;
	}
} // }}}

static inline void yacli_moveleftw(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->cursor) {
		if (cli->buffer[cli->cursor]!=' '&&cli->buffer[cli->cursor-1]==' ') // if we are at word begin, step left
			cli->cursor--;
		while (cli->cursor&&cli->buffer[cli->cursor]==' ') // skip space
			cli->cursor--;
		if (cli->cursor&&cli->buffer[cli->cursor]!=' ') { // skip word
			while (cli->cursor&&cli->buffer[cli->cursor]!=' ')
				cli->cursor--;
			if (cli->buffer[cli->cursor]==' ')
				cli->cursor++; // step right at the begining of the word
		}
		if (cli->cursor<cli->bufpos)
			cli->bufpos=cli->cursor;
		cli->redraw=1;
	}
} // }}}

static inline void yacli_moverightw(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->cursor<cli->buflen) {
		int shiftr=yacli_bufpos_shiftr(cli);

		while (cli->cursor<cli->buflen&&cli->buffer[cli->cursor]==' ') { // skip space
			cli->cursor++;
			if (shiftr)
				cli->bufpos++;
			shiftr=yacli_bufpos_shiftr(cli);
		}
		while (cli->cursor<cli->buflen&&cli->buffer[cli->cursor]!=' ') { // skip word
			cli->cursor++;
			if (shiftr)
				cli->bufpos++;
			shiftr=yacli_bufpos_shiftr(cli);
		}
		cli->redraw=1;
	}
} // }}}

static inline void yacli_movehome(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->cursor||cli->bufpos) {
		cli->cursor=0;
		cli->bufpos=0;
		cli->redraw=1;
	}
} // }}}

static inline void yacli_moveend(yacli *cli) { // {{{
	int endcur;
	int endpos;

	if (!cli)
		return;

	endcur=cli->buflen;
	endpos=cli->buflen-yacli_dispspace(cli)+1;

	if (endpos<0)
		endpos=0;

	if (cli->cursor!=endcur||cli->bufpos!=endpos) {
		cli->cursor=endcur;
		cli->bufpos=endpos;
		cli->redraw=1;
	}
} // }}}

static inline void yacli_del(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->cursor<cli->buflen) {
		memmove(cli->buffer+cli->cursor,cli->buffer+cli->cursor+1,cli->buflen-cli->cursor-1);
		cli->buflen--;
		cli->redraw=1;
	}
} // }}}

static inline void yacli_bsp(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->cursor) {
		memmove(cli->buffer+cli->cursor-1,cli->buffer+cli->cursor,cli->buflen-cli->cursor);
		cli->buflen--;
		cli->cursor--;
		if (cli->bufpos>cli->cursor)
			cli->bufpos--;
		cli->redraw=1;
	}
} // }}}

static inline void yacli_delword(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->cursor>=cli->buflen) // nothing to delete
		return;

	if (cli->buffer[cli->cursor]==' ')
		while (cli->buffer[cli->cursor]==' '&&cli->cursor<cli->buflen)
			yacli_del(cli);
	while (cli->buffer[cli->cursor]!=' '&&cli->cursor<cli->buflen)
		yacli_del(cli);
} // }}}

static inline void yacli_delprevword(yacli *cli) { // {{{
	if (!cli)
		return;

	if (!cli->cursor) // nothing to delete
		return;

	if (cli->buffer[cli->cursor-1]==' ')
		while (cli->buffer[cli->cursor-1]==' '&&cli->cursor)
			yacli_bsp(cli);
	while (cli->buffer[cli->cursor-1]!=' '&&cli->cursor)
		yacli_bsp(cli);
} // }}}

static inline void yacli_deltoend(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->cursor<cli->buflen) {
		cli->buflen=cli->cursor;
		cli->redraw=1;
	}
} // }}}

static inline void yacli_setbuf(yacli *cli,const char *buf) { // {{{
	int len;
	int add;

	if (!cli)
		return;

	len=strlen(buf);
	add=len+1-cli->bufsiz; // add 1 for zero term by strcpy

	if (yacli_buf_inc(&cli->buffer,&cli->bufsiz,&cli->buflen,add)) // error in malloc
		return;

	strncpy(cli->buffer,buf,cli->bufsiz);
	cli->buflen=len;
	cli->cursor=len;
	cli->redraw=1;
} // }}}

static inline void yacli_buf_zeroterm(yacli *cli) { // {{{
	if (!cli)
		return;

	if (yacli_buf_inc(&cli->buffer,&cli->bufsiz,&cli->buflen,1)) // error in malloc
		return;
	cli->buffer[cli->buflen]=0; // zero terminate the buffer
} // }}}

static inline void yacli_up(yacli *cli) { // {{{
	if (!cli)
		return;

	if (!cli->hist_p&&!cli->hist) // no history
		return;
	if (!cli->hist_p) {
		yacli_buf_zeroterm(cli); // zero terminate the buffer
		if (cli->savbuf) // free old saved buffer
			free(cli->savbuf);
		cli->savbuf=strdup(cli->buffer); // save current buffer
		cli->hist_p=cli->hist; // start with last history command
		cli->hist_p=cli->hist_p->prev;
		yacli_setbuf(cli,cli->hist_p->command);
	} else { // we are already somewhere in history
		if (cli->hist_p==cli->hist) // limit history rollover
			return;
		cli->hist_p=cli->hist_p->prev;
		yacli_setbuf(cli,cli->hist_p->command);
	}
} // }}}

static inline void yacli_down(yacli *cli) { // {{{
	if (!cli)
		return;

	if (!cli->hist_p) // do not allow history rollover
		return;
	cli->hist_p=cli->hist_p->next;
	if (cli->hist_p==cli->hist) { // restore previously saved command
		cli->hist_p=NULL;
		yacli_setbuf(cli,cli->savbuf);
		free(cli->savbuf);
		cli->savbuf=NULL;
		return;
	}
	yacli_setbuf(cli,cli->hist_p->command);
} // }}}

static inline void yacli_start_search(yacli *cli) { // {{{
	if (!cli)
		return;

	cli->state=IN_SEARCH;
	if (cli->sbuf)
		free(cli->sbuf);
	cli->sbuf=NULL;
	cli->redraw=1;
} // }}}

static inline void yacli_end_search(yacli *cli) { // {{{
	if (!cli)
		return;

	cli->state=IN_NORM;
	if (cli->sbuf)
		free(cli->sbuf);
	cli->sbuf=NULL;
	cli->redraw=1;
	if (cli->rcmd)
		yacli_setbuf(cli,cli->rcmd);
	cli->rcmd=NULL;
} // }}}

static inline void yacli_find_first(yacli *cli,int skip) { // {{{
	int rpos=0;
	int sl;

	if (!cli)
		return;

	sl=cli->sbuf?strlen(cli->sbuf):0;

	cli->rcmd=NULL;
	if (cli->hist) { // find most recent matching command
		history *h;

		h=cli->hist->prev;
		do {
			if (sl&&strstr(h->command,cli->sbuf)) {
				cli->rcmd=h->command; // save last found command
				if (!skip) { // if we do not skip, use it
					rpos++;
					break;
				} else {
					skip--; // try to find another
					rpos++;
				}
			}

			h=h->prev;
		} while (h!=cli->hist->prev);
		rpos--;
		if (cli->rcmd)
			cli->rpos=rpos;
	}
} // }}}

static inline void yacli_search_up(yacli *cli) { // {{{
	int rpos;

	if (!cli)
		return;

	rpos=cli->rpos;

	cli->rpos++;
	yacli_find_first(cli,cli->rpos);

	if (rpos!=cli->rpos) // have found one more
		cli->redraw=1;
} // }}}

static inline void yacli_search_down(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->rpos) {
		cli->rpos--;
		yacli_find_first(cli,cli->rpos);
		cli->redraw=1;
	}
} // }}}

static inline void yacli_search_bsp(yacli *cli) { // {{{
	if (!cli)
		return;

	if (!cli->sbuf)
		return;
	if (strlen(cli->sbuf)) {
		cli->sbuf[strlen(cli->sbuf)-1]=0;
		cli->redraw=1;

		// now find appropriate command
		yacli_find_first(cli,0);
	}
} // }}}

static inline void yacli_add_search(yacli *cli,unsigned char ch) { // {{{
	int l;

	if (!cli)
		return;

	if (!cli->sbuf) {
		cli->sbuf=malloc(2);
		if (!cli->sbuf)
			return;
		cli->sbuf[0]=0;
	} else {
		char *t=cli->sbuf;
		size_t sz=strlen(t)+2;

		cli->sbuf=malloc(sz);
		if (!cli->sbuf) {
			cli->sbuf=t;
			return;
		}
		strncpy(cli->sbuf,t,sz);
		free(t);
	}
	l=strlen(cli->sbuf);
	cli->sbuf[l]=ch;
	cli->sbuf[l+1]=0;
	cli->redraw=1;

	// now find appropriate command
	yacli_find_first(cli,0);
} // }}}

static inline void yacli_delall(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->buflen||cli->bufpos||cli->cursor) {
		cli->buflen=0;
		cli->bufpos=0;
		cli->cursor=0;
		cli->redraw=1;
	}
	yacli_free_fcmd(cli);
} // }}}

static inline void yacli_ctrl_c(yacli *cli) { // {{{
	if (!cli)
		return;

	yacli_delall(cli);
	cli->redraw=1; // always redraw after ^C
	yascreen_puts(cli->s,"^C\r\n");
	if (cli->savbuf) { // kill last saved command
		free(cli->savbuf);
		cli->savbuf=NULL;
	}
	cli->hist_p=NULL; // reset history position
} // }}}

static inline void yacli_ctrl_d(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->buflen)
		yacli_del(cli);
	else {
		yacli_eof(cli);
		yascreen_puts(cli->s,"\r\n"); // keep consistent with command that caused exit, because enter prints new line
	}
} // }}}

static inline void yacli_insert(yacli *cli,char ch) { // {{{
	if (!cli)
		return;

	if (yacli_buf_inc(&cli->buffer,&cli->bufsiz,&cli->buflen,1)) // error in malloc
		return;
	memmove(cli->buffer+cli->cursor+1,cli->buffer+cli->cursor,cli->buflen-cli->cursor);
	cli->buflen++;
	cli->buffer[cli->cursor]=ch;
	cli->redraw=1;
	yacli_moveright(cli);
} // }}}

inline void yacli_winch(yacli *cli) { // {{{
	if (!cli)
		return;

	yascreen_reqsize(cli->s);
} // }}}

static inline void yacli_clear(yacli *cli) { // {{{
	if (!cli)
		return;

	yascreen_clear(cli->s);
	yacli_winch(cli);
	cli->redraw=1;
} // }}}

static inline void debugstate(yacli_in_state os,yacli_in_state ns,unsigned char ch) { // {{{
#if DEBUG
	char *ostate="?";
	char *nstate="?";

	switch (os) {
		case IN_MORE:
			ostate="IN_MORE";
			break;
		case IN_NORM:
			ostate="IN_NORM";
			break;
		case IN_C_X:
			ostate="IN_C_X";
			break;
		case IN_SEARCH:
			ostate="IN_SEARCH";
			break;
	}
	switch (ns) {
		case IN_MORE:
			nstate="IN_MORE";
			break;
		case IN_NORM:
			nstate="IN_NORM";
			break;
		case IN_C_X:
			nstate="IN_C_X";
			break;
		case IN_SEARCH:
			nstate="IN_SEARCH";
			break;
	}
	printf(" state %s(%02x[%c]) -> %s\n",ostate,ch,isprint(ch)?ch:' ',nstate);
#endif
} // }}}

inline void yacli_set_list_cb(yacli *cli,void (*listcb)(yacli *cli,void *ctx,int code)) { // {{{
	if (!cli)
		return;
	cli->listcb=listcb;
} // }}}

inline void yacli_set_cmd_cb(yacli *cli,void (*cmdcb)(yacli *cli,const char *cmd,int code)) { // {{{
	if (!cli)
		return;
	cli->cmdcb=cmdcb;
} // }}}

inline void yacli_set_ctrlz_cb(yacli *cli,void (*ctrlzcb)(yacli *cli)) { // {{{
	if (!cli)
		return;
	cli->ctrlzcb=ctrlzcb;
} // }}}

static inline void yacli_cmd_free(cmnode *cn) { // {{{
	cmnode *n,*c,*d;

	if (!cn)
		return;

	c=cn->child;
	n=cn->next;
	d=cn->dyn;

	if (cn->cmd)
		free(cn->cmd);
	if (cn->help)
		free(cn->help);
	free(cn);
	if (c)
		yacli_cmd_free(c);
	if (n)
		yacli_cmd_free(n);
	if (d)
		yacli_cmd_free(d);
} // }}}

static inline void yacli_dyn_vacuum(cmnode *n) { // {{{
	if (!n)
		return;
	if (n->dyn)
		yacli_cmd_free(n->dyn);
	n->dyn=NULL;
	yacli_dyn_vacuum(n->next);
	yacli_dyn_vacuum(n->child);
} // }}}

static void yacli_dyn_upd(yacli *cli,cmnode *n) { // {{{
	cmnode *pnode;

	if (!cli)
		return;

	if (!n)
		return;
	if (!cli->listcb) // no way to update
		return;
	if (n->cmd[0]!='@') // not a dynamic node
		return;

	if (n->dyn) {
		yacli_cmd_free(n->dyn);
		n->dyn=NULL;
	}
	pnode=n;
	cli->listcb(cli,pnode,atoi(n->cmd+1));
} // }}}

static inline int yacli_cmd_dump_node(yacli *cli,cmnode *n) { // {{{
	if (!cli)
		return 1; // stop any processing on NULL cli

	if (!n)
		return 0; // this is recursion end case and should not interrupt processing
	if (n->cmd[0]=='@') {
		cmnode *p;
		int first=1;

		yacli_dyn_upd(cli,n);
		p=n->dyn;
		if (!p)
			return 1;
		while (p) {
			if (!first)
				yacli_print(cli,"\n");
			first=0;
			if (n->parent) {
				if (yacli_cmd_dump_node(cli,n->parent))
					return 1;
				yacli_print(cli," ");
			}
			yacli_print(cli,"%s",p->cmd);
			p=p->next;
		}
	} else {
		if (n->parent) {
			if (yacli_cmd_dump_node(cli,n->parent))
				return 1;
			yacli_print(cli," ");
		}
		yacli_print(cli,"%s",n->cmd[0]=='^'?n->help:n->cmd);
	}
	return 0;
} // }}}

static inline void yacli_cmd_dump_r(yacli *cli,cmnode *n) { // {{{
	if (!cli)
		return;

	if (!n)
		return;
	if (n->cb) {
		if (!yacli_cmd_dump_node(cli,n)) // returns non-zero if there is empty dynamic list in the command path
			yacli_print(cli,"\n");
	}
	yacli_cmd_dump_r(cli,n->child);
	yacli_cmd_dump_r(cli,n->next);
} // }}}

static inline void yacli_cmd_dump(yacli *cli) { // {{{
	if (!cli)
		return;

	yacli_print(cli,"%s\rCommand dump:\n",yascreen_clearln_s(cli->s));
	yacli_cmd_dump_r(cli,cli->cmdt);
	yacli_dyn_vacuum(cli->cmdt);
} // }}}

static inline void yacli_replace(yacli *cli,int pos,int len,const char *word) { // {{{
	int wlen=strlen(word);
	int add=wlen-len;

	if (!cli)
		return;

	if (yacli_buf_inc(&cli->buffer,&cli->bufsiz,&cli->buflen,1)) // assure buffer can hold what is needed
		return;
	memmove(cli->buffer+pos+wlen,cli->buffer+pos+len,cli->buflen-pos-len);
	memcpy(cli->buffer+pos,word,wlen);
	cli->buflen+=add;
	cli->redraw=1;
} // }}}

static inline void yacli_cmd_help_pr(yacli *cli,const char *cmd,const char *help,int prcr,int padto) { // {{{
	const char *pcmd=cmd[0]=='^'?help:cmd;
	const char *phlp=cmd[0]=='^'?"":help;
	const char *pcr=cmd[0]&&prcr?" <cr>":(prcr?"<cr>":"");

	if (!cli)
		return;

	yacli_print(cli,"%s%s %*s %s\n",pcmd,pcr,padto-(int)strlen(pcmd)-(int)strlen(pcr),"",phlp);
	if (!*cmd)
		yacli_print(cli,"%-*sOutput filters\n",padto+2,"|");
} // }}}

static inline int yacli_cmd_help_len(const char *cmd,const char *help,int prcr) { // {{{
	const char *pcmd=cmd[0]=='^'?help:cmd;
	const char *pcr=cmd[0]&&prcr?" <cr>":(prcr?"<cr>":"");
	int len=strlen(pcmd)+strlen(pcr);

	return len;
} // }}}

static inline void yacli_compact_spaces(yacli *cli) { // {{{
	int posd,poss;

	if (!cli)
		return;

	posd=0;
	poss=0;

	for (;;) {
		while (cli->buffer[posd]==' '&&cli->buffer[poss]==' '&&poss<cli->buflen)
			poss++;
		if (poss!=posd) {
			memmove(cli->buffer+posd,cli->buffer+poss,cli->buflen-poss);
			cli->buflen-=poss-posd;
			if (cli->cursor>=poss)
				cli->cursor-=poss-posd;
			posd++;
			poss=posd;
		}
		while (cli->buffer[poss]!=' '&&poss<cli->buflen) {
			poss++;
			posd++;
		}
		poss++; // leave at least one intermediate space
		posd++;
		if (poss>=cli->buflen)
			break;
	}
} // }}}

static inline int yacli_trycomplete(yacli *cli,int docomplete) { // {{{
	char *lastword=NULL;
	cmnode *lastcn,*cn;
	char *word,*worde;
	int alonematch=0;
	int canexalone=0;
	int completex=0;
	int complete=0;
	int havepipe=0;
	int added=0;
	int dyn=0;
	char *buf;
	char *fb;

	if (!cli)
		return 0;

	cn=cli->cmdt;

	// remove previously parsed command
	if (docomplete==2)
		yacli_clear_parsed(cli);

	if (docomplete)
		yacli_compact_spaces(cli);

	lastcn=cn;
	if (!cn) // nothing to do
		return 0;

	yacli_buf_zeroterm(cli);
	fb=buf=strdup(cli->buffer);

	if (!buf) // no memory
		return 0;

	word=buf; // start to parse the whole buffer

	while (*word) {
		while (*word==' ') // skip leading ws
			word++;
		lastword=word;
		worde=word;

		while (*worde&&*worde!=' ')
			worde++;
		if (*worde)
			*worde++=0;

		if (!strcmp(word,"|")) { // handle filters
			havepipe=1;
			word=worde;
			break;
		}

		if (!cn) { // no path in command tree
			yacli_print_nof(cli,"\nNo matched command (1)\n");
			cli->redraw=1;
			free(fb);
			if (dyn)
				yacli_dyn_vacuum(cli->cmdt);
			return 0x80;
		}

		if (strlen(word)) { // ignore trailing ws yielding empty word
			for (;;) {
				int cmp;

				if (cn->cmd[0]=='@') { // dynamic command
					yacli_dyn_upd(cli,cn);
					dyn=1;
					cn=cn->dyn;
					if (!cn)
						break;
					cmp=strcmp(word,cn->cmd);
				} else if (cn->cmd[0]=='^') { // regex
					cmp=yacli_regx(cn->cmd,word);
				} else
					cmp=strcmp(word,cn->cmd);

				if (!cmp) { // exact match (check if next is prefix and if there is space after this word)
					int pos=word-fb+added;
					int len=strlen(word);
					int nxprefix=cn->next&&strncmp(word,cn->next->cmd,strlen(word))==0&&strlen(word)<strlen(cn->next->cmd);
					int havespace=pos+len<cli->buflen&&cli->buffer[pos+len]==' ';

					if (docomplete)
						if (cli->cursor>=pos&&cli->cursor<=pos+len) { // if cursor was inside exact word, move to its end
							cli->cursor=pos+len;

							if (!nxprefix||havespace) { // leave as is, if current is exact but next is a prefix
								if (cli->cursor==cli->buflen) // cursor is at end, append space
									yacli_insert(cli,' ');
								while (cli->cursor<cli->buflen&&cli->buffer[cli->cursor]==' ') // walk spaces
									cli->cursor++;
							}
						}

					if (docomplete==2)
						yacli_add_parsed(cli,word);

					complete=!nxprefix||havespace; // last word was complete
					completex=complete&&!!(cn->isdyn?cn->parent->cb:cn->cb);
					alonematch=1;
					canexalone=!!(cn->isdyn?cn->parent->cb:cn->cb); // if next is prefix, but no space, we can exec the command
					if (canexalone)
						cli->parsedcb=cn->isdyn?cn->parent->cb:cn->cb;

					if (cn->isdyn)
						cn=cn->parent;
					cn=cn->child;
					if (cn)
						lastcn=cn;
					break;
				} else if (cmp<0) { // check for single/multiple possibilities
					int isprefix=strncmp(word,cn->cmd,strlen(word))==0&&strlen(word)<strlen(cn->cmd);
					int nxprefix=cn->next&&strncmp(word,cn->next->cmd,strlen(word))==0&&strlen(word)<strlen(cn->next->cmd);

					if (!isprefix) {
						yacli_print_nof(cli,"\nNo matched command (2)\n");
						cli->redraw=1;
						free(fb);
						if (dyn)
							yacli_dyn_vacuum(cli->cmdt);
						return 0x80;
					} else if (isprefix&&!nxprefix) { // unfinished match
						int pos=word-fb+added;
						int len=strlen(word);
						int add=strlen(cn->cmd)-len;

						if (docomplete) {
							yacli_replace(cli,pos,len,cn->cmd); // do complete
							added+=add; // calculate compensation for changed buffer position
							if (cli->cursor>=pos&&cli->cursor<=pos+len) { // if cursor was inside partial word, move to its end
								cli->cursor=pos+len+add;
								if (cli->cursor==cli->buflen) // cursor is at end, append space
									yacli_insert(cli,' ');
								while (cli->cursor<cli->buflen&&cli->buffer[cli->cursor]==' ') // walk spaces
									cli->cursor++;
							} else if (cli->cursor>pos) // if cursor is on the right, move it too
								cli->cursor+=add;
						}

						if (docomplete==2)
							yacli_add_parsed(cli,cn->cmd);

						complete=1; // last word was completed
						completex=!!(cn->isdyn?cn->parent->cb:cn->cb);
						alonematch=1;
						canexalone=!!(cn->isdyn?cn->parent->cb:cn->cb); // if next is prefix, but no space, we can exec the command
						if (canexalone)
							cli->parsedcb=cn->isdyn?cn->parent->cb:cn->cb;

						if (cn->isdyn)
							cn=cn->parent;
						cn=cn->child;
						if (cn)
							lastcn=cn;
						break;
					} else { // try to do partial complete
						cmnode *t=cn->next->next;
						int cangrow=0;
						int cnt=2; // we have 2 commands with common prefix
						int i,j;

						complete=0; // last word was not complete
						completex=0;
						cli->parsedcb=NULL;

						while (t) {
							int isprefix=strncmp(word,t->cmd,strlen(word))==0&&strlen(word)<strlen(cn->cmd);

							if (isprefix)
								cnt++;
							else
								break;
							t=t->next;
						}
						for (i=strlen(word);i<strlen(cn->cmd);i++) {
							int allmatch=1;
							t=cn->next;

							for (j=1;j<cnt;j++) {
								if (strlen(t->cmd)<=i) {
									allmatch=0;
									break;
								}
								if (t->cmd[i]!=cn->cmd[i]) {
									allmatch=0;
									break;
								}
								t=t->next;
							}
							if (allmatch)
								cangrow++;
							else
								break;
						}
						if (docomplete&&cangrow) {
							char *comm=strdup(cn->cmd);

							if (comm) { // do nothing if allocation fails
								char *toins=comm+strlen(word);
								int pos=word-fb+added;
								int len=strlen(word);

								toins[cangrow]=0;
								yacli_replace(cli,pos+strlen(word),0,toins); // do complete
								added+=cangrow;
								if (cli->cursor>=pos&&cli->cursor<=pos+len) { // if cursor was inside partial word, move to its end
									cli->cursor=pos+len+cangrow;
								} else if (cli->cursor>pos) // if cursor is on the right, move it too
									cli->cursor+=cangrow;

								free(comm);
							}
						}
						alonematch=0;
						canexalone=0;
						if (cangrow&&strlen(word)+cangrow==strlen(cn->cmd)) { // first is exact match
							alonematch=1;
							canexalone=!!(cn->isdyn?cn->parent->cb:cn->cb); // if next is prefix, but no space, we can exec the command
						}
						if (canexalone)
							cli->parsedcb=cn->isdyn?cn->parent->cb:cn->cb;
					}
					break;
				} else { // regex match always returns 1 on no match
					cn=cn->next;
					if (cn)
						lastcn=cn;
					if (!cn) {
						yacli_print_nof(cli,"\nNo matched command (2)\n");
						cli->redraw=1;
						free(fb);
						if (dyn)
							yacli_dyn_vacuum(cli->cmdt);
						return 0x80;
					}
				}
			}
		}

		word=worde;
		while (*word==' ') // skip leading ws
			word++;
	}

	if (havepipe&&!completex) { // pipe after incomplete or not executable command
		yacli_print_nof(cli,"\nCannot apply filter to incomplete command\n");
		cli->redraw=1;
		free(fb);
		if (dyn)
			yacli_dyn_vacuum(cli->cmdt);
		return 0x80;
	}
	if (havepipe) {
		filter *f;

	nextfilter:
		while (*word==' ') // skip leading ws
			word++;
		worde=word;

		while (*worde&&*worde!=' ')
			worde++;
		if (*worde)
			*worde++=0;

		if (!*word) {
			yacli_print_nof(cli,"\nCannot apply empty filter\n");
			cli->redraw=1;
			free(fb);
			if (dyn)
				yacli_dyn_vacuum(cli->cmdt);
			return 0x80;
		}
		for (f=cli->flts;f;f=f->next) {
			int cmp=strcmp(word,f->cmd);

			if (cmp==0) { // exact match
				int havenextfltr;
				char *nword;

			addfilter:
				havenextfltr=0;
				word=worde;
				while (*word==' ') // skip leading ws
					word++;
				worde=word;
				while (*worde&&*worde!='|')
					worde++;
				nword=worde;
				if (*worde) {
					nword++;
					if (*worde=='|')
						havenextfltr=1;
					*worde=0;
				}
				while (worde[-1]==' ') { // sooner or later we will hit \0 or |
					worde[-1]=0;
					worde--;
				}

				// now we have filter in f and params in word
				if (docomplete==2)
					yacli_add_fcmd(cli,f,word);
				word=nword;
				if (havenextfltr)
					goto nextfilter;
				else
					goto donewithfilters;
			} else if (cmp<0) {
				int isprefix=strncmp(word,f->cmd,strlen(word))==0&&strlen(word)<strlen(f->cmd);
				int nxprefix=f->next&&strncmp(word,f->next->cmd,strlen(word))==0&&strlen(word)<strlen(f->next->cmd);

				if (!isprefix) {
					yacli_print_nof(cli,"\nNo matched filter\n");
					cli->redraw=1;
					free(fb);
					if (dyn)
						yacli_dyn_vacuum(cli->cmdt);
					return 0x80;
				} else if (isprefix&&!nxprefix) { // unfinished match
					int pos=word-fb+added;
					int len=strlen(word);
					int add=strlen(f->cmd)-len;

					if (docomplete) {
						yacli_replace(cli,pos,len,f->cmd); // do complete
						added+=add; // calculate compensation for changed buffer position
						if (cli->cursor>=pos&&cli->cursor<=pos+len) { // if cursor was inside partial word, move to its end
							cli->cursor=pos+len+add;
							if (cli->cursor==cli->buflen) // cursor is at end, append space
								yacli_insert(cli,' ');
							while (cli->cursor<cli->buflen&&cli->buffer[cli->cursor]==' ') // walk spaces
								cli->cursor++;
						} else if (cli->cursor>pos) // if cursor is on the right, move it too
							cli->cursor+=add;
					}
					goto addfilter;
				} else { // try to do partial complete
					filter *t=f->next->next;
					int cangrow=0;
					int cnt=2; // we have 2 commands with common prefix
					int i,j;

					while (t) {
						int isprefix=strncmp(word,t->cmd,strlen(word))==0&&strlen(word)<strlen(f->cmd);

						if (isprefix)
							cnt++;
						else
							break;
						t=t->next;
					}
					for (i=strlen(word);i<strlen(f->cmd);i++) {
						int allmatch=1;
						t=f->next;

						for (j=1;j<cnt;j++) {
							if (strlen(t->cmd)<=i) {
								allmatch=0;
								break;
							}
							if (t->cmd[i]!=f->cmd[i]) {
								allmatch=0;
								break;
							}
							t=t->next;
						}
						if (allmatch)
							cangrow++;
						else
							break;
					}
					if (docomplete&&cangrow) {
						char *comm=strdup(f->cmd);

						if (comm) { // do nothing if allocation fails
							char *toins=comm+strlen(word);
							int pos=word-fb+added;
							int len=strlen(word);

							toins[cangrow]=0;
							yacli_replace(cli,pos+strlen(word),0,toins); // do complete
							added+=cangrow;
							if (cli->cursor>=pos&&cli->cursor<=pos+len) { // if cursor was inside partial word, move to its end
								cli->cursor=pos+len+cangrow;
							} else if (cli->cursor>pos) // if cursor is on the right, move it too
								cli->cursor+=cangrow;

							free(comm);
						}
					}
					if (cangrow&&strlen(word)+cangrow==strlen(f->cmd)) { // first is exact match
						goto addfilter;
					} else { // filter is ambiguos
						// with fixed filters this case will most probably not happen :)
					}
				}
			}
		}
		yacli_print_nof(cli,"\nNo matched filter\n");
		cli->redraw=1;
		free(fb);
		if (dyn)
			yacli_dyn_vacuum(cli->cmdt);
		return 0x80;
	}
donewithfilters:

	// print context help
	if ((cli->wastab||!docomplete)&&docomplete!=2) { // double tab pressed or ?
		yacli_print_nof(cli,"\n"); // keep prompt in place for reference
		if (complete) { // print self (if cb) then walk children
			if (lastcn) {
				int maxcmdlen=0;
				cmnode *p;

				if (cn) // if we didn't hit leaf in tree, go one node up
					lastcn=lastcn->parent;

				p=lastcn->child;
				if (p&&p->cmd[0]=='@') {
					yacli_dyn_upd(cli,p);
					dyn=1;
					p=p->dyn;
				}

				// calculate max len of printed stuff
				if (lastcn->isdyn?lastcn->parent->cb:lastcn->cb) // print self, if valid alone
					maxcmdlen=mymax(maxcmdlen,strlen("<cr>"));
				while (p) { // print children
					maxcmdlen=mymax(maxcmdlen,yacli_cmd_help_len(p->cmd,p->help,!!p->cb));
					p=p->next;
				}

				// print in columns based on calculated max len
				if (lastcn->isdyn?lastcn->parent->cb:lastcn->cb)
					yacli_cmd_help_pr(cli,"",lastcn->isdyn?lastcn->parent->help:lastcn->help,1,maxcmdlen);
				p=lastcn->child;
				if (p&&p->cmd[0]=='@')
					p=p->dyn;
				while (p) {
					yacli_cmd_help_pr(cli,p->cmd,p->isdyn?p->parent->help:p->help,!!p->cb,maxcmdlen);
					p=p->next;
				}
			}
		} else { // walk siblings
			int maxcmdlen=0;
			cmnode *p;

			if (!lastword) { // list top level commands
				p=cli->cmdt;
				// calculate max len of printed stuff
				while (p) {
					maxcmdlen=mymax(maxcmdlen,yacli_cmd_help_len(p->cmd,p->help,!!p->cb));
					p=p->next;
				}
				// print in columns based on calculated max len
				p=cli->cmdt;
				while (p) {
					yacli_cmd_help_pr(cli,p->cmd,p->isdyn?p->parent->help:p->help,!!p->cb,maxcmdlen);
					p=p->next;
				}
			} else {
				if (lastcn->isdyn) // always use parent command when current node is dynamically generated
					lastcn=lastcn->parent;
				else
					if (cn&&alonematch) // last word was executable alone, so we have dived one more level, pop it up
						lastcn=lastcn->parent;
				p=lastcn;
				if (p&&p->cmd[0]=='@') {
					yacli_dyn_upd(cli,p);
					dyn=1;
					p=p->dyn;
				}
				// calculate max len of printed stuff
				while (p) {
					int isprefix=strncmp(lastword,p->cmd,strlen(lastword))==0&&strlen(lastword)<=strlen(p->cmd);

					if (isprefix)
						maxcmdlen=mymax(maxcmdlen,strlen(p->cmd)+(p->cb?5:0));
					p=p->next;
				}
				// print in columns based on calculated max len
				p=lastcn;
				if (p&&p->cmd[0]=='@')
					p=p->dyn;
				while (p) {
					int isprefix=strncmp(lastword,p->cmd,strlen(lastword))==0&&strlen(lastword)<=strlen(p->cmd);

					if (isprefix)
						yacli_cmd_help_pr(cli,p->cmd,p->isdyn?p->parent->help:p->help,!!p->cb,maxcmdlen);
					p=p->next;
				}
			}
		}
	}
	cli->redraw=1;

	free(fb);
	if (dyn)
		yacli_dyn_vacuum(cli->cmdt);
	// bit 0: last word was complete and executable
	// bit 1: last word was complete
	// bit 2: command is executable, but next is exact match and there is no space after it
	// bit 7: used internally to redraw prompt after enter on empty line
	// bit 8: (set above) no matched command
	return (!!completex)|((!!complete)<<1)|((!!canexalone<<2));
} // }}}

static inline void yacli_enter(yacli *cli) { // {{{
	int cmdok;

	if (!cli)
		return;

	// cmdok value:
	// bit 0: last word was complete and executable
	// bit 1: last word was complete
	// bit 2: command is executable, but next is exact match and there is no space after it
	// bit 7: used internally to redraw prompt after enter on empty line
	// bit 8: (set above) no matched command
	cmdok=yacli_trycomplete(cli,2);
	yacli_prompt(cli);
	yacli_buf_zeroterm(cli);
	yacli_add_hist(cli,cli->buffer);
	if (!cli->buflen) // allow pumping enter to reprint prompt
		cmdok=0x40;
	cli->retcode=YACLI_ERROR;
	if (cli->cmdcb)
		cli->cmdcb(cli,cli->buffer,cmdok>=3&&cmdok<=7);
	switch (cmdok) {
		case 0:
		case 1:
		case 2:
			yacli_print_nof(cli,"\n");
			yacli_print_nof(cli,"Command is not complete (%d)\n",cmdok);
			break;
		case 3: // last word is complete and command is executable
		case 4: // next is prefix, but there is no space
		case 5:
		case 6:
		case 7:
			yacli_print_nof(cli,"\n");
			// fcmd should be populated by yacli_trycomplete
			if (!cli->parsedcb)
				yacli_print_nof(cli,"BUG: callback is NULL for valid command?!\n");
			else {
				cli->incmdcb=1;
				cli->parsedcb(cli,cli->parsedcnt,cli->parsedcmd);
				cli->incmdcb=0;
			}
			yacli_free_fcmd(cli); // call done to flush the chain, then free chained filters
			break;
		case 0x40:
			yacli_print(cli,"\n");
			cli->retcode=YACLI_ENTER;
			cli->redraw=1;
			break;
		case 0x80: // error was already printed - no such command
			break;
	}
	yacli_delall(cli);
} // }}}

static inline void yacli_more_end(yacli *cli,more_type mt) { // {{{
	if (!cli)
		return;

	yacli_more_clear_prompt(cli,mt); // clear more prompt
	cli->morelen=0;
	cli->buffered=0;
	cli->state=IN_NORM;
	cli->redraw=1;
} // }}}

static inline void yacli_more_line(yacli *cli) { // {{{
	int i;

	if (!cli)
		return;

	for (i=0;i<cli->morelen;i++) {
		if (cli->morebuf[i]=='\n') {
			yacli_more_clear_prompt(cli,MORE_LINE); // clear more prompt
			yascreen_write(cli->s,cli->morebuf,i+1);
			memmove(cli->morebuf,cli->morebuf+i+1,cli->morelen-i-1);
			cli->morelen-=i+1;
			if (!cli->morelen) {
				yacli_more_end(cli,MORE_NONE);
				return;
			}
			cli->redraw=1;
			return;
		}
	}
	yacli_more_clear_prompt(cli,MORE_LINE); // clear more prompt
	yascreen_write(cli->s,cli->morebuf,cli->morelen);
	yacli_more_end(cli,MORE_NONE);
} // }}}

static inline void yacli_more_page(yacli *cli) { // {{{
	int i;

	if (!cli)
		return;

	cli->lines=0;
	yacli_more_clear_prompt(cli,MORE_PAGE); // clear more prompt

	for (i=0;i<cli->morelen;i++) {
		if (cli->morebuf[i]=='\n')
			cli->lines++;
		if (cli->lines+1>=cli->sy) {
			yascreen_write(cli->s,cli->morebuf,i+1);
			memmove(cli->morebuf,cli->morebuf+i+1,cli->morelen-i-1);
			cli->morelen-=i+1;
			if (!cli->morelen) {
				yacli_more_end(cli,MORE_NONE);
				return;
			}
			cli->redraw=1;
			return;
		}
	}
	yascreen_write(cli->s,cli->morebuf,cli->morelen);
	yacli_more_end(cli,MORE_NONE);
} // }}}

static inline void yacli_more_continue(yacli *cli) { // {{{
	if (!cli)
		return;

	yacli_more_clear_prompt(cli,MORE_CONT); // clear more prompt
	yascreen_write(cli->s,cli->morebuf,cli->morelen);
	yacli_more_end(cli,MORE_NONE);
} // }}}

static inline void yacli_ctrl_z(yacli *cli) { // {{{
	if (!cli)
		return;

	if (!cli->handlectrlz)
		return;

	yascreen_puts(cli->s,"^Z\r\n");
	if (cli->ctrlzcb)
		cli->ctrlzcb(cli);
	if (cli->ctrlzexeccmd)
		yacli_enter(cli);
	else // zero command buffer
		yacli_delall(cli);

	while (cli->cstack) {
		if (cli->ctrlzcb)
			cli->ctrlzcb(cli);
		yacli_exit_mode(cli);
	}

	if (!cli->ctrlzexeccmd) // yascli_enter already did redraw
		cli->redraw=1;

	if (cli->savbuf) { // kill last saved command
		free(cli->savbuf);
		cli->savbuf=NULL;
	}
	cli->hist_p=NULL; // reset history position
} // }}}

inline yacli_loop yacli_key(yacli *cli,int key) { // {{{
	int enterinsearch=0;
	yacli_in_state os;

	if (!cli)
		return YACLI_ERROR;

	os=cli->state;
	cli->retcode=YACLI_LOOP;
	switch (cli->state) {
		case IN_MORE:
			switch (key) {
				case YAS_K_C_C: // ^C
					yacli_more_end(cli,MORE_CTRC);
					break;
				case 'q':
				case 'Q': // discard the rest of the output
					yacli_more_end(cli,MORE_QUIT);
					break;
				case ' ': // space - scroll whole page
					yacli_more_page(cli);
					break;
				//case YAS_K_RET: // enter
				case YAS_K_C_M: // enter - scroll single line
					yacli_more_line(cli);
					break;
				case 'c':
				case 'C': // continue without more prompt
					yacli_more_continue(cli);
					break;
				default:
					if (isprint(key)) // ignore non-printing stuff
						yacli_more_line(cli);
					break;
			}
			break;
		case IN_SEARCH:
			switch (key) {
				case YAS_K_C_C: // fast cancel search
					cli->rcmd=NULL;
					yacli_ctrl_c(cli);
					yacli_end_search(cli);
					break;
				case YAS_K_C_G: // cancel search
					cli->rcmd=NULL;
					yacli_end_search(cli);
					break;
				//case YAS_K_RET: // enter
				case YAS_K_C_M: // enter - use and execute current command
					if (cli->rcmd)
						enterinsearch=1;
				case YAS_K_ESC:
					yacli_end_search(cli);
					if (enterinsearch)
						yacli_enter(cli);
					break;
				case YAS_K_C_R:
				case YAS_K_UP: // up
					yacli_search_up(cli);
					break;
				case YAS_K_C_S:
				case YAS_K_DOWN: // down
					yacli_search_down(cli);
					break;
				case YAS_K_C_H:
				case YAS_K_BSP:
					yacli_search_bsp(cli);
					break;
				default:
					if (isprint(key)) // ignore non-printing stuff
						yacli_add_search(cli,key);
					break;
			}
			break;
		case IN_C_X:
			// beware converting the following to switch because of the breaks
			if (key==YAS_K_C_X) // Ctrl-X Ctrl-X - ignore first one and expect next key
				break;
			cli->state=IN_NORM; // return to norm state
			if (key==YAS_K_C_V) { // Ctrl-X Ctrl-V show version
				const char *yasver=yascreen_ver();

				// yascreen_ver ends in \n\n, skip the last one
				yacli_print(cli,"%s\r%.*s",yascreen_clearln_s(cli->s),(int)strlen(yasver)-1,yasver);
				// myver ends in \n\n, skip the last one
				yacli_print(cli,"%s\r%.*s",yascreen_clearln_s(cli->s),(int)strlen(myver)-1,myver);
				cli->redraw=1;
				break;
			} else if (key==YAS_K_C_H) { // Ctrl-X Ctrl-H dump history
				history *h=cli->hist;

				yacli_print(cli,"%s\rHistory dump:\n",yascreen_clearln_s(cli->s));
				if (h)
					do {
						yacli_print(cli,"%s\r\n",h->command);
						h=h->next;
					} while (h!=cli->hist);
				cli->redraw=1;
				break;
			} else if (key==YAS_K_C_Z) { // Ctrl-X Ctrl-Z show terminal size
				yacli_print(cli,"%s\rTerminal size: %dx%d\n",yascreen_clearln_s(cli->s),cli->sx,cli->sy);
				cli->redraw=1;
				break;
			} else if (key==YAS_K_C_C) { // Ctrl-X Ctrl-C - dump command tree
				yacli_cmd_dump(cli);
				cli->redraw=1;
				break;
			}
		case IN_NORM:
			switch (key) {
				case YAS_K_NUL: // 0x00
					break;
				case YAS_K_TAB: // tab
					yacli_trycomplete(cli,1);
					break;
				case YAS_K_C_A: // home
					yacli_movehome(cli);
					break;
				case YAS_K_C_B: // left
					yacli_moveleft(cli);
					break;
				case YAS_K_C_C: // ^C
					yacli_ctrl_c(cli);
					break;
				case YAS_K_C_D: // EOF or del char
					yacli_ctrl_d(cli);
					break;
				case YAS_K_C_E: // end
					yacli_moveend(cli);
					break;
				case YAS_K_C_F: // right
					yacli_moveright(cli);
					break;
				case YAS_K_C_H:
				case YAS_K_BSP:
					yacli_bsp(cli);
					break;
				case YAS_K_C_J: // line feed; ignore
					break;
				case YAS_K_C_K: // del to end of line
					yacli_deltoend(cli);
					break;
				case YAS_K_C_L:
					yacli_clear(cli);
					break;
				//case YAS_K_RET: // enter
				case YAS_K_C_M: // enter
					yacli_enter(cli);
					break;
				case YAS_K_C_N: // down
					yacli_down(cli);
					break;
				case YAS_K_C_P: // up
					yacli_up(cli);
					break;
				case YAS_K_C_R: // ctrl-r (incremental search)
					yacli_start_search(cli);
					break;
				case YAS_K_C_U:
					yacli_delall(cli);
					break;
				case YAS_K_C_W:
					yacli_delprevword(cli);
					break;
				case YAS_K_C_X:
					cli->state=IN_C_X;
					break;
				case YAS_K_C_Z:
					yacli_ctrl_z(cli);
					break;
				case YAS_K_ESC:
					break;
				case '?':
					yacli_trycomplete(cli,0);
					break;
				case YAS_K_A_b: // word backward
					yacli_moveleftw(cli);
					break;
				case YAS_K_A_f: // word forward
					yacli_moverightw(cli);
					break;
				case YAS_K_A_d: // del word at cursor
					yacli_delword(cli);
					break;
				case YAS_K_A_BSP: // del prev word
					yacli_delprevword(cli);
					break;
				case YAS_K_UP: // up
					yacli_up(cli);
					break;
				case YAS_K_DOWN: // down
					yacli_down(cli);
					break;
				case YAS_K_RIGHT: // right
					yacli_moveright(cli);
					break;
				case YAS_K_LEFT: // left
					yacli_moveleft(cli);
					break;
				case YAS_K_HOME: // home
					yacli_movehome(cli);
					break;
				case YAS_K_END: // end
					yacli_moveend(cli);
					break;
				case YAS_K_DEL: // del
					yacli_del(cli);
					break;
				case YAS_K_C_RIGHT:
					yacli_moverightw(cli);
					break;
				case YAS_K_C_LEFT:
					yacli_moveleftw(cli);
					break;
				default:
					if (isprint(key)) // ignore non-printing chars
						yacli_insert(cli,key);
					break;
			}
			break;
	}
	switch (key) { // process events that do not depend on state
		case YAS_TELNET_SIZE:
			yacli_winch(cli);
			break;
		case YAS_SCREEN_SIZE:
			yascreen_getsize(cli->s,&cli->sx,&cli->sy);
			if (cli->showtsize) {
				yacli_print(cli,"%s\rTerminal size: %dx%d\n",yascreen_clearln_s(cli->s),cli->sx,cli->sy);
				cli->redraw=1;
			}
			break;
	}
	debugstate(os,cli->state,key);
	cli->wastab=key==YAS_K_TAB; // track double tab press
	if (cli->redraw&&cli->retcode!=YACLI_EOF)
		yacli_prompt(cli);
	return cli->retcode;
} // }}}

inline void yacli_start(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->istelnet) // setup telnet
		yascreen_init_telnet(cli->s);
	yascreen_reqsize(cli->s); // request screen size update, regardless of operation mode
	if (cli->banner&&strlen(cli->banner)) {
		yascreen_puts(cli->s,"  \b\b\r"); // dirty hack for secure crt bug
		yascreen_puts(cli->s,cli->banner);
	}
	cli->redraw=1;
} // }}}

inline void yacli_stop(yacli *cli) { // {{{
	if (!cli)
		return;

	if (cli->istelnet) { // revert telnet setup
		yascreen_set_telnet(cli->s,0);
		yascreen_init_telnet(cli->s);
	}
} // }}}

inline void yacli_free(yacli *cli) { // {{{
	history *h;

	if (!cli)
		return;

	if (cli->buffer)
		free(cli->buffer);
	if (cli->hostname)
		free(cli->hostname);
	if (cli->banner)
		free(cli->banner);
	if (cli->level)
		free(cli->level);
	if (cli->savbuf)
		free(cli->savbuf);
	if (cli->morebuf)
		free(cli->morebuf);
	if (cli->moreprompt)
		free(cli->moreprompt);

	if (cli->hist) {
		h=cli->hist;
		do {
			history *t=h;

			h=h->next;

			free(t->command);
			free(t);
		} while (h!=cli->hist);
	}
	yacli_cmd_free(cli->cmdt);
	yacli_free_parsed(cli);

	free(cli);
} // }}}

inline void yacli_set_telnet(yacli *cli,int on) { // {{{
	if (!cli)
		return;

	cli->istelnet=on;
	yascreen_set_telnet(cli->s,on);
} // }}}

inline void *yacli_add_cmd(yacli *cli,void *parent,const char *cmd,const char *help,void (*cb)(yacli *cli,int cnt,char **cmd)) { // {{{
	cmnode *par=parent,*t=parent,**place;

	if (!cli)
		return NULL;

	if (!cmd)
		return NULL;

	if (par&&par->cli!=cli) // parent node must match cli
		return NULL;

	if (par) // find proper place in the tree
		place=&par->child;
	else
		place=&cli->cmdt;

	if ((cmd[0]=='^'||cmd[0]=='@')&&*place) // cannot combine dynamic/regex and static commands
		return NULL;
	if (*place&&((*place)->cmd[0]=='^'||(*place)->cmd[0]=='@'))
		return NULL;

	while (*place) {
		int cmp=strcmp(cmd,(*place)->cmd);

		if (cmp==0) // duplicate command
			return NULL;
		if (cmp<0) // found proper place
			break;
		place=&(*place)->next;
	}

	t=calloc(1,sizeof *t);
	if (!t)
		return NULL;

	t->cli=cli;
	t->cmd=strdup(cmd);
	t->help=strdup(help?help:"");
	t->cb=cb;
	t->parent=par;
	t->next=*place;
	*place=t;

	return t;
} // }}}

inline void yacli_list(yacli *cli,void *ctx,const char *item) { // {{{
	cmnode *pnode=ctx;
	cmnode **place,*t;

	if (!cli)
		return;

	if (!pnode)
		return;

	place=&pnode->dyn;

	t=calloc(1,sizeof *t);
	if (!t)
		return;

	while (*place) {
		int cmp=strcmp(item,(*place)->cmd);

		if (cmp==0) { // duplicate command
			free(t);
			return;
		}
		if (cmp<0) // found proper place
			break;
		place=&(*place)->next;
	}

	t->cli=cli;
	t->parent=pnode;
	t->cmd=strdup(item);
	t->help=NULL;
	t->next=*place;
	t->isdyn=1;
	*place=t;
} // }}}

static inline void yacli_gen_modes(yacli *cli) { // {{{
	int modelen=0;
	cmstack *s,*l;
	int first=1;

	if (!cli)
		return;

	if (!cli->cstack) { // top mode
		if (cli->modes)
			free(cli->modes);
		cli->modes=NULL;
		return;
	}

	// calculate len
	s=cli->cstack;
	while (s) {
		modelen+=1+strlen(s->mode); // open brace or dash+mode len
		if (!s->next) // keep last element for reverse walk
			l=s;
		s=s->next;
	}
	if (cli->cstack) // closing brace
		modelen++;

	if (cli->modes)
		free(cli->modes);
	cli->modes=calloc(modelen+1,1);
	if (!cli->modes) // crit - no memory
		return;

	while (l) {
		sprintf(cli->modes+strlen(cli->modes),"%c%s",first?'(':'-',l->mode);
		l=l->prev;
		first=0;
	}
	strcat(cli->modes,")");
} // }}}

inline void yacli_enter_mode(yacli *cli,const char *mode,void *hint) { // {{{
	cmstack *s;

	if (!cli)
		return;

	s=calloc(1,sizeof *s);
	if (!s) // crit err - no memory
		return;

	s->next=cli->cstack;
	if (s->next)
		s->next->prev=s;
	s->mode=strdup(mode);
	s->cmdt=cli->cmdt;
	s->hint=hint;
	cli->cmdt=NULL;
	cli->cstack=s;
	yacli_gen_modes(cli);
} // }}}

inline void yacli_exit_mode(yacli *cli) { // {{{
	cmstack *s;
	cmnode *n;

	if (!cli)
		return;

	if (!cli->cstack) // top mode, nothing to do
		return;

	s=cli->cstack;
	n=cli->cmdt;

	cli->cmdt=cli->cstack->cmdt; // restore commands
	cli->cstack=cli->cstack->next; // pop top items from mode stack
	if (cli->cstack)
		cli->cstack->prev=NULL;

	yacli_gen_modes(cli);
	if (s->mode)
		free(s->mode);
	free(s);
	yacli_cmd_free(n);
} // }}}

inline void yacli_set_mode_hint_p(yacli *cli,void *hint) { // {{{
	if (!cli)
		return;
	if (!cli->cstack)
		return;
	cli->cstack->hint=hint;
} // }}}

inline void *yacli_get_mode_hint_p(yacli *cli) { // {{{
	if (!cli)
		return NULL;
	if (!cli->cstack)
		return NULL;
	return cli->cstack->hint;
} // }}}

inline const char *yacli_buf_get(yacli *cli) { // {{{
	yacli_buf_zeroterm(cli); // zero terminate the buffer
	return cli->buffer;
} // }}}

