#include <cassert>
#include <cstring>
#include <gcrypt.h>

GCRY_THREAD_OPTION_PTHREAD_IMPL;

#include "Logger.h"
#include "CEncrypt.h"

//https://gnupg.org/documentation/manuals/gcrypt/Working-with-cipher-handles.html#Working-with-cipher-handles
//https://gnupg.org/documentation/manuals/gcrypt/Key-Derivation.html#Key-Derivation

typedef struct
{
    int32_t crc;
    char magic[8];
    uint16_t majorversion;
    uint16_t minorversion;
    uint8_t salt[32];
    struct {
        char username[128];
        uint8_t key[64];
        uint8_t enccheckbytes[64];
        uint8_t checkbytes[64];
        int32_t hashreps;
    } user[4];

} TEncHeader;

void GCryptCheckError(const char *function, gpg_error_t ret)
{
    if (ret)
    {
        LOG(LogLevel::ERROR) << function << ": Failure: " << gcry_strsource(ret) << "/" << gcry_strerror (ret);
        throw std::exception();
    }
}


void CEncrypt::PassToHash(char *pass, uint8_t salt[32], uint8_t passkey[64], unsigned long hashreps)
{
    gpg_error_t ret = gcry_kdf_derive(
    pass, strlen(pass),
    GCRY_KDF_SCRYPT,
    GCRY_MD_SHA256,
    salt, 32,
    hashreps,
    64, passkey);
    GCryptCheckError("getpass", ret);
}

void CEncrypt::CreateEnc(int8_t *block, char *pass)
{
    LOG(LogLevel::INFO) << "Create Encryption block";
    uint8_t key[64];
    gcry_randomize (key, 64, GCRY_STRONG_RANDOM);

    auto *h = (TEncHeader*)block;
    memset(h, 0, sizeof(blocksize));
    h->majorversion = 1;
    h->minorversion = 0;
    strcpy(h->magic, "coverfs");

    gcry_create_nonce (h->salt, 32);

    h->user[0].hashreps = 500;
    strcpy(h->user[0].username, "poke");
    gcry_create_nonce (h->user[0].enccheckbytes, 64);
    
    uint8_t passkey[64];
    PassToHash(pass, h->salt, passkey, static_cast<unsigned long>(h->user[0].hashreps));
    gpg_error_t ret;

    gcry_cipher_hd_t hd;
    ret =  gcry_cipher_open(&hd, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_ECB, 0);
    GCryptCheckError("gcry_cipher_open", ret);
    ret = gcry_cipher_setkey(hd, passkey, 32);
    GCryptCheckError("gcry_cipher_setkey", ret);
    ret = gcry_cipher_encrypt (hd, 
    h->user[0].checkbytes, 64,
    h->user[0].enccheckbytes, 64);
    GCryptCheckError("gcry_cipher_encrypt", ret);
    ret = gcry_cipher_encrypt (hd, h->user[0].key, 64, key, 64);
    GCryptCheckError("gcry_cipher_encrypt", ret);
    for (unsigned char &i : key) i = 0x0;
    gcry_cipher_close(hd);

    gcry_md_hash_buffer(GCRY_MD_CRC32, &h->crc, (int8_t*)h+4, blocksize-4);
}

CEncrypt::CEncrypt(CAbstractBlockIO &bio, char *pass)
{
    static_assert(sizeof(TEncHeader) == 4+8+4+32+(128+64+64+64+4)*4, "");
    assert(bio.blocksize >= 1024);
    blocksize = bio.blocksize;
    gpg_error_t ret;

    const char* gcryptversion = gcry_check_version (GCRYPT_VERSION);
    LOG(LogLevel::INFO) << "gcrypt version " << gcryptversion;
    if (gcryptversion == nullptr)
    {
        LOG(LogLevel::ERROR) << "gcrypt version too old";
        throw std::exception();
    }
    ret = gcry_control (GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
    GCryptCheckError("gcry_control", ret);
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

    assert(gcry_md_get_algo_dlen (GCRY_MD_CRC32) == 4);

    int8_t block[blocksize];
    bio.Read(0, 1, block);
    auto *h = (TEncHeader*)block;
    if (strncmp(h->magic, "coverfs", 8) != 0)
    {
        CreateEnc(block, pass);
        bio.Write(0, 1, block);
    }

    int32_t crc;
    gcry_md_hash_buffer(GCRY_MD_CRC32, &crc, (int8_t*)h+4, blocksize-4);
    assert(h->crc == crc);
    assert(h->majorversion == 1);
    assert(h->minorversion == 0);

    uint8_t passkey[64];
    PassToHash(pass, h->salt, passkey, static_cast<unsigned long>(h->user[0].hashreps));

    gcry_cipher_hd_t hd;
    ret =  gcry_cipher_open(&hd, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_ECB, 0);
    GCryptCheckError("gcry_cipher_open", ret);
    ret = gcry_cipher_setkey(hd, passkey, 32);
    GCryptCheckError("gcry_cipher_setkey", ret);
    uint8_t check[64];
    ret = gcry_cipher_encrypt(hd, check, 64, h->user[0].enccheckbytes, 64);
    GCryptCheckError("gcry_cipher_encrypt", ret);

    if (memcmp(check, h->user[0].checkbytes, 64) != 0)
    {
        LOG(LogLevel::ERROR) << "Cannot decrypt filesystem. Did you type the right password?";
        throw std::exception();
    }
    uint8_t key[64];
    ret = gcry_cipher_decrypt(hd, key, 64, h->user[0].key, 64);
    GCryptCheckError("gcry_cipher_decrypt", ret);
    gcry_cipher_close(hd);

    for (auto &i : hdblock) {
        ret =  gcry_cipher_open(&i, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_CBC, 0);
        GCryptCheckError("gcry_cipher_open", ret);
        ret = gcry_cipher_setkey(i, key, 32);
        GCryptCheckError("gcry_cipher_setkey", ret);
    }

    memset(key, 0, 64);
    memset(block, 0, blocksize);
}

void CEncrypt::Decrypt(const int blockidx, int8_t *d)
{
    //printf("Decrypt blockidx %i\n", blockidx);
    int32_t iv[4];
    iv[0] = blockidx; iv[1] = 0; iv[2] = 0; iv[3] = 0; // I know this is bad

    if (blockidx == 0) return;

    for(int i=0; i<4; i++)
    {
        if (!mutex[i].try_lock()) continue;
        gcry_cipher_setiv (hdblock[i], iv, 16);
        gpg_error_t ret = gcry_cipher_decrypt(hdblock[i], d, blocksize, NULL, 0);
        GCryptCheckError("gcry_cipher_decrypt", ret);
        mutex[i].unlock();
        return;
    }

    // all cipher handles are locked. Wait ...
    mutex[0].lock();
    gcry_cipher_setiv (hdblock[0], iv, 16);
    gpg_error_t ret = gcry_cipher_decrypt(hdblock[0], d, blocksize, NULL, 0);
    GCryptCheckError("gcry_cipher_decrypt", ret);
    mutex[0].unlock();
}

void CEncrypt::Encrypt(const int blockidx, int8_t* d)
{
    //printf("Encrypt blockidx %i\n", blockidx);
    int32_t iv[4];
    iv[0] = blockidx; iv[1] = 0; iv[2] = 0; iv[3] = 0; // I know, this is bad
    if (blockidx == 0) return;

    for(int i=0; i<4; i++)
    {
        if (!mutex[i].try_lock()) continue;
        gcry_cipher_setiv (hdblock[i], iv, 16);
        gpg_error_t ret = gcry_cipher_encrypt(hdblock[i], d, blocksize, 0, 0);
        GCryptCheckError("gcry_cipher_encrypt", ret);
        mutex[i].unlock();
        return;
    }

    // all cipher handles are locked. Wait ...
    mutex[1].lock();
    gcry_cipher_setiv (hdblock[1], iv, 16);
    gpg_error_t ret = gcry_cipher_encrypt(hdblock[1], d, blocksize, 0, 0);
    GCryptCheckError("gcry_cipher_encrypt", ret);
    mutex[1].unlock();
}
