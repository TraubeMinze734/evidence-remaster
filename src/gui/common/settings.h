#ifndef SETTINGS_H
#define SETTINGS_H

/* persistent user-settings (~/.evidence/evidence.conf).
   the file holds plain "key=value" lines; the set of keys that is
   persisted is the settings_keys[] list in settings.c -- extend there
   when new settings become configurable. */

/* file-name of the settings file below ~/.evidence/ */
#define SETTINGS_FILE "evidence.conf"

char  *settings_file_path(void);              /* must be free()d */
int    settings_load(void);                   /* read file into props_global */
int    settings_save(void);                   /* write props_global to file */
char  *settings_get(const char *key);         /* malloc'd value, or NULL */
int    settings_set(const char *key,const char *val); /* set prop (fires listeners) */

/* list theme-names found in the setup-paths (each a sub-dir of a
   "themes" dir containing an evidence.theme file); sorted.
   @return array of malloc'd names (free each, then the array), or NULL.
   @param count  where to store the number of entries */
char **settings_theme_list(int *count);
void   settings_theme_list_free(char **list,int count);

#endif /* SETTINGS_H */
