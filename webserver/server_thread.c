#include "request.h"
#include "server_thread.h"
#include "common.h"

#define EXPECTED_MEAN_FILE_SIZE 12288//given by fileset function

//prototypes
static void do_server_request(struct server *sv, int connfd);
int cache_lookup(struct server *sv, char *key);
int cache_insert(struct server *sv, struct file_data *data);
int cache_evict(struct server *sv, int amount_to_evict);
void clear_cache(struct server *sv);

struct hash_unit {
    struct file_data *data;
    int occupied;
    int count; //number of threads using this file
};

//if only one item in the que, its prev and next should all be null
//head_elem->prev=NULL
//tail_elem->next=NULL

typedef struct LRU_node {
    int key; //this is the position of the file in the hash table
    struct LRU_node* next;
    struct LRU_node* prev;
} LRU_node;

LRU_node *LRU_head;
LRU_node *LRU_tail;

void LRU_add_head(LRU_node **head, LRU_node **tail, int key) {
    LRU_node *new_node = (LRU_node*) Malloc(sizeof (LRU_node));
    new_node->key = key;
    new_node->next = NULL;
    new_node->prev = NULL;
    if (*head == NULL) {
        *head = new_node;
        *tail = new_node;
        return;
    } else {
        LRU_node *tmp = *head;
        *head = new_node;
        new_node->next = tmp;
        tmp->prev = new_node;
        return;
    }
}

void LRU_pop_tail(LRU_node **head, LRU_node **tail) {
    if (*tail == NULL) {
        return;
    } else if (*head == *tail) {//one elem in list
        LRU_node *tmp = *tail;
        free(tmp);
        *tail = NULL;
        *head = NULL;
    } else {//at least 2 elem in list 
        LRU_node *tmp = *tail;
        LRU_node *tmp2 = tmp->prev;
        *tail = tmp->prev;
        tmp2->next = NULL;
        free(tmp);
    }
}

void LRU_pop_given(LRU_node **head, LRU_node **tail, int key) {
    if (*head == NULL) {
        return;
    } else {
        LRU_node *tmp = *head;
        if (tmp->key == key) {
            if (tmp->next == NULL) {
                free(tmp);
                *head = NULL;
                *tail = NULL;
            } else {
                *head = tmp->next;
                tmp->next->prev = NULL;
                free(tmp);
            }
            return;
        } else {
            tmp = tmp->next;
            while (tmp != NULL) {
                if (tmp->key == key) {
                    //do deletion and return
                    LRU_node *tmp2 = tmp->prev;
                    if (tmp->next == NULL) {
                        //delete last one
                        tmp2->next = NULL;
                        *tail = tmp2;
                        free(tmp);
                        return;
                    } else {//middle nodes
                        tmp2->next = tmp->next;
                        tmp2->next->prev = tmp2;
                        free(tmp);
                        return;
                    }
                } else {
                    tmp = tmp->next;
                }
            }
            //we shouldn't get here, as the value is not found
            assert(0);
        }
    }
}

void LRU_update(LRU_node **head, LRU_node **tail, int key) {
    LRU_pop_given(head, tail, key);
    LRU_add_head(head, tail, key);
}
//just for debug

//void print_LRU(LRU_node **head) {
//    if (*head == NULL) {
//        printf("fu\n");
//    } else {
//        LRU_node *tmp = *head;
//        while (tmp != NULL) {
//            printf("%d ", tmp->key);
//            tmp = tmp->next;
//        }
//        printf(" OVO\n");
//    }
//}

void clear_LRU(LRU_node **head, LRU_node **tail) {
    LRU_node *tmp = *head;
    LRU_node *next = NULL;
    while (tmp != NULL) {
        next = tmp->next;
        free(tmp);
        tmp = next;
    }
    *head = NULL;
    *tail = NULL;
}

//following hash function comes from 
//https://stackoverflow.com/questions/10696223/reason-for-5381-number-in-djb-hash-function

unsigned long DJBHash(char* str, unsigned int len) {
    unsigned long hash = 5381;
    unsigned int i = 0;

    for (i = 0; i < len; str++, i++) {
        hash = ((hash << 5) + hash) + (*str);
    }

    return hash;
}

typedef struct que_node {
    int id;
    struct que_node *next;
} que_node;

struct server {
    int nr_threads;
    int max_requests;
    int max_cache_size;
    int exiting;
    /* add any other parameters you need */
    //int used_threads;
    int *request_buffer;
    int in;
    int out;

    pthread_t *thread_array;
    pthread_mutex_t mutex;
    pthread_mutex_t cache_lock;
    pthread_cond_t full;
    pthread_cond_t empty;

    struct hash_unit *hash_table;
    int hash_table_size;
    int available_cache;
};

/* static functions */

//handle requests

void worker(struct server *sv) {
    while (!sv->exiting) {

        //block if no request
        pthread_mutex_lock(&sv->mutex);

        while (sv->in == sv->out) {
            if (sv->exiting == 1) {
                pthread_mutex_unlock(&sv->mutex);
                return;
            }
            pthread_cond_wait(&sv->empty, &sv->mutex);
        }

        int connfd = sv->request_buffer[sv->out];

        if ((sv->in - sv->out + sv->max_requests + 1) % (sv->max_requests + 1) == sv->max_requests) {
            pthread_cond_broadcast(&sv->full);
        }
        sv->out = (sv->out + 1) % (sv->max_requests + 1);


        pthread_mutex_unlock(&sv->mutex);
        do_server_request(sv, connfd);
    }
    //return;
}

/* initialize file data */
static struct file_data *
file_data_init(void) {
    struct file_data *data;

    data = Malloc(sizeof (struct file_data));
    data->file_name = NULL;
    data->file_buf = NULL;
    data->file_size = 0;
    return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data) {
    free(data->file_name);
    free(data->file_buf);
    free(data);
}

static void
do_server_request(struct server *sv, int connfd) {
    int ret;
    struct request *rq;
    struct file_data *data;
    int pos;

    data = file_data_init();

    /* fill data->file_name with name of the file being requested */
    rq = request_init(connfd, data);
    if (!rq) {
        file_data_free(data);
        return;
    }
    if (sv->max_cache_size == 0) {
        /* read file, 
         * fills data->file_buf with the file contents,
         * data->file_size with file size. */
        ret = request_readfile(rq);
        if (ret == 0) { /* couldn't read file */
            //goto out;
            request_destroy(rq);
            file_data_free(data);
            return;
        }
        /* send file to client */
        request_sendfile(rq);
        request_destroy(rq);
        file_data_free(data);
        return;
    } else if (sv->max_cache_size > 0) {
        pthread_mutex_lock(&sv->cache_lock);
        pos = cache_lookup(sv, data->file_name);
        if (pos != -1) {//we have this data cached 
            assert(strcmp(data->file_name, sv->hash_table[pos].data->file_name) == 0);
            sv->hash_table[pos].count++;
            request_set_data(rq, sv->hash_table[pos].data);
            LRU_update(&LRU_head, &LRU_tail, pos);
        } else {//we need to insert 1 
            pthread_mutex_unlock(&sv->cache_lock);
            ret = request_readfile(rq);
            if (ret == 0) {//read file failed
                request_destroy(rq);
                file_data_free(data);
                return;
            }
            pthread_mutex_lock(&sv->cache_lock);
            pos = cache_insert(sv, data);
            if (pos == -1) {//insertion failed, we will just do the request
                pthread_mutex_unlock(&sv->cache_lock);
                request_sendfile(rq);
                request_destroy(rq);
                file_data_free(data);
                return;
            }
            assert(strcmp(data->file_name, sv->hash_table[pos].data->file_name) == 0);
            sv->hash_table[pos].count++;
            //LRU_add_head(&LRU_head, &LRU_tail, pos);
        }
        //pthread_mutex_unlock(&sv->cache_lock);
        //request_sendfile(rq);
        //pthread_mutex_lock(&sv->cache_lock);
        if (sv->hash_table[pos].occupied == 1) {
            sv->hash_table[pos].count--;
            // LRU_update(&LRU_head, &LRU_tail, pos);
        }
        pthread_mutex_unlock(&sv->cache_lock);
        request_sendfile(rq);
        request_destroy(rq);
        //data = NULL;
        file_data_free(data);
        return;
    }
    //out:
    //request_destroy(rq);
    //file_data_free(data);
}

/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size) {
    struct server *sv;

    sv = Malloc(sizeof (struct server));
    //sv->used_threads = 0;
    sv->nr_threads = nr_threads;
    sv->max_requests = max_requests;
    sv->max_cache_size = max_cache_size;
    sv->exiting = 0;
    sv->in = 0;
    sv->out = 0;
    //initialization of threadlocks 
    pthread_mutex_init(&sv->mutex, NULL);
    pthread_cond_init(&sv->empty, NULL);
    pthread_cond_init(&sv->full, NULL);
    pthread_mutex_init(&sv->cache_lock, NULL);

    if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
        //TBD();
        //create worker threads
        if (nr_threads > 0) {
            sv->thread_array = Malloc(sizeof (pthread_t) * nr_threads);
            for (int i = 0; i < nr_threads; i++) {
                pthread_create(&(sv->thread_array[i]), NULL, (void *) &worker, (void*) sv);
            }
        }

        if (max_requests > 0) {
            sv->request_buffer = Malloc(sizeof (int)*(max_requests + 1));
        }

        if (max_cache_size > 0) {
            //calculate cache_table_size
            sv->available_cache = sv->max_cache_size;
            //int load_factor = sv->max_cache_size / EXPECTED_MEAN_FILE_SIZE;
            sv->hash_table_size = 10000;
            sv->hash_table = Malloc(sizeof (struct hash_unit)*sv->hash_table_size);
            for (long i = 0; i < sv->hash_table_size; i++) {
                sv->hash_table[i].count = 0;
                sv->hash_table[i].occupied = 0;
                sv->hash_table[i].data = NULL;
            }
            LRU_head = NULL;
            LRU_tail = NULL;
        }
    }
    /* Lab 4: create queue of max_request size when max_requests > 0 */
    /* Lab 5: init server cache and limit its size to max_cache_size */
    /* Lab 4: create worker threads when nr_threads > 0 */
    return sv;
}

void
server_request(struct server *sv, int connfd) {
    if (sv->nr_threads == 0) { /* no worker threads */
        do_server_request(sv, connfd);
    } else {
        /*  Save the relevant info in a buffer and have one of the
         *  worker threads do the work. */
        pthread_mutex_lock(&sv->mutex);
        while ((sv->in - sv->out + sv->max_requests + 1) % (sv->max_requests + 1) == sv->max_requests) {
            pthread_cond_wait(&sv->full, &sv->mutex);
        }//full

        sv->request_buffer[sv->in] = connfd;

        if (sv->in == sv->out) {
            pthread_cond_broadcast(&sv->empty);
        }

        sv->in = (sv->in + 1) % (sv->max_requests + 1);
        pthread_mutex_unlock(&sv->mutex);
        //TBD();

    }
}

void
server_exit(struct server *sv) {
    /* when using one or more worker threads, use sv->exiting to indicate to
     * these threads that the server is exiting. make sure to call
     * pthread_join in this function so that the main server thread waits
     * for all the worker threads to exit before exiting. */
    sv->exiting = 1;
    pthread_cond_broadcast(&sv->full);
    pthread_cond_broadcast(&sv->empty);

    if (sv->nr_threads > 0) {
        for (int i = 0; i < sv->nr_threads; i++) {
            int ret = pthread_join(sv->thread_array[i], NULL);
            assert(ret == 0);
        }

    }
    /* make sure to free any allocated resources */
    //if (sv->max_requests>0){
    clear_cache(sv);
    clear_LRU(&LRU_head, &LRU_tail);
    free(sv->thread_array);
    free(sv->request_buffer);
    //free(sv->hash_table);
    //}
    free(sv);
}

/////////////////////
//Caching functions//
//Require Mutex Lock/
/////////////////////

//on success, this function returns the index of 
//the cached file in the table,
//on failure, return -1

int cache_lookup(struct server *sv, char *name) {
    //if (sv->hash_table_size=0){
        //return -1;
    //}
    long pos = DJBHash(name, strlen(name)) % sv->hash_table_size;
    //if (sv->hash_table[pos].occupied == 0) {//empty position
    //return -1;
    //} else {//we either found it or collision
    //if (sv->hash_table[pos].data->file_name == name) {
    //return pos;
    //} else {
    int count = 0; //make sure we will not end up looping in this hash_table
    while (count < sv->hash_table_size) {
        //move to next slot

        if (sv->hash_table[pos].occupied == 0) {//empty slot, check next
            if (pos == sv->hash_table_size - 1) {
                pos = 0;
                count++;
            } else {
                pos++;
                count++;
            }
            //continue;
        } else {
            if (strcmp(sv->hash_table[pos].data->file_name, name) == 0) {//found our data
                return pos;
            }
            if (pos == sv->hash_table_size - 1) {
                pos = 0;
                count++;
            } else {
                pos++;
                count++;
            }
        }
    }
    return -1; //we didn't find the data we want
    //}

    assert(0);
}

//on success, return position of the slot
//on failure, return -1

int cache_insert(struct server *sv, struct file_data *data) {
    
    if (sv->max_cache_size<data->file_size){
        return -1;
    }
    
    int look_up_ret = cache_lookup(sv, data->file_name);
    if (look_up_ret != -1) {//the cache has already been created, just return the pos
        return look_up_ret;
    } else {
        //if (sv->available_cache < data->file_size) {
        int ret = cache_evict(sv, data->file_size);
        if (ret == 1) {
            //do insertion
            long pos = DJBHash(data->file_name, strlen(data->file_name)) % sv->hash_table_size;
            if (sv->hash_table[pos].occupied == 0) {

                //sv->hash_table[pos].data=malloc(sizeof(struct file_data));
                sv->hash_table[pos].data = file_data_init();
                sv->hash_table[pos].data->file_name = strdup(data->file_name);
                sv->hash_table[pos].data->file_size = data->file_size;
                sv->hash_table[pos].data->file_buf = strdup(data->file_buf);

                sv->hash_table[pos].occupied = 1;
                sv->hash_table[pos].count = 0;
                sv->available_cache -= data->file_size;
                LRU_add_head(&LRU_head, &LRU_tail, pos);
                return pos;
            } else {//we need to find a position for it
                int count = 0;
                while (count < sv->hash_table_size) {
                    //move forward in the table
                    if (pos == sv->hash_table_size - 1) {
                        pos = 0;
                        count++;
                    } else {
                        pos++;
                        count++;
                    }
                    if (sv->hash_table[pos].occupied == 0) {

                        //sv->hash_table[pos].data=malloc(sizeof(struct file_data));
                        sv->hash_table[pos].data = file_data_init();
                        sv->hash_table[pos].data->file_name = strdup(data->file_name);
                        sv->hash_table[pos].data->file_size = data->file_size;
                        sv->hash_table[pos].data->file_buf = strdup(data->file_buf);

                        sv->hash_table[pos].occupied = 1;
                        sv->hash_table[pos].count = 0;
                        sv->available_cache -= data->file_size;
                        LRU_add_head(&LRU_head, &LRU_tail, pos);
                        return pos;
                    }
                }
                return -1; //we tried all empty slots, and didn't insert it 
            }
        } else {
            return -1; //we dont have enough space for this file
        }
        //}
    }
    return -1;
    //return 0;
}

//if we have enough space, we will not evict, just return 1
//on success, return 1
//on failure, return -1 to indicate the amount oversize the max_cache_size
//and then we will not do evict

int cache_evict(struct server *sv, int amount_to_evict) {
    if (amount_to_evict > sv->max_cache_size) {
        return -1;
    } else {
        LRU_node *tmp = LRU_tail;

        while (sv->available_cache < amount_to_evict && tmp != NULL) {
            assert(tmp!=NULL);
            int curr_key = tmp->key;
            if (sv->hash_table[curr_key].count == 0) {//nobody is using it
                //delete it from the hash table
                assert(sv->hash_table[curr_key].data!=NULL);
                sv->available_cache += sv->hash_table[curr_key].data->file_size;
                file_data_free(sv->hash_table[curr_key].data);
                sv->hash_table[curr_key].occupied = 0;
                sv->hash_table[curr_key].data = NULL;

                //update out LRU_list
                tmp=tmp->prev;
                LRU_pop_given(&LRU_head, &LRU_tail, curr_key);
            }
            else{
                tmp = tmp->prev;
            }
        }
        //we have delete all unused slots, check if the space is good to go 
        if (sv->available_cache > amount_to_evict) {//success
            return 1;
        } else {
            return -1;
        }
    }
}

void clear_cache(struct server *sv) {
    for (int i = 0; i < sv->hash_table_size; i++) {
        if (sv->hash_table[i].occupied == 1)
            file_data_free(sv->hash_table[i].data);
    }
    free(sv->hash_table);
}