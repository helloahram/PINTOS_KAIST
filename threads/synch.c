/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL); /* Semaphore 유효성 검사 */
	ASSERT (!intr_context ()); /* 인터럽트 컨텍스트에서 블로킹 호출 방지 */

	old_level = intr_disable (); /* 인터럽트 비활성화 */
	/* 공유 자원이 없는 경우 Waiter Push Back 하고 Thread_block */
	while (sema->value == 0) { 
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters, &thread_current()->elem, 
		thread_compare_priority, 0);
		thread_block (); /* 자원이 반환될 때까지 대기 */
	}
	sema->value--; /* 공유 자원이 있는 경우 Semaphore -1 */
	intr_set_level (old_level); /* 인터럽트 원상복구 */
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL); /* Semaphore 유효성 검사 */

	old_level = intr_disable (); /* 인터럽트 비활성화 */
	/* Waiter 가 있다면 첫번째 Thread 를 thread_unblock 으로 깨워서
	실행 대기 상태로 전환한다, thread_Unblock 은 pop Front 방식 */
	if (!list_empty (&sema->waiters)) {
		list_sort(&sema->waiters, thread_compare_priority, 0); /* 정렬 */
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++; /* 자원 사용을 마치고 반환하므로 Semaphore +1 */
	preempt_priority(); /* 우선순위 비교하여 CPU 선점 결정 */
	intr_set_level (old_level); /* 인터럽트 원상복구 */
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* lock_acquire 는 sema_down 에 의해 Lock 을 기다리는 동안 Blocking 된다 */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL); /* Lock 이 NULL 이 아닌지 확인 */
	ASSERT (!intr_context ()); /* 인터럽트 컨텍스트가 아닌지 확인 */
	/* 현재 Thread 가 이미 이 Lock 을 가지고 있지 않은지 확인 */
	ASSERT (!lock_held_by_current_thread (lock));

	/* Semaphore 를 감소시키며 Lock 을 획득하려 시도 */
	sema_down (&lock->semaphore);
	/* Lock 획득한 후 현재 Thread 를 Lock 소유자로 설정 */
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
	/* Lock 을 획득했으면, 현재 Thread 를 Lock 소유자로 설정 */
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL); /* Lock 이 NULL 이 아닌지 확인 */
	/* 현재 Thread 가 이미 이 Lock 을 가지고 있는지 확인 */
	/* 다른 함수들과 달리 ! 가 없음, Lock 을 가지고 있어야 Release 할 수 있다 */
	ASSERT (lock_held_by_current_thread (lock));

	lock->holder = NULL; /* Lock 이 가지고 있는 Thread 해제 */
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Semaphore 의 Waiters list Thread Priority 비교하는 함수 */
bool
sema_compare_priority (const struct list_elem *l, const struct list_elem *s, void *aux UNUSED) {
		/* list_entry 매크로를 통해 struct semaphore_elem 구조체에 접근한다 */
		struct semaphore_elem *l_sema = list_entry(l, struct semaphore_elem, elem);
		struct semaphore_elem *s_sema = list_entry(s, struct semaphore_elem, elem);

		/* 두 Semaphore 의 대기 리스트 Waiters 참조한다 */
		struct list *waiters_l_sema = &(l_sema->semaphore.waiters);
		struct list *waiters_s_sema = &(s_sema->semaphore.waiters);

		/* list_begin 으로 첫 번째 요소를 반환하고, list_entry 로 struct thread 의 포인터를 얻어서
		Priority 필드에 접근할 수 있다, 그래서 두 개의 세마포어 대기자 리스트에서 가장 높은 우선순위 Thread 비교 */
		return list_entry (list_begin (waiters_l_sema), struct thread, elem) -> priority
			> list_entry (list_begin (waiters_s_sema), struct thread, elem) -> priority;
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* 조건 변수에서 특정 조건이 만족될 때까지 Thread 를 대기시키는 함수 */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter; /* Semaphore Waiter List */

	ASSERT (cond != NULL); /* 컨디션 유효성 검사 */
	ASSERT (lock != NULL); /* Lock 유효성 검사 */
	ASSERT (!intr_context ()); /* 인터럽트 컨텍스트 블로킹 방지 */
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0); /* 대기 중인 Thread Sema 0 으로 초기화 */
	// list_push_back (&cond->waiters, &waiter.elem); /* 조건 변수 대기 리스트에 Waiter 추가 */
	/* sema_compare_priority 를 활용하여 Waiter 를 cond->waiters 리스트에 우선순위에 맞게 추가*/
	list_insert_ordered(&cond->waiters, &waiter.elem, sema_compare_priority, 0);
	lock_release (lock); /* 다른 Thread 가 작업을 진행할 수 있도록 Lock 해제 (후 대기 상태로 변경) */
	sema_down (&waiter.semaphore); /* Sema 가 0이면 Blocking (대기), 0보다 커지면 깨어남 */
	lock_acquire (lock); /* Sema 가 1로 증가하면, Lock 다시 획득 */
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* 조건 변수에서 대기 중인 Thread 를 깨워 Ready 상태로 만드는 함수 */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	/* 현재 Thread 가 Lock Holder 인지 확인 */
	ASSERT (lock_held_by_current_thread (lock));

	/* 대기 중인 Thread Waiter 가 있다면 Waiter 의 Semaphore +1 */
	/* list_entry(LIST_ELEM, STRUCT, MEMBER)*/
	if (!list_empty (&cond->waiters)){
		/* Wait 도중 Priority 가 바뀔 수 있으니 list_sort 로 정렬*/
		list_sort (&cond->waiters, sema_compare_priority, 0);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore); 
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

