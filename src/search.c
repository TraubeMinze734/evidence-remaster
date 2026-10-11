/** @file  search.c
 *  @brief search files by meta-data and create a "live" folder
 */

#include "config.h"
#include <stdio.h>
#include "errors.h"        /* NOIR_* */



#ifdef HAVE_DOODLE

#  include <stdlib.h>      /* free() */
#  include <string.h>      /* str*() */
#  include <dlfcn.h>       /* RTLD_NOW */
#  ifdef HAVE_EXTRACTOR
#    include <extractor.h>
#  endif
#  include <doodle.h>
#  include <errno.h>
#  include <stdarg.h>

#  include "gui/common/alert.h"
#  include "search.h"      /* SEARCH_* */
#  include "debug.h"       /* E() */
#  include "setup.h"       /* get_setup_file() */
#  include "file.h"        /* file_exists() */
#  include "util.h"        /* esprintf() */
#  include "model.h"       /* file_info_...() */
#  include "file_lcl.h"    /* file_compose_path() */

#include <dirent.h>






/*****************************************************************************/



struct _libdoodle {
  void *handle;
#if (DOODLE_VERSION>=0x00030000)
  struct DOODLE_SuffixTree *(*DOODLE_tree_create)(DOODLE_Logger log,void *context,const char *database);
#else
  struct DOODLE_SuffixTree *(*DOODLE_tree_create)(DOODLE_Logger log,const char *database);
#endif
  void                      (*DOODLE_tree_destroy)(struct DOODLE_SuffixTree *tree);
  int                       (*DOODLE_tree_dump)(FILE *stream,struct DOODLE_SuffixTree *tree);
#if (DOODLE_VERSION>=0x00040000)
  int                       (*DOODLE_tree_search)(struct DOODLE_SuffixTree *tree,const char *ss,DOODLE_ResultCallback callback,void *arg);
  int                       (*DOODLE_tree_search_approx)(struct DOODLE_SuffixTree *tree,const unsigned int approx,const int ignore_case,const char *ss,DOODLE_ResultCallback callback,void *arg);
#else
  struct DOODLE_Node       *(*DOODLE_tree_search)(const struct DOODLE_SuffixTree *tree,const char *substring);
  int                       (*DOODLE_tree_search_approx)(struct DOODLE_Node *pos,const unsigned int approx,const int ignore_case,const struct DOODLE_SuffixTree *tree,const char *ss,struct DOODLE_Node ***result,unsigned int *resultSize);
  int                       (*DOODLE_tree_iterate)(const struct DOODLE_SuffixTree *tree,struct DOODLE_Node *node,DOODLE_TreeCallback callback,void *arg);
  struct DOODLE_Node       *(*DOODLE_getTreeRoot)(const struct DOODLE_SuffixTree *tree);
#endif
  int                       (*DOODLE_tree_expand)(struct DOODLE_SuffixTree *tree,const unsigned char *searchString,const char *fileName);
   int (*DOODLE_tree_truncate)(struct DOODLE_SuffixTree * tree, const char * fileName); 
};



#ifdef HAVE_EXTRACTOR
struct _libextractor {
  void *handle;
  EXTRACTOR_ExtractorList *(*EXTRACTOR_loadDefaultLibraries)(void);
  EXTRACTOR_KeywordList   *(*EXTRACTOR_getKeywords)(EXTRACTOR_ExtractorList *extractor,const char *filename);
  EXTRACTOR_KeywordList   *(*EXTRACTOR_removeDuplicateKeywords)(EXTRACTOR_KeywordList *list,const unsigned int options);
  void                     (*EXTRACTOR_freeKeywords)(EXTRACTOR_KeywordList *keywords);
  void                     (*EXTRACTOR_removeAll)(EXTRACTOR_ExtractorList *libraries);
};
#endif



struct _libdoodle        *libdoodle=NULL;
struct _libextractor     *libextractor=NULL;



typedef struct _search_context {
  char                    **filenames_seen;
  int                       seen_size;
  int                       seen_count;
  search_opts               options;
  file_info                *vfolder;
#ifdef HAVE_EXTRACTOR
  EXTRACTOR_ExtractorList  *extractors;
#else
  void                     *extractors;
#endif
} search_context;



typedef struct _scan_context {
#ifdef HAVE_EXTRACTOR
  EXTRACTOR_ExtractorList * elist;
#endif
#ifdef HAVE_DOODLE
  struct DOODLE_SuffixTree * tree;
#endif
  char *db_filename;
  FILE * logFile;
} scan_context;

scan_context cls;

int recursions = 0;


/*****************************************************************************/



/**
 * print debug-info
 * @param ctx    the context, or NULL for none
 * @param level  the level
 * @param msg    the message to print
 * @param ...    any % substitutions for msg
 */
#  if (DOODLE_VERSION>=0x00030000)
static void search_log(void *ctx,unsigned int level,const char *msg,...) {
#  else
static void search_log(unsigned int level,const char *msg,...) {
#  endif
  va_list args;
  if((level==0)||(level<=debug)) {
    va_start(args,msg);
    vfprintf(stdout,msg,args);
    va_end(args);
  }
   
# if( DOODLE_VERSION >= 0x00030000 )
}
# else
}
# endif


   
/**
 * get full path to search-database
 * @return an error-code
 * @param dbp  -> path-fragment or NULL,  <- newly-allocated full path, or NULL
 */
int search_find_db(char **dbp, int show_warning)
{
   char       *cdb,*db;

   if(!dbp)
     return NOIR_FAIL;

   cdb=*dbp;
   *dbp=NULL;

   
   if(!cdb)
     {
	if( !( db = file_user_setup( EVIDENCE_DEFAULT_SEARCH_DB ) ) ) 
	  {
	     E( 10, "Default search database \"%s\" not found...\n", EVIDENCE_DEFAULT_SEARCH_DB );
	     return NOIR_NOFILE;
	  }
        
     }
   else if( !(db = strdup( cdb ) ) )
     return NOIR_OOM;
   
   if( !file_exists( db ) )
     {
	E(0,"Search database \"%s\" not found...\n", db );
	if( show_warning == 1 )
	  {
	     form_alert(NULL,"Search database \"%s\" not found...\n",db);
	  }
	
	free(db);
	return NOIR_FAIL;
     }
   
   *dbp=db;
   
   return NOIR_SUCC;
}

/**
 * closes the open search-database
 */
int search_close_db()
{
   int ret;
   
   ret = NOIR_SUCC;
   
   /* is it open already? */
   if( cls.db_filename == NULL )
     {
	/* no. how did we get here? */
	E( 0, "Trying to close a database that is not open...\n" );
	return NOIR_FAIL;
     }
   else
     {
	libdoodle->DOODLE_tree_destroy( cls.tree );
	cls.db_filename = NULL;
     }
   
   return ret;  
}

/**
 * open search-database
 * @param database file
 */
int search_open_db( char *dflt_db )
{
   int ret;
 
   ret = NOIR_SUCC;

   E( 0, "Opening database. Current is \"%s\", requested is \"%s\".\n", cls.db_filename, dflt_db );
   
   if( cls.db_filename != NULL && strcmp( cls.db_filename, dflt_db ) == 0 )
     {
	ret = NOIR_SUCC;
     }
      
   /* is it open already? */
   else if( cls.db_filename == NULL )
     {
	/* no. lets try to find a suitable database. */
	/* first, we'll try to open the database */
	E( 1, "Creating the search tree for \"%s\"\n", dflt_db );
	cls.tree = libdoodle->DOODLE_tree_create( &search_log, NULL, dflt_db );
	if( !cls.tree )
	  {
	     E( 1, "Couldn't create the search tree. Try to find a suitable database.\n" );
	     /* couldn't open this database. try finding a default one */
	     ret = search_find_db( &dflt_db, 0 );
	     if( ret != NOIR_SUCC  )
	       {
		  /* couldn't find it :( try and create default anyway */
		  char *ndb = file_user_setup( EVIDENCE_DEFAULT_SEARCH_DB );
		  if( ndb == NULL )
		    {
		       E( 1, "Can't write search-db. Exiting :(\n" );
		       return ret;
		    }
		  E( 1, "Didn't find anything... using \"%s\".\n", ndb );
		  free( dflt_db );
		  size_t i2;
		  i2 = strlen( ndb ) + 1;
		  if( !( dflt_db = malloc( i2 ) ) )
		    {
		       return NOIR_OOM;
		    }
		  
		  strcpy( dflt_db, ndb );
		  free( ndb );
		  ret = NOIR_SUCC;
	       }
	     
	     cls.tree = libdoodle->DOODLE_tree_create( &search_log, NULL, dflt_db );
	     if( !cls.tree )
	       {
		  /* something went wrong in our second pass. */
		  return NOIR_FAIL;
	       }
	     
	  }
	size_t i1;
	free( cls.db_filename );
	i1 = strlen( dflt_db ) + 1;
	if( !( cls.db_filename = malloc( i1 ) ) )
	  {
	     return NOIR_OOM;
	  }
	
	strcpy( cls.db_filename, dflt_db );
	E( 0, "Filename is now \"%s\" (got it from \"%s\").\n", cls.db_filename, dflt_db );
	     
	/* load libextractor plugins */
#  ifdef HAVE_EXTRACTOR
	if( libextractor && !cls.elist )
	  {
	     E( 1, "Loading libextractor plugins...\n" );
	     cls.elist=libextractor->EXTRACTOR_loadDefaultLibraries(); 
	     E( 1, "Finished loading. " );
	  }
	
	if( !cls.elist )
	  {
	     /* hmm... something went wrong. maybe show an error message of some sort? */
	     E( 1, "Could not load libextractor libraries!\n" );
	     return NOIR_FAIL;
	  }
#  endif
     }
   else
     {  
	E( 0, "You requested to open \"%s\" but \"%s\" is open and you should close that first...\n", dflt_db, cls.db_filename );
	ret = NOIR_FAIL;
     }
   
   return ret;
}

/**
 * update the search-database
 */
int search_update_db()
{
   int result;
 
   char *tmp_db_filename;
   
   result = NOIR_SUCC;
   
   /* check if database is open */
   if( cls.db_filename == NULL )
     {
	E( 0, "Database is not open :(\n" );
	return NOIR_NOFILE;
     }
   else
     {
	/* store the database filename */
	size_t i1;
	i1 = strlen( cls.db_filename ) + 1;
	if( !( tmp_db_filename = malloc( i1 ) ) )
	  {
	     return NOIR_OOM;
	  }
	
	strcpy(tmp_db_filename, cls.db_filename );

	/* database should be open. dump everything to the tree and open it again. */
	E( 1, "Updating \"%s\". tmp_db_filename == \"%s\".\n", cls.db_filename, tmp_db_filename );
	result = search_close_db();
	
	if( result != NOIR_SUCC )
	  {
	     E( 0, "Could not update \"%s\" for some reason :(\n", cls.db_filename );
	  }
	else
	  {
	     E( 0, "Re-openning \"%s\"\n", tmp_db_filename );
	     result = search_open_db( tmp_db_filename );
	     if( result != NOIR_SUCC )
	       {
		  E( 0, "Could not open the updated version of the search-database :(\n" );
	       }
	     return result;
	  }
     }

   free( tmp_db_filename );
   
   return result;
}


/*****************************************************************************/

int search_remove_file_keywords( const char *file )
{
   libdoodle->DOODLE_tree_truncate( cls.tree, file );  
   return NOIR_SUCC;
}


int search_index_file( const char *file, char *filename )
{

   int ret;
	
   ret = NOIR_SUCC;
   if( !filename )
     {
	char *tmp_filename;
	
	size_t i1;
	i1 = strlen( strrchr( file, '/' ) + 1 );
	if( !( tmp_filename = malloc( i1 ) ) )
	  {
	     return NOIR_OOM;
	  }
	tmp_filename = strrchr( file, '/' );
	tmp_filename++;
	
	i1 = strlen( tmp_filename ) + 1;
	filename = malloc( i1 );
	strcpy( filename, tmp_filename );
	free( tmp_filename );
     }
   
   /* FIXME: Check if the file was changed before re-indexing */
   /* clean previous file info */
   search_remove_file_keywords( file );

#  ifdef HAVE_EXTRACTOR
   if( libextractor ) 
     {
	
	EXTRACTOR_KeywordList *head = libextractor->EXTRACTOR_getKeywords( cls.elist, file );
	EXTRACTOR_KeywordList *pos;
	
	head = libextractor->EXTRACTOR_removeDuplicateKeywords( head, EXTRACTOR_DUPLICATES_REMOVE_UNKNOWN );
	
	E( 1,"Getting keywords from \"%s\" using extractor...\n", file );
	
	/* for each keyword... */
	for( pos = head; pos; pos = pos->next )
	  {
	     
	     char * keyword_current_position;
	     E(1,"Got \"%s\" keyword for file \"%s\".\n", pos->keyword, file );
	     
	     /* ... extract it's substrings */
	     for( keyword_current_position = pos->keyword; keyword_current_position[0] != '\0'; keyword_current_position++ )
	       libdoodle->DOODLE_tree_expand( cls.tree, keyword_current_position, file );
	  }
	
	/* free the keywords */
	libextractor->EXTRACTOR_freeKeywords( head );
     }
   
# endif

   /* add the complete filename to the tree ... */
   libdoodle->DOODLE_tree_expand( cls.tree, filename, file );
   
   /* ... and start trimming the filename as well. */
   const char *current_position;
   for( current_position = filename; current_position[0] != '\0'; current_position++ )
     libdoodle->DOODLE_tree_expand( cls.tree, current_position, file );
   
   /* should I update the database myself? */
   if( recursions == 0 )
     {
	ret = search_update_db();
     }

   /* guess not */
   return ret;
}

int search_scan_dir(const char *cdb,const char *dir,int recoursive_indexing)
{
   int            ret;
   DIR           *dinfo;
   struct dirent *finfo; 
   
   if( !libdoodle )
     {
	E( 0, "libdoodle not loaded, ignoring request to scan dir \"%s\"...\n",dir?dir:"");
	return NOIR_NOTSUPP;
     }
   
   E(1,"Making \"%s\" searchable...\n",dir);
   ret = NOIR_SUCC;

   if( recursions == 0 )
     {
	/* trying to make a first pass at opening the database */
	if( ( ret = search_open_db( cls.db_filename ) ) != NOIR_SUCC )
	  {
	     /* the file doesn't exist. try to get the file from a default location */
	     ret = search_open_db( EVIDENCE_DEFAULT_SEARCH_DB );
	     if( ret != NOIR_SUCC )
	       {
		  /* the user directory might not be writable or the disk might be full. throw an error and quit */
		  E( 0, "FATAL ERROR: evidence setup dir is not writable or your disk is full." );
		  form_alert( NULL, "FATAL ERROR: evidence setup dir is not writable or your disk is full." );
		  return NOIR_FAIL;
	       }
	     
	  }
	
	E( 1, "Using \"%s\" for search database.\n", cls.db_filename );   
     }
   
   /**
    * Start indexing files.
    * Browse each file from the current directory.
    */
   if( !( dinfo = opendir( dir ) ) )
     {
	E( 0, "Couldn't open \"%s\"!\n", dir );	
	form_alert( "Couldn't open \"%s\"! Please check if you have permissions to access that folder.", dir );
	return NOIR_FAIL;
     }
   
   /* start reading files from directory */
   recursions++;
   while( ( finfo = readdir( dinfo ) ) )
     {
	char *dn;
	
	/* skip hidden files */
	if( finfo->d_name[0] == '.' )
	  continue;
	
	if( !( dn = file_compose_path( dir, finfo->d_name, NULL) ) )
	  {
	     ret=NOIR_OOM;
	     break;
	  }

	/* scan the file's meta-data */
	search_index_file( dn, finfo->d_name );
	
	free( dn );

	/* index inside if it's a directory and if indexation type is recoursive */
	if( recoursive_indexing == 1 && finfo->d_type == DT_DIR )
	  {
	     /* build the full path */
	     char *dir_to_search;
	     size_t i2;
	     i2 = strlen( dir ) + strlen( finfo->d_name ) + 2;
	     if( !( dir_to_search = malloc( i2 ) ) )
	       {
		  return NOIR_OOM;
	       }
	     
	     snprintf( dir_to_search, i2, "%s/%s", dir, finfo->d_name );
	     E( 1, "Recoursivelly searching inside \"%s\"...\n", dir_to_search );
	     ret = search_scan_dir( cls.db_filename, dir_to_search, 1 );
	     if( ret != NOIR_SUCC )
	       {
		  return ret;
	       }
	     
	     free( dir_to_search );
	  }
     }
   recursions--;
   
   closedir(dinfo);
   
   /* pheewww. we're done. let's pack up and go home. */

   if( recursions == 0 )
     {
	ret = search_update_db();
     }
   
   return ret;

}


/*****************************************************************************/



/**
 * process a match
 * @param filename  the match
 * @param ctx       the context, or NULL for none
 */
#  if (DOODLE_VERSION>=0x00040000)
static void search_found(const DOODLE_FileInfo *fileinfo,search_context *ctx) {
  const char *filename=fileinfo->filename;
#  else
static void search_found(const char *filename,search_context *ctx) {
#  endif
  int i;

   /* avoid non-existing files */
   /* FIXME: We need to remove them from the tree... */
   if( !file_exists( filename ) )
     {
	search_remove_file_keywords( filename );	  
	E( 1, "The file \"%s\" is on the database, but didn't exist on the system. Removed :)\n", filename );
	return;
     }
   
   
  /* avoid dupes */  
  for(i=ctx->seen_count-1;i>=0;i--)
    if(!strcmp(filename,ctx->filenames_seen[i]))
      return;

  /* grow results array as needed */
  if(ctx->seen_count==ctx->seen_size) {
    int    nc=ctx->seen_size*2+128;
    char **np;

    np=(char **)realloc(ctx->filenames_seen,sizeof(char *)*nc);
    if(!np) {
      E(0,"out of memory, ignoring result \"%s\"...\n",filename);
      return; }

    ctx->filenames_seen=np;
    ctx->seen_size=nc; }

  /* remember file-name */
  ctx->filenames_seen[ctx->seen_count++]=strdup(filename);

  file_info_virtual(ctx->vfolder,filename,VIM_INDEX); }






/*****************************************************************************/
/* public */
/**********/



/**
 * search for files with meta-data matching a given keyword
 * @return an error-code
 * @param db       the database to use (file-name)
 * @param prefix   prefix for searches ("/Searches")
 * @param keyword  the keyword to look for
 * @param opt      search-options (case-folding, approx-search, ...)
 */
int search_file_by_md(const char *cdb,const char *prefix,const char *keyword,search_opts opt) {
  int                       ret,i;
  struct DOODLE_SuffixTree *tree;
  search_context            ctx;
  file_info                *searches,*root=NULL;
  char                     *vfolder_name=NULL,*db=(char *)cdb;

  if(!libdoodle) {
    E(0,"libdoodle not loaded, ignoring search request \"%s\"...\n",keyword?keyword:"");
    return NOIR_NOTSUPP; }

  if((ret=search_find_db(&db, 1))!=NOIR_SUCC)
    return ret;

#  if (DOODLE_VERSION>=0x00030000)
  tree=libdoodle->DOODLE_tree_create(&search_log,NULL,db);
#  else
  tree=libdoodle->DOODLE_tree_create(&search_log,db);
#  endif
  if(!tree) {
    E(0,"failed to read search database \"%s\": %s\n",db,strerror(errno));
    form_alert(NULL,"failed to read search database \"%s\": %s\n",db,strerror(errno));
    free(db);
    return NOIR_NOFILE; }
  else
    free(db);

  ret=NOIR_SUCC;

  if(!keyword||!*keyword) {
    libdoodle->DOODLE_tree_dump(stdout,tree);
    goto done; }

  if(!(searches=file_info_fetch(root,prefix))) {
    if(!(searches=file_info_vfolder(root,prefix))) {
      ret=NOIR_FAIL;
      goto done; }}

  if(!(vfolder_name=vfolder_name=file_compose_path(prefix,keyword,NULL))) {
    ret=NOIR_OOM;
    goto done; }

  ctx.filenames_seen=NULL;
  ctx.seen_size     =0;
  ctx.seen_count    =0;
  ctx.options       =opt;
  ctx.extractors    =NULL;

  if(!(ctx.vfolder=file_info_vfolder(root,vfolder_name)))
    E(0,"couldn't create vfolder \"%s\"!?\n",vfolder_name);

  free(vfolder_name);

  if(!ctx.vfolder) {
    ret=NOIR_FAIL;
    goto done; }

  if(!(opt&(SEARCH_CASEFOLD|SEARCH_APPROX))) {
#  if (DOODLE_VERSION>=0x00040000)
    if(!libdoodle->DOODLE_tree_search(tree,keyword,(DOODLE_ResultCallback)&search_found,&ctx)) {
      E(0,"\"%s\" not found!\n",keyword);
      ret=NOIR_NOTFOUND; }
#  else
    struct DOODLE_Node *res=libdoodle->DOODLE_tree_search(tree,keyword);
    if(!libdoodle->DOODLE_tree_iterate(tree,res,(DOODLE_TreeCallback)&search_found,&ctx)) {
      E(0,"\"%s\" not found!\n",keyword);
      ret=NOIR_NOTFOUND; }
#  endif
  }

  else {
#  if (DOODLE_VERSION>=0x00040000)
    libdoodle->DOODLE_tree_search_approx(tree,
                                         (opt&SEARCH_APPROX)?1:0,
                                         (opt&SEARCH_CASEFOLD)?1:0,
                                         keyword,
                                         (DOODLE_ResultCallback)&search_found,
                                         &ctx);
#  else
    struct DOODLE_Node **results=NULL;
    int                  rs=0,j;

    libdoodle->DOODLE_tree_search_approx(libdoodle->DOODLE_getTreeRoot(tree),
                                         (opt&SEARCH_APPROX)?1:0,
                                         (opt&SEARCH_CASEFOLD)?1:0,
                                         tree,keyword,&results,&rs);

    for(j=0;j<rs;j++)
      libdoodle->DOODLE_tree_iterate(tree,results[j],(DOODLE_TreeCallback)&search_found,&ctx);
      if(!rs) {
        E(0,"\"%s\" not found!\n",keyword);
        ret=NOIR_NOTFOUND; }
      if(results!=NULL)
        free(results);
#  endif
  }

 done:
  libdoodle->DOODLE_tree_destroy(tree);

  for(i=0;i<ctx.seen_count;i++)
    free(ctx.filenames_seen[i]);

  free(ctx.filenames_seen);

  return ret; }



/**
 * initialize searches
 * load libraries etc.
 * @return an error-code
 */
int search_init(void) {
  int   ret;
  char *dp=NULL;

   cls.db_filename = NULL;
   
  if(!(libdoodle=malloc(sizeof(struct _libdoodle))))
    return NOIR_OOM;
  memset(libdoodle,0,sizeof(struct _libdoodle));

#  if (DOODLE_VERSION<0x00050000)
  if(!(dp=file_compose_path(DOODLE_LIBDIR,"libdoodle.so.0",NULL))) {
#  else
  if(!(dp=file_compose_path(DOODLE_LIBDIR,"libdoodle.so.1",NULL))) {
#  endif
    free(libdoodle);
    return NOIR_OOM; }

  ret=dlmulti("searches by meta-data",dp,RTLD_NOW,&libdoodle->handle,
              "!DOODLE_tree_create !DOODLE_tree_destroy !DOODLE_tree_dump "
              "!DOODLE_tree_search_approx !DOODLE_tree_search "
              "?DOODLE_tree_expand !DOODLE_tree_truncate"

#  if (DOODLE_VERSION<0x00040000)
              "!DOODLE_getTreeRoot !DOODLE_tree_iterate"
#  endif
              ,&libdoodle->DOODLE_tree_create,&libdoodle->DOODLE_tree_destroy
              ,&libdoodle->DOODLE_tree_dump
              ,&libdoodle->DOODLE_tree_search_approx
              ,&libdoodle->DOODLE_tree_search
              ,&libdoodle->DOODLE_tree_expand
	      ,&libdoodle->DOODLE_tree_truncate
#  if (DOODLE_VERSION<0x00040000)
              ,&libdoodle->DOODLE_getTreeRoot
              ,&libdoodle->DOODLE_tree_iterate
#  endif
	      );
  if((ret!=NOIR_SUCC)&&(ret!=NOIR_PARTIAL)) {
    const char *err;
    if(!(err=dlerror()))
      err="???";
    E(0,"failed to load \"%s\": %s\n",dp,err);
    E(0,"unable to support searches by meta-data (%s not found).\n",dp);
    free(libdoodle);
    libdoodle=NULL; }
  free(dp);



#  ifdef HAVE_EXTRACTOR
  if(!(libextractor=malloc(sizeof(struct _libextractor))))
    return NOIR_OOM;
  memset(libextractor,0,sizeof(struct _libextractor));

  /* ### FIXME should be EXTRACTOR_LIBDIR */
  if(!(dp=file_compose_path(DOODLE_LIBDIR,"libextractor.so.1",NULL))) {
    free(libextractor);
    return NOIR_OOM; }

  ret=dlmulti("keyword extraction",dp,RTLD_NOW,&libextractor->handle,
              "!EXTRACTOR_loadDefaultLibraries !EXTRACTOR_getKeywords "
              "!EXTRACTOR_removeDuplicateKeywords !EXTRACTOR_freeKeywords "
              "!EXTRACTOR_removeAll",
              &libextractor->EXTRACTOR_loadDefaultLibraries,
              &libextractor->EXTRACTOR_getKeywords,
              &libextractor->EXTRACTOR_removeDuplicateKeywords,
              &libextractor->EXTRACTOR_freeKeywords,
              &libextractor->EXTRACTOR_removeAll);
  if((ret!=NOIR_SUCC)&&(ret!=NOIR_PARTIAL)) {
    const char *err;
    if(!(err=dlerror()))
      err="???";
    E(0,"failed to load \"%s\": %s\n",dp,err);
    E(0,"unable to support searches by meta-data (%s not found).\n",dp);
    free(libextractor);
    libextractor=NULL; }
  free(dp);
#  else
     E( 0, "Unable to support searches by meta-data.\n" );
#  endif

     /* open database */
     char *ev_dflt_db;
     
     size_t i1;
     
     i1 = strlen( getenv( "HOME" ) ) + strlen( "/.evidence/" ) + strlen( EVIDENCE_DEFAULT_SEARCH_DB ) + 1;
     if( !( ev_dflt_db = malloc( i1 ) ) )
       {
	  return NOIR_OOM;
       }
     
     snprintf( ev_dflt_db, i1, "%s/.evidence/%s", getenv( "HOME" ), EVIDENCE_DEFAULT_SEARCH_DB );
     
     E( 0, "Initializing with \"%s\".\n", ev_dflt_db );
     
     ret = search_open_db( ev_dflt_db );

     free( ev_dflt_db );
  return ret;
  }



/**
 * deinitialize searches
 * free resources, unload libraries
 * @return an error-code
 */
int search_exit(void)
{
   if(libdoodle)
     {
	search_close_db();
	if(libdoodle->handle)
	  dlclose(libdoodle->handle);
	free(libdoodle);

#  ifdef HAVE_EXTRACTOR
	if( libextractor )
	  {
	     libextractor->EXTRACTOR_removeAll( cls.elist );
	     if( libextractor->handle )
	       dlclose( libextractor->handle );
	     free( libextractor );
	  }
#  endif
	return NOIR_SUCC;
     }
   else
     return NOIR_IGNORED;

}






/*****************************************************************************/


#else

int search_init(void) { return NOIR_IGNORED; }
int search_exit(void) { return NOIR_IGNORED; }
/* stub so that the searchable/searchable_r action plugins can be loaded
   even when evidence was built without libdoodle; they report "not
   supported" instead of failing with an undefined symbol at dlopen() */
int search_scan_dir(const char *cdb,const char *dir,int recoursive_indexing) {
  fprintf(stderr,"evidence: searches by meta-data are not supported (built without libdoodle).\n");
  return NOIR_NOTSUPP; }

#endif
