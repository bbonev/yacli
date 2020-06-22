// $Id: yacli.h,v 1.30 2020/06/21 12:51:27 bbonev Exp $

#ifndef ___YACLI_H___
#define ___YACLI_H___

#include <yascreen.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	YACLI_LOOP,
	YACLI_ENTER,
	YACLI_ERROR,
	YACLI_EOF,
} yacli_loop;

struct _yacli;
typedef struct _yacli yacli;

// allocate and initialize cli data
inline yacli *yacli_init(yascreen *s);
// get library version as static string
inline const char *yacli_ver(void);
// convenience to access screen
inline yascreen *yacli_get_screen(yacli *cli);
// set cli start banner
inline void yacli_set_banner(yacli *cli,const char *banner);
// set cli level prompt
inline void yacli_set_level(yacli *cli,const char *level);
// set cli hostname
inline void yacli_set_hostname(yacli *cli,const char *hostname);
// set showing terminal size on change
inline void yacli_set_showtermsize(yacli *cli,int v);
// set telnet mode
inline void yacli_set_telnet(yacli *cli,int on);
// enable more
inline void yacli_set_more(yacli *cli,int on);
// set more prompt behaviour after line/page/continue/quit/^C
inline void yacli_set_more_clear(yacli *cli,int ln,int pg,int co,int qu);
// enable ctrl-z handling (pops mode stack to top level)
inline void yacli_set_ctrlz(yacli *cli,int on);
inline void yacli_set_ctrlz_exec(yacli *cli,int on);

// get/set user hint
inline void yacli_set_hint_i(yacli *cli,int hint);
inline int yacli_get_hint_i(yacli *cli);
inline void yacli_set_hint_p(yacli *cli,void *hint);
inline void *yacli_get_hint_p(yacli *cli);

// start cli DFA
inline void yacli_start(yacli *cli);
// revert telnet setup, if needed
inline void yacli_stop(yacli *cli);
// send input key to cli DFA
inline yacli_loop yacli_key(yacli *cli,int key);
// signal terminal size change
inline void yacli_winch(yacli *cli);
// signal cli to exit
inline void yacli_exit(yacli *cli);

// filtered print, using print cb
inline int yacli_print(yacli *cli,const char *format,...) __attribute__((format(printf,2,3)));
inline int yacli_write(yacli *cli,const char *format,size_t len);
// unfiltered print for line messages (will clear the prompt, print the line and reprint prompt)
inline void yacli_message(yacli *cli,const char *line);

// add command to history buffer
inline int yacli_add_hist(yacli *cli,const char *buf);
// add part of command to command tree
inline void *yacli_add_cmd(yacli *cli,void *parent,const char *cmd,const char *help,void (*cb)(yacli *cli,int cnt,char **cmd));
// add item to dynamic list
inline void yacli_list(yacli *cli,void *ctx,const char *item);
// set list callback function
inline void yacli_set_list_cb(yacli *cli,void (*list_cb)(yacli *cli,void *ctx,int code));
// set command callback function
inline void yacli_set_cmd_cb(yacli *cli,void (*cmd_cb)(yacli *cli,const char *cmd,int code));
// set ctrl-z callback function
inline void yacli_set_ctrlz_cb(yacli *cli,void (*ctrlzcb)(yacli *cli));
// enter submode with shortname (all commands for the submode should be added after this call)
inline void yacli_enter_mode(yacli *cli,const char *mode,void *hint);
// exit submode (submode commands are deleted)
inline void yacli_exit_mode(yacli *cli);
// get/set user hint for the current mode
inline void yacli_set_mode_hint_p(yacli *cli,void *hint);
inline void *yacli_get_mode_hint_p(yacli *cli);

// get current buffer contents
inline const char *yacli_buf_get(yacli *cli);

// cleanup and free cli data
inline void yacli_free(yacli *cli);

#ifdef __cplusplus
}
#endif

#endif
