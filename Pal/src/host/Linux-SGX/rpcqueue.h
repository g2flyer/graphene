/*
 * RPC threads are helper threads that run in untrusted mode alongside enclave threads. RPC threads
 * issue system calls on behalf of enclave threads. This allows "exitless" design when app threads
 * never leave the enclave (except for a few syscalls where there is no benefit, e.g., nanosleep).
 *
 * "Exitless" design alleviates expensive OCALLs/ECALLs. This was first proposed by SCONE (by
 * Arnautov et al at OSDI 2016) and by Eleos (by Orenbach et al at EuroSys 2017).
 *
 * Brief description: user must specify "sgx.rpc_thread_num = 2" in manifest to create two RPC
 * threads. If user specifies "0" or omits this directive, then no RPC threads are created and all
 * syscalls perform an enclave exit (as in previous versions of Graphene).
 *
 * All enclave and RPC threads work on a single shared RPC queue (global variable `g_rpc_queue`).
 * To issue syscall, enclave thread enqueues syscall request in the queue and spins waiting for
 * result. RPC threads spin waiting for syscall requests; when request comes, first lucky RPC
 * thread grabs request, issues syscall to OS, and notifies enclave thread by releasing the request
 * lock. RPC queue is implemented as a FIFO ring buffer with one global lock.
 *
 * RPC queue can have up to RPC_QUEUE_SIZE requests simultaneously. All requests are allocated on
 * the untrusted stack of the enclave thread; enclave thread owns its requests and pops them off
 * stack when done with the system call. After enqueuing the request, enclave thread first spins
 * for some time in hope the system call returns immediately (fast path), then sleeps waiting on
 * futex (slow path, useful for blocking syscalls).
 *
 * NOTE: number of created RPC threads must match max number of simultaneous enclave threads. If
 * there are more RPC threads, CPU time is wasted. If there are less, some enclave threads may
 * starve, especially if there are many blocking syscalls by other enclave threads.
 *
 * Prototype code was written by Meni Orenbach and adapted to Graphene by Dmitrii Kuvaiskii.
 */
#ifndef QUEUE_H_
#define QUEUE_H_

#include <stddef.h>
#include <stdint.h>
#include "spinlock.h"

#define RPC_QUEUE_SIZE 1024         /* max # of requests in RPC queue */
#define MAX_RPC_THREADS 256         /* max number of RPC threads */
#define RPC_SPINLOCK_TIMEOUT 4096   /* # of iterations to spin before sleeping */

typedef struct {
    int result;
    int ocall_index;
    void* buffer;
    spinlock_t lock;  /* can be UNLOCKED / LOCKED_NO_WAITERS / LOCKED_WITH_WAITERS */
} rpc_request_t;

typedef struct rpc_queue {
    uint64_t front, rear;
    rpc_request_t* q[RPC_QUEUE_SIZE]; /* queue of syscall requests */
    int rpc_threads[MAX_RPC_THREADS]; /* RPC threads (thread IDs) */
    volatile size_t rpc_threads_num;  /* number of RPC threads */
    spinlock_t lock;                  /* global lock for enclave and RPC threads */
} rpc_queue_t;

extern rpc_queue_t* g_rpc_queue;  /* global RPC queue */

rpc_request_t* rpc_enqueue(rpc_queue_t* q, rpc_request_t* req);
rpc_request_t* rpc_dequeue(rpc_queue_t* q);

#endif /* QUEUE_H_ */
