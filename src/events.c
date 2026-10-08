/** @file  events.c
 *  @brief inform the user of something.
 */

#include "errors.h"

#include <stdio.h>      /* vsnprintf() */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>     /* malloc(), free() */



/*****************************************************************************/



/**
 * get plaintext description for error-code. (strerror() clone)
 * @param no - a noir error-code
 */
const char *noir_error(int no) {
  static const char *noir_errstr[]={
    "Success",
    "General failure",
    "No data",
    "Action was not necessary",
    "Out of memory",
    "Not found",
    "Resource unavailable",
    "Parameters were of the wrong type",
    "Maximum length for path exceeded",
    "Partial success",
    "Parameter is not an executable file",
    "The type of monitor you are trying to set does not match the type of the object you are trying to set the monitor on",
    "File handed to meta-data provider has invalid contents",
    "File-I/O error in meta-data provider",
    "Could not activate meta-data provider",
    "Action requires a file",
    "Action not supported" };

  if(no<0)
    no=-no;
  if(no<=-NOIR_NOTSUPP)
    return noir_errstr[no];
  return "No such error"; }



/*****************************************************************************/



/**
 * inform the user of something.
 * @param ns - the session that wanted to talk to you
 * @param fmt - the message, possibly containing %-substitutions as per sprintf()
 * @param ... - variables for the above %-substitutions
 */
void alert(void *ns, const char *fmt, ...) {
/*  long     err=errno; */
  va_list  ap;
  int      len;
  char    *buffer;

  va_start(ap,fmt);
  len=vsnprintf(NULL,0,fmt,ap);
  if(len>0) {
    buffer=malloc(len+1);
    vsnprintf(buffer,len+1,fmt,ap);
/*  error(0,err,"%s",buffer); */
    free(buffer); }
  va_end(ap); }



/*****************************************************************************/
