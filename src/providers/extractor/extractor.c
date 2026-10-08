/** @file  extractor.c
 *  @brief meta-data provider plug-in "extractor"
 *
 *  get meta-data for various file-types using the "extractor" framework.
 *  note that extractor can only determine but not set such meta-data.
 */

#include "evidence.h"

#include "meta.h"
#include "errors.h"

#include <extractor.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <stdio.h>



#define MY_NAME "extractor"
#define CHUNK   1024

#undef EVIL_HACK

#ifdef EVIL_HACK
#  define LLP     "LD_LIBRARY_PATH"

static char     *sp=NULL,*op=NULL;
char            *lt_dlgetsearchpath();
#endif



/**
 * module requires special deinitialization before it can be unloaded
 * @return an error-code
 * @param data  a handle created by meta_plugin_init()
 */
int meta_plugin_exit(void *data) {
  EXTRACTOR_plugin_remove_all((struct EXTRACTOR_PluginList *)data);

#ifdef EVIL_HACK
  if(op) {
    putenv(op);
    free(sp);
    sp=NULL; }
#endif

  E(1,"extractor exit (%p)\n",data);

  return NOIR_SUCC; }



/**
 * module needs special initialization before it can be used
 * @return a handle that will be handed to meta_plugin_exit() before the module is unloaded
 */
void *meta_plugin_init(void) {
  struct EXTRACTOR_PluginList *extractors=EXTRACTOR_plugin_add_defaults(EXTRACTOR_OPTION_DEFAULT_POLICY);

#ifdef EVIL_HACK
  if(!sp) {
    size_t  l;
    char   *m=lt_dlgetsearchpath();
    if((op=getenv(LLP)))
      op-=sizeof(LLP);
    if(!m||!*m)
      m="/usr/lib:/usr/local/lib:/lib";

    if((sp=malloc(l=(strlen(m)+sizeof(LLP)+2)))) {
      snprintf(sp,l,LLP "=%s",m);
      putenv(sp);
      E(2,"extractor: " LLP " set to \"%s\" (\"%s\"), was \"%s\".\n",sp,getenv(LLP),op?op:"unset"); }}
#endif

  return extractors; }



/**
 * write meta-data for this file
 * extractor framework cannot write meta-data, only read it
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_set_for_file(noir_md *ret,char *file,noir_provider *prov) {
  return NOIR_FAIL; }







/**
 * callback for EXTRACTOR_extract(): add each meta-data item to the list
 * @return 0 to continue extraction
 */
static int meta_add_cb(void *cls,const char *plugin_name,
                       enum EXTRACTOR_MetaType type,
                       enum EXTRACTOR_MetaFormat format,
                       const char *data_mime_type,const char *data,size_t data_len) {
  noir_md    *ret=(noir_md *)cls;
  const char *key=EXTRACTOR_metatype_to_string(type);
  noir_meta  *m;

  (void)plugin_name; (void)data_mime_type; (void)data_len;

  if(!key||!strcmp(key,"unknown")||!strcmp(key,"mimetype"))
    return 0;

  /* only take textual meta-data; ignore binary blobs */
  if((format!=EXTRACTOR_METAFORMAT_UTF8)&&(format!=EXTRACTOR_METAFORMAT_C_STRING))
    return 0;

  meta_add(ret,META_RO,META_STR,(char *)key,NULL,MY_NAME,(char *)data,NULL);

  if((m=meta_in_group(ret,"title",MY_NAME))&&meta_get_val(m))
    meta_add(ret,META_RO,META_STR,"sortkey","If you enable 'Sort by meta-data', files will be sorted according to this field.",MY_NAME,(char *)meta_get_val(m),NULL);

  return 0; }


/**
 * get meta-data for this file
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_get_for_file(noir_md *ret,char *file,noir_provider *prov) {
  struct EXTRACTOR_PluginList *extractors=(struct EXTRACTOR_PluginList *)meta_provider_get_data(prov);
  /* avoid duplicate info (info was already set by a specialist provider). */
  if(meta_in_group(ret,"sortkey",NULL))
    return NOIR_IGNORED;

  EXTRACTOR_extract(extractors,file,NULL,0,meta_add_cb,ret);

  return NOIR_SUCC; }



/**
 * let info-dialog do the layout for us
 * @return NOIR_SUCC to do our own layout, NOIR_FAIL to have the caller do it
 */
int layoutp(void) {
  return NOIR_FAIL; }
