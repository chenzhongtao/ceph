// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "common/Mutex.h"
#include "common/HeartbeatMap.h"
#include "common/ceph_context.h"
#include "common/config.h"
#include "common/ceph_argparse.h"
#include "global/global_context.h"
#include "global/global_init.h"

#include "test.h"


using namespace ceph;

void test_Healthy()
	{
  HeartbeatMap hm(g_ceph_context);
  heartbeat_handle_d *h = hm.add_worker("one");

  hm.reset_timeout(h, 9, 18);
  bool healthy = hm.is_healthy();
  ASSERT_EQ(healthy, true);

  hm.remove_worker(h);
}

void test_Unhealth()
	{
  HeartbeatMap hm(g_ceph_context);
  heartbeat_handle_d *h = hm.add_worker("one");

  hm.reset_timeout(h, 1, 3);
  sleep(2); //# sleep 4 ½«»á assert
  bool healthy = hm.is_healthy();
  ASSERT_EQ(healthy, false);

  hm.remove_worker(h);
}


int main(int argc, char **argv)
{

  vector<const char*> args;
  argv_to_vec(argc, (const char **)argv, args);

  global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT, CODE_ENVIRONMENT_UTILITY, 0);
  common_init_finish(g_ceph_context);

  test_Healthy();
  test_Unhealth();
  
}
/*
   ./test_heartbeat_map   -c /etc/ceph/ceph2.conf --log-to-stderr=false
   gdb --args ./test_heartbeat_map -c /etc/ceph/ceph2.conf --log-to-stderr=false

 * End:
 */