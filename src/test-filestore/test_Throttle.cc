
#include <stdio.h>
#include <signal.h>
#include "common/Mutex.h"
#include "common/Thread.h"
#include "common/Throttle.h"
#include "common/ceph_argparse.h"
#include "global/global_init.h"

#include "test.h"


class Thread_get : public Thread
{
public:
    Throttle &throttle;
    int64_t count;
    bool waited;

    Thread_get(Throttle& _throttle, int64_t _count) :
        throttle(_throttle),
        count(_count),
        waited(false)
    {
    }

    virtual void *entry()
    {
        waited = throttle.get(count);
        throttle.put(count);
        return NULL;
    }
};


void test_Throttle()
{

    int64_t throttle_max = 10;
    Throttle throttle(g_ceph_context, "throttle", throttle_max);
    ASSERT_EQ(throttle.get_max(), throttle_max);
    ASSERT_EQ(throttle.get_current(), 0);
}

void test_take()
{
    int64_t throttle_max = 10;
    Throttle throttle(g_ceph_context, "throttle", throttle_max);
    ASSERT_EQ(throttle.take(throttle_max), throttle_max);
    ASSERT_EQ(throttle.take(throttle_max), throttle_max * 2);
}

void test_get()
{
    int64_t throttle_max = 10;
    Throttle throttle(g_ceph_context, "throttle");

    // test increasing max from 0 to throttle_max
    {
        ASSERT_FALSE(throttle.get(throttle_max, throttle_max));
        ASSERT_EQ(throttle.get_max(), throttle_max);
        ASSERT_EQ(throttle.put(throttle_max), 0);
    }

    ASSERT_FALSE(throttle.get(5));
    ASSERT_EQ(throttle.put(5), 0);

    ASSERT_FALSE(throttle.get(throttle_max));
    ASSERT_FALSE(throttle.get_or_fail(1));
    ASSERT_FALSE(throttle.get(1, throttle_max + 1));
    ASSERT_EQ(throttle.put(throttle_max + 1), 0);
    ASSERT_FALSE(throttle.get(0, throttle_max));
    ASSERT_FALSE(throttle.get(throttle_max));
    ASSERT_FALSE(throttle.get_or_fail(1));
    ASSERT_EQ(throttle.put(throttle_max), 0);

    useconds_t delay = 1;

    bool waited;

    do {
        cout << "Trying (1) with delay " << delay << "us\n";

        ASSERT_FALSE(throttle.get(throttle_max));
        ASSERT_FALSE(throttle.get_or_fail(throttle_max));

        Thread_get t(throttle, 7); //# 创建一个线程一直在尝试 count + 7
        t.create();
        usleep(delay);
        ASSERT_EQ(throttle.put(throttle_max), 0);
        t.join();

        if (!(waited = t.waited))
            delay *= 2;
    } while(!waited);

    do {
        cout << "Trying (2) with delay " << delay << "us\n";

        ASSERT_FALSE(throttle.get(throttle_max / 2));
        ASSERT_FALSE(throttle.get_or_fail(throttle_max));

        Thread_get t(throttle, throttle_max);
        t.create();
        usleep(delay);

        Thread_get u(throttle, 1);
        u.create();
        usleep(delay);

        throttle.put(throttle_max / 2);

        t.join();
        u.join();

        if (!(waited = t.waited && u.waited))
            delay *= 2;
    } while(!waited);

}

void test_get_or_fail()
{
    {
        Throttle throttle(g_ceph_context, "throttle");

        ASSERT_TRUE(throttle.get_or_fail(5));
        ASSERT_TRUE(throttle.get_or_fail(5));
    }

    {
        int64_t throttle_max = 10;
        Throttle throttle(g_ceph_context, "throttle", throttle_max);

        ASSERT_TRUE(throttle.get_or_fail(throttle_max));
        ASSERT_EQ(throttle.put(throttle_max), 0);

        ASSERT_TRUE(throttle.get_or_fail(throttle_max * 2));
        ASSERT_FALSE(throttle.get_or_fail(1));
        ASSERT_FALSE(throttle.get_or_fail(throttle_max * 2));
        ASSERT_EQ(throttle.put(throttle_max * 2), 0);

        ASSERT_TRUE(throttle.get_or_fail(throttle_max));
        ASSERT_FALSE(throttle.get_or_fail(1));
        ASSERT_EQ(throttle.put(throttle_max), 0);
    }
}

void test_wait()
{
    int64_t throttle_max = 10;
    Throttle throttle(g_ceph_context, "throttle");

    // test increasing max from 0 to throttle_max
    {
        ASSERT_FALSE(throttle.wait(throttle_max));
        ASSERT_EQ(throttle.get_max(), throttle_max);
    }

    useconds_t delay = 1;

    bool waited;

    do {
        cout << "Trying (3) with delay " << delay << "us\n";

        ASSERT_FALSE(throttle.get(throttle_max / 2));
        ASSERT_FALSE(throttle.get_or_fail(throttle_max));

        Thread_get t(throttle, throttle_max);
        t.create();
        usleep(delay);

        //
        // Throttle::_reset_max(int64_t m) used to contain a test
        // that blocked the following statement, only if
        // the argument was greater than throttle_max.
        // Although a value lower than throttle_max would cover
        // the same code in _reset_max, the throttle_max * 100
        // value is left here to demonstrate that the problem
        // has been solved.
        //
        throttle.wait(throttle_max * 100);
        usleep(delay);
        t.join();
        ASSERT_EQ(throttle.get_current(), throttle_max / 2);

        if (!(waited = t.waited)) {
            delay *= 2;
            // undo the changes we made
            throttle.put(throttle_max / 2);
            throttle.wait(throttle_max);
        }
    } while(!waited);
}

void test_destructor()
{
    Thread_get *t;
    {
        int64_t throttle_max = 10;
        Throttle *throttle = new Throttle(g_ceph_context, "throttle", throttle_max);

        ASSERT_FALSE(throttle->get(5));

        t = new Thread_get(*throttle, 7);
        t->create();
        bool blocked;
        useconds_t delay = 1;
        do {
            usleep(delay);
            if (throttle->get_or_fail(1)) {
                throttle->put(1);
                blocked = false;
            } else {
                blocked = true;
            }
            delay *= 2;
        } while(!blocked);
        delete throttle;
    }

    {
        //
        // The thread is left hanging, otherwise it will abort().
        // Deleting the Throttle on which it is waiting creates a
        // inconsistency that will be detected: the Throttle object that
        // it references no longer exists.
        //
        pthread_t id = t->get_thread_id();
        ASSERT_EQ(pthread_kill(id, 0), 0);
        delete t;
        ASSERT_EQ(pthread_kill(id, 0), 0);
    }
}

int main(int argc, char **argv)
{
    vector<const char*> args;
    argv_to_vec(argc, (const char **)argv, args);

    global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT, CODE_ENVIRONMENT_UTILITY, 0);
    common_init_finish(g_ceph_context);\
	MAKE_IF(test_Throttle);
	MAKE_IF(test_take);
	MAKE_IF(test_get);
	MAKE_IF(test_get_or_fail);
	MAKE_IF(test_wait);
	MAKE_IF(test_destructor);

}


/*
   ./test_Throttle   -c /etc/ceph/ceph2.conf --log-to-stderr=false test_Create
   gdb --args ./test_Throttle -c /etc/ceph/ceph2.conf --log-to-stderr=false test_Create

 * End:
 */

