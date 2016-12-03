// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
#include <stdlib.h>
#include <limits.h>

#include "common/ceph_argparse.h"
#include "common/common_init.h"
#include "global/global_init.h"
#include "common/config.h"
#include "common/Finisher.h"
#include "os/FileJournal.h"
#include "include/Context.h"
#include "common/Mutex.h"
#include "common/safe_io.h"

#include "test.h"

Finisher *finisher;
Cond sync_cond;
char path[200];
uuid_d fsid;
struct test_info {
    bool directio, aio, faio;
    const char *description;
} subtests[3] = {
    { false, false, false, "DIRECTIO OFF  AIO OFF" },
    { true, false, false, "DIRECTIO ON  AIO OFF" },
    { true, true, true, "DIRECTIO ON  AIO ON"}
};

// ----
Cond cond;
Mutex wait_lock("lock");
bool done;

void wait()
{
    wait_lock.Lock();
    while (!done)
        cond.Wait(wait_lock);
    wait_lock.Unlock();
}

// ----
class C_Sync
{
public:
    Cond cond;
    Mutex lock;
    bool done;
    C_SafeCond *c;

    C_Sync()
        : lock("C_Sync::lock"), done(false)
    {
        c = new C_SafeCond(&lock, &cond, &done);
    }
    ~C_Sync()
    {
        lock.Lock();
        //cout << "wait" << std::endl;
        while (!done)
            cond.Wait(lock);
        //cout << "waited" << std::endl;
        lock.Unlock();
    }
};

unsigned size_mb = 200;
//Gtest argument prefix
const char GTEST_PRFIX[] = "--gtest_";





void test_Create_and_Write()
{
    g_ceph_context->_conf->set_val("journal_ignore_corruption", "false");
    g_ceph_context->_conf->set_val("journal_write_header_frequency", "0");
    g_ceph_context->_conf->apply_changes(NULL);

    for (unsigned i = 0 ; i < 1; ++i) {
        cout << subtests[i].description << std::endl;;
        fsid.parse((char *)"eba579d1-94c1-4cc2-a44f-68653ef77c2d");
        FileJournal j(fsid, finisher, &sync_cond, path, subtests[i].directio,
                      subtests[i].aio, subtests[i].faio);
        ASSERT_EQ(0, j.create());
        j.make_writeable();

        bufferlist bl;
        bl.append("small");
        j.submit_entry(1, bl, 0, new C_SafeCond(&wait_lock, &cond, &done));
        wait();

        j.close();
    }
}

void test_Write()
{
    g_ceph_context->_conf->set_val("journal_ignore_corruption", "false");
    g_ceph_context->_conf->set_val("journal_write_header_frequency", "0");
    g_ceph_context->_conf->apply_changes(NULL);

    for (unsigned i = 0 ; i < 1; ++i) {
        cout << subtests[i].description << std::endl;;
        fsid.parse((char *)"eba579d1-94c1-4cc2-a44f-68653ef77c2d");
        FileJournal j(fsid, finisher, &sync_cond, path, subtests[i].directio,
                      subtests[i].aio, subtests[i].faio);
        ASSERT_EQ(0, j.open(0));
        j.make_writeable();

        bufferlist bl;
        bl.append("small");
        j.submit_entry(1, bl, 0, new C_SafeCond(&wait_lock, &cond, &done));
        wait();

        j.close();
    }
}




/*
OPTION(journal_max_corrupt_search, OPT_U64, 10<<20)
OPTION(journal_block_align, OPT_BOOL, true)
OPTION(journal_write_header_frequency, OPT_U64, 0)
OPTION(journal_max_write_bytes, OPT_INT, 10 << 20)
OPTION(journal_max_write_entries, OPT_INT, 100)
OPTION(journal_queue_max_ops, OPT_INT, 300)
OPTION(journal_queue_max_bytes, OPT_INT, 32 << 20)
OPTION(journal_align_min_size, OPT_INT, 64 << 10)  // align data payloads >= this.
OPTION(journal_replay_from, OPT_INT, 0)
OPTION(journal_zero_on_create, OPT_BOOL, false)
OPTION(journal_ignore_corruption, OPT_BOOL, false) // assume journal is not corrupt
OPTION(journal_discard, OPT_BOOL, false) //using ssd disk as journal, whether support discard nouse journal-data.
*/


int main(int argc, char **argv)
{
    vector<const char*> args;
    argv_to_vec(argc, (const char **)argv, args);

    global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT, CODE_ENVIRONMENT_UTILITY, 0);
    common_init_finish(g_ceph_context);

    char mb[10];
    sprintf(mb, "%u", size_mb);
    g_ceph_context->_conf->set_val("osd_journal_size", mb);
    g_ceph_context->_conf->apply_changes(NULL);

    finisher = new Finisher(g_ceph_context);

    path[0] = '\0';
	/*
    if (!args.empty()) {
        for ( unsigned int i = 0; i < args.size(); ++i) {
            if (strncmp(args[i], GTEST_PRFIX, sizeof(GTEST_PRFIX) - 1)) {
                //Non gtest argument, set to path.
                size_t copy_len = std::min(sizeof(path) - 1, strlen(args[i]));
                strncpy(path, args[i], copy_len);
                path[copy_len] = '\0';
                break;
            }
        }
    }
    */

    snprintf(path, sizeof(path), "/var/tmp/ceph_test_filejournal");

    cout << "path " << path << std::endl;


    finisher->start();


    MAKE_IF(test_Create_and_Write);
    MAKE_IF(test_Write);

    finisher->stop();

    return 0;
}
/*
   ./test_journal2   -c /etc/ceph/ceph2.conf --log-to-stderr=false test_Create_and_Write
   gdb --args ./test_journal2 -c /etc/ceph/ceph2.conf --log-to-stderr=false test_Create_and_Write

 * End:
 */


