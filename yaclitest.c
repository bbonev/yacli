#include <time.h>
#include <stdio.h>
#include <yacli.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

#define MSG_TO 4

static int winch=0;
static int msgtype=0;
static int showmsg=0;
static char *msgs[]={
	"[log message] simple message w/o newline",
	"[log message] another message with newline\n",
	"[log message] first line\nsecond line\nthird line w/o newline",
	"[log message] first\nsecond (with newline)\n",
};

static void sigwinch(int sign) {
	winch=1;
}

static void cmd_more(yacli *cli,int cnt,char **cmd) {
	yacli_print(cli,"More prompt is on\n");
	yacli_set_more(cli,1);
}

static void cmd_no_more(yacli *cli,int cnt,char **cmd) {
	yacli_print(cli,"More prompt is off\n");
	yacli_set_more(cli,0);
}

static void cmd_exit(yacli *cli,int cnt,char **cmd) {
	yacli_print(cli,"Exiting from cli...\n");
	yacli_exit(cli);
}

static void cmd_bliak(yacli *cli,int cnt,char **cmd) {
	yacli_print(cli,"Command with next that is a prefix called...\n");
}

static void cmd_show_watch(yacli *cli,int cnt,char **cmd) {
	yacli_print(cli,"Current watch is %s.\n",showmsg?"on":"off");
}

static void cmd_watch_on(yacli *cli,int cnt,char **cmd) {
	showmsg=1;
	yacli_print(cli,"Current watch is %s.\n",showmsg?"on":"off");
}

static void cmd_watch_off(yacli *cli,int cnt,char **cmd) {
	showmsg=0;
	yacli_print(cli,"Current watch is %s.\n",showmsg?"on":"off");
}

static void cmd_version(yacli *cli,int cnt,char **cmd) {
	yascreen *s=yacli_get_screen(cli);

	if (s) {
		yascreen_ungetch(s,YAS_K_C_V);
		yascreen_ungetch(s,YAS_K_C_X);
	}
}

static void cmd_size(yacli *cli,int cnt,char **cmd) {
	yascreen *s=yacli_get_screen(cli);

	if (s) {
		yascreen_ungetch(s,YAS_K_C_Z);
		yascreen_ungetch(s,YAS_K_C_X);
	}
}

static void cmd_generic(yacli *cli,int cnt,char **cmd) {
	int i;

	yacli_print(cli,"my foo: %d",cnt);
	for (i=0;i<cnt;i++)
		yacli_print(cli," %s",cmd[i]);
	yacli_print(cli,"\n");

	for (i=0;i<45;i++)
		yacli_print(cli,"some line #%d :)\n",i);
}

static void list_cb(yacli *cli,void *ctx,int code) {
	char s[50];
	int i,j;

	switch (code) {
		case 0: // list interfaces
			yacli_list(cli,ctx,"eth0.0012");
			yacli_list(cli,ctx,"eth0.0014");
			yacli_list(cli,ctx,"eth0.1010");
			yacli_list(cli,ctx,"eth0");
			yacli_list(cli,ctx,"eth1");
			yacli_list(cli,ctx,"eth2");
			yacli_list(cli,ctx,"eth1.0100");
			yacli_list(cli,ctx,"eth1.0101");
			yacli_list(cli,ctx,"eth1.0102");
			break;
		case 1: // list ips
			for (j=0;j<5;j++)
				for (i=1;i<3;i++) {
					snprintf(s,sizeof s,"10.10.%d.%d",j,i);
					yacli_list(cli,ctx,s);
				}
			yacli_list(cli,ctx,"10.10.23.4");
			yacli_list(cli,ctx,"10.10.23.5");
			yacli_list(cli,ctx,"10.10.23.6");
			yacli_list(cli,ctx,"10.10.23.7");
			yacli_list(cli,ctx,"10.10.23.8");
			yacli_list(cli,ctx,"10.10.23.9");
			yacli_list(cli,ctx,"10.10.23.10");
			yacli_list(cli,ctx,"10.10.23.11");
			yacli_list(cli,ctx,"10.10.15.3");
			yacli_list(cli,ctx,"10.10.15.4");
			yacli_list(cli,ctx,"10.10.15.5");
			yacli_list(cli,ctx,"10.10.15.6");
			yacli_list(cli,ctx,"10.10.15.7");
			yacli_list(cli,ctx,"10.10.15.8");
			yacli_list(cli,ctx,"10.10.15.9");
			yacli_list(cli,ctx,"10.10.15.10");
			yacli_list(cli,ctx,"10.10.15.11");
			break;
	}
}

int main(void) {
	time_t lastmsg=time(NULL);
	void *ip3,*i3a,*i3b,*i3c;
	void *ter,*no_ter;
	unsigned char ch;
	void *ip1,*ip2;
	void *unprov;
	void *pshow;
	void *pwatc;
	yascreen *s;
	yacli *cli;
	void *prov;
	void *pip;
	void *no;

	// handle window change signal
	signal(SIGWINCH,sigwinch);

	s=yascreen_init(80,25);
	if (!s) {
		fprintf(stderr,"cannot allocate screen\n");
		return 1;
	}

	// alloc a cli
	cli=yacli_init(s);
	// save current term and setup
	yascreen_term_set(s,YAS_NOBUFF|YAS_NOSIGN|YAS_NOECHO);
	yacli_set_banner(cli,"Some custom device message 1.0.3\n\n");
	yacli_set_banner(cli,"\x1b[0;31mIP\x1b[1;30mACCT\x1b[0m \x1b[1;37mFlashOS\x1b[0m \x1b[1;30mRelease 6.28.65-x86_64-big\x1b[0m\n\n");
	yacli_set_level(cli,"#");
	yacli_set_hostname(cli,"ipacct");
	yacli_set_more(cli,1); // more prompt
	yacli_set_list_cb(cli,list_cb);

	pwatc=yacli_add_cmd(cli,NULL,"watch","Config current log print status",NULL);
	yacli_add_cmd(cli,pwatc,"on","Enable log print",cmd_watch_on);
	yacli_add_cmd(cli,pwatc,"off","Disable log print",cmd_watch_off);
	pshow=yacli_add_cmd(cli,NULL,"show","Show system status and configuration",NULL);
	//yacli_add_cmd(cli,NULL,"ship","Ship help :)",NULL);
	yacli_add_cmd(cli,NULL,"exit","Terminate current session",cmd_exit);
	yacli_add_cmd(cli,NULL,"quit","Terminate current session",cmd_exit);
	ip3=yacli_add_cmd(cli,pshow,"33","Display 33",cmd_generic);
	ip1=yacli_add_cmd(cli,pshow,"1","Display 1",cmd_generic);
	ip2=yacli_add_cmd(cli,pshow,"2","Display 2",cmd_generic);
	ip1=yacli_add_cmd(cli,ip1,"@1","Display IP information",cmd_generic);
	yacli_add_cmd(cli,ip1,"novo20","Novo 20",cmd_generic);
	yacli_add_cmd(cli,ip2,"@1","Display IP information",cmd_generic);
	yacli_add_cmd(cli,pshow,"version","Display library version in a tricky way",cmd_version);
	yacli_add_cmd(cli,pshow,"term-size","Display terminal size in a tricky way",cmd_size);

	i3a=yacli_add_cmd(cli,ip3,"aaa","Display IP information",NULL);
	i3b=yacli_add_cmd(cli,ip3,"bbb","Display IP information",NULL);
	i3c=yacli_add_cmd(cli,ip3,"ccc","Display IP information",NULL);
	ip3=yacli_add_cmd(cli,i3a,"@2","Display IP information",cmd_generic);
	ip3=yacli_add_cmd(cli,ip3,"aaa","Display IP information",NULL);
	yacli_add_cmd(cli,ip3,"@2","Display IP information",cmd_generic);
	ip3=yacli_add_cmd(cli,i3b,"@3","Display IP information",cmd_generic);
	ip3=yacli_add_cmd(cli,ip3,"bbb","Display IP information",NULL);
	yacli_add_cmd(cli,ip3,"@3","Display IP information",cmd_generic);
	ip3=yacli_add_cmd(cli,i3c,"@4","Display IP information",cmd_generic);
	ip3=yacli_add_cmd(cli,ip3,"ccc","Display IP information",NULL);
	yacli_add_cmd(cli,ip3,"@4","Display IP information",cmd_generic);

	pip=yacli_add_cmd(cli,pshow,"ip","Display IP information",cmd_generic);
	//yacli_add_cmd(cli,pip,"<A.B.C.D|XX:XX::XX:XX>","Display IP information",cmd_generic);
	yacli_add_cmd(cli,pip,"@1","Display IP information",cmd_generic);
	yacli_add_cmd(cli,pshow,"id","Display shaping class information",cmd_generic);
	yacli_add_cmd(cli,pshow,"shaper","Display shaper tree information",cmd_generic);
	yacli_add_cmd(cli,pshow,"map","Display traffic map information",cmd_generic);
	yacli_add_cmd(cli,pshow,"master","Display master data (test)",cmd_generic);
	yacli_add_cmd(cli,pshow,"connections","Display API and CLI connections",cmd_generic);
	yacli_add_cmd(cli,pshow,"state","Display system state",cmd_generic);
	yacli_add_cmd(cli,pshow,"networks","Display traffic classification prefixes",cmd_generic);
	yacli_add_cmd(cli,pshow,"dscp","Display DSCP classification rules",cmd_generic);
	yacli_add_cmd(cli,pshow,"expiries","Display timers information",cmd_generic);
	yacli_add_cmd(cli,pshow,"mac","Display mac address bindings",cmd_generic);
	yacli_add_cmd(cli,pshow,"dhcp","Display DHCP config",cmd_generic);
	yacli_add_cmd(cli,pshow,"pptp","Display PPTP config",cmd_generic);
	yacli_add_cmd(cli,pshow,"pppoe","Display PPPOE config",cmd_generic);
	ip1=yacli_add_cmd(cli,pshow,"ppp","Display PPP state",NULL);
	yacli_add_cmd(cli,ip1,"sessions","Display PPP sessions",cmd_generic);
	yacli_add_cmd(cli,pshow,"pool","Display IP pool config",cmd_generic);
	yacli_add_cmd(cli,pshow,"radius","Display radius config",cmd_generic);
	yacli_add_cmd(cli,pshow,"watch","Display log show status",cmd_show_watch);

	yacli_add_cmd(cli,pshow,"t123456a","test a",cmd_bliak);
	yacli_add_cmd(cli,pshow,"t123456ab","test b",cmd_generic);
	yacli_add_cmd(cli,pshow,"t123456ac","test c",cmd_generic);
	yacli_add_cmd(cli,pshow,"t123456acaaad","test d",cmd_generic);
	yacli_add_cmd(cli,pshow,"t123456acaaae","test e",cmd_generic);

	yacli_add_cmd(cli,NULL,"more","Enable paging (more prompt)",cmd_more);
	no=yacli_add_cmd(cli,NULL,"no","Negate command",NULL);
	yacli_add_cmd(cli,no,"more","Disable paging (more prompt)",cmd_no_more);

	ter=yacli_add_cmd(cli,NULL,"terminal","Terminal settings",NULL);
	no_ter=yacli_add_cmd(cli,no,"terminal","Terminal settings",NULL);
	yacli_add_cmd(cli,no_ter,"datadump","Enable paging (more prompt)",cmd_more);
	yacli_add_cmd(cli,ter,"datadump","Disable paging (more prompt)",cmd_no_more);

	prov=yacli_add_cmd(cli,NULL,"provision","Provision ip address",NULL);
	unprov=yacli_add_cmd(cli,no,"provision","Un-provision ip address",NULL);
	yacli_add_cmd(cli,prov,"^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)[.]){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$","<A.B.C.D>",cmd_generic);
	yacli_add_cmd(cli,unprov,"^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)[.]){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$","<A.B.C.D>",cmd_generic);

	ip1=yacli_add_cmd(cli,NULL,"file","file path test",cmd_bliak);
	yacli_add_cmd(cli,ip1,"^[^`{<|:,;>}'\"]+$","<file_path>",cmd_bliak);
	yacli_start(cli);

	for (;;) {
		time_t now=time(NULL);
		struct timeval sto;
		yacli_loop rc;
		fd_set rfd;
		int key;

		if (winch) {
			winch=0;
			yacli_winch(cli);
		}

		if (lastmsg+MSG_TO<now) { // time to send message
			lastmsg=now;
			if (showmsg)
				yacli_message(cli,msgs[msgtype]);
			msgtype++;
			msgtype%=sizeof msgs/sizeof *msgs;
		}

		sto.tv_sec=0;
		sto.tv_usec=250*1000; // four times per sec
		FD_ZERO(&rfd);
		FD_SET(STDIN_FILENO,&rfd);
		key=yascreen_getch_nowait(s);
		if (key!=-1) {
			rc=yacli_key(cli,key);
			switch (rc) {
				case YACLI_LOOP:
				case YACLI_ENTER:
				case YACLI_ERROR:
					break;
				case YACLI_EOF:
					yacli_print(cli,"done...\n");
					yascreen_term_restore(s);
					goto done;
			}
		}
		if (-1!=yascreen_peekch(s))
			sto.tv_usec=0;
		if (-1==select(STDIN_FILENO+1,&rfd,NULL,NULL,&sto))
			continue;
		if (!FD_ISSET(STDIN_FILENO,&rfd))
			continue;
		if (sizeof ch==read(STDIN_FILENO,&ch,sizeof ch)) {
			if (ch==0x0a) // translate enter to telnet code
				ch=0x0d;
			yascreen_feed(s,ch);
		} else {
			yacli_print(cli,"got strange result\n");
			break;
		}
	}
done:
	yacli_free(cli);
	return 0;
}

