// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provide place to put profiling methods for the
// Lock class.
// The Lock class is used everywhere, and hence any changes
// to lock.h tend to require a complete rebuild.  To facilitate
// profiler development, all the profiling methods are listed
// here.  Note that they are only instantiated in a debug
// build, and the header provides all the trivial implementations
// in a production build.

#include "base/lock.h"
#include "base/logging.h"

Lock::Lock()
    : lock_(),
      recursion_count_shadow_(0) {
#ifndef NDEBUG
  recursion_used_ = false;
  acquisition_count_ = 0;
  contention_count_ = 0;
#endif
}

Lock::~Lock() {
#ifndef NDEBUG
  // There should be no one to contend for the lock,
  // ...but we need the memory barrier to get a good value.
  lock_.Lock();
  int final_recursion_count = recursion_count_shadow_;
  lock_.Unlock();
#endif

  // Allow unit test exception only at end of method.
#ifndef NDEBUG
  DCHECK(0 == final_recursion_count);
#endif
}

void Lock::Acquire() {
#ifdef NDEBUG
  lock_.Lock();
  recursion_count_shadow_++;
#else  // NDEBUG
  if (!lock_.Try()) {
    // We have contention.
    lock_.Lock();
    contention_count_++;
  }
  // ONLY access data after locking.
  recursion_count_shadow_++;
  if (1 == recursion_count_shadow_)
    acquisition_count_++;
  else if (2 == recursion_count_shadow_ && !recursion_used_)
    // Usage Note: Set a break point to debug.
    recursion_used_ = true;
#endif  // NDEBUG
}

void Lock::Release() {
  --recursion_count_shadow_;  // ONLY access while lock is still held.
#ifndef NDEBUG
  DCHECK(0 <= recursion_count_shadow_);
#endif
  lock_.Unlock();
}

bool Lock::Try() {
  if (lock_.Try()) {
    recursion_count_shadow_++;
#ifndef NDEBUG
    if (1 == recursion_count_shadow_)
      acquisition_count_++;
    else if (2 == recursion_count_shadow_ && !recursion_used_)
      // Usage Note: Set a break point to debug.
      recursion_used_ = true;
#endif
    return true;
  } else {
    return false;
  }
}

// GetCurrentThreadRecursionCount returns the number of nested Acquire() calls
// that have been made by the current thread holding this lock.  The calling
// thread is ***REQUIRED*** to be *currently* holding the lock.   If that
// calling requirement is violated, the return value is not well defined.
// Return results are guaranteed correct if the caller has acquired this lock.
// The return results might be incorrect otherwise.
// This method is designed to be fast in non-debug mode by co-opting
// synchronization using lock_ (no additional synchronization is used), but in
// debug mode it slowly and carefully validates the requirement (and fires a
// a DCHECK if it was called incorrectly).
int32 Lock::GetCurrentThreadRecursionCount() {
#ifndef NDEBUG
  // If this DCHECK fails, then the most probable cause is:
  // This method was called by class AutoUnlock during processing of a
  // Wait() call made into the ConditonVariable class. That call to
  // Wait() was made (incorrectly) without first Aquiring this Lock
  // instance.
  lock_.Lock();
  int temp = recursion_count_shadow_;
  lock_.Unlock();
  // Unit tests catch an exception, so we need to be careful to test
  // outside the critical section, since the Leave would be skipped!?!
  DCHECK(temp >= 1);  // Allow unit test exception only at end of method.
#endif  // DEBUG

  // We hold lock, so this *is* correct value.
  return recursion_count_shadow_;
}


AutoUnlock::AutoUnlock(Lock& lock) : lock_(&lock), release_count_(0) {
  // We require our caller have the lock, so we can call for recursion count.
  // CRITICALLY: Fetch value before we release the lock.
  int32 count = lock_->GetCurrentThreadRecursionCount();
  DCHECK(count > 0);  // Make sure we owned the lock.
  while (count-- > 0) {
    release_count_++;
    lock_->Release();
  }
}

AutoUnlock::~AutoUnlock() {
  DCHECK(release_count_ >= 0);
  while (release_count_-- > 0)
    lock_->Acquire();
}

