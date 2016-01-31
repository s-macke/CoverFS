#ifndef CENCRYPT_H
#define CENCRYPT_H

#include <gcrypt.h>

#include <mutex>
#include "CBlockIO.h"

class CEncrypt
{
public:
    CEncrypt(CAbstractBlockIO &_bio);
    void PassToHash(const std::string &message, uint8_t salt[32], uint8_t passkey[32], int hashreps);
    void Decrypt(const int blockidx, int8_t *d);
    void Encrypt(const int blockidx, int8_t* d);
    void CreateEnc(int8_t* block);

private:
    CAbstractBlockIO &bio;
    gcry_cipher_hd_t hdblock;
    std::mutex mtx;    
};


#endif
