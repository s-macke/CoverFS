CoverFS
=======

Zero-knowledge client-side encrypted network filesystem.
This program offers an encrypted container, which can be accessed remotely.

Features
   * The data is stored in a single file (container) database
   * File access based on FUSE (file system in user space) 
   * Client side zero-knowledge encryption (CBC AES block encryption, PBKDF2 password hashing)
   * Windows and Linux support
   * Several backends for block filesystem
     - remote storage (needs a program which runs on the server)
     - local file
     - RAM
   * In-memory encrypted data caching
   * Random access
   * Automatic growing container (but no shrinking yet)
   * Secure SSL connection
   * No local storage of files

Limits
   * At the moment very slow connection (no streaming)
   * Only one user at a time
   * This is V1.0. The filesystem might break, no check disk tool available yet
   * No change of password after creation yet
   * Fragmentation of filesystem
   * No good testing environment yet

Build CoverFS
=============

Dependencies:
   * fuse (dokan on Windows)
   * boost
   * openssl
   * libgcrypt
   * pthread
   * make build system
   * cygwin build environment under Windows

Type ´make´ to build the binaries.

Run CoverFS
===========

On the server run ´./coverfsserver [port]´ where [port] is the port number on which the server should listen.
On the client run ´./coverfs [host] [port] [mountpoint]´
where [host] is the host address with the running server program, [port] is the portnumber to connect to and [mountpoint] is the
folder which will contain the content of the filesystem.

The first time you run coverfs you are asked for a password for the new filesystem. The filesystem is stored in the file "cfscontainer" on the server.

Optional but highly recommened:

To retrieve a unique set of keys for the SSL encryption run "gen.sh" in the "ssl" directory.  Then copy the 
content of the folder into the SSL folder of the server and the client. Otherwise the connection is vulnerable against
man-in-the middle attacks.


Filesystem Design
=================

The block size is 4096 bytes and the filesystem is encrypted by this block size. The first block is not encrypted but contains data
like password hashes and keys for decrypting the volume.

Overall container layout:

| Encryption block | Superblock | Basic layout table block | Further layout tables and blocks containing directory structure and data |
|----------------------------------------------------------|--------------------------------------------------------------------------|
| This layout is the same for each filesystem              | This is specific to the content                                          |


The tables contain a list of descriptors which define the content of certain fragments in the container.

| inode id |  size  | block ofs in container | 
|----------|---------------------------------|
| 4 byte   | 4 byte | 8 byte                 |

So each descriptor can define the content up to a size of 4 GB.

The "basic layout table" contains only the descriptors of one fixed id (=-2) which defines the position of the "further layout tables"

inode id =  0 is the id of the root directory structure
inode id = -1 defines a descriptor which is not used and can be overwritten
inode id = -2 contains the layout tables of the whole filesystem
inode id = -3 defines the super block
inode id = -4 defines an invalid or unknown id like the parent dir of the root directory

The id of the parent must always be either invalid (unknown) and point to a directory structure


FAQ
===

Why the name CoverFS?

  FS = Filesystem
  Cover = to hide, to mask, to obfuscate, to cloud, covert, covered by clouds


Is it secure? 
Short answer: No, this is V1.0. Ask me again in 10 years.

Long answer:
Encryption is difficult to implement correctly and I am no expert in that field. I think I made no obvious 
mistakes but the code needs to be checked by more experienced people.
And even then, without a broad range of users and professional attackers who fail, I can 
never claim to be safe.


What about filesystem corruption?

The filesystem structure is simple and optimized in order to minimize read and write access. 
It is build on the principle, that the filesystem is always consistent no matter of the writing order.
But there is no guarantee, that no data will be lost.

Also a file system structure checking and correction tool is not implemented yet. 
My small test suite works, but I haven't used it much under real conditions. 


What does zero-knowledge mean?

A zero knowledge storage hides also meta-data such as file sizes, file names and directory structures from the server operator.
Most of the storage providers preserve this information on the server and therefore can't be true "zero-knowledge" services.
Of course this is only possible with client-side encryption.

However, CoverFS might reveal coarse information by analyzing certain access patterns such as 
overall size of the encrypted filesystem and coarse number of entries in a directory.


Are similar tools available?

Take a look here:
https://en.wikipedia.org/wiki/Comparison_of_online_backup_services
https://en.wikipedia.org/wiki/Comparison_of_file_hosting_services

