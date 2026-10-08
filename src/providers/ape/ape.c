/** @file  ape.c
 *  @brief meta-data provider plug-in "APE"
 *
 *  get APE tags
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "errors.h"
#include "meta.h"
#include "util.h"



#define MY_NAME    "APE"
#define CHUNK_SIZE 4096
#define GET_INT(i) ((i[3]<<24)|(i[2]<<16)|(i[1]<<8)|i[0])



/**
 * make sort-key from artist and title
 * @return an error-code
 * @param ret   the meta-data list
 * @param prov  the provider (us)
 */
static int meta_make_sortkey(noir_md *ret,noir_provider *prov) {
  noir_meta *x;
  char      *t=NULL,*a=NULL,*s=NULL;

  if((x=meta_in_group(ret,"title",MY_NAME))&&meta_get_val(x))
    t=(char *)meta_get_val(x);
  if((x=meta_in_group(ret,"artist",MY_NAME))&&meta_get_val(x))
    a=(char *)meta_get_val(x);

  if(t||a) {
    if(!a)
      a="diverse";

    if(t)
      esprintf(&s,"%s - %s",a,t);
    else
      s=strdup(a);

    if(s) {
      meta_add(ret,META_RO,META_STR,"sortkey","If you enable 'Sort by meta-data', files will be sorted according to this field.",MY_NAME,s,prov);
      free(s);
      return NOIR_SUCC; }
    else
      return NOIR_OOM; }
  return NOIR_NODATA; }



static int ape_read_fields(FILE *fp,int version,noir_md *md,noir_provider *prov) {
  unsigned char *tag=NULL,*p,numbuf[4],
                *key,*val;
  long           offset;
  int            i,size,fields,ret=NOIR_FAIL;
  size_t         l;

  if(fread(numbuf,1,4,fp)!=4)   goto done;    /* read tag meta-data */
  if(!(size=GET_INT(numbuf)))   goto done;

  if(fread(numbuf,1,4,fp)!=4)   goto done;
  if(!(fields=GET_INT(numbuf))) goto done;

  if(fread(numbuf,1,4,fp)!=4)   goto done;
  if(!(numbuf[3]&0x20)||
     (version==1000))
    offset=8-size;
  else
    offset=8;
  fseek(fp,offset,SEEK_CUR);

  if(!(tag=malloc(size)))                    /* allocate RAM */
    return NOIR_OOM;

  if(fread(tag,1,size,fp)!=size) {           /* read actual tag */
    ret=NOIR_NODATA;
    goto done; }

  p=tag;
  for(i=0;i<fields;i++) {                    /* process tag */
    key=val=NULL;

    memcpy(numbuf,p,4);                      /* get data-len (of value only) */
    size=GET_INT(numbuf);
    p+=8;

    key=p;                                   /* get key */
    l=strlen(key);
    p+=l+1;

    if((l>0)&&(val=malloc(size+1))) {        /* get value */
      memcpy(val,p,size);
      *(val+size)='\0';
      meta_add(md,META_RO,META_STR,key,NULL,MY_NAME,val,prov);
      free(val); }

    p+=size; }

  ret=NOIR_SUCC;

 done:
  free(tag);
  return ret; }



/**
 * find tag header/footer and point the file pointer there
 * @return a version number>0, or an error-code
 * @param file  file-name
 * @param fp    a FILE
 */
static int ape_scan(char *file,FILE *fp) {
  unsigned char *buffer,
                *p;
  unsigned char  numbuf[4];
  int            pos=0,
                 version=NOIR_NOTFOUND;
  size_t         got,remain;

  if(!(buffer=malloc(CHUNK_SIZE)))          /* allocate RAM */
    return NOIR_OOM;

  if((got=fread(buffer,1,CHUNK_SIZE,fp))<8) /* read chunk */
    goto fail;

  while(TRUE) {                             /* find magic */
    remain=got;
    p=buffer;

    while((remain>=8)&&(p=memchr(p,'A',remain))) {
      if(!strncmp(p,"APETAGEX",8))
        goto succ;
      p++;
      remain=got-(p-buffer); }

    if(feof(fp))
      goto fail;

    memmove(buffer,buffer+got-7,7);
    pos+=got-7;

    if((got=fread(buffer+7,1,CHUNK_SIZE-7,fp))<8)
      goto fail;
    else
      got+=7; }

succ:
  fseek(fp,pos+(p-buffer)+8,SEEK_SET);       /* find version info in file */

  if(fread(numbuf,1,4,fp)==4)                /* get version */
    version=GET_INT(numbuf);

fail:
  free(buffer);

  return version; }



/**
 * get meta-data for this file
 * @return an error-code
 * @param md    the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int meta_get_for_file(noir_md *md,char *file,noir_provider *prov) {
  FILE *fp;
  int   version,
        ret=NOIR_IGNORED;
	
  if((fp=fopen(file,"rb"))) {
    fseek(fp,0,SEEK_SET);
  
    if((version=ape_scan(file,fp))>0) {
      if((ret=ape_read_fields(fp,version,md,prov))==NOIR_SUCC)
        meta_make_sortkey(md,prov); }
    else
      ret=version;
  
    fclose(fp); }
  else
    ret=NOIR_NOFILE;

  return ret; }



/**
 * let info-dialog do the layout for us
 * @return NOIR_SUCC to do our own layout, NOIR_FAIL to have the caller do it
 */
int layoutp(void) {
  return NOIR_FAIL; }
