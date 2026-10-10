#ifndef NOCT_PIN_H
#define NOCT_PIN_H
#include <stddef.h>
int pin_valid(const char *pin);
int pin_encode(const char *pin,const unsigned char salt[16],char out[97]);
int pin_verify(const char *pin,const char *encoded);
#endif
