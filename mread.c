/*
 *
 *  mread.c - CR Space read access
 *
 */

#include "mtcr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void usage(const char *n)
{
    printf("%s <device> <addr>\n", n);
    exit(1);
}

int main(int ac, char *av[])
{
    char          *endp;
    int           rc=0;
    unsigned int  addr, val;
    mfile         *mf;

    if (ac != 3 )
        usage(av[0]);

    addr = strtoul(av[2], &endp, 0);
    if (*endp)
        usage(av[0]);

    mf = mopen(av[1]);
    if (!mf)
    {
        perror("mopen");
        return 1;
    }

    if ((rc = mread4(mf, addr, &val)) < 0)
    {
        mclose(mf);
        perror("mread");
        return 1;
    }
    if (rc < 4)
    {
        mclose(mf);
        printf("Read only %d bytes\n", rc);
        return 1;
    }

    mclose(mf);
    printf("Read 0x%08x:0x%08x\n", addr, val);
    return rc;
}
