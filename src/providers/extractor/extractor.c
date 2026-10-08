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
  EXTRACTOR_removeAll((EXTRACTOR_ExtractorList *)data);

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
  EXTRACTOR_ExtractorList *extractors=EXTRACTOR_loadDefaultLibraries();

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
 * get meta-data for this file
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_get_for_file(noir_md *ret,char *file,noir_provider *prov) {
  EXTRACTOR_ExtractorList *extractors=(EXTRACTOR_ExtractorList *)meta_provider_get_data(prov);
  EXTRACTOR_KeywordList   *keywords;
  const char              *key;
  noir_meta               *m;

  /* avoid duplicate info (info was already set by a specialist provider). */
  if(meta_in_group(ret,"sortkey",NULL))
    return NOIR_IGNORED;

#if !defined(EXTRACTOR_VERSION)||(EXTRACTOR_VERSION<0x00030300)
  /* AVIs send extractor in a very very long loop at the moment, skip them */
  if(((m=meta_in_group(ret,"MIME",NULL))&&meta_get_val(m))&&!strcmp(meta_get_val(m),"video/x-msvideo"))
    return NOIR_IGNORED;
#endif

/*  extractors=EXTRACTOR_addLibraryLast(extractors,"libextractor_printable"); */
/*  extractors=EXTRACTOR_addLibraryLast(extractors,"libextractor_split"); */

  keywords=EXTRACTOR_getKeywords(extractors,file);
  keywords=EXTRACTOR_removeDuplicateKeywords(keywords,0);

#if 0
  { char *p=file+strlen(file)-4;
    if((p>=file)&&!strcasecmp(p,".wav"))
      E(0,"extractor: %s\n",p); }
#endif

  while(keywords) {
    if((key=EXTRACTOR_getKeywordTypeAsString(keywords->keywordType))&&
       strcmp(key,"mimetype")&&
       strcmp(key,"unknown"))
      meta_add(ret,META_RO,META_STR,key,NULL,MY_NAME,keywords->keyword,prov);
    keywords=keywords->next; }

  EXTRACTOR_freeKeywords(keywords);

  if((m=meta_in_group(ret,"title",MY_NAME))&&meta_get_val(m))
    meta_add(ret,META_RO,META_STR,"sortkey","If you enable 'Sort by meta-data', files will be sorted according to this field.",MY_NAME,(char *)meta_get_val(m),prov);

  return NOIR_SUCC; }



/**
 * let info-dialog do the layout for us
 * @return NOIR_SUCC to do our own layout, NOIR_FAIL to have the caller do it
 */
int layoutp(void) {
  return NOIR_FAIL; }
