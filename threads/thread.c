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

void destory_thread(Tid id);

typedef struct que_node {
    Tid id;
    struct que_node *next;
} que_node;

/* This is the wait queue structure */
typedef struct wait_queue {
    que_node *wait_que_head;
    /* ... Fill this in Lab 3 ... */
} wait_que;

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
    struct wait_queue *waited;
} thread;



/* Global Variable For Threads and thread queues*/
thread all_thread[THREAD_MAX_THREADS];
Tid who_waits_on_who[THREAD_MAX_THREADS];
Tid running_thread;
que_node *ready_head;
que_node *exited_head;
volatile int setcontext_called;

//functions for ques
//head just contain nothing but a invaid

void que_init(que_node **any_head) {
    int enabled = interrupts_set(0);
    *any_head = NULL;
    interrupts_set(enabled);
}

void que_add(que_node **any_head, Tid id) {
    int enabled = interrupts_set(0);
    //create new node
    que_node *new_node = (que_node*) malloc(sizeof (que_node));
    new_node->id = id;
    new_node->next = NULL;
    if (*any_head == NULL) {//nothing in the que
        *any_head = new_node;
        interrupts_set(enabled);
        return;
    } else {
        que_node *tmp = *any_head; //tmp->whatever head is pointing to
        while (tmp->next != NULL) {//haven't reach end
            tmp = tmp->next;
        }
        tmp->next = new_node;
        interrupts_set(enabled);
        return;
    }
}

void que_pop_head(que_node **any_head) {
    int enabled = interrupts_set(0);

    if (*any_head == NULL) {
        interrupts_set(enabled);
        return;
    } else {
        que_node *tmp = *any_head;
        *any_head = tmp->next;
        free(tmp);
        tmp = NULL;
        interrupts_set(enabled);
        return;
    }
}

void que_pop_given(que_node **any_head, Tid id) {
    int enabled = interrupts_set(0);
    que_node *tmp = *any_head;
    if (tmp == NULL) {//que is empty
        assert(0);
    }
    if (tmp->id == id) {
        que_pop_head(any_head);
        interrupts_set(enabled);
        return;
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
    interrupts_set(enabled);
    return;
}

void destroy_exited_threads(){
    int enabled = interrupts_set(0);
    while(exited_head!=NULL){
        destory_thread(exited_head->id);
        que_pop_head(&exited_head);
    }
    interrupts_set(enabled);
    return;
}

void
thread_stub(void (*thread_main)(void *), void *arg) {
    interrupts_set(1);
    thread_main(arg);

    thread_exit();
    return;
}

Tid first_available_tid(int x) {
    int enabled = interrupts_set(0);
    UNUSED(x);
    int i = 0;
    for (i = 0; i < THREAD_MAX_THREADS; i++) {
        if (all_thread[i].status == THREAD_UNUSED) {
            interrupts_set(enabled);
            return i;
        }
    }
    interrupts_set(enabled);
    return THREAD_NOMORE;
}

//this function only does switch context

Tid call_setcontext(Tid want_tid, int enabled) {
    interrupts_off();
    volatile int really_setcontext_called = 0;
    //save current context
    int ret = getcontext(&all_thread[running_thread].tcontext);
    assert(!ret);
    if (really_setcontext_called == 1) { 
        destroy_exited_threads();
        interrupts_set(enabled);
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
    int enabled = interrupts_set(0);
    //que_init(&wait_head);
    que_init(&ready_head);
    que_init(&exited_head);
    //standing_by.wait_que_head=wait_head;
    for (int i = 0; i < THREAD_MAX_THREADS; i++) {
        all_thread[i].status = THREAD_UNUSED;
        all_thread[i].first_stack = NULL;
        all_thread[i].stack = NULL;
        all_thread[i].waited = NULL;
        who_waits_on_who[i] = THREAD_NONE;
    }
    all_thread[0].waited = wait_queue_create();
    all_thread[0].status = THREAD_RUNNING;
    running_thread = 0;
    interrupts_set(enabled);
    return;
}

Tid
thread_id() {

    return running_thread;
}

Tid
thread_create(void (*fn) (void *), void *parg) {

    int enabled = interrupts_set(0);
    //first find a tid
    Tid available_tid = first_available_tid(0);
    if (available_tid == THREAD_NOMORE) {
        interrupts_set(enabled);
        return THREAD_NOMORE;
    }
    //we have a tid, start creating a new_thread

    //    You will use malloc to allocate a new per-thread stack.
    //allocate a new stack

    void *stack_ptr = (void*) malloc(THREAD_MIN_STACK);
    if (stack_ptr == NULL) {
        interrupts_set(enabled);
        return THREAD_NOMEMORY;
    }

    //start changing contexts
    who_waits_on_who[available_tid]=THREAD_NONE;
    all_thread[available_tid].status = THREAD_READY;
    all_thread[available_tid].waited = wait_queue_create();
    all_thread[available_tid].first_stack = stack_ptr;

    int ret = getcontext(&all_thread[available_tid].tcontext);
    assert(!ret);

    //    You will change the program counter to point to a stub function, described below, that should be the first function the thread runs.
    //    You will change the stack pointer to point to the top of the new stack. (Warning: in x86-64, stacks grow down!)
    //    You will initialize the argument registers, described below, with the arguments that are to be passed to the stub function.

    stack_ptr += THREAD_MIN_STACK;

    stack_ptr -= 8;


    all_thread[available_tid].tcontext.uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_stub;
    all_thread[available_tid].tcontext.uc_mcontext.gregs[REG_RSP] = (unsigned long) stack_ptr; //top
    all_thread[available_tid].tcontext.uc_mcontext.gregs[REG_RDI] = (unsigned long) fn;
    all_thread[available_tid].tcontext.uc_mcontext.gregs[REG_RSI] = (unsigned long) parg;


    stack_ptr = NULL;

    que_add(&ready_head, available_tid);
    interrupts_set(enabled);

    return available_tid;
}

void destory_thread(Tid id) {
    int enabled = interrupts_set(0);
    if (id == running_thread) {
        assert(0);
    }
    if (id == 0) {
        interrupts_set(enabled);
        return;
    }
    free(all_thread[id].first_stack);
    all_thread[id].status = THREAD_UNUSED;
    all_thread[id].stack = NULL;
    all_thread[id].first_stack = NULL;
    thread_wakeup(all_thread[id].waited,1);
    wait_queue_destroy(all_thread[id].waited);
    interrupts_set(enabled);
    return;
}

Tid
thread_yield(Tid want_tid) {

    int enabled = interrupts_set(0);
    //kill all exited thread before create
    //que_node *tmp;
    //tmp = exited_head;
    //int last_self = 0;
//    while (tmp != NULL) {
//        Tid tmp_id = tmp->id;
//        if (tmp_id == running_thread) {
//            que_add(&exited_head, tmp_id);
//            if (last_self == 1) {
//                break;
//            } else {
//                last_self = 1;
//            }
//        }  
//        destory_thread(tmp_id);
//        que_pop_head(&exited_head);
//        tmp = exited_head;
//    }
    destroy_exited_threads();

    if (want_tid == THREAD_SELF || want_tid == running_thread) {
        return call_setcontext(running_thread, enabled);
    }
    if (want_tid == THREAD_ANY) {//pick the head of the ready_que
        if (ready_head == NULL) {
            interrupts_set(enabled);
            return THREAD_NONE;
        } else {
            
            //unintr_printf("Current: %d switch to %d \n",running_thread,ready_head->id);
            
            //modify thread status
            all_thread[ready_head->id].status = THREAD_RUNNING;
            all_thread[running_thread].status = THREAD_READY;
            //switch context between running and first in ready que

            Tid new_running = ready_head->id;

            que_add(&ready_head, running_thread);

            que_pop_head(&ready_head);



            return call_setcontext(new_running, enabled);
        }
    }
    if (want_tid >= THREAD_MAX_THREADS || want_tid < THREAD_FAILED) {
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    if (all_thread[want_tid].status != THREAD_READY) {
        interrupts_set(enabled);
        return THREAD_INVALID;
    } else {//given tid
        all_thread[want_tid].status = THREAD_RUNNING;
        all_thread[running_thread].status = THREAD_READY;

        Tid new_running = want_tid;
        que_pop_given(&ready_head, want_tid);
        que_add(&ready_head, running_thread);

        return call_setcontext(new_running, enabled);
    }
    interrupts_set(enabled);
    return THREAD_FAILED;
}

void
thread_exit() {
    int enabled = interrupts_set(0);
    thread_wakeup(all_thread[running_thread].waited,1);
    if (ready_head != NULL) {//dont want to exit 0 for now
        all_thread[running_thread].status = THREAD_EXITED;

        Tid tmp = ready_head->id;

        all_thread[ready_head->id].status = THREAD_RUNNING;
        que_pop_head(&ready_head);
        
        que_add(&exited_head, running_thread);
        call_setcontext(tmp, enabled);
        return;
    } else if (ready_head == NULL) {//last thread
        //last thread still have some child sleep on it 
        //need them to run to completion
        if (all_thread[running_thread].waited->wait_que_head!=NULL){
            //thread_wakeup(all_thread[running_thread].waited,1);
//            while(ready_head!=NULL){
//                thread_kill()
//            }
            
            //thread_yield(THREAD_ANY);
            interrupts_set(enabled);
            return;
        }
        
        //first kill other threads
        if (running_thread != 0) {
            
            free(all_thread[running_thread].first_stack);
            //call_setcontext(0,0);
            //exit(0);
        }
        interrupts_set(enabled);
        exit(0);
        
        return;
    }
    //TBD();
}

Tid
thread_kill(Tid tid) {
    int enabled = interrupts_set(0);
    //TBD();

    if (tid==running_thread||tid<0||tid>=THREAD_MAX_THREADS){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    
    if(all_thread[tid].status==THREAD_BLOCKED){//this thread sleeps on someone
        Tid thread_sleep_on = who_waits_on_who[tid];
        //we need to wake up it and put it into the ready que
        all_thread[tid].status=THREAD_READY;
        que_pop_given(&all_thread[thread_sleep_on].waited->wait_que_head,tid);
        que_add(&ready_head,tid);
        who_waits_on_who[tid]=THREAD_NONE;
        
    }
    
    
    if (tid == running_thread
            || tid < 0
            || tid >= THREAD_MAX_THREADS
            || all_thread[tid].status != THREAD_READY) {
        interrupts_set(enabled);
        return THREAD_INVALID;
    } else {
        
       
        
        que_add(&exited_head, tid);
        que_pop_given(&ready_head, tid);
        all_thread[tid].status = THREAD_EXITED;
        interrupts_set(enabled);
        return tid;
    }
    interrupts_set(enabled);
    return THREAD_INVALID;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create() {
    int enabled = interrupts_set(0);
    struct wait_queue *wq;

    wq = malloc(sizeof (struct wait_queue));

    assert(wq);
    wq->wait_que_head = NULL;
    //TBD();
    interrupts_set(enabled);
    return wq;
}

void
wait_queue_destroy(struct wait_queue *wq) {
    int enabled = interrupts_set(0);
    //TBD();
    if (wq->wait_que_head == NULL) {//the wait que is empty, we can destroy it
        free(wq);
        interrupts_set(enabled);
    } else
        assert(wq->wait_que_head != NULL);
}

Tid
thread_sleep(struct wait_queue *queue) {
    //TBD();
    int enabled = interrupts_set(0);
    if (queue == NULL) {//que is NULL
        interrupts_set(enabled);
        return THREAD_INVALID;
    } else if (ready_head == NULL) {//no more threads
        //unintr_printf("WOOOO\n");
        interrupts_set(enabled);
        return THREAD_NONE; 
    } 
        
    else if (all_thread[running_thread].status==THREAD_RUNNING)
        {
        //keep track of who sleep on who
        //who_waits_on_who[running_thread] = ready_head->id;
        //there is something in the ready_que, we can sleep
        //unintr_printf("WDNMD\n");
        all_thread[ready_head->id].status = THREAD_RUNNING;
        all_thread[running_thread].status = THREAD_BLOCKED;
        que_add(&queue->wait_que_head, running_thread);
        Tid new_running = ready_head->id;
        que_pop_head(&ready_head);
        call_setcontext(new_running, 0); //not enable signal now
        interrupts_set(enabled);
        return running_thread;
    }
    else if (all_thread[running_thread].status==THREAD_BLOCKED){
        //unintr_printf("lol\n");
        interrupts_set(enabled);
        return 0;
    }
    assert(0);
    return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all) {
    //TBD();
    int enabled = interrupts_set(0);
    if (queue == NULL || queue->wait_que_head == NULL) {
        interrupts_set(enabled);
        return 0;
    } else {
        if (all == 0) {//wakeup single
            
            Tid tmp_id = queue->wait_que_head->id;
            all_thread[tmp_id].status=THREAD_READY;
            que_pop_head(&queue->wait_que_head);
            que_add(&ready_head, tmp_id);
            interrupts_set(enabled);
            return 1;
        }
        if (all == 1) {//wakeup all
            //unintr_printf("WAKE UP CALLED\n");
            int threads_waked=0;
            while (queue->wait_que_head != NULL) {
                Tid tmp_id = queue->wait_que_head->id;
                all_thread[tmp_id].status=THREAD_READY;
                que_pop_head(&queue->wait_que_head);
                que_add(&ready_head, tmp_id);
                threads_waked++;
            }
            interrupts_set(enabled);
            return threads_waked;
        }
    }
    assert(0);
    interrupts_set(enabled);
    return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid) {
    //TBD();
    int enabled = interrupts_set(0);
    if (tid<0
            ||tid==running_thread
            ||tid>THREAD_MAX_THREADS){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
   
        if (all_thread[tid].status==THREAD_EXITED||all_thread[tid].status==THREAD_UNUSED){
        interrupts_set(enabled);
        return THREAD_INVALID;
        }
    
    else{
        who_waits_on_who[running_thread] = tid;
        thread_sleep(all_thread[tid].waited);
        interrupts_set(enabled);
        return tid;
    }
    assert(0);
    interrupts_set(enabled);
    return 0;
}
//////////////////////////////////////////////////////////////////
struct lock {
    /* ... Fill this in ... */
    struct wait_queue *wait_que;
    Tid acquirer;
};

struct lock *
lock_create() {
    int enabled = interrupts_set(0);
    struct lock *lock;

    lock = malloc(sizeof (struct lock));
    assert(lock);
    
    lock->wait_que = wait_queue_create();
    lock->acquirer = THREAD_NONE;
    //TBD();
    interrupts_set(enabled);
    return lock;
}

void
lock_destroy(struct lock *lock) {
    int enabled = interrupts_set(0);
    assert(lock != NULL);
    //check availability
    if (lock->acquirer!=THREAD_NONE){
        assert(0);
    }
    if (lock->wait_que->wait_que_head!=NULL){//the lock is not clear
        assert(0);
    }
    //TBD();
    wait_queue_destroy(lock->wait_que);
    
    free(lock);
    interrupts_set(enabled);
    //return;
}

void
lock_acquire(struct lock *lock) {
    int enabled = interrupts_set(0);
    assert(lock != NULL);
    while(lock->acquirer!=THREAD_NONE){
        thread_sleep(lock->wait_que);
    }
    lock->acquirer=running_thread;
    
    //TBD();
    interrupts_set(enabled);
    //return;
}

void
lock_release(struct lock *lock) {
    int enabled = interrupts_set(0);
    assert(lock != NULL);
    
    //make sure current thread has acquired the lock
//    if (running_thread!=lock->acquirer){
//        assert(0);
//    }
    
    thread_wakeup(lock->wait_que,0);
    
    lock->acquirer=THREAD_NONE;
    
    //TBD();
    interrupts_set(enabled);
    //return;
}

struct cv {
    /* ... Fill this in ... */
    struct wait_queue *wait_que;
    Tid acquirer;
};

struct cv *
cv_create() {
    int enabled = interrupts_set(0);
    struct cv *cv;

    cv = malloc(sizeof (struct cv));
    assert(cv);
    cv->wait_que=wait_queue_create();
    cv->acquirer=THREAD_NONE;
    //TBD();
    interrupts_set(enabled);
    return cv;
}

void
cv_destroy(struct cv *cv) {
    int enabled = interrupts_set(0);
    assert(cv != NULL);
    
    if (cv->wait_que->wait_que_head!=NULL){
        assert(0);
    }
    
    wait_queue_destroy(cv->wait_que);
    //TBD();

    free(cv);
    interrupts_set(enabled);
}

void
cv_wait(struct cv *cv, struct lock *lock) {
    int enabled = interrupts_set(0);
    assert(cv != NULL);
    assert(lock != NULL);
    //make sure the lock is acquired by the current thread
    if (lock->acquirer!=running_thread){
        assert(0);
    }

    lock_release(lock);
    //while(cv->acquirer!=THREAD_NONE){
        thread_sleep(cv->wait_que);
    //}
        lock_acquire(lock);
    //TBD();
    
    interrupts_set(enabled);
}

void
cv_signal(struct cv *cv, struct lock *lock) {
    int enabled = interrupts_set(0);
    assert(cv != NULL);
    assert(lock != NULL);
    if (lock->acquirer!=running_thread){
        assert(0);
    }
    if (cv->wait_que->wait_que_head!=NULL){
        thread_wakeup(cv->wait_que,0);
    }
    //TBD();
    interrupts_set(enabled);
}

void
cv_broadcast(struct cv *cv, struct lock *lock) {
    int enabled = interrupts_set(0);
    assert(cv != NULL);
    assert(lock != NULL);
    if (lock->acquirer!=running_thread){
        assert(0);
    }
    if (cv->wait_que->wait_que_head!=NULL){
        thread_wakeup(cv->wait_que,1);
    }
    //TBD();
    interrupts_set(enabled);
}
