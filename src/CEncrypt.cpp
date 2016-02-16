#include <assert.h>
#include <string.h>
#include <gcrypt.h>
#include <unistd.h>
GCRY_THREAD_OPTION_PTHREAD_IMPL;

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
        uint8_t key[32];
        uint8_t enccheckbytes[32];
        uint8_t checkbytes[32];
        int32_t hashreps;
    } user[4];

} TEncHeader;


void CEncrypt::PassToHash(const std::string &message, uint8_t salt[32], uint8_t passkey[32], int hashreps)
{
    char *pass = getpass(message.c_str());
    gpg_error_t ret = gcry_kdf_derive( pass, strlen(pass), 
    GCRY_KDF_PBKDF2, GCRY_MD_SHA256,
    salt, 32, hashreps, 
    32, passkey);
    memset(pass, 0, strlen(pass));
    assert(ret == 0);
}

void CEncrypt::CreateEnc(int8_t *block)
{
    printf("Create Encryption block\n");
    uint8_t key[32];
    gcry_randomize (key, 32, GCRY_STRONG_RANDOM);

    TEncHeader *h = (TEncHeader*)block;
    memset(h, 0, sizeof(bio.blocksize));
    h->majorversion = 1;
    h->minorversion = 0;
    strcpy(h->magic, "coverfs");

    gcry_create_nonce (h->salt, 32);

    h->user[0].hashreps = 1000;
    strcpy(h->user[0].username, "poke");
    gcry_create_nonce (h->user[0].enccheckbytes, 32);
    
    uint8_t passkey[32];
    PassToHash("Set Password for filesystem: ", h->salt, passkey, h->user[0].hashreps); 
    gpg_error_t ret;

    gcry_cipher_hd_t hd;
    ret =  gcry_cipher_open(&hd, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_ECB, 0);
    assert(ret == 0);
    ret = gcry_cipher_setkey(hd, passkey, 32);
    assert(ret == 0);
    ret = gcry_cipher_encrypt (hd, 
    h->user[0].checkbytes, 32, 
    h->user[0].enccheckbytes, 32);
    ret = gcry_cipher_encrypt (hd, h->user[0].key, 32, key, 32);
    for(int i=0; i<32; i++) key[i] = 0x0;
    gcry_cipher_close(hd);

    gcry_md_hash_buffer(GCRY_MD_CRC32, &h->crc, (int8_t*)h+4, bio.blocksize-4);
}

CEncrypt::CEncrypt(CAbstractBlockIO &_bio) : bio(_bio)
{
    assert(sizeof(TEncHeader) == 4+8+4+32+(128+32+32+32+4)*4);
    assert(bio.blocksize >= 1024);
    gpg_error_t ret;

    ret = gcry_control (GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
    gcry_check_version (NULL);
    gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);

    assert(gcry_md_get_algo_dlen (GCRY_MD_CRC32) == 4);

    int8_t block[bio.blocksize];
    bio.Read(0, 1, block);
    TEncHeader *h = (TEncHeader*)block;
    if (strncmp(h->magic, "coverfs", 8) != 0)
    {
        CreateEnc(block);
        bio.Write(0, 1, block);
    }

    int32_t crc;
    gcry_md_hash_buffer(GCRY_MD_CRC32, &crc, (int8_t*)h+4, bio.blocksize-4);
    assert(h->crc == crc);
    assert(h->majorversion == 1);
    assert(h->minorversion == 0);

    uint8_t passkey[32];
    PassToHash("Password: ", h->salt, passkey, h->user[0].hashreps); 

    gcry_cipher_hd_t hd;
    ret =  gcry_cipher_open(&hd, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_ECB, 0);
    assert(ret == 0);
    ret = gcry_cipher_setkey(hd, passkey, 32);
    assert(ret == 0);
    uint8_t check[32];
    ret = gcry_cipher_encrypt(hd, check, 32, h->user[0].enccheckbytes, 32);
    assert(ret == 0);

    if (memcmp(check, h->user[0].checkbytes, 32) != 0)
    {
        fprintf(stderr, "Error: Cannot decrypt filesystem. Did you type the right password?\n");
        exit(1);
    }
    uint8_t key[32];
    ret = gcry_cipher_decrypt(hd, key, 32, h->user[0].key, 32);
    assert(ret == 0);
    gcry_cipher_close(hd);

    for(int i=0; i<4; i++)
    {
        ret =  gcry_cipher_open(&hdblock[i], GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_CBC, 0);
        assert(ret == 0);
        ret = gcry_cipher_setkey(hdblock[i], key, 32);
        assert(ret == 0);
    }

    memset(key, 0, 32);
    memset(block, 0, bio.blocksize);
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
        gpg_error_t ret = gcry_cipher_decrypt(hdblock[i], d, bio.blocksize, NULL, 0);
        assert(ret == 0);
        mutex[i].unlock();
        return;
    }

    // all cipher handles are locked. Wait ...
    mutex[0].lock();
    gcry_cipher_setiv (hdblock[0], iv, 16);
    gpg_error_t ret = gcry_cipher_decrypt(hdblock[0], d, bio.blocksize, NULL, 0);
    assert(ret == 0);
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
        gpg_error_t ret = gcry_cipher_encrypt(hdblock[i], d, bio.blocksize, 0, 0);
        assert(ret == 0);
        mutex[i].unlock();
        return;
    }

    // all cipher handles are locked. Wait ...
    mutex[1].lock();
    gcry_cipher_setiv (hdblock[1], iv, 16);
    gpg_error_t ret = gcry_cipher_encrypt(hdblock[1], d, bio.blocksize, 0, 0);
    assert(ret == 0);
    mutex[1].unlock();
}
