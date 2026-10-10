
#include "core/pin.h"
#include <string.h>
#ifdef __vita__
#include <mbedtls/pkcs5.h>
#else
#include <openssl/evp.h>
#endif
int pin_valid(const char *pin)
{
 size_t n=strlen(pin); if(n<4 || n>8) return 0;
 for(size_t i=0;i<n;i++) if(pin[i]<'0'||pin[i]>'9') return 0;
 return 1;
}
static void wipe(void *p,size_t n) { volatile unsigned char *s=p; while(n--) *s++=0; }
static int derive(const char *pin,const unsigned char salt[16],unsigned char out[32])
{
#ifdef __vita__
 return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,(const unsigned char *)pin,strlen(pin),salt,16,100000,32,out);
#else
 return PKCS5_PBKDF2_HMAC(pin,(int)strlen(pin),salt,16,100000,EVP_sha256(),32,out)==1 ? 0:-1;
#endif
}
int pin_encode(const char *pin,const unsigned char salt[16],char out[97])
{
 if(!pin_valid(pin)) return -1;
 unsigned char bytes[48]; memcpy(bytes,salt,16);
 if(derive(pin,salt,bytes+16)<0) { wipe(bytes,sizeof bytes); return -1; }
 const char *hex="0123456789abcdef";
 for(int i=0;i<48;i++) { out[i*2]=hex[bytes[i]>>4]; out[i*2+1]=hex[bytes[i]&15]; }
 out[96]=0; wipe(bytes,sizeof bytes); return 0;
}
int pin_verify(const char *pin,const char *encoded)
{
 if(!pin_valid(pin)||strlen(encoded)!=96) return 0;
 unsigned char salt[16]; const char *hex="0123456789abcdef";
 for(int i=0;i<16;i++) {
  const char *a=strchr(hex,encoded[2*i]),*b=strchr(hex,encoded[2*i+1]);
  if(!a||!b) return 0;
  salt[i]=(unsigned char)(((a-hex)<<4)|(b-hex));
 }
 char check[97]; if(pin_encode(pin,salt,check)<0) return 0;
 unsigned diff=0; for(int i=0;i<96;i++) diff|=(unsigned char)check[i]^(unsigned char)encoded[i];
 wipe(check,sizeof check); wipe(salt,sizeof salt); return !diff;
}
