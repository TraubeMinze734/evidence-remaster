/** @file  options.c
 *  @brief handle command-line options.
 */

#include <stdlib.h>
#include <ctype.h>

#include "config.h"

#include "session.h"
#include "gui/common/session_private.h"

#include "options.h"
#include "setup.h"
#include "util.h"
#include "file_lcl.h"

#include "gui/efl/common/theme.h"
/* START_IN_* (noir_viewtype) lives in gui/common/session.h */

#ifdef HAVE_EVAS2
#  include <Evas.h>
#endif



char *view_name[]={ "tree-view", "browser-view", "icon-view", "credits-screen" };



/**
 * print version information.
 */
void print_version(void) {
  printf("Evidence: 20 minutes into the future.\n");
  printf("Version %s (%s)\n",VERSION,__DATE__);
  printf("(c)2002-2005 by Azundris et al.\n");
  printf("\nBuilt with %s\n\n",CONFIGURED_WITH); }



/**
 * print usage information and exit.
 * @param name - the program's name
 */
static void print_help(char *name) {
  print_version();
  printf("Usage: %s [option(s)] [directory]\n\n",name);
  printf("Supported Options:\n");
#ifdef HAVE_EVAS2
  printf("-e, --efm\t \tLook like efm\n");
#endif
  puts("");
  printf("-h, --help\t \tPrint this help text\n");
  printf("-V, --version\t \tPrints version information and exits\n");
  printf("-D, --debug=dl\t \tSet verbosity to level <dl> (-1..4)\n");
  printf("-v, --verbose\t \tIncrease verbosity\n");
  printf("-q, --quiet\t \tDecrease verbosity\n");
  puts("");
  printf("-g, --geometry 1x2+3+4 \tmake window 1 wide and 2 high, place at 3,4\n");
  printf("-M, --desktop\t \tStart with a fullscreen \"desktop\" (maximized)\n");
  puts("");
  printf("-T, --theme=theme\tuses that theme\n");
  printf("-P, --icon-set=is\tSelect icon-set (name or path)\n");
  puts("");
  printf("-i, --icons\t \tStart in icon-view\n");
  printf("-t, --tree\t \tStart in tree-view\n");
  printf("-b, --browser\t \tStart in browser\n");
  printf("-a, --about\t \tStart with credits\n");
  puts("");
  printf("-n, --without-notebook\tDo not show a \"notebook\" (\"tabs\")\n");
  printf("-s, --without-shelf\tDo not create a \"shelf\" area (-S)\n");
  printf("-f, --without-border\tDo not \"decorate\" window (-F)\n");
  printf("-w, --without-tooltips\twhen mouse rests on a file-icon, don't open info box --\n\t \t \twait for a click of the middle mouse-button\n");
  puts("");
#ifdef HAVE_EVAS_GL
  printf("-E         \t \tset evas rendering engine (software_x11, gl_x11, ...)\n");
#endif
  puts("");
  printf("-x         \t \tDon't create thumbnails for icon-view\n");
  printf("-y         \t \tDon't create thumbnails for shelf\n");
  puts("");
  printf("-Z, --select\t \topen a view on the directory that contains this\n\t \t \titem.  select the item.  may be given multiple\n\t \t \ttimes if all items reside in the same directory.\n");
  puts("");
  printf("-d, --dir=dir\t \tStart in directory <dir> (efm compatibility)\n");
#if 0
  printf("-c, --config=fn\t \tUse alternate config directory (default ~/.evidence)\n");
#endif
  exit(0); }



/**
 * make path absolute
 * @param ns   - the session (so we can get the cwd)
 * @param path - the path to be made absolute
 */
static char *make_abs(noir_sess *ns,char *path) {
  if(!path||!*path)
    return NULL;
  else if(file_if_absolute(path))
    return strdup(path);
  else if(file_if_local(path))
    return file_compose_path(noir_sess_get_cwd(ns),path,NULL);
  return strdup(path); }



/**
 * react to command-line options.
 * @param ns - session
 * @param argc - as passed to main()
 * @param argv - as passed to main()
 * @param dir_ - anchor of list of dirs to open at start-up
 * @param sel_ - list of files to select at start-up
 */
int parse_options(noir_sess *ns,int argc,char **argv,action_context **dir_,action_context **sel_) {
  int             c,longopt_idx,ret=NOIR_SUCC;
  struct stat     stat_buffer;
  action_context *sel=NULL,*dirs=NULL;

  static struct option longopts[]={

    {"without-shelf",           0, 0, 's'},
    {"no-shelf",                0, 0, 's'},
    {"disable-shelf",           0, 0, 's'},

    {"with-shelf",              0, 0, 'S'},
    {"shelf",                   0, 0, 'S'},
    {"enable-shelf",            0, 0, 'S'},



    {"without-tabs",            0, 0, 'n'},
    {"no-tabs",                 0, 0, 'n'},
    {"disable-tabs",            0, 0, 'n'},

    {"with-tabs",               0, 0, 'N'},
    {"tabs",                    0, 0, 'N'},
    {"enable-tabs",             0, 0, 'N'},



    {"without-notebook",        0, 0, 'n'},
    {"no-notebook",             0, 0, 'n'},
    {"disable-notebook",        0, 0, 'n'},

    {"with-notebook",           0, 0, 'N'},
    {"notebook",                0, 0, 'N'},
    {"enable-notebook",         0, 0, 'N'},



    {"without-borders",         0, 0, 'f'},
    {"no-borders",              0, 0, 'f'},
    {"disable-borders",         0, 0, 'f'},
    {"without-decorations",     0, 0, 'f'},
    {"no-decorations",          0, 0, 'f'},
    {"disable-decorations",     0, 0, 'f'},

    {"with-borders",            0, 0, 'F'},
    {"borders",                 0, 0, 'F'},
    {"enable-borders",          0, 0, 'F'},
    {"with-decorations",        0, 0, 'F'},
    {"decorations",             0, 0, 'F'},
    {"enable-decorations",      0, 0, 'F'},



    {"geometry",                1, 0, 'g'},

    {"full-screen",             0, 0, 'M'},
    {"desktop",                 0, 0, 'M'},
    {"maximize",                0, 0, 'M'},



    {"with-tooltips",           0, 0, 'W'},
    {"tooltips",                0, 0, 'W'},
    {"enable-tooltips",         0, 0, 'W'},

    {"without-tooltips",        0, 0, 'w'},
    {"disable-tooltips",        0, 0, 'w'},



    {"icons",                   0, 0, 'i'},
    {"icon-view",               0, 0, 'i'},
    {"browser-view",            0, 0, 'b'},
    {"tree-view",               0, 0, 't'},
    {"about",                   0, 0, 'a'},
    {"credits",                 0, 0, 'a'},

    {"theme",                   1, 0, 'T'},
    {"icon-set",                1, 0, 'P'},
    {"iconset",                 1, 0, 'P'},

    {"config",                  1, 0, 'c'},

    {"engine",                  1, 0, 'E'},



#ifdef HAVE_EVAS2
    {"efm",                     0, 0, 'e'},
    {"essence",                 0, 0, 'e'},
#endif


    {"select",                  1, 0, 'Z'},

    {"debug",                   1, 0, 'D'},

    {"help",                    0, 0, 'h'},
    {"verbose",                 0, 0, 'v'}, 
    {"version",                 0, 0, 'V'}, 
    {"quiet",                   0, 0, 'q'},
    {NULL,                      0, 0, 0}};



  ns->without_shelf=TRUE;
#if defined(HAVE_EVAS2)||defined(HAVE_GNOMECANVAS)
  ns->start_in=START_IN_ICONVIEW;
  ns->without_notebook=TRUE;
  ns->thumbnails_in_iconlist=ns->thumbnails_in_shelf=TRUE;
#else
  ns->start_in=START_IN_BROWSER;
  ns->without_notebook=FALSE;
  ns->thumbnails_in_iconlist=ns->thumbnails_in_shelf=FALSE;
#endif



  ns->with_tooltips=0; /* TOOLTIP_ENABLE; */

  for(;;) {
    if((c=getopt_long(argc,argv,"MqwWmfFenNsISatibT:P:hHqvVc:D:g:Z:E:",longopts,&longopt_idx))==-1)
      break;

    switch (c) {
#ifdef HAVE_EVAS2
    case 'e':
      ns->without_notebook=TRUE;
      ns->without_shelf=TRUE;
      ns->start_in=START_IN_ICONVIEW;
      ((noir_theme *)ns->theme)->name=strdup("efm");
      if(ns->iconset)
        free(ns->iconset);
      ns->iconset=strdup("efm");
      ns->thumbnails_in_iconlist=TRUE;
      ns->fullscreen=TRUE;
      E(0,"Starting in efm/essence-mode...\n");
      break;
#endif

    case 'g':
#if 0
    { char *e=optarg;
      int gx=0,gy=0,gw=-1,gh=-1,v=0;
      if(((gw=(int)strtol(e,&e,10))>0)&&(*e=='x')) {
        if((gh=(int)strtol(e+1,&e,10))>0) {
          if(!*e)
            v=1;
          else if(*e=='+') {
            gx=(int)strtol(e+1,&e,10);
            if(*e=='+') {
              gy=(int)strtol(e+1,&e,10);
              if(!*e)
                v=3; }}}}
      if(v) {
        ns->gt=v;
        ns->gw=gw;
        ns->gh=gh;
        ns->gx=gx;
        ns->gy=gy; }}
#endif
      ns->geometry=optarg;
      break;

    case 'm':
      ns->fullscreen=FALSE;
      break;
    case 'M':
      ns->fullscreen=TRUE;
      break;

    case 'n':
      ns->without_notebook=TRUE;
      break;
    case 'N':
      ns->without_notebook=FALSE;
      break;

    case 's':
      ns->without_shelf=TRUE;
      break;
    case 'S':
      ns->without_shelf=FALSE;
      break;

    case 'Z':
      if(optarg) {
        char *a=NULL;
        if(!sel)
          sel=action_context_new(0,NULL);
        if(sel) {
          if((a=make_abs(ns,optarg))) {
            action_file_new(sel,a);
            free(a); }}
        if(!sel||!a)
          E(0,"out of memory!?\n"); }
      else
        E(0,"no file given to --select!?\n");
      break;

    case 'f':
      ns->without_decorations=TRUE;
      break;
    case 'F':
      ns->without_decorations=FALSE;
      break;

    case 'W':
      ns->with_tooltips=TOOLTIP_ENABLE;
      break;
    case 'w':
      ns->with_tooltips=0;
      break;



    case 'x':
      ns->thumbnails_in_iconlist=FALSE;
      break;
    case 'X':
      ns->thumbnails_in_iconlist=TRUE;
      break;
    case 'y':
      ns->thumbnails_in_shelf=FALSE;
      break;
    case 'Y':
      ns->thumbnails_in_shelf=TRUE;
      break;



    case 'E':
#ifdef HAVE_EVAS2
      prop_bag_prop_const(props_global,"evas-engine",optarg);
#else
      E(0,"cannot set engine unless compiled with the evas canvas\n");
#endif
      break;

    case 't':
#ifdef WITH_TREEVIEW
      ns->start_in=START_IN_TREEVIEW;
#else
      E(0,"tree-view not available, as evidence was compiled without it.\n");
#endif
      break;
    case 'b':
#ifdef WITH_BROWSERVIEW
      ns->start_in=START_IN_BROWSER;
#else
      E(0,"browser-view not available, as evidence was compiled without it.\n");
#endif
      break;
    case 'i':
#if defined(HAVE_EVAS2)||defined(HAVE_GNOMECANVAS)
      ns->start_in=START_IN_ICONVIEW;
#else
      E(0,"icon-view not available, as evidence was compiled without it.\n");
#endif
      break;
    case 'a':
      ns->start_in=START_IN_CREDITS;
      break;



    case 'c':
      if(!stat(optarg,&stat_buffer)&&ns)
        ns->cfg_file=optarg;
        /* may want to go thru GConf instead.  azou */
      else {
        perror("fstat");
        E(0,"Could not access \"%s\"...\n",optarg); }
      break;

    case 'T':
#if defined(HAVE_EVAS2)||defined(HAVE_GNOMECANVAS)
      /* want theme-dir. if theme-dir/theme-setup was given, cut. */
      if(((noir_theme *)ns->theme)) {
        size_t l;
        ((noir_theme *)ns->theme)->name=strdup(optarg);
        l=strlen(optarg)-strlen(THEME_CFG);
        if((l>0)&&!strcmp(&optarg[l-1],"/" THEME_CFG))
          ((noir_theme *)ns->theme)->name[l-1]='\0'; }
      else
        E(0,"theme support was not initialized(!?), ignoring theme...\n");
#else
      E(0,"not built with a canvas, ignoring theme...\n");
#endif
      break;

    case 'P':
      if(ns->iconset)
        free(ns->iconset);
      ns->iconset=strdup(optarg);
      break;



    case 'V':
      print_version();
      exit(0);
      break;
    case 'h':
    case 'H':
      print_help(argv[0]);
      break;

    case 'D':
      debug=atoi(optarg);
      break;
    case 'v':
      debug++;
      break;
    case 'q':
      debug--;
      break;

    default:
      E(0,"Error: we should never get here! : )   (option -%c ignored)\n",c);
      ret=NOIR_PARTIAL;
      break; }}



  if((dirs=action_context_new(0,NULL))) {
    char *a;
    while(optind<argc) {
      if((a=make_abs(ns,argv[optind++]))) {
        action_file_new(dirs,a);
        free(a); }}}

  E(1,"verbosity set to %d...\n",debug);

#if defined(HAVE_EVAS2)||defined(HAVE_GNOMECANVAS)
  if(((noir_theme *)ns->theme)&&!((noir_theme *)ns->theme)->name)
    ((noir_theme *)ns->theme)->name="default";

  if(!((noir_theme *)ns->theme))
    E(0,"no theme!?\n");
  else
    E(1,"theme \"%s\" selected...\n",((noir_theme *)ns->theme)->name?((noir_theme *)ns->theme)->name:"<null>");
#endif

  E(1,"starting in %s...\n",view_name[ns->start_in]);

  if(ns->cfg_file)
    E(1,"Loading configuration from file \"%s\"...\n",ns->cfg_file);

  if(dir_) {
    *dir_=dirs;
    if(dirs) {
      action_file *d=action_context_get_selection(dirs);
      while(d) {
        E(2,"directory to open: \"%s\"...\n",action_file_get_tag(d));
        d=action_file_get_next(d); }}}
  else
    action_context_dst(dirs);

  if(sel_) {
    *sel_=sel;
    E(2,"pre-selecting %s...\n",sel?"files":"nothing"); }
  else
    action_context_dst(sel);

  if(debug>=0) {
    if(!ns->with_tooltips)
      printf("automatic tooltips are disabled. use the middle mouse-button to call\n"
        "up a tooltip, or start me like so to enable tooltips:\n\n\t%s --enable-tooltips\n\n",
        argv[0]); }

  prop_bag_prop_const(props_global,"evas-engine","software_x11");

  return ret; }
