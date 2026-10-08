#ifndef ERRORS_H

#  define ERRORS_H

#  define NOIR_NOTSUPP     (-16)
#  define NOIR_NOFILE      (-15)
#  define NOIR_META_DLFAIL (-14)
#  define NOIR_META_FILE   (-13)
#  define NOIR_META_FORMAT (-12)
#  define NOIR_MONMIS      (-11)
#  define NOIR_NOEXEC      (-10)
#  define NOIR_PARTIAL      (-9)
#  define NOIR_PATHEX       (-8)
#  define NOIR_TYPEMISMATCH (-7)
#  define NOIR_MUTEX        (-6)
#  define NOIR_NOTFOUND     (-5)
#  define NOIR_OOM          (-4)
#  define NOIR_IGNORED      (-3)
#  define NOIR_NODATA       (-2)
#  define NOIR_FAIL         (-1)
#  define NOIR_SUCC          (0)
#  define NOIR_NOERRORCODE   (1)

const char *noir_error(int);

#endif
