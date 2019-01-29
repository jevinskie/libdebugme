/*
 * Copyright 2016-2017 Yury Gribov
 * 
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#include <debugme.h>
#include "common.h"
#include "gdb.h"

#include <stdlib.h>
#include <stdio.h>
#define __USE_GNU
#include <signal.h>
#include <string.h>

#include <unistd.h>

#include <assert.h>
#include <dlfcn.h>

unsigned dbg_flags;
const char *dbg_opts;
int init_done;
int debug;
int disabled;
int quiet;
static int bad_signals[] = { SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGBUS };

static typeof(signal) *signal_p;
static typeof(sigaction) *sigaction_p;
static typeof(sigprocmask) *sigprocmask_p;
static typeof(sigemptyset) *sigemptyset_p;
static typeof(sigaddset) *sigaddset_p;
static typeof(sigfillset) *sigfillset_p;

// Interface with debugger
EXPORT volatile int __debugme_go;

__attribute__((used))
static void sighandler(int sig) {
  sig = sig;
  kill(getpid(), SIGSTOP);
  // debugme_debug(dbg_flags, dbg_opts);
  // exit(1);
}

__attribute__((used))
static void sighandler_turbo(int sig, siginfo_t *info, void *context) {
  (void)sig;
  (void)info;
  (void)context;
  fprintf(stderr, "connect by running:\ngdb --pid=%d\n", getpid());
  kill(getpid(), SIGSTOP);
  // debugme_debug(dbg_flags, dbg_opts);
  // exit(1);
}

INIT static void debugme_init_fptrs(void) {
  // fprintf(stderr, "%s: start\n", __func__);
  signal_p = dlsym(RTLD_NEXT, "signal");
  assert(signal_p);
  // fprintf(stderr, "%s: signal: %p signal_p: %p\n", __func__, signal, signal_p);
  sigaction_p = dlsym(RTLD_NEXT, "sigaction");
  assert(sigaction_p);
  sigprocmask_p = dlsym(RTLD_NEXT, "sigprocmask");
  assert(sigprocmask_p);
  sigemptyset_p = dlsym(RTLD_NEXT, "sigemptyset");
  assert(sigemptyset_p);
  sigaddset_p = dlsym(RTLD_NEXT, "sigaddset");
  assert(sigaddset_p);
  sigfillset_p = dlsym(RTLD_NEXT, "sigfillset");
  assert(sigfillset_p);
  // fprintf(stderr, "%s: end\n", __func__);
}

int called_by_debugme;

__attribute__((noinline, used))
static void print_caller(void) {
  fprintf(stderr, "%s: caller: %p\n", __func__, __builtin_return_address(0));
}

EXPORT sighandler_t signal(int signum, sighandler_t handler) {
  // fprintf(stderr, "debugme %s(%d, %p)\n", __func__, signum, handler);
  // fprintf(stderr, "debugme %s called_by_debugme: %d\n", __func__, called_by_debugme);
  // print_caller();
  return signal_p(signum, handler);
}

EXPORT int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
  // fprintf(stderr, "debugme %s(%d, %p, %p)\n", __func__, signum, act, oldact);
  if (SIGSEGV == signum) {
    // fprintf(stderr, "debugme %s: skipping for SIGSEGV\n", __func__);
    return 0;
  }
  return sigaction_p(signum, act, oldact);
}

EXPORT int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
  // fprintf(stderr, "debugme %s(%d, %p, %p)\n", __func__, how, set, oldset);
  return sigprocmask_p(how, set, oldset);
}

EXPORT int sigemptyset(sigset_t *set) {
  // fprintf(stderr, "debugme %s(%p)\n", __func__, set);
  return sigemptyset_p(set);
}

EXPORT int sigaddset(sigset_t *set, int signum) {
  // fprintf(stderr, "debugme %s(%p, %d)\n", __func__, set, signum);
  return sigaddset_p(set, signum);
}

EXPORT int sigfillset(sigset_t *set) {
  // fprintf(stderr, "debugme %s(%p,)\n", __func__, set);
  return sigfillset_p(set);
}

// TODO: optionally preserve existing handlers
// TODO: init if not yet (here and in debugme_debug)
EXPORT int debugme_install_sighandlers(unsigned dbg_flags_, const char *dbg_opts_) {
  if(disabled)
    return 0;

  dbg_flags = dbg_flags_;
  dbg_opts = dbg_opts_;

  size_t i;
  for(i = 0; i < ARRAY_SIZE(bad_signals); ++i) {
    int sig = bad_signals[i];
    const char *signame = sys_siglist[sig];
    if(debug) {
      fprintf(stderr, "debugme: setting signal handler for signal %d (%s)\n", sig, signame);
    }
    // sighandler_t sig_ret;
    int sig_ret;
    called_by_debugme = 1;
    // sig_ret = signal_p(sig, sighandler);
    struct sigaction sa = {NULL};
    sa.sa_sigaction = sighandler_turbo;
    sa.sa_flags = SA_SIGINFO;
    sig_ret = sigaction_p(sig, &sa, NULL);
    called_by_debugme = 0;
    // if(SIG_ERR == sig_ret) {
    if (!sig_ret) {
      fprintf(stderr, "libdebugme: failed to intercept signal %d (%s)\n", sig, signame);
    }
  }

  if (dbg_flags & DEBUGME_ALTSTACK) {
    static char ALIGNED(16) stack[SIGSTKSZ];
    stack_t st;
    st.ss_sp = stack;
    st.ss_flags = 0;
    st.ss_size = sizeof(stack);
    if(0 != sigaltstack(&st, 0)) {
      perror("libdebugme: failed to install altstack");
    }
  }

  return 1;
}

EXPORT int debugme_debug(unsigned dbg_flags, const char *dbg_opts) {
  // Note that this function and it's callee's should be signal-safe

  if(disabled)
    return 0;

  static int in_debugme_debug;
  if(!in_debugme_debug)
    in_debugme_debug = 1;  // TODO: make this thread-safe
  else {
    SAFE_MSG("libdebugme: can't attach more than one debugger simultaneously\n");
    return 1;
  }

  // TODO: select from the list of frontends (gdbserver, gdb+xterm, kdebug, ddd, etc.)
  if(!run_gdb(dbg_flags, dbg_opts ? dbg_opts : ""))
    return 0;

  // TODO: raise(SIGSTOP) and wait for gdb? But that's not signal/thread-safe...

  size_t us = 0;
  while(!__debugme_go) {  // Wait for debugger to unblock us
    usleep(10);
    us += 10;
    if(us > 1000000) {  // Allow for 1 sec. delay
      SAFE_MSG("libdebugme: debugger failed to attach\n");
      return 0;
    }
  }
  __debugme_go = 0;

  raise(SIGTRAP);

  in_debugme_debug = 0;
  return 1;
}

