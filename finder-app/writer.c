#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

#define U8 unsigned char

void usage()
{
    syslog(LOG_ERR, "USAGE: writer.c <file full path> <text to find>");
}

int main (int argc, char** argv)
{
    openlog("writer.c:", LOG_NDELAY, LOG_USER);
    if (argc != 3)
    {
        syslog(LOG_ERR, "ERROR: Please specify exactly two arguments!");
        usage();
        return 1;
    }

    char* filename = argv[1];
    char* query_str = argv[2];

    syslog(LOG_DEBUG, "Writing %s to %s", query_str, filename);

    FILE* fstream = fopen(filename, "w+");
    if (fstream == NULL)
    {
        syslog(LOG_ERR, "ERROR: %s", strerror(errno));
        return 1;
    }

    if (fwrite(query_str, sizeof(U8), strlen(query_str), fstream) == 0)
    {
        syslog(LOG_ERR, "ERROR: Nothing is written to %s", filename);
    }

    fflush(fstream);
    fclose(fstream);

    return 0;
}
