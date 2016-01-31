#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/stat.h>

#define MAXSIZE 0xFFFFF

// ----------------------

typedef struct
{
//	FILE *fp;
	int fd;
	char filename[256];
	size_t size;
	char data[MAXSIZE+2];
} FSTRUCT;

FSTRUCT files[10];

// ----------------------


size_t fsize(int fd) 
{
    struct stat st;
    if(fstat(fd, &st) != 0) {
        return 0;
    }
    return st.st_size;   
}


char data[MAXSIZE+2];

int main()
{
	for(int i=0; i<10; i++)
	{
		sprintf(files[i].filename, "tests%i.dat", i);
		//files[i].fp = fopen(files[i].filename, "wb+");
		//files[i].fd = fileno(files[i].fp);
		files[i].fd = open(files[i].filename, O_RDWR | O_CREAT | O_TRUNC);
		files[i].size = 0;
		memset(files[i].data, 0, MAXSIZE+1);
	}

	//while(1)
	for(int iter=0; iter<2000; iter++)
	{
		int cmd = rand()%4;
		int id = rand()%10;
		int ofs = 0;
		if (files[id].size > 0) ofs = rand() % files[id].size;
		int newsize = rand()& MAXSIZE;
                if (ofs+newsize > MAXSIZE) newsize = MAXSIZE - ofs - 1;
		if (newsize < 0) newsize = 0;

		//printf("cmd=%i id=%i\n", cmd, id);
		switch(cmd)
		{
			case 0: // Truncate
				newsize = rand() & MAXSIZE;
				printf("%5i: Truncate %i size=%i\n", iter, id, newsize);
	                        if (newsize > files[id].size)
				{
					memset(&(files[id].data[ files[id].size ]), 0, newsize-files[id].size);
				}
				files[id].size = newsize;
				if (ftruncate(files[id].fd, files[id].size) != 0)
				{
					printf("Error during truncate\n");
					exit(1);
				}
			break;

			case 1: // write
				printf("%5i: write %i ofs=%i size=%i\n", iter, id, ofs, newsize);
				for(int i=0; i<newsize; i++)
				{
					files[id].data[ofs+i] = rand();
				}
				pwrite(files[id].fd, &(files[id].data[ofs]), newsize, ofs);
				if (ofs+newsize > files[id].size) files[id].size = ofs+newsize;
			break;

			case 2: // read
				printf("%5i: read %i ofs=%i size=%i\n", iter, id, ofs, newsize);
		                if (ofs+newsize > files[id].size) newsize = files[id].size - ofs - 1;
				//fseek(files[id].fp, ofs, SEEK_SET);
				//fread(data, newsize, 1, files[id].fp);
				pread(files[id].fd, &data, newsize, ofs);
				for(int i=0; i<newsize; i++)
				{
					if (data[i] != files[id].data[ofs+i])
					{
						printf("read data from file %i does not match at ofs=%i read=%i but should be %i\n", id, ofs+i, data[i], files[id].data[ofs+i]);
						exit(1);
					}
				}
			break;

			case 3: // check filesize
				printf("%5i: filesize %i\n", iter, id);
				if (fsize(files[id].fd) != files[id].size)
				{
					printf("size of file %i does not match %li %li\n", id, files[id].size, fsize(files[id].fd));
					exit(1);
				}
			break;
		}

	}

	for(int i=0; i<10; i++)
	{
		close(files[i].fd);
	}


	return 0;
}
