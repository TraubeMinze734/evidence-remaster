#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "evidence.h"
#include "action.h"


extern char *view_name[];


void print_version(void);
int  parse_options(noir_sess *ns,int argc, char **argv,action_context **dir_,action_context **sel);


#endif /*OPTIONS_H*/
