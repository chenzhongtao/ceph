/*
 * Local Variables:
 * compile-command: "cd ../.. ; make ceph_test_filestore &&
 *    ./ceph_test_filestore \
 *        --gtest_filter=*.* --log-to-stderr=true --debug-filestore=20  -c /etc/ceph/ceph2.conf
 *  "

   ./test_filestore --debug-filestore=20  -c /etc/ceph/ceph2.conf --log-to-stderr=false
 
 * End:
 */


#include "common/ceph_argparse.h"
#include "global/global_init.h"
#include "os/FileStore.h"

#include "test.h"

class TestFileStore
{
public:
    static void create_backend(FileStore &fs, long f_type)
    {
        fs.create_backend(f_type);
    }
};

void test_mkfs(void)
{

    {
        int ret;
        map<string,string> pm;
        FileStore fs("/tmp/osd", "/tmp/journal/journal");
        ret = fs.mkfs();
		//fs.collect_metadata(&pm);
        //cout << pm["filestore_backend"] << " xfs" << std::endl;
        if (ret != 0)
            cout << "test mkfs error" << std::endl;
    }

}

void test_mkfs_block_device(void)
{

    {
        int ret;
        map<string,string> pm;
        FileStore fs("/tmp/osd2", "/dev/sdc2");
        ret = fs.mkfs();
        if (ret != 0)
            cout << "test mkfs blockdevice error" << std::endl;
    }

}

void test_mount_umount(void)
{

    {
        int ret;
        map<string,string> pm;
        FileStore fs("/tmp/osd", "/tmp/journal/journal");
        ret = fs.mount();
        if (ret != 0)
            cout << "test mount error" << std::endl;
		ret = fs.umount();
        if (ret != 0)
            cout << "test umount error" << std::endl;
    }

}

void test_dump_journal(void)
{

    {
        int ret;
        map<string,string> pm;
        FileStore fs("/tmp/osd", "/tmp/journal/journal");
        ret = fs.dump_journal(cout);
        if (ret != 0)
            cout << "test dump journal error" << std::endl;
    }

}
/*
{
    "header": {
        "flags": 1,
        "fsid": "ad37d285-6643-4d0e-8729-a3a24c7da597",
        "block_size": 4096,
        "alignment": 4096,
        "max_size": 104857600,
        "start": 4096,
        "committed_up_to": 0,
        "start_seq": 0
    },
    "entries": []
}

{
    "header": {
        "flags": 1,
        "fsid": "ad37d285-6643-4d0e-8729-a3a24c7da597",
        "block_size": 4096,
        "alignment": 4096,
        "max_size": 104857600,
        "start": 12288,  //# 4096µÄÕûÊý±¶
        "committed_up_to": 3,
        "start_seq": 4
    },
    "entries": []
}




*/



int main(int argc, char **argv)
{
    vector<const char*> args;
    argv_to_vec(argc, (const char **)argv, args);

    global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT, CODE_ENVIRONMENT_UTILITY, 0);
    common_init_finish(g_ceph_context);
    g_ceph_context->_conf->set_val("osd_journal_size", "100");
    g_ceph_context->_conf->apply_changes(NULL);
	
    MAKE_IF(test_mkfs);
	MAKE_IF(test_mkfs_block_device);
	MAKE_IF(test_mount_umount);
	MAKE_IF(test_dump_journal);


}


