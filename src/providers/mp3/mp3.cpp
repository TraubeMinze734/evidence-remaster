/** @file  mp3.cpp
 *  @brief meta-data provider plug-in "MP3/ID3"
 *
 *  get/set MP3 ID3 tags using id3lib
 */

extern "C" {

#  include "config.h"
#  include "debug.h"

#  include "util.h"
#  include "meta.h"
#  include "errors.h"

#  include <string.h>
#  include <math.h>

}

#  include "genre.h"

#  include <id3/tag.h>
#  include <id3/misc_support.h>

#  include <stdio.h>
#  include <ctype.h>

#  ifndef TRUE
#    define FALSE (0)
#    define TRUE (!(FALSE))
#  endif



#define MY_NAME "MP3"
#define GENRE_OTHER 12

/* wrapper around meta_init_mp3() */
#define mimp3(f,k,d,v)  meta_init_mp3(ret,META_RW,META_STR,(k),(d),MY_NAME,v,prov,f)



/**
 * convenience function for setting meta-data defaults
 * @return a meta-data entry
 * @param a  the list of meta-data
 * @param flags  flags (META_RW)
 * @param type   type  (META_STR)
 * @param k      name
 * @param d      description
 * @param g      group (MY_NAME)
 * @param v      default-value
 * @param p      provider (caller's 'prov')
 * @param u2     ID3lib field-ID
 */
noir_meta *meta_init_mp3(noir_md *a,meta_flags flags,meta_type type,const char *k,const char *d,const char *g,const char *v,noir_provider *p,long u2) {
  if(meta_dflt(a,flags,type,k,d,g,v,p)==NOIR_SUCC) {
    noir_meta *m=meta_in_group(a,k,g);
    if(m) {
      meta_set_user2(m,u2);
      return m; }}
  return NULL; }



/**
 * make sort-key from artist and title
 * @return an error-code
 * @param ret   the meta-data list
 * @param prov  the provider (us)
 */
static int meta_make_sortkey(noir_md *ret,noir_provider *prov) {
  noir_meta *x;
  char      *t=NULL,*s=NULL;
  const char *a=NULL;

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



/**
 * remove all non-alpha characters from genre-name and convert to lowercase
 * called from genre_name_cmp(), genre_name_to_index()
 * @return the cleaned-up genre-name
 * @param c  the potentially non-normal genre-name. modified.
 */
static char *genre_name_normalize(char *c) {
  char *p=c;

  if(!p)
    return NULL;

  while(*p) {
    if(!isalpha(*p))
      memmove(p,&p[1],strlen(p));
    else {
      *p=tolower(*p);
      p++; }}
  return c; }



/**
 * compare two genre-names
 * called by genre_name_to_index()
 * @return -1,0,1
 * @param pre  pre-normalized 1st name
 * @param now  not normalized 2nd name (not modified)
 */
static int genre_name_cmp(char *pre,const char *now) {
  int   ret;
  char *p=strdup(now);

  genre_name_normalize(p);
  ret=strcmp(pre,p);
  free(p);
  return ret; }



/**
 * have genre-name, want index
 * if there is any text after the genre-name, it will be preserved
 * @return the textual description of the genre. caller must free()
 * @param i  the genre-name, as a C-string
 */
static char *genre_name_to_index(char *i) {
  char   *r=NULL,*p,*m=NULL;
  int     n=GENRE_OTHER;
  size_t  l;

  if(!i||!*i)
    return NULL;
  m=i=strdup(i);
  if(i[0]=='(')
    return i;
  if((p=r=strchr(i,':'))) {
     do {
       *(r++)='\0';
     } while(isspace(*r));
     while((p>i)&&isspace(*p))
       *(p--)='\0'; }
  while(isspace(*i))
    memmove(i,&i[1],strlen(i));
  if(i) {
    int c=0,f=0;
    genre_name_normalize(i);
    while((c<genre_count)&&(f=genre_name_cmp(i,genre_table[c])))
      c++;
    if(!f)
      n=c; }

  l=8+(r?(strlen(r)+1):0);
  if((p=(char *)malloc(l)))
    snprintf(p,l,"(%d)%s%s",n,r?" ":"",r?r:"");
  if(m)
    free(m);
  return p; }



/**
 * have genre index, want name.
 * if there is any text after the index, it will be preserved
 * @return the textual description of the genre. caller must free()
 * @param t  the index, as a C-string
 */
static char *genre_index_to_name(char *t) {
  estring *s=estring_new(512);
  char    *p=NULL;
  int      i=0;

  if(sscanf(t,"(%d)",&i)==1) {
    if(i>genre_count)   // they added genres w/o telling us, default to "other"
      i=GENRE_OTHER;
//  std::cout << genre_table[i] << " " << t << std::endl;

    // if there is any text after the index, preserve it
    if((p=strchr(t,')'))) {
      p++;
      while(isspace(*p))
         ++p; }
    if(p&&*p)
      estring_appendf(s,"%s: %s",genre_table[i],p);
    else
      estring_appendf(s,"%s",genre_table[i]);
    p=strdup(ESTRING_GET_CSTRING(s)); }
  estring_free(s,TRUE);
  return p; }



/* key xlation entry */
typedef struct _kxe {
  int         id;
  const char *name; } kxe;

/**
 * translate ID3v2 field-name into something human readable
 * @return  the "better" name
 * @param id  the ID of the field
 */
static inline const char *key_xlate(int id) {
  kxe tab[]= {{ ID3FID_TITLE,       "Title" },
              { ID3FID_BAND,        "Band" },
              { ID3FID_LEADARTIST,  "Artist" },
              { ID3FID_ALBUM,       "Album" },
              { ID3FID_TRACKNUM,    "Track #" },
              { ID3FID_CONTENTTYPE, "Genre" }};
  int entries=sizeof(tab)/sizeof(kxe),i;

  for(i=0;i<entries;i++)
    if(tab[i].id==id)
      return tab[i].name;
  return NULL; }



/**
 * write meta-data for this file
 * extractor framework cannot write meta-data, only read it
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int cpp_meta_set_for_file(noir_md *ret,char *file,noir_provider *prov) {
  ID3_Tag            myTag;
  noir_meta         *r;
  ID3_FrameID        id;
  ID3_Tag::Iterator *iter;
  ID3_Frame         *myFrame;

  myTag.Clear();
//  myTag.Link(file,ID3TT_ID3V1|ID3TT_LYRICS3V2|ID3TT_MUSICMATCH);
  myTag.Link(file,ID3TT_ALL);

  iter=myTag.CreateIterator();

  if(!ret||!file)
    return NOIR_FAIL;
  r=meta_get_1st(ret);

  myTag.SetPadding(TRUE);  /* enable padding (for faster writing!) */

// myTag.Strip(ID3TT_ID3);  /* throw out all existing tags (this is evil, use RemoveFrame() instead, much faster!!) */

  while((myFrame=iter->GetNext())) {
     myFrame=myTag.RemoveFrame(myFrame);
     delete myFrame; }

  /* add comments */
  while(r) {                                      /* traverse meta-data */
    id=(ID3_FrameID)meta_get_user2(r);
    if((meta_get_flags(r)&META_RW)&&              /* writable field? */
       (id!=ID3FID_NOFRAME)&&                     /* valid frame? */
       (meta_get_group(r)&&!strcmp(MY_NAME,meta_get_group(r)))&&   /* handled by MP3 provider? */
       (meta_get_val(r))&&                        /* valid meta-data (value?) */
       (strlen((const char *)meta_get_val(r)))) { /* value length? */

      myFrame=new ID3_Frame(id);

      if(myFrame) {
        char *v=(char *)meta_get_val(r),*p=NULL;
        /* if user entered encoded string (starting with '('), don't encode */
        if(id==ID3FID_CONTENTTYPE) {
          if((p=genre_name_to_index(v)))
            v=p; }
        myFrame->GetField(ID3FN_TEXT)->Set(v);

E(3,"adding ||%s||%s||\n",meta_get_key(r),v);

        if(p) {
          char *q=genre_index_to_name(p);
          meta_chg(r,meta_get_flags(r),meta_get_type(r),meta_get_key(r),q?q:p);
          if(q)
            free(q);
          free(p); }
        myTag.RemoveFrame(myFrame);
        myTag.AttachFrame(myFrame); }}
    r=meta_get_next(r); }

  myTag.Update();

  delete iter;

  return NOIR_SUCC; }



/**
 * get meta-data for this file
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
int cpp_meta_get_for_file(noir_md *ret,char *file,noir_provider *prov) {
  ID3_Tag               myTag;
  ID3_Frame            *myFrame=NULL;
  estring              *s=estring_new(1024);
  meta_type             type=META_OLD;
  ID3_Tag::Iterator    *iter;
  noir_meta            *x;

  const Mp3_Headerinfo *mp3info=NULL;

  myTag.Clear();
  myTag.Link(file,ID3TT_ID3V1|ID3TT_LYRICS3V2|ID3TT_MUSICMATCH);
// myTag.Link(file,ID3TT_ALL);



  if((mp3info=myTag.GetMp3HeaderInfo())) {
    estring *t=estring_new(1024);
    const char *v,*l;

    switch(mp3info->version) {
      case MPEGVERSION_1:
        v="MPEG1";   break;
      case MPEGVERSION_2:
        v="MPEG2";   break;
      case MPEGVERSION_2_5:
        v="MPEG2.5"; break;
      default:
        v="unknown MPEGVERSION";
        break; }

    switch(mp3info->layer) {
       case MPEGLAYER_I:
         l="layer I";  break;
       case MPEGLAYER_II:
         l="layer II"; break;
      case MPEGLAYER_III:
        l="layer III";  break;
       default:
         l="unknown layer";
         break; }

    estring_printf(t,"%s/%s",v,l);
    meta_add(ret,META_RO,META_STR,"Version","The MPEG version and layer",MY_NAME,ESTRING_GET_CSTRING(t),prov);

    estring_printf(t,"%4.0f KB/s",((float)mp3info->bitrate)/1000.0);
    meta_add(ret,META_RO,META_STR,"Bitrate","The bitrate (in KB/s)",MY_NAME,ESTRING_GET_CSTRING(t),prov);

    estring_printf(t,"%3.1f KHz",((float)mp3info->frequency)/1000.0);
    meta_add(ret,META_RO,META_STR,"Frequency","The frequency (in KHz)",MY_NAME,ESTRING_GET_CSTRING(t),prov);

    estring_free(t,TRUE); }
  else
    E(1,"GetMp3HeaderInfo(\"%s\") failed...\n",file);



  iter=myTag.CreateIterator();

  while((myFrame=iter->GetNext())) {
    ID3_Frame::Iterator *iter2=myFrame->CreateIterator();
    ID3_Field *myField=NULL;

    const char *desc=myFrame->GetDescription();

    estring_truncate(s,0);

    if(desc) {
      while((myField=iter2->GetNext())) {
        switch(myField->GetType()) {
          case ID3FTY_INTEGER:
            if(myField->Get()) {
              type=META_INT;
              estring_appendf(s,"%d ",myField->Get()); }
            break;
          case ID3FTY_TEXTSTRING:
            type=META_STR;
            if(myFrame->GetID()==ID3FID_CONTENTTYPE) {
              char *t=ID3_GetString(myFrame,ID3FN_TEXT);
              char *p=genre_index_to_name(t);
              estring_appendf(s,"%s ",p?p:t);
              if(p)
                free(p);
              delete [] t; }
            else
              estring_appendf(s,"%s ",ID3_GetString(myFrame,myField->GetID()));
            break;
          default:
          { char *t=ID3_GetString(myFrame,ID3FN_TEXT);
            E(0,"ignoring \"%s\"...\n",t);
            delete [] t;
            break; }}}

        if(type!=META_OLD) {
          char       *p;
          const char *cs="XXX ID3v1 Comment ";
          const char *k;
          size_t      l;

          if(!strncmp(p=ESTRING_GET_CSTRING(s),cs,strlen(cs)))
            p+=strlen(cs);
          while((l=strlen(p))&&isspace(p[l-1]))
            p[l-1]='\0';
          if(!(k=key_xlate(myFrame->GetID())))
             k=desc; // myFrame->GetTextID();
          meta_add(ret,META_RW,META_STR,k,desc,MY_NAME,p,prov);
          if((x=meta_in_group(ret,k,MY_NAME))) {
            meta_set_user1(x,(myFrame->GetID()==ID3FID_CONTENTTYPE)?(void *)genre_table:NULL);
            meta_set_user2(x,(long)myFrame->GetID()); }}}

      delete iter2; }

  mimp3(ID3FID_TITLE,      "Title",  "The song's title","");
  mimp3(ID3FID_BAND,       "Artist", "The artist/band who created or performed this song","");
  mimp3(ID3FID_ALBUM,      "Album",  "The album this song appears on","");
  mimp3(ID3FID_TRACKNUM,   "Track #","The track-number of this song (1..)","");
  mimp3(ID3FID_CONTENTTYPE,"Genre",  "The genre of this song","Other");
  mimp3(ID3FID_YEAR,       "Year",   "The year this track was created","");

  delete iter;
  estring_free(s,TRUE);

  return meta_make_sortkey(ret,prov); }



extern "C" {

/**
 * let info-dialog do the layout for us
 * @return NOIR_SUCC to do our own layout, NOIR_FAIL to have the caller do it
 */
  int layoutp(void) {
    return NOIR_FAIL; }

/**
 * write meta-data for this file -- C wrapper
 * extractor framework cannot write meta-data, only read it
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
  int meta_set_for_file(noir_md *ret,char *file,noir_provider *prov) {
    return cpp_meta_set_for_file(ret,file,prov); }

/**
 * get meta-data for this file -- C wrapper
 * @return an error-code
 * @param ret   the list of (all) meta-data
 * @param file  the file to set the meta-data on
 * @param prov  the provider-entry for this plugin
 */
  int meta_get_for_file(noir_md *ret,char *file,noir_provider *prov) {
    return cpp_meta_get_for_file(ret,file,prov); }}
