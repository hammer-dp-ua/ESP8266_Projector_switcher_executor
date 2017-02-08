#include "esp_common.h"

#ifndef true // needed only for Eclipse
   typedef unsigned char bool;
   #define true 1
   #define false 0
#endif

void set_flag(unsigned int *flags, unsigned int flag_value);
void reset_flag(unsigned int *flags, unsigned int flag_value);
bool read_flag(unsigned int flags, unsigned int flag_value);
void *set_string_parameters(char string[], char *parameters[]);
char *generate_post_request(char *request);
