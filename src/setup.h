extern char  *prefix;
extern char  *path_process,*path_setup;



void  paths_init(const char *,const char *);
void  paths_exit(void);
int   paths_add(const char *);
char *get_setup_file(const char *);
char *get_path_file(const char *,const char *);
