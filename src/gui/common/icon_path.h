/* fully qualified paths to icon-files, rather than the "icon-path"
   in the browser-view! */

int        icon_path_del(char *key);
int        icon_path_add(const char *key,const char *icon_path);
char      *icon_path_find_file(const char *file);
char      *icon_path_get(const char *file,const char *mime);
char      *icon_path_get_always(const char *,const char *);
int        icon_path_save(void);
int        icon_path_load(void);
int        icon_path_exit(void);
int        icon_path_init(const char *iconset);

