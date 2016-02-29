//compile:
//gcc -O0 -Wall -g -o rbd_client rbd_client.c  -lrbd -lrados

#include <unistd.h> //getopt
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rados/librados.h>
#include <rbd/librbd.h>
#include <time.h>
#include <sys/time.h>

double diff_time(struct timeval tv_begin, struct timeval tv_end)
{
    double end_d = tv_end.tv_sec + tv_end.tv_usec / 1000000.0;
    double begin_d = tv_begin.tv_sec + tv_begin.tv_usec / 1000000.0;
    return end_d - begin_d;
}

static void usage(void)
{
    printf("optional arguments:\n"
           "-h,  show this help message and exit\n"
           "-p POOL,  The pool name of ceph rbd\n"
           "-i IMAGE,  The image name of ceph rbd\n"
           "-b *B,*K,*M,  block size.\n"
           "-o *B,*K,*M,  offset size.\n"
           "-f *B,*K,*M,*G,  ,*G filesize size.\n"
           "-r read mode\n");
}

int main(int argc, char** argv)
{
    rados_t cluster;
    rados_ioctx_t io;
    rbd_image_t image;
    char cluster_name[] = "ceph";
    char user_name[] = "client.admin";
    uint64_t flags = 0;
    int i=0;
    char *buf;
    char c;
    char unit = '\0';
    uint64_t size_max;
    uint64_t rt_num;

    struct timeval tv_begin, tv_end;
    double diff;


    char *poolname = "rep-pool";
    char *imagename = "volume-1";
    char *conf = "/etc/ceph/ceph2.conf";
    uint64_t block = 4 * 1024 * 1024;  // 4M
    uint64_t   offset = 0;
    uint64_t   filesize =16 * 1024 * 1024; //16M
    int r = 0;

    while (-1 != (c = getopt(argc, argv,
                             "p:"
                             "i:"
                             "b:"
                             "o:"
                             "f:"
                             "c:"
                             "r"
                             "h"
                            )))
    {
        switch (c)
        {
        case 'p':
            poolname = strdup(optarg);
            break;
        case 'i':
            imagename = strdup(optarg);
            break;
        case 'c':
            conf = strdup(optarg);
            break;
        case 'b':
            buf = strdup(optarg);
            unit = buf[strlen(buf)-1];
            if (unit == 'k' || unit == 'm' ||
                    unit == 'K' || unit == 'M' ||
                    unit == 'g' || unit == 'G' ||
                    unit == 'b' || unit == 'B' )
            {
                buf[strlen(buf)-1] = '\0';
                size_max = atoi(buf);
                if (unit == 'k' || unit == 'K')
                    size_max *= 1024;
                if (unit == 'm' || unit == 'M')
                    size_max *= 1024 * 1024;
                if (unit == 'g' || unit == 'G')
                    size_max *= 1024 * 1024 * 1024;
                block = size_max;
            }
            break;
        case 'o':
            buf = strdup(optarg);
            unit = buf[strlen(buf)-1];
            if (unit == 'k' || unit == 'm' ||
                    unit == 'K' || unit == 'M' ||
                    unit == 'g' || unit == 'G' ||
                    unit == 'b' || unit == 'B' )
            {
                buf[strlen(buf)-1] = '\0';
                size_max = atoi(buf);
                if (unit == 'k' || unit == 'K')
                    size_max *= 1024;
                if (unit == 'm' || unit == 'M')
                    size_max *= 1024 * 1024;
                if (unit == 'g' || unit == 'G')
                    size_max *= 1024 * 1024 * 1024;
                offset = size_max;
            }
            break;
        case 'f':
            buf = strdup(optarg);
            unit = buf[strlen(buf)-1];
            if (unit == 'k' || unit == 'm' ||
                    unit == 'K' || unit == 'M' ||
                    unit == 'g' || unit == 'G' ||
                    unit == 'b' || unit == 'B' )
            {
                buf[strlen(buf)-1] = '\0';
                size_max = atoi(buf);
                if (unit == 'k' || unit == 'K')
                    size_max *= 1024;
                if (unit == 'm' || unit == 'M')
                    size_max *= 1024 * 1024;
                if (unit == 'g' || unit == 'G')
                    size_max *= 1024 * 1024 * 1024;
                filesize = size_max;
            }
            break;
        case 'r':
            r = 1;
            break;
        case 'h':
            usage();
            exit(0);
            break;
        }
    }


    rt_num = rados_create2(&cluster, cluster_name, user_name, flags);
    if(rt_num<0)
    {
        printf("%s: Couldn¡®t create the cluster handle! %s\n", argv[0], strerror(-rt_num));
        return -1;
    }


//Read a ceph configuration file to configure the cluster handle
    rt_num = rados_conf_read_file(cluster, conf);
    if(rt_num<0)
    {
        printf("%s:can¡®t read config file: %s\n", argv[0], strerror(-rt_num));
        return -1;
    }

    rt_num = rados_connect(cluster);
    if(rt_num<0)
    {
        printf("%s:cannot connect to cluster:%s\n", argv[0], strerror(-rt_num));
        return -1;
    }

    rt_num = rados_ioctx_create(cluster, poolname, &io);
    if(rt_num<0)
    {
        printf("%s:cannot open rados pool %s:%s", argv[0], poolname, strerror(-rt_num));
        rados_shutdown(cluster);
        return -1;
    }

    rt_num = rbd_open( io, imagename, &image, NULL);

    char* buffer=(char*)malloc(block);
    uint64_t ofs = offset;
    ssize_t  ok_size = 0;

    gettimeofday(&tv_begin, NULL);
    if (r == 0)
    {
        for(i=0; i<filesize/block; i++)
        {

            ok_size = rbd_write(image, ofs, block , buffer);
            ofs = block + ofs;
            if(ok_size != block)
            {
                printf("rbd_write fail!!\n");
            }
        }
    }
    else
    {
        for(i=0; i<filesize/block; i++)
        {
            ok_size = rbd_read(image, ofs, block, buffer);
            ofs = block + ofs;
            if(ok_size != block)
            {
                printf("rbd_read fail!!\n");
            }
        }
    }
    gettimeofday(&tv_end, NULL);
    diff = diff_time(tv_begin, tv_end);
    printf("time: %f\n", diff);
    printf("speed = %fKB/s \n", filesize/diff/1024);

    rbd_close(image);
    rados_ioctx_destroy(io);
    rados_shutdown(cluster);
    return 0;
}
