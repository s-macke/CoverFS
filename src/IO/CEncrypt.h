#ifndef CENCRYPT_H
#define CENCRYPT_H

#include <gcrypt.h>

#include <mutex>
#include "CBlockIO.h"

class CEncrypt
{
public:
    CEncrypt(CAbstractBlockIO &_bio, char* pass);
    void PassToHash(char* pass, uint8_t salt[32], uint8_t passkey[32], unsigned long hashreps);
    void Decrypt(int blockidx, int8_t *d);
    void Encrypt(int blockidx, int8_t* d);
    void CreateEnc(int8_t* block, char *pass);

private:
    int blocksize;
    gcry_cipher_hd_t hdblock[4]; // for multi-threading support we need several cipher handles
    std::mutex mutex[4];
};


#endif
