/*
 *
 * Copyright (c) 2013 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *  mtcr_ul.c - Mellanox Hardware Access implementation
 *
 */


//use memory mapped /dev/mem for access
#define CONFIG_ENABLE_MMAP 1
//mmap /dev/mem for memory access (does not work on sparc)
#define CONFIG_USE_DEV_MEM 1
//use pci configuration cycles for access
#define CONFIG_ENABLE_PCICONF 1

#ifndef _XOPEN_SOURCE
#if CONFIG_ENABLE_PCICONF && CONFIG_ENABLE_MMAP
/* For strerror_r */
#define _XOPEN_SOURCE 600
#elif CONFIG_ENABLE_PCICONF
#define _XOPEN_SOURCE 500
#endif
#endif

#if CONFIG_ENABLE_MMAP
#define _FILE_OFFSET_BITS 64
#endif

#define MTCR_MAP_SIZE 0x100000

#include <stdio.h>
#include <dirent.h>
#include <string.h>

#include <unistd.h>

#include <netinet/in.h>
#include <endian.h>
#include <byteswap.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>


#if CONFIG_ENABLE_MMAP
#include <sys/mman.h>
#include <sys/pci.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#endif

#include <mtcr.h>
#include "mtcr_int_defs.h"
#include "mtcr_ib.h"

#ifndef __be32_to_cpu
#define __be32_to_cpu(x) ntohl(x)
#endif
#ifndef __cpu_to_be32
#define __cpu_to_be32(x) htonl(x)
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#ifndef __cpu_to_le32
#define  __cpu_to_le32(x) (x)
#endif
#ifndef __le32_to_cpu
#define  __le32_to_cpu(x) (x)
#endif
#elif __BYTE_ORDER == __BIG_ENDIAN
#ifndef __cpu_to_le32
#define  __cpu_to_le32(x) bswap_32(x)
#endif
#ifndef __le32_to_cpu
#define  __le32_to_cpu(x) bswap_32(x)
#endif
#else
#ifndef __cpu_to_le32
#define  __cpu_to_le32(x) bswap_32(__cpu_to_be32(x))
#endif
#ifndef __le32_to_cpu
#define  __le32_to_cpu(x) __be32_to_cpu(bswap_32(x))
#endif
#endif

struct pcicr_context {
    int              fd;
    void            *ptr;
    int              connectx_flush; /* For ConnectX/ConnectX3 */
    int              need_flush; /* For ConnectX/ConnectX3 */
};

struct pciconf_context {
    int              fd;
};

static void mtcr_connectx_flush(void *ptr)
{
    u_int32_t value;
    *((u_int32_t *)((char *)ptr + 0xf0380)) = 0x0;
    do {
        asm volatile ("":::"memory");
        value = __be32_to_cpu(*((u_int32_t *)((char *)ptr + 0xf0380)));
    } while(value);
}

int mread4(mfile *mf, unsigned int offset, u_int32_t *value)
{
    return mf->mread4(mf,offset,value);
}

int mwrite4(mfile *mf, unsigned int offset, u_int32_t value)
{
    return mf->mwrite4(mf,offset,value);
}


// TODO: Verify change 'data' type from void* to u_in32_t* does not mess up things
static int
mread_chunk_as_multi_mread4(mfile *mf, unsigned int offset, u_int32_t* data, int length)
{
    int i;
    if (length % 4) {
        return EINVAL;
    }
    for (i = 0; i < length ; i += 4) {
        u_int32_t value;
        if (mread4(mf, offset + i, &value) != 4) {
            return -1;
        }
        memcpy((char*)data + i , &value,4);
    }
    return length;
}

static int
mwrite_chunk_as_multi_mwrite4(mfile *mf, unsigned int offset, u_int32_t* data, int length)
{
    int i;
    if (length % 4) {
        return EINVAL;
    }
    for (i = 0; i < length ; i += 4) {
        u_int32_t value;
        memcpy(&value, (char*)data + i ,4);
        if (mwrite4(mf, offset + i, value) != 4) {
            return -1;
        }
    }
    return length;
}


enum mtcr_access_method {
    MTCR_ACCESS_ERROR  = 0x0,
    MTCR_ACCESS_MEMORY = 0x1,
    MTCR_ACCESS_CONFIG = 0x2,
    MTCR_ACCESS_INBAND = 0x3
};


/*
* Return values:
* 0:  OK
* <0: Error
* 1 : Device does not support memory access
*
*/
static
int mtcr_check_signature(mfile *mf)
{
    unsigned signature;
    int rc;
    rc = mread4(mf, 0xF0014, &signature);
    if (rc != 4) {
        if (!errno)
            errno = EIO;
        return -1;
    }

    switch (signature) {
    case 0xbad0cafe:  /* secure host mode device id */
        return 0;
    case 0xbadacce5:  /* returned upon mapping the UAR bar */
    case 0xffffffff:  /* returned when pci mem access is disabled (driver down) */
        return 1;
    }

    switch (signature & 0xffff) {
    case 0x190 : /* 400 */
    case 0x1f5 :
    case 0x1f7 :
        if ((signature == 0xa00190         ||
                    (signature & 0xffff) == 0x1f5  ||
                    (signature & 0xffff) == 0x1f7)    && mf->access_type == MTCR_ACCESS_MEMORY) {
            struct pcicr_context* ctx = mf->ctx;
            ctx->connectx_flush = 1;
            mtcr_connectx_flush(ctx->ptr);
        }
    case 0x5a44: /* 23108 */
    case 0x6278: /* 25208 */
    case 0x5e8c: /* 24204 */
    case 0x6274: /* 25204 */
    case 0x1b3:  /*   435 */
    case 6100:   /*  6100 */
    case 0x245:
    case 0x1ff:
        return 0;
    default:
        fprintf(stderr, "-W- Unknown dev id: 0x%x\n", signature);
        errno = ENOTTY;
        return -1;
    }
}

#if CONFIG_ENABLE_MMAP
/*
 * The PCI interface treats multi-function devices as independent
 * devices.  The slot/function address of each device is encoded
 * in a single byte as follows:
 *
 *  7:3 = slot
 *  2:0 = function
 */
#define PCI_DEVFN(slot,func)    ((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)     (((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)     ((devfn) & 0x07)

static
unsigned long long mtcr_procfs_get_offset(unsigned my_bus, unsigned my_dev,
                      unsigned my_func)
{
    FILE* f;
    unsigned irq;
    unsigned long long base_addr[6], rom_base_addr, size[6], rom_size;

    unsigned bus, dev, func;
    //unsigned vendor_id;
    //unsigned device_id;
    unsigned int cnt;

    unsigned long long offset = (unsigned long long)-1;

    char buf[4048];

    f = fopen("/proc/bus/pci/devices", "r");
    if (!f) return offset;

    for(;;) if (fgets(buf, sizeof(buf) - 1, f)) {
        unsigned dfn, vend;

        cnt = sscanf(buf,
                 "%x %x %x %llx %llx %llx %llx %llx %llx "
                 "%llx %llx %llx %llx %llx %llx %llx %llx",
                 &dfn,
                 &vend,
                 &irq,
                 &base_addr[0],
                 &base_addr[1],
                 &base_addr[2],
                 &base_addr[3],
                 &base_addr[4],
                 &base_addr[5],
                 &rom_base_addr,
                 &size[0],
                 &size[1],
                 &size[2],
                 &size[3],
                 &size[4],
                 &size[5],
                 &rom_size);
        if (cnt != 9 && cnt != 10 && cnt != 17)
        {
            fprintf(stderr,"proc: parse error (read only %d items)\n", cnt);
            fprintf(stderr,"the offending line in " "/proc/bus/pci/devices" " is "
                "\"%.*s\"\n", (int)sizeof(buf), buf);
            goto error;
        }
        bus = dfn >> 8U;
        dev = PCI_SLOT(dfn & 0xff);
        func = PCI_FUNC(dfn & 0xff);
        //vendor_id = vend >> 16U;
        //device_id = vend & 0xffff;

        if (bus == my_bus && dev == my_dev && func == my_func)
            break;
    }
    else
        goto error;

    if (cnt != 17 || size[1] != 0 || size[0] != MTCR_MAP_SIZE) {
        if (0) fprintf(stderr,"proc: unexpected region size values: "
            "cnt=%d, size[0]=%#llx, size[1]=%#llx\n",
            cnt,size[0],size[1]);
        if (0) fprintf(stderr,"the offending line in " "/proc/bus/pci/devices"
                   " is \"%.*s\"\n", (int)sizeof(buf), buf);
        goto error;
    }


    offset = ((unsigned long long)(base_addr[1]) << 32) +
        ((unsigned long long)(base_addr[0]) & ~(unsigned long long)(0xfffff));

    fclose(f);
    return offset;

error:
    fclose(f);
    errno = ENXIO;
    return offset;
}

static
unsigned long long mtcr_sysfs_get_offset(unsigned domain, unsigned bus,
                     unsigned dev, unsigned func)
{
    unsigned long long start, end, type;
    unsigned long long offset = (unsigned long long)-1;
    FILE *f;
    int cnt;
    char mbuf[] = "/sys/bus/pci/devices/XXXX:XX:XX.X/resource";
    sprintf(mbuf, "/sys/bus/pci/devices/%4.4x:%2.2x:%2.2x.%1.1x/resource",
               domain, bus, dev, func);

    f = fopen(mbuf, "r");
    if (!f)
        return offset;

    cnt = fscanf(f, "0x%llx 0x%llx 0x%llx", &start, &end, &type);
    if (cnt != 3 || end != start + MTCR_MAP_SIZE - 1) {
        if (0) fprintf(stderr,"proc: unexpected region size values: "
            "cnt=%d, start=%#llx, end=%#llx\n",
            cnt, start, end);
        goto error;
    }

    fclose(f);
    return start;

error:
    fclose(f);
    errno = ENOENT;
    return offset;
}

//
// PCI MEMORY ACCESS FUNCTIONS
//

static
int mtcr_pcicr_mclose(mfile *mf)
{
    struct pcicr_context* ctx = mf->ctx;
    if (ctx) {
        if (ctx->ptr) {
            munmap(ctx->ptr,MTCR_MAP_SIZE);
        }

        if (ctx->fd != -1) {
            close(ctx->fd);
        }
        free(ctx);
        mf->ctx = NULL;
    }

    return 0;
}

static
int mtcr_mmap(struct pcicr_context *mf, const char *name, off_t off, int ioctl_needed)
{
    int err;

    mf->fd = open(name, O_RDWR | O_SYNC);
    if (mf->fd < 0)
        return -1;

    if (ioctl_needed && ioctl(mf->fd, PCIIOC_MMAP_IS_MEM) < 0) {
        err = errno;
        close(mf->fd);
        errno = err;
        return -1;
    }

    mf->ptr = mmap(NULL, MTCR_MAP_SIZE, PROT_READ | PROT_WRITE,
               MAP_SHARED, mf->fd, off);

    if (!mf->ptr || mf->ptr == MAP_FAILED) {
        err = errno;
        close(mf->fd);
        errno = err;
        return -1;
    }

    return 0;
}

int mtcr_pcicr_mread4(mfile *mf, unsigned int offset, u_int32_t *value)
{
    struct pcicr_context *ctx = mf->ctx;
    if (ctx->need_flush) {
        mtcr_connectx_flush(ctx->ptr);
        ctx->need_flush = 0;
    }
    *value = __be32_to_cpu(*((u_int32_t *)((char *)ctx->ptr + offset)));
    return 4;
}

int mtcr_pcicr_mwrite4(mfile *mf, unsigned int offset, u_int32_t value)
{
    struct pcicr_context *ctx = mf->ctx;

    *((u_int32_t *)((char *)ctx->ptr + offset)) = __cpu_to_be32(value);
    ctx->need_flush = ctx->connectx_flush;
    return 4;
}

static
int mtcr_pcicr_open(mfile *mf, const char *name, off_t off, int ioctl_needed)
{
    int rc;
    struct pcicr_context *ctx;

    mf->access_type   = MTCR_ACCESS_MEMORY;

    mf->mread4        = mtcr_pcicr_mread4;
    mf->mwrite4       = mtcr_pcicr_mwrite4;
    mf->mread4_block  = mread_chunk_as_multi_mread4;
    mf->mwrite4_block = mwrite_chunk_as_multi_mwrite4;
    mf->mclose        = mtcr_pcicr_mclose;

    ctx = (struct pcicr_context*)malloc(sizeof(struct pcicr_context));
    if (!ctx)
        return 1;

    ctx->ptr = NULL;
    ctx->fd = -1;
    ctx->connectx_flush = 0;
    ctx->need_flush = 0;

    mf->ctx = ctx;

    rc = mtcr_mmap(ctx, name, off, ioctl_needed);
    if (rc) {
        goto end;
    }

    rc = mtcr_check_signature(mf);

end:
    if (rc) {
        mtcr_pcicr_mclose(mf);
    }

    return rc;
}


//
// PCI CONF ACCESS FUNCTIONS
//


#if CONFIG_ENABLE_PCICONF

int mtcr_pciconf_mread4(mfile *mf, unsigned int offset, u_int32_t *value)
{
    struct pciconf_context *ctx = mf->ctx;
    int rc;
    offset = __cpu_to_le32(offset);
    rc=pwrite(ctx->fd, &offset, 4, 22*4);
    if (rc < 0) {
        perror("write offset");
        return rc;
    }
    if (rc != 4)
        return 0;

    rc=pread(ctx->fd, value, 4, 23*4);
    if (rc < 0) {
        perror("read value");
        return rc;
    }
    *value = __le32_to_cpu(*value);
    return rc;
}

int mtcr_pciconf_mwrite4(mfile *mf, unsigned int offset, u_int32_t value)
{
    struct pciconf_context *ctx = mf->ctx;
    int rc;
    offset = __cpu_to_le32(offset);
    rc = pwrite(ctx->fd, &offset, 4, 22*4);
    if (rc < 0) {
        perror("write offset");
        return rc;
    }
    if (rc != 4)
        return 0;
    value = __cpu_to_le32(value);
    rc = pwrite(ctx->fd, &value, 4, 23*4);
    if (rc < 0) {
        perror("write value");
        return rc;
    }
    return rc;
}

static
int mtcr_pciconf_mclose(mfile *mf)
{
    struct pciconf_context *ctx = mf->ctx;
    unsigned int word;

    if (ctx) {
        mread4(mf, 0xf0014, &word);
        if (ctx->fd != -1) {
            close(ctx->fd);
        }
        free(ctx);
    }

    return 0;
}

static
int mtcr_pciconf_open(mfile *mf, const char *name)
{
    unsigned signature;
    int err;
    int rc;
    struct pciconf_context *ctx;

    mf->access_type   = MTCR_ACCESS_CONFIG;

    mf->mread4        = mtcr_pciconf_mread4;
    mf->mwrite4       = mtcr_pciconf_mwrite4;
    mf->mread4_block  = mread_chunk_as_multi_mread4;
    mf->mwrite4_block = mwrite_chunk_as_multi_mwrite4;
    mf->mclose        = mtcr_pciconf_mclose;

    ctx = (struct pciconf_context*)malloc(sizeof(struct pciconf_context));
    if (!ctx)
        return 1;

    mf->ctx = ctx;

    ctx->fd = -1;

    ctx->fd = open(name, O_RDWR | O_SYNC);
    if (ctx->fd < 0)
        return -1;

    /* Kernels before 2.6.12 carry the high bit in each byte
     * on <device>/config writes, overriding higher bits.
     * Make sure the high bit is set in some signature bytes,
     * to catch this. */
    /* Do this test before mtcr_check_signature,
       to avoid system failure on access to an illegal address. */
    signature = 0xfafbfcfd;
    rc = pwrite(ctx->fd, &signature, 4, 22*4);
    if (rc != 4) {
        rc = -1;
        goto end;
    }

    rc = pread(ctx->fd, &signature, 4, 22*4);
    if (rc != 4) {
        rc = -1;
        goto end;
    }

    if (signature != 0xfafbfcfd) {
        rc = -1;
        errno = EIO;
        goto end;
    }

    rc = mtcr_check_signature(mf);
    if (rc) {
        rc = -1;
        goto end;
    }
end:
    if (rc) {
        err = errno;
        mtcr_pciconf_mclose(mf);
        errno = err;
    }
    return rc;
}
#else
static
int mtcr_pciconf_open(mfile *mf, const char *name)
{
    return -1;
}
#endif

//
// IN-BAND ACCESS FUNCTIONS
//


static
int mtcr_inband_open(mfile* mf, const char* name)
{
    mf->access_type   = MTCR_ACCESS_INBAND;

    mf->mread4        = mib_read4;
    mf->mwrite4       = mib_write4;
    mf->mread4_block  = mib_readblock;
    mf->mwrite4_block = mib_writeblock;
    mf->maccess_reg   = mib_acces_reg_mad;
    mf->mclose        = mib_close;

    return mib_open(name,mf,0);
}



static
enum mtcr_access_method mtcr_parse_name(const char* name, int *force,
                        unsigned *domain_p, unsigned *bus_p,
                        unsigned *dev_p, unsigned *func_p)
{
    unsigned my_domain = 0;
    unsigned my_bus;
    unsigned my_dev;
    unsigned my_func;
    int scnt, r;
    char config[] = "/config";
    char resource0[] = "/resource0";
    char procbuspci[] = "/proc/bus/pci/";

    unsigned len = strlen(name);
    unsigned tmp;

    if (len >= sizeof config && !strcmp(config, name + len + 1 - sizeof config)) {
        *force = 1;
        return MTCR_ACCESS_CONFIG;
    }

    if (len >= sizeof resource0 &&
        !strcmp(resource0, name + len + 1 - sizeof resource0)) {
        *force = 1;
        return MTCR_ACCESS_MEMORY;
    }

    if (!strncmp(name,"/proc/bus/pci/", sizeof procbuspci - 1)) {
        *force = 1;
        return MTCR_ACCESS_CONFIG;
    }

    if (sscanf(name, "lid-%x", &tmp) == 1 ||
        sscanf(name, "ibdr-%x", &tmp) == 1) {
        *force = 1;
        return MTCR_ACCESS_INBAND;
    }


    if (sscanf(name, "mthca%x", &tmp) == 1 ||
        sscanf(name, "mlx4_%x", &tmp) == 1 ||
        sscanf(name, "mlx5_%x", &tmp) == 1) {
        char mbuf[4048];
        char pbuf[4048];
        char *base;

        r = snprintf(mbuf, sizeof mbuf, "/sys/class/infiniband/%s/device", name);
        if (r <= 0 || r >= (int)sizeof mbuf) {
            fprintf(stderr,"Unable to print device name %s\n", name);
            goto parse_error;
        }

        r = readlink(mbuf, pbuf, sizeof pbuf - 1);
        if (r < 0) {
            perror("read link");
            fprintf(stderr,"Unable to read link %s\n", mbuf);
            return MTCR_ACCESS_ERROR;
        }
        pbuf[r] = '\0';

        base = basename(pbuf);
        if (!base)
            goto parse_error;
        scnt = sscanf(base, "%x:%x:%x.%x",
                  &my_domain, &my_bus, &my_dev, &my_func);
        if (scnt != 4)
            goto parse_error;
        goto name_parsed;
    }

    scnt = sscanf(name, "%x:%x.%x", &my_bus, &my_dev, &my_func);
    if (scnt == 3)
        goto name_parsed;

    scnt = sscanf(name, "%x:%x:%x.%x", &my_domain, &my_bus, &my_dev, &my_func);
    if (scnt == 4)
        goto name_parsed;

parse_error:
    fprintf(stderr,"Unable to parse device name %s\n", name);
    errno = EINVAL;
    return MTCR_ACCESS_ERROR;

name_parsed:
    *domain_p = my_domain;
    *bus_p = my_bus;
    *dev_p = my_dev;
    *func_p = my_func;
    *force = 0;
    return MTCR_ACCESS_MEMORY;
}
#endif

int mread4_block (mfile *mf, unsigned int offset, u_int32_t* data, int byte_len)
{
    return mread_chunk_as_multi_mread4(mf, offset, data, byte_len);
}

int mwrite4_block (mfile *mf, unsigned int offset, u_int32_t* data, int byte_len)
{
    return mwrite_chunk_as_multi_mwrite4(mf, offset, data, byte_len);
}

int msw_reset(mfile *mf)
{
    (void)mf; /* Warning */
    return -1;
}

int mdevices(char *buf, int len, int mask)
{

#define MDEVS_TAVOR_CR  0x20
#define MLNX_PCI_VENDOR_ID  "0x15b3"

    FILE* f;
    DIR* d;
    struct dirent *dir;
    int pos = 0;
    int sz;
    int rsz;
    int ndevs = 0;

    if (!(mask & MDEVS_TAVOR_CR)) {
        return 0;
    }

    char inbuf[64];
    char fname[64];

    d = opendir("/sys/bus/pci/devices");
    if (d == NULL) {
        return -2;
    }

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_name[0] == '.') {
            continue;
        }
        sprintf(fname, "/sys/bus/pci/devices/%s/vendor", dir->d_name);
        sz = strlen(dir->d_name);
        f = fopen(fname, "r");
        if (f == NULL) {
            ndevs = -2;
            goto cleanup_dir_opened;
        }
        if (fgets(inbuf, sizeof(inbuf), f)) {
            if(!strncmp(inbuf, MLNX_PCI_VENDOR_ID, strlen(MLNX_PCI_VENDOR_ID))) {
                rsz = sz + 1; //dev name size + place for Null char
                if ((pos + rsz) > len) {
                    ndevs = -1;
                    goto cleanup_file_opened;
                }
                memcpy(&buf[pos], dir->d_name, rsz);
                pos += rsz;
                ndevs++;
            }
        }
        fclose(f);
    }
    closedir(d);

    return ndevs;

cleanup_file_opened:
    fclose(f);
cleanup_dir_opened:
    closedir(d);
    return ndevs;
}

static
int read_pci_config_header(u_int16_t domain, u_int8_t bus, u_int8_t dev, u_int8_t func, u_int8_t data[0x40])
{
    char proc_dev[64];
    sprintf(proc_dev, "/sys/bus/pci/devices/%04x:%02x:%02x.%d/config", domain, bus, dev, func);
    FILE* f = fopen(proc_dev, "r");
    if (!f) {
        fprintf(stderr, "Failed to open (%s) for reading: %s\n", proc_dev, strerror(errno));
        return 1;
    }
    setvbuf(f, NULL, _IONBF, 0);
    if (fread(data, 0x40, 1, f) != 1) {
        fprintf(stderr, "Failed to read from (%s): %s\n", proc_dev, strerror(errno));
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}

dev_info* mdevices_info(int mask, int* len)
{
    char* devs = 0;
    char* dev_name;
    int size = 2048;
    int rc;
    int i;

    // Get list of devices
    do {
        if (devs)
            free(devs);
        size *= 2;
        devs = (char*)malloc(size);
        rc = mdevices(devs, size, mask);
    } while (rc == -1);

    // For each device read
    dev_info* dev_info_arr = (dev_info*) malloc(sizeof(dev_info)*rc);
    memset(dev_info_arr, 0, sizeof(dev_info)*rc);
    dev_name = devs;
    for (i = 0; i < rc; i++) {
        int domain = 0;
        int bus = 0;
        int dev = 0;
        int func = 0;

        dev_info_arr[i].type = (Mdevs)MDEVS_TAVOR_CR;
        u_int8_t conf_header[0x40];
        u_int32_t *conf_header_32p = (u_int32_t*)conf_header;

        // update default device name
        strcpy(dev_info_arr[i].dev_name, dev_name);
        strcpy(dev_info_arr[i].pci.cr_dev, dev_name);

        // update dbdf
        sscanf(dev_name, "%x:%x:%x.%x", &domain, &bus, &dev, &func);
        dev_info_arr[i].pci.domain = domain;
        dev_info_arr[i].pci.bus = bus;
        dev_info_arr[i].pci.dev = dev;
        dev_info_arr[i].pci.func = func;

        // read configuration space header
        if (read_pci_config_header(domain, bus, dev, func, conf_header)) {
            goto next;
        }

        dev_info_arr[i].pci.dev_id = __le32_to_cpu(conf_header_32p[0]) >> 16;
        dev_info_arr[i].pci.vend_id = __le32_to_cpu(conf_header_32p[0]) & 0xffff;
        dev_info_arr[i].pci.class_id = __le32_to_cpu(conf_header_32p[2]) >> 8;
        dev_info_arr[i].pci.subsys_id = __le32_to_cpu(conf_header_32p[11]) >> 16;
        dev_info_arr[i].pci.subsys_vend_id = __le32_to_cpu(conf_header_32p[11]) & 0xffff;

        // set pci conf device
        snprintf(dev_info_arr[i].pci.conf_dev, sizeof(dev_info_arr[i].pci.conf_dev), "/sys/bus/pci/devices/%04x:%02x:%02x.%x/config", domain, bus,dev, func);
        //Copy to dev_name as default device
        snprintf(dev_info_arr[i].dev_name, sizeof(dev_info_arr[i].dev_name), "/sys/bus/pci/devices/%04x:%02x:%02x.%x/config", domain, bus,dev, func);

next:
        dev_name += strlen(dev_name) + 1;
    }

    free(devs);
    *len = rc;
    return dev_info_arr;
}

dev_info* mdevices_info(int mask, int* len);

void mdevice_info_destroy(dev_info* dev_info, int len)
{
    (void)len;
    if (dev_info)
        free(dev_info);
}


mfile *mopen(const char *name)
{
    mfile *mf;
    off_t offset;
    unsigned domain = 0, bus = 0, dev = 0, func = 0;
    enum mtcr_access_method access;
    int force;
    char rbuf[] = "/sys/bus/pci/devices/XXXX:XX:XX.X/resource0";
    char cbuf[] = "/sys/bus/pci/devices/XXXX:XX:XX.X/config";
    char pdbuf[] = "/proc/bus/pci/XXXX:XX/XX.X";
    char pbuf[] = "/proc/bus/pci/XX/XX.X";
    char errbuf[4048]="";
    int err;
    int rc;

    mf = (mfile *)malloc(sizeof(mfile));
    if (!mf)
        return NULL;

    memset(mf, 0, sizeof(mfile));
    mf->dev_name = strdup(name);
    if (!mf->dev_name)
        goto open_failed;

    access = mtcr_parse_name(name, &force, &domain, &bus, &dev, &func);
    if (access == MTCR_ACCESS_ERROR)
        goto open_failed;

    if (force) {
        switch (access) {
        case MTCR_ACCESS_CONFIG:
            rc = mtcr_pciconf_open(mf, name);
            break;
        case MTCR_ACCESS_MEMORY:
            rc = mtcr_pcicr_open(mf, name, 0, 0);
            break;
        case MTCR_ACCESS_INBAND:
            rc = mtcr_inband_open(mf, name);
            break;
        default:
            goto open_failed;
        }

        if (0 == rc) {
            return mf;
        } else {
            goto open_failed;
        }
    }

    if (access == MTCR_ACCESS_CONFIG)
        goto access_config_forced;

    sprintf(rbuf, "/sys/bus/pci/devices/%4.4x:%2.2x:%2.2x.%1.1x/resource0",
        domain, bus, dev, func);

    rc = mtcr_pcicr_open(mf, rbuf, 0, 0);
    if (rc == 0) {
        return mf;
    } else if (rc == 1) {
        goto access_config_forced;
    }

    /* Following access methods need the resource BAR */
    offset = mtcr_sysfs_get_offset(domain, bus, dev, func);
    if (offset == -1 && !domain)
        offset = mtcr_procfs_get_offset(bus, dev, func);
    if (offset == -1)
        goto access_config;

    sprintf(pdbuf, "/proc/bus/pci/%4.4x:%2.2x/%2.2x.%1.1x",
        domain, bus, dev, func);
    rc = mtcr_pcicr_open(mf, pdbuf, offset, 1);
    if (rc == 0) {
        return mf;
    } else if (rc == 1) {
        goto access_config;
    }

    rc = mtcr_pcicr_open(mf, pdbuf, offset, 1);
    if (rc == 0) {
        return mf;
    } else if (rc == 1) {
        goto access_config;
    }

    if (!domain) {
        sprintf(pbuf, "/proc/bus/pci/%2.2x/%2.2x.%1.1x",
            bus, dev, func);
        rc = mtcr_pcicr_open(mf, pbuf, offset, 1);
        if (rc == 0) {
            return mf;
        } else if (rc == 1) {
            goto access_config;
        }
    }

#if CONFIG_USE_DEV_MEM
    /* Non-portable, but helps some systems */
    if (!mtcr_pcicr_open(mf, "/dev/mem", offset, 0))
        return mf;
#endif

access_config:
#if CONFIG_ENABLE_PCICONF && CONFIG_ENABLE_PCICONF
    strerror_r(errno, errbuf, sizeof errbuf);
    fprintf(stderr,
            "Warning: memory access to device %s failed: %s. Switching to PCI config access.\n",
            name, errbuf);
#endif

access_config_forced:
    // Cleanup the mfile struct from any previous garbage.
    memset(mf, 0, sizeof(mfile));

    sprintf(cbuf, "/sys/bus/pci/devices/%4.4x:%2.2x:%2.2x.%1.1x/config",
        domain, bus, dev, func);
    if (!mtcr_pciconf_open(mf, cbuf))
        return mf;

    sprintf(pdbuf, "/proc/bus/pci/%4.4x:%2.2x/%2.2x.%1.1x",
        domain, bus, dev, func);
    if (!mtcr_pciconf_open(mf, pdbuf))
        return mf;

    if (!domain) {
        sprintf(pbuf, "/proc/bus/pci/%2.2x/%2.2x.%1.1x",
            bus, dev, func);
        if (!mtcr_pciconf_open(mf, pdbuf))
            return mf;
    }

open_failed:
        err = errno;
        mclose(mf);
        errno = err;
        return NULL;
}


mfile *mopend(const char *name, int type)
{
    if (type != 1) {
        return NULL;
    }
    return mopen(name);
}

int mclose(mfile *mf)
{
    if (mf->mclose != NULL && mf->ctx != NULL) {
        mf->mclose(mf);
    }
    if (mf->dev_name) {
        free(mf->dev_name);
    }
    free(mf);
    return 0;
}

unsigned char mset_i2c_slave(mfile *mf, unsigned char new_i2c_slave)
{
    (void)mf;
    (void)new_i2c_slave; /* compiler warning */
    fprintf(stderr, "Warning: libmtcr: mset_i2c_slave() is not implemented and has no effect.\n");
    return 0;
}


int mget_mdevs_flags(mfile *mf, u_int32_t *devs_flags)
{
    (void)mf;
    *devs_flags = MDEVS_TAVOR_CR;
        return 0;
}

int maccess_reg_mad(mfile *mf, u_int8_t *data)
{
    if (mf->access_type != MTCR_ACCESS_INBAND) {
        errno = EINVAL;
        return -1;
    }

    return mf->maccess_reg(mf, data);
}

int mos_reg_access(mfile *mf, int reg_access, void *reg_data, u_int32_t cmd_type)
{
    (void)mf;
    (void)reg_data; /* compiler warning */
    (void)cmd_type; /* compiler warning */
    (void)reg_access; /* compiler warning */
    fprintf(stderr, "Warning: libmtcr: maccess_reg_mad() is not implemented and has no effect.\n");
    return -1;
}

static void mtcr_fix_endianness(u_int32_t *buf, int len) {
    int i;

    for (i = 0; i < (len/4); ++i) {
        buf[i] = __be32_to_cpu(buf[i]);
    }
}

int mread_buffer(mfile *mf, unsigned int offset, u_int8_t* data, int byte_len)
{
    int rc;
    rc = mread4_block(mf, offset, (u_int32_t*)data, byte_len);
    mtcr_fix_endianness((u_int32_t*)data, byte_len);
    return rc;

}

int mwrite_buffer(mfile *mf, unsigned int offset, u_int8_t* data, int byte_len)
{
    mtcr_fix_endianness((u_int32_t*)data, byte_len);
    return mwrite4_block(mf, offset, (u_int32_t*)data, byte_len);
}
