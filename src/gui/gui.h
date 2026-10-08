noir_sess *gui_init(int argc,char *argv[]);  /* initialize */
int        gui_start(noir_sess *);           /* start main loop */
int        gui_exit(void);                   /* deinitialize */

void       gui_quit(void);                   /* end main loop / destroy all windows */
