#ifndef TRUE
#  define FALSE (0)
#  define TRUE  (!FALSE)
#endif

extern int debug;

const char *file_name(const char *tag);

#ifndef D
#  ifdef NOIR_DEBUG
#    define D(fmt,args...) do { if(debug>=0) fprintf(stderr,fmt,## args); } while(0);
#  else
#    define D(msg,args...)
#  endif
#endif
#ifndef E
#  define E(lvl,fmt,args...) do { if(debug>=(lvl)) fprintf(stderr, "%d %s::%s: " fmt,lvl,__FILE__,__FUNCTION__, ## args); } while(0)
#endif
