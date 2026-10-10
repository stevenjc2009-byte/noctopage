
#include "test.h"
#include "core/pin.h"
int main(void)
{
 unsigned char salt[16]={1,2,3}; char encoded[97],other[97];
 CHECK(pin_valid("1234")); CHECK(pin_valid("12345678")); CHECK(!pin_valid("123")); CHECK(!pin_valid("123456789")); CHECK(!pin_valid("123x"));
 CHECK_INT(pin_encode("1234",salt,encoded),0); CHECK_INT(strlen(encoded),96); CHECK(pin_verify("1234",encoded)); CHECK(!pin_verify("1235",encoded)); CHECK(!pin_verify("1234","bad"));
 salt[0]++; CHECK_INT(pin_encode("1234",salt,other),0); CHECK(strcmp(encoded,other)); encoded[0]='z'; CHECK(!pin_verify("1234",encoded));
 return TEST_DONE("pin");
}
