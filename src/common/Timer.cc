// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "Cond.h"
#include "Mutex.h"
#include "Thread.h"
#include "Timer.h"

#include "common/config.h"
#include "include/Context.h"

#define dout_subsys ceph_subsys_timer
#undef dout_prefix
#define dout_prefix *_dout << "timer(" << this << ")."

#include <sstream>
#include <signal.h>
#include <sys/time.h>
#include <math.h>


class SafeTimerThread : public Thread
{
    SafeTimer *parent;
public:
    SafeTimerThread(SafeTimer *s) : parent(s) {}
    void *entry()
    {
        parent->timer_thread();
        return NULL;
    }
};



typedef std::multimap < utime_t, Context *> scheduled_map_t;
typedef std::map < Context*, scheduled_map_t::iterator > event_lookup_map_t;

SafeTimer::SafeTimer(CephContext *cct_, Mutex &l, bool safe_callbacks)
    : cct(cct_), lock(l),
      safe_callbacks(safe_callbacks),
      thread(NULL),
      stopping(false)
{
}

SafeTimer::~SafeTimer()
{
    assert(thread == NULL);
}

//# SafeTimer初始化,启动一个线程
void SafeTimer::init()
{
    ldout(cct,10) << "init" << dendl;
    thread = new SafeTimerThread(this);
    thread->create();
}
//# 退出线程
void SafeTimer::shutdown()
{
    ldout(cct,10) << "shutdown" << dendl;
    if (thread) {
        assert(lock.is_locked());
        cancel_all_events();
        stopping = true;
        cond.Signal();//# 唤醒线程
        lock.Unlock();
        thread->join(); //# 等待线程退出
        lock.Lock();
        delete thread;
        thread = NULL;
    }
}

//# timer线程主函数
void SafeTimer::timer_thread()
{
    lock.Lock();
    ldout(cct,10) << "timer_thread starting" << dendl;
    while (!stopping) {
        utime_t now = ceph_clock_now(cct);

        while (!schedule.empty()) {
            scheduled_map_t::iterator p = schedule.begin();

            // is the future now?
            if (p->first > now)//# 还没到时间
                break;

            Context *callback = p->second;
            events.erase(callback);
            schedule.erase(p);
            ldout(cct,10) << "timer_thread executing " << callback << dendl;

            if (!safe_callbacks)//# 不安全的回调,不用加锁
                lock.Unlock();
            callback->complete(0);
            if (!safe_callbacks)
                lock.Lock();
        }

        // recheck stopping if we dropped the lock
        if (!safe_callbacks && stopping)
            break;

        ldout(cct,20) << "timer_thread going to sleep" << dendl;
        if (schedule.empty())
            cond.Wait(lock);//# 如果时间表为空,一直等待
        else
            cond.WaitUntil(lock, schedule.begin()->first);//# 等待到第一个事件的触发时间
        ldout(cct,20) << "timer_thread awake" << dendl;
    }
    ldout(cct,10) << "timer_thread exiting" << dendl;
    lock.Unlock();
}

//# 添加一个多久之后触发的事件
void SafeTimer::add_event_after(double seconds, Context *callback)
{
    assert(lock.is_locked());

    utime_t when = ceph_clock_now(cct);
    when += seconds;
    add_event_at(when, callback);
}
//# 添加一个在某个时间触发的事件
void SafeTimer::add_event_at(utime_t when, Context *callback)
{
    assert(lock.is_locked());
    ldout(cct,10) << "add_event_at " << when << " -> " << callback << dendl;

    scheduled_map_t::value_type s_val(when, callback); //#  typedef pair value_type 这里其实就是pair
    scheduled_map_t::iterator i = schedule.insert(s_val);  //# multimap insert 的返回值: iterator

    event_lookup_map_t::value_type e_val(callback, i);  
    pair < event_lookup_map_t::iterator, bool > rval(events.insert(e_val)); //# map insert 的返回值: pair<iterator,bool>

    /* If you hit this, you tried to insert the same Context* twice. */
    assert(rval.second);

    /* If the event we have just inserted comes before everything else, we need to
     * adjust our timeout. */
    if (i == schedule.begin())//# 表示之前的事件列表为空,或者时间比之前的事件都早,线程在wait,直接唤醒
        cond.Signal();

}

//# 根据callback取消一个事件
bool SafeTimer::cancel_event(Context *callback)
{
    assert(lock.is_locked());

    std::map<Context*, std::multimap<utime_t, Context*>::iterator>::iterator p = events.find(callback);
    if (p == events.end()) {
        ldout(cct,10) << "cancel_event " << callback << " not found" << dendl;
        return false;
    }

    ldout(cct,10) << "cancel_event " << p->second->first << " -> " << callback << dendl;
    delete p->first;

    schedule.erase(p->second);
    events.erase(p);
    return true;
}
//# 取消所有事件
void SafeTimer::cancel_all_events()
{
    ldout(cct,10) << "cancel_all_events" << dendl;
    assert(lock.is_locked());

    while (!events.empty()) {
        std::map<Context*, std::multimap<utime_t, Context*>::iterator>::iterator p = events.begin();
        ldout(cct,10) << " cancelled " << p->second->first << " -> " << p->first << dendl;
        delete p->first;
        schedule.erase(p->second);
        events.erase(p);
    }
}
//# 打印所有事件
void SafeTimer::dump(const char *caller) const
{
    if (!caller)
        caller = "";
    ldout(cct,10) << "dump " << caller << dendl;

    for (scheduled_map_t::const_iterator s = schedule.begin();
         s != schedule.end();
         ++s)
        ldout(cct,10) << " " << s->first << "->" << s->second << dendl;
}
