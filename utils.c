#include "utils.h"

void set_flag(unsigned int *flags, unsigned int flag_value) {
   *flags |= flag_value;
}

void reset_flag(unsigned int *flags, unsigned int flag_value) {
   *flags &= ~(*flags & flag_value);
}

bool read_flag(unsigned int flags, unsigned int flag_value) {
   return (flags & flag_value) ? true : false;
}
