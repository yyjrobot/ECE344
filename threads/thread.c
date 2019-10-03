#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"
#include <stdint.h>
#include <malloc.h>

//#define DEBUG_USE_VALGRIND
//
//#ifdef DEBUG_USE_VALGRIND
//#include <valgrind.h>
//#endif


#define UNUSED(x) (void)(x)

/* This is the wait queue structure */
struct wait_queue {
    /* ... Fill this in Lab 3 ... */
};

enum {
    THREAD_RUNNING = 0,
    THREAD_BLOCKED = 1,
    THREAD_READY = 2,
    THREAD_EXITED = 3,
    THREAD_UNUSED = 4
};

/* This is the thread control block */
typedef struct thread {
    /* ... Fill this in ... */
    void *stack;
    void *first_stack;
    ucontext_t tcontext;
    int status;
} thread;

typedef struct que_node {
    Tid id;
    struct que_node *next;
} que_node;

/* Global Variable For Threads and thread queues*/
thread all_thread[THREAD_MAX_THREADS];
Tid running_thread;
que_node *ready_head;
que_node *exited_head;
volatile int setcontext_called;

//functions for ques
//head just contain nothing but a invaid

void que_init(que_node **any_head) {
    *any_head = NULL;
}

void que_add(que_node **any_head, Tid id) {
    //create new node
    que_node *new_node = (que_node*) malloc(sizeof (que_node));
    new_node->id = id;
    new_node->next = NULL;
    if (*any_head == NULL) {//nothing in the que
        *any_head = new_node;
        return;
    } else {
        que_node *tmp = *any_head; //tmp->whatever head is pointing to



        while (tmp->next != NULL) {//haven't reach end
            tmp = tmp->next;
        }
        tmp->next = new_node;
        return;
    }
}

void que_pop_head(que_node **any_head) {


    if (*any_head == NULL) {
        return;
    } else {
        que_node *tmp = *any_head;
        *any_head = tmp->next;
        free(tmp);
        tmp = NULL;
        return;
    }
}

void que_pop_given(que_node **any_head, Tid id) {
    que_node *tmp = *any_head;
    if (tmp == NULL) {//que is empty
        assert(0);
    }
    if (tmp->id == id) {
        return que_pop_head(any_head);
    } else {
        que_node *prev;
        while (tmp != NULL && tmp->id != id) {
            prev = tmp;
            tmp = tmp->next;
        }
        if (tmp == NULL) {//id not in que
            assert(0);
        }
        prev->next = tmp->next;
        free(tmp);
        tmp = NULL;
    }
}

void
thread_stub(void (*thread_main)(void *), void *arg) {

    thread_main(arg);
    thread_exit();
    return;
}

Tid first_available_tid(int x) {
    UNUSED(x);
    int i = 0;
    for (i = 0; i < THREAD_MAX_THREADS; i++) {
        if (all_thread[i].status == THREAD_UNUSED) {
            //printf("WE FOUND %d in separate function\n",i);
            return i;
        }
    }
    //printf("WE FOUND nomore in separate function\n");
    return THREAD_NOMORE;
}

//this function only does switch context

Tid call_setcontext(Tid want_tid) {
    volatile int really_setcontext_called = 0;
    //save current context
    int ret = getcontext(&all_thread[running_thread].tcontext);
    assert(!ret);
    if (really_setcontext_called == 1) {
        //interrupts_on();
        //printf("RUNNING: %d",want_tid);

        return want_tid;
    }
    really_setcontext_called = 1;
    running_thread = want_tid;
    int err = setcontext(&all_thread[want_tid].tcontext);
    assert(!err);
    return THREAD_FAILED;
}

void
thread_init(void) {
    que_init(&ready_head);
    que_init(&exited_head);
    for (int i = 0; i < THREAD_MAX_THREADS; i++) {
        all_thread[i].status = THREAD_UNUSED;
        all_thread[i].first_stack=NULL;
        all_thread[i].stack = NULL;
    }
    all_thread[0].status = THREAD_RUNNING;
    //int err = getcontext(&all_thread[0].tcontext);
    //assert(!err);
    running_thread = 0;
    /* your optional code here */

}

Tid
thread_id() {

    return running_thread;
}

Tid
thread_create(void (*fn) (void *), void *parg) {


    //TBD();
    //first find a tid
    Tid available_tid = first_available_tid(0);
    //printf("WE FOUND %d\n",available_tid);
    if (available_tid == THREAD_NOMORE) {
        return THREAD_NOMORE;
    }
    //we have a tid, start creating a new_thread



    //    You will use malloc to allocate a new per-thread stack.
    //allocate a new stack

    
    void *stack_ptr = (void*) malloc(THREAD_MIN_STACK);
    if (stack_ptr == NULL) {
        return THREAD_NOMEMORY;
    }
    
        
//    #ifdef DEBUG_USE_VALGRIND
//	unsigned valgrind_register_retval = VALGRIND_STACK_REGISTER(stack_ptr - THREAD_MIN_STACK, stack_ptr);
//	assert(valgrind_register_retval);
//#endif

    //start changing contexts
    all_thread[available_tid].status = THREAD_READY;
    
    all_thread[available_tid].first_stack=stack_ptr;
    
    int ret = getcontext(&all_thread[available_tid].tcontext);
    assert(!ret);

    //    You will change the program counter to point to a stub function, described below, that should be the first function the thread runs.
    //    You will change the stack pointer to point to the top of the new stack. (Warning: in x86-64, stacks grow down!)
    //    You will initialize the argument registers, described below, with the arguments that are to be passed to the stub function.

    stack_ptr += THREAD_MIN_STACK;

    stack_ptr -=8;
        
   // stack_ptr -= ((uintptr_t) stack_ptr % 16) + 8;
    //printf("FOKING %ld\n",(uintptr_t)stack_ptr);

    all_thread[available_tid].tcontext.uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_stub;
    all_thread[available_tid].tcontext.uc_mcontext.gregs[REG_RSP] = (unsigned long) stack_ptr; //top
    all_thread[available_tid].tcontext.uc_mcontext.gregs[REG_RDI] = (unsigned long) fn;
    all_thread[available_tid].tcontext.uc_mcontext.gregs[REG_RSI] = (unsigned long) parg;


    stack_ptr=NULL;

    que_add(&ready_head, available_tid);
    //printf("We created a thread: %d\n",available_tid);
    return available_tid;
}

void destory_thread(Tid id) {
    if (id == running_thread) {
        assert(0);
    }
    if (id==0){
        return;
    }
//    #ifdef DEBUG_USE_VALGRIND
//	VALGRIND_STACK_DEREGISTER(all_thread[id].stack - THREAD_MIN_STACK);
//#endif
    //struct mallinfo minfo_start = mallinfo();
    free(all_thread[id].first_stack);
    //struct mallinfo minfo_end = mallinfo();
    //printf("Previous used: %d, Now: %d\n",minfo_start.uordblks,minfo_end.uordblks);
    all_thread[id].status = THREAD_UNUSED;
    all_thread[id].stack=NULL;
    all_thread[id].first_stack=NULL;
}

Tid
thread_yield(Tid want_tid) {


    //kill all exited thread before create
    que_node *tmp;
    tmp = exited_head;
    int last_self = 0;
    while (tmp != NULL) {
        Tid tmp_id = tmp->id;
        if (tmp_id == running_thread) {
            que_add(&exited_head, tmp_id);
            if (last_self == 1) {
                break;
            } else {
                last_self = 1;
            }
        } else {

            //que_pop_head(&exited_head);
        }
        destory_thread(tmp_id);
        //printf("CUrrent: %d |||| %d deleted, status:%d\n",running_thread, tmp->id, all_thread[tmp->id].status);

        que_pop_head(&exited_head);
        tmp = exited_head;
    }


    if (want_tid == THREAD_SELF || want_tid == running_thread) {
        //volatile int really_setcontext_called=0;
        return call_setcontext(running_thread);
    }
    if (want_tid == THREAD_ANY) {//pick the head of the ready_que
        if (ready_head == NULL)
            return THREAD_NONE;
        else {
            //modify thread status
            all_thread[ready_head->id].status = THREAD_RUNNING;
            all_thread[running_thread].status = THREAD_READY;
            //switch context between running and first in ready que


            Tid new_running = ready_head->id;
            // printf ("YEILDING to: %d\n",new_running);

            que_add(&ready_head, running_thread);

            que_pop_head(&ready_head);

            //running_thread = new_running;


            return call_setcontext(new_running);
        }
    }
    if (want_tid >= THREAD_MAX_THREADS || want_tid < THREAD_FAILED) {
        return THREAD_INVALID;
    }
    if (all_thread[want_tid].status != THREAD_READY) {
        return THREAD_INVALID;
    } else {//given tid
        all_thread[want_tid].status = THREAD_RUNNING;
        all_thread[running_thread].status = THREAD_READY;

        Tid new_running = want_tid;
        que_pop_given(&ready_head, want_tid);
        que_add(&ready_head, running_thread);





        return call_setcontext(new_running);
    }
    return THREAD_FAILED;
}

void
thread_exit() {
    if (ready_head != NULL ) {//dont want to exit 0 for now
        //que_add(&exited_head, running_thread);
        all_thread[running_thread].status = THREAD_EXITED;

        Tid tmp = ready_head->id;

        all_thread[ready_head->id].status = THREAD_RUNNING;
        que_pop_head(&ready_head);

        que_add(&exited_head, running_thread);
        call_setcontext(tmp);
        return;
    } else if (ready_head==NULL) {//last thread
        
        //first kill other threads
        if (running_thread!=0){
            free(all_thread[running_thread].first_stack);
            exit(0);
        }
        
        return;
    }
    
    //TBD();
}

Tid
thread_kill(Tid tid) {
    //TBD();
    if (tid == running_thread
            || tid < 0
            || tid >= THREAD_MAX_THREADS
            || all_thread[tid].status != THREAD_READY) {
        //printf("IDIOT ERROR\n");
        return THREAD_INVALID;
    } else {
        que_add(&exited_head, tid);
        que_pop_given(&ready_head, tid);
        all_thread[tid].status = THREAD_EXITED;
        //printf("ADDING %d to exit que\n",tid);
        return tid;
    }

    return THREAD_FAILED;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create() {
    struct wait_queue *wq;

    wq = malloc(sizeof (struct wait_queue));
    assert(wq);

    TBD();

    return wq;
}

void
wait_queue_destroy(struct wait_queue *wq) {
    TBD();
    free(wq);
}

Tid
thread_sleep(struct wait_queue *queue) {
    TBD();
    return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all) {
    TBD();
    return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid) {
    TBD();
    return 0;
}

struct lock {
    /* ... Fill this in ... */
};

struct lock *
lock_create() {
    struct lock *lock;

    lock = malloc(sizeof (struct lock));
    assert(lock);

    TBD();

    return lock;
}

void
lock_destroy(struct lock *lock) {
    assert(lock != NULL);

    TBD();

    free(lock);
}

void
lock_acquire(struct lock *lock) {
    assert(lock != NULL);

    TBD();
}

void
lock_release(struct lock *lock) {
    assert(lock != NULL);

    TBD();
}

struct cv {
    /* ... Fill this in ... */
};

struct cv *
cv_create() {
    struct cv *cv;

    cv = malloc(sizeof (struct cv));
    assert(cv);

    TBD();

    return cv;
}

void
cv_destroy(struct cv *cv) {
    assert(cv != NULL);

    TBD();

    free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock) {
    assert(cv != NULL);
    assert(lock != NULL);

    TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock) {
    assert(cv != NULL);
    assert(lock != NULL);

    TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock) {
    assert(cv != NULL);
    assert(lock != NULL);

    TBD();
}
