// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <errno.h>

#include "common/Throttle.h"
#include "common/dout.h"
#include "common/ceph_context.h"
#include "common/perf_counters.h"

#define dout_subsys ceph_subsys_throttle

#undef dout_prefix
#define dout_prefix *_dout << "throttle(" << name << " " << (void*)this << ") "

enum {
    l_throttle_first = 532430,
    l_throttle_val,
    l_throttle_max,
    l_throttle_get,
    l_throttle_get_sum,
    l_throttle_get_or_fail_fail,
    l_throttle_get_or_fail_success,
    l_throttle_take,
    l_throttle_take_sum,
    l_throttle_put,
    l_throttle_put_sum,
    l_throttle_wait,
    l_throttle_last,
};

Throttle::Throttle(CephContext *cct, const std::string& n, int64_t m, bool _use_perf)
    : cct(cct), name(n), logger(NULL),
      max(m),
      lock("Throttle::lock"),
      use_perf(_use_perf)
{
    assert(m >= 0);

    if (!use_perf)
        return;

    if (cct->_conf->throttler_perf_counter) {
        PerfCountersBuilder b(cct, string("throttle-") + name, l_throttle_first, l_throttle_last);
        b.add_u64_counter(l_throttle_val, "val", "Currently available throttle");
        b.add_u64_counter(l_throttle_max, "max", "Max value for throttle");
        b.add_u64_counter(l_throttle_get, "get", "Gets");
        b.add_u64_counter(l_throttle_get_sum, "get_sum", "Got data");
        b.add_u64_counter(l_throttle_get_or_fail_fail, "get_or_fail_fail", "Get blocked during get_or_fail");
        b.add_u64_counter(l_throttle_get_or_fail_success, "get_or_fail_success", "Successful get during get_or_fail");
        b.add_u64_counter(l_throttle_take, "take", "Takes");
        b.add_u64_counter(l_throttle_take_sum, "take_sum", "Taken data");
        b.add_u64_counter(l_throttle_put, "put", "Puts");
        b.add_u64_counter(l_throttle_put_sum, "put_sum", "Put data");
        b.add_time_avg(l_throttle_wait, "wait", "Waiting latency");

        logger = b.create_perf_counters();
        cct->get_perfcounters_collection()->add(logger);
        logger->set(l_throttle_max, max.read());
    }
}

Throttle::~Throttle()
{
    while (!cond.empty()) {
        Cond *cv = cond.front();
        delete cv;
        cond.pop_front();
    }

    if (!use_perf)
        return;

    if (logger) {
        cct->get_perfcounters_collection()->remove(logger);
        delete logger;
    }
}

//# 重新设置最大值
void Throttle::_reset_max(int64_t m)
{
    assert(lock.is_locked());
    if ((int64_t)max.read() == m)
        return;
    if (!cond.empty())
        cond.front()->SignalOne(); //# 唤醒一个等待
    if (logger)
        logger->set(l_throttle_max, m);
    max.set((size_t)m);
}
//# 增加count是否需要等待,并wait
bool Throttle::_wait(int64_t c)
{
    utime_t start;
    bool waited = false;
    if (_should_wait(c) || !cond.empty()) { // always wait behind other waiters.
        Cond *cv = new Cond;
        cond.push_back(cv);
        do {
            if (!waited) {
                ldout(cct, 2) << "_wait waiting..." << dendl;
                if (logger)
                    start = ceph_clock_now(cct);
            }
            waited = true;
            cv->Wait(lock); //# 等待有人put,并发送信号来唤醒
        } while (_should_wait(c) || cv != cond.front());

        if (waited) {
            ldout(cct, 3) << "_wait finished waiting" << dendl;
            if (logger) {
                utime_t dur = ceph_clock_now(cct) - start;
                logger->tinc(l_throttle_wait, dur);
            }
        }

        delete cv;
        cond.pop_front();

        // wake up the next guy
        if (!cond.empty())
            cond.front()->SignalOne();
    }
    return waited;
}
//# 重置max是否需要等待,并wait
bool Throttle::wait(int64_t m)
{
    if (0 == max.read() && 0 == m) {
        return false;
    }

    Mutex::Locker l(lock);
    if (m) {
        assert(m > 0);
        _reset_max(m);
    }
    ldout(cct, 10) << "wait" << dendl;
    return _wait(0);
}
//# count增加 c,直接加,不用等待
int64_t Throttle::take(int64_t c)
{
    if (0 == max.read()) {
        return 0;
    }
    assert(c >= 0);
    ldout(cct, 10) << "take " << c << dendl;
    {
        Mutex::Locker l(lock);
        count.add(c);
    }
    if (logger) {
        logger->inc(l_throttle_take);
        logger->inc(l_throttle_take_sum, c);
        logger->set(l_throttle_val, count.read());
    }
    return count.read();
}

//# count + c 并重新设置 max(m默认为0,不重置), 返回是否需要等待
bool Throttle::get(int64_t c, int64_t m)
{
    if (0 == max.read() && 0 == m) {
        return false;
    }

    assert(c >= 0);
    ldout(cct, 10) << "get " << c << " (" << count.read() << " -> " << (count.read() + c) << ")" << dendl;
    bool waited = false;
    {
        Mutex::Locker l(lock);
        if (m) {
            assert(m > 0);
            _reset_max(m);
        }
        waited = _wait(c);
        count.add(c);
    }
    if (logger) {
        logger->inc(l_throttle_get);
        logger->inc(l_throttle_get_sum, c);
        logger->set(l_throttle_val, count.read());
    }
    return waited;
}

/* Returns true if it successfully got the requested amount,
 * or false if it would block.
 */
 //# count + c , 如果需要等待,不加返回false,不需要等待就+c,并返回true
bool Throttle::get_or_fail(int64_t c)
{
    if (0 == max.read()) {
        return true;
    }

    assert (c >= 0);
    Mutex::Locker l(lock);
    if (_should_wait(c) || !cond.empty()) {
        ldout(cct, 10) << "get_or_fail " << c << " failed" << dendl;
        if (logger) {
            logger->inc(l_throttle_get_or_fail_fail);
        }
        return false;
    } else {
        ldout(cct, 10) << "get_or_fail " << c << " success (" << count.read() << " -> " << (count.read() + c) << ")" << dendl;
        count.add(c);
        if (logger) {
            logger->inc(l_throttle_get_or_fail_success);
            logger->inc(l_throttle_get);
            logger->inc(l_throttle_get_sum, c);
            logger->set(l_throttle_val, count.read());
        }
        return true;
    }
}

//# count 减去 c,返回 count的当前值
int64_t Throttle::put(int64_t c)
{
    if (0 == max.read()) {
        return 0;
    }

    assert(c >= 0);
    ldout(cct, 10) << "put " << c << " (" << count.read() << " -> " << (count.read()-c) << ")" << dendl;
    Mutex::Locker l(lock);
    if (c) {
        if (!cond.empty())
            cond.front()->SignalOne(); //# 唤醒一个等待
        assert(((int64_t)count.read()) >= c); //if count goes negative, we failed somewhere!
        count.sub(c);
        if (logger) {
            logger->inc(l_throttle_put);
            logger->inc(l_throttle_put_sum, c);
            logger->set(l_throttle_val, count.read());
        }
    }
    return count.read();
}

SimpleThrottle::SimpleThrottle(uint64_t max, bool ignore_enoent)
    : m_lock("SimpleThrottle"),
      m_max(max),
      m_current(0),
      m_ret(0),
      m_ignore_enoent(ignore_enoent)
{
}

SimpleThrottle::~SimpleThrottle()
{
    Mutex::Locker l(m_lock);
    assert(m_current == 0);
}

void SimpleThrottle::start_op()
{
    Mutex::Locker l(m_lock);
    while (m_max == m_current)
        m_cond.Wait(m_lock);
    ++m_current;
}

void SimpleThrottle::end_op(int r)
{
    Mutex::Locker l(m_lock);
    --m_current;
    if (r < 0 && !m_ret && !(r == -ENOENT && m_ignore_enoent))
        m_ret = r;
    m_cond.Signal();
}

bool SimpleThrottle::pending_error() const
{
    Mutex::Locker l(m_lock);
    return (m_ret < 0);
}

int SimpleThrottle::wait_for_ret()
{
    Mutex::Locker l(m_lock);
    while (m_current > 0)
        m_cond.Wait(m_lock);
    return m_ret;
}

void C_OrderedThrottle::finish(int r)
{
    m_ordered_throttle->finish_op(m_tid, r);
}

OrderedThrottle::OrderedThrottle(uint64_t max, bool ignore_enoent)
    : m_lock("OrderedThrottle::m_lock"), m_max(max), m_current(0), m_ret_val(0),
      m_ignore_enoent(ignore_enoent), m_next_tid(0), m_complete_tid(0)
{
}

C_OrderedThrottle *OrderedThrottle::start_op(Context *on_finish)
{
    assert(on_finish != NULL);

    Mutex::Locker locker(m_lock);
    uint64_t tid = m_next_tid++;
    m_tid_result[tid] = Result(on_finish);
    C_OrderedThrottle *ctx = new C_OrderedThrottle(this, tid);

    complete_pending_ops();
    while (m_max == m_current) {
        m_cond.Wait(m_lock);
        complete_pending_ops();
    }
    ++m_current;

    return ctx;
}

void OrderedThrottle::end_op(int r)
{
    Mutex::Locker locker(m_lock);
    assert(m_current > 0);

    if (r < 0 && m_ret_val == 0 && (r != -ENOENT || !m_ignore_enoent)) {
        m_ret_val = r;
    }
    --m_current;
    m_cond.Signal();
}

void OrderedThrottle::finish_op(uint64_t tid, int r)
{
    Mutex::Locker locker(m_lock);

    TidResult::iterator it = m_tid_result.find(tid);
    assert(it != m_tid_result.end());

    it->second.finished = true;
    it->second.ret_val = r;
    m_cond.Signal();
}

bool OrderedThrottle::pending_error() const
{
    Mutex::Locker locker(m_lock);
    return (m_ret_val < 0);
}

int OrderedThrottle::wait_for_ret()
{
    Mutex::Locker locker(m_lock);
    complete_pending_ops();

    while (m_current > 0) {
        m_cond.Wait(m_lock);
        complete_pending_ops();
    }
    return m_ret_val;
}

void OrderedThrottle::complete_pending_ops()
{
    assert(m_lock.is_locked());

    while (true) {
        TidResult::iterator it = m_tid_result.begin();
        if (it == m_tid_result.end() || it->first != m_complete_tid ||
            !it->second.finished) {
            break;
        }

        Result result = it->second;
        m_tid_result.erase(it);

        m_lock.Unlock();
        result.on_finish->complete(result.ret_val);
        m_lock.Lock();

        ++m_complete_tid;
    }
}
