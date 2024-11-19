#include "request.h"
#include "server_thread.h"
#include "common.h"
#include <string.h>
#include <stdbool.h>

#define HASHTABLE_SIZE  40000

struct lru_node{
    char* file_name;
    struct lru_node* next;
    struct lru_node* prev;
};

struct lru_list{ //a doubly linked list, most recently used at the front, least recently used at the tail
    struct lru_node* head;
    struct lru_node* tail;
};

struct cache_entry{ //key value pair of a hash-table
    //char* key; //file name
    struct lru_node* access_lru_node; //used for O(1) access in the lru_list
    struct file_data* value; //file_data fetched from disk
    //int value;
    int in_use;
    struct cache_entry* next;
};

struct cache{     //hash-table array
    struct cache_entry** headptrs; //double pointer to each entry
    int cache_memory_limit; //size of hash-table
    int cache_memory_used;
};

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	/* add any other parameters you need */
        struct cache* server_cache;
        struct lru_list* least_recently_used_list;
	int *conn_buf;
	pthread_t *threads;
	int request_head;
	int request_tail;
	pthread_mutex_t mutex;
	pthread_cond_t prod_cond;
	pthread_cond_t cons_cond;	
};

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

/* static functions */
/*
 this function inserts a new node at the head, this is the most recently used file when we have a cache miss
 at cache miss, we get a new file from disk so this has to be the entry at the front 
 returns pointer to the recently added node in lru_list*/
struct lru_node* push_cache_miss(struct lru_list* lru_list, char* file_name){
    struct lru_node*  new_node = (struct lru_node*)malloc(sizeof(struct lru_node));
    new_node->file_name = (char*)malloc(strlen(file_name) + 1);
    new_node->next = NULL;
    new_node->prev = NULL;
    strcpy(new_node->file_name, file_name);
    
    if(lru_list->head == NULL){ //empty list
        lru_list->head = new_node;
        lru_list->tail = new_node;
    }else if(lru_list->head != NULL){
        lru_list->head->prev = new_node;
        new_node->next = lru_list->head;
        lru_list->head = new_node;
    }
    
    return new_node;
}

void print_list(struct lru_list* list){
    struct lru_node* temp = list->head;
    while(temp != list->tail){
        printf("%s\n", temp->file_name);
        temp = temp->next;
    }
}
/*
 this function is called if we have a cache hit, and we will update the hit entry to be at the front of the list (most recently used)*/
void push_cache_hit(struct lru_list* lru_list, struct lru_node* mru_node){
    //print_list(lru_list);
    if(strcmp(lru_list->head->file_name, mru_node->file_name) == 0) //if most recently used node is already at head, do nothing 
        return;
    else if(strcmp(lru_list->tail->file_name, mru_node->file_name) == 0){ //if it is a tail, bring it to head
        struct lru_node* new_head_node = lru_list->tail;
        struct lru_node* last_node = lru_list->tail->prev;
        new_head_node->next = lru_list->head;
        new_head_node->prev = NULL;
        lru_list->head = new_head_node;
        last_node->next = NULL;
        lru_list->tail = last_node;
        return;
    }
    else if(strcmp(lru_list->tail->file_name, mru_node->file_name) != 0){   //somewhere in the middle
        //printf("here:\n");
        //print_list(lru_list);
        //printf("mru node: %s\n", mru_node->file_name);
        struct lru_node* prev_node = mru_node->prev;
        //printf("prev node: %s\n", prev_node->file_name);
        struct lru_node* next_node = mru_node->next;
        mru_node->next = lru_list->head;
        mru_node->prev = NULL;
        prev_node->next = next_node;
        next_node->prev = prev_node;
        lru_list->head = mru_node;
        return;
    }
}

/*
 this function removes the least recently used node from lru, and we also need to remove it in the cache
 this function is called when we need to evict cache to free up space*/
void remove_lru_node(struct lru_list* lru_list){
    //print_list(lru_list);
    if(lru_list == NULL)
        return;
    if(lru_list->head == NULL)
        return;
    if(lru_list->head->next == NULL){ //only 1 node in list
        free(lru_list->head->file_name);
        free(lru_list->head);
    }else if(lru_list->head->next != NULL){
        struct lru_node* last_node = lru_list->tail->prev;
        last_node->next = NULL;
        free(lru_list->tail->file_name);
        free(lru_list->tail);
        lru_list->tail = last_node;
    }
}

struct cache* create_cache(int max_cache_memory){
    
    //general structure of our hash-table
    struct cache* hashTable = (struct cache*)malloc(sizeof(struct cache)); //starting pointer to hash-table
    hashTable->cache_memory_limit = max_cache_memory;
    hashTable->cache_memory_used = 0;
    hashTable->headptrs = (struct cache_entry**)calloc(HASHTABLE_SIZE,sizeof(struct cache_entry*)); //allocated memory to store an array of double pointers
    
    for(long i = 0; i< HASHTABLE_SIZE; i++ )                       //initialize every pointer to null
        hashTable->headptrs[i] = NULL;

    return hashTable;
}

unsigned long int pri_hash_function(char *str){ //djb2 hash function
    
    unsigned long hash = 5381;
    int character;
    
    while((character = *str++))
        hash = ((hash << 20) + hash) + character;
    
    return hash;
}

/*
 look up files in cache, if found, return the file data, if not found, return null
 */
struct cache_entry* cache_lookup(struct server* server, char* str){
    //printf("INSIDE CACHE_LOOKUP: HERE IS LRU_LIST:\n");
    //print_list(server->least_recently_used_list);
    
    unsigned long int cacheIndex = pri_hash_function(str) % HASHTABLE_SIZE;

    if(server->server_cache->headptrs[cacheIndex] == NULL){    // cache miss
        return NULL;
    }else if(server->server_cache->headptrs[cacheIndex] != NULL){
        struct cache_entry* temp_node = server->server_cache->headptrs[cacheIndex];
        //struct cache_entry* last_node = server->server_cache->headptrs[cacheIndex];
        
        while(temp_node != NULL){
            if(strcmp(temp_node->value->file_name, str) == 0){         //cache hit 
                //temp_node->in_use = true;       //currently using this file 
                //move the accessed file in lru to the fron of the list; update lru list 
                //push_cache_hit(server->least_recently_used_list, temp_node->access_lru_node);
                return temp_node;
            }else{
                //last_node = temp_node;
                temp_node = temp_node->next;
            }
        } 
    }
    //at the end of the list, did not find file
    //if we get here means we traversed all the list 
    return NULL;
}
/*
 helper function to allocate dynamic memory for a cache entry*/
struct cache_entry* create_cache_entry(struct cache_entry* node, struct file_data* file){
    struct file_data* data;
    data = file_data_init();
    node->value = data;
    node->in_use = 0;
    node->value->file_buf = (char*)malloc(strlen(file->file_buf) + 1);
    node->value->file_name = (char*)malloc(strlen(file->file_name) + 1);
    strcpy(node->value->file_buf, file->file_buf);
    strcpy(node->value->file_name, file->file_name);
    node->value->file_size = file->file_size;
    
    return node;
}

void insert_linked_list_head(struct cache* cache, struct file_data* file, int cache_index){
    struct cache_entry* head = (struct cache_entry*)malloc(sizeof(struct cache_entry));
    cache->headptrs[cache_index] = create_cache_entry(head, file);
}

/*
 this helper function keeps evicting files based on lru until we have enough memory inside cache for the incoming file 
 returns true if successful evicted, else return false*/
bool cache_evict(struct server* server, int file_size){
    int file_size_removed = 0;
    if(server->least_recently_used_list == NULL){
        //lru doesn't exist
        return false;
    }
    if(server->least_recently_used_list->head == NULL){
        //nothing inside to evict
        return false;
    }
    //remove least recently used file 
    while(file_size > (server->server_cache->cache_memory_limit - server->server_cache->cache_memory_used)){
        
        int removed_index = pri_hash_function(server->least_recently_used_list->tail->file_name) % HASHTABLE_SIZE;
  
        struct cache_entry* temp_node = server->server_cache->headptrs[removed_index];
        struct cache_entry* last_node = server->server_cache->headptrs[removed_index];
        printf("INSIDE CACHE EVICT: NODE:\n");
        
        while(temp_node != NULL){
            
            printf("cache node: %s\n",temp_node->value->file_name);
            print_list(server->least_recently_used_list);
            printf("tail node: %s\n", server->least_recently_used_list->tail->file_name);
            
            if(strcmp(temp_node->value->file_name, server->least_recently_used_list->tail->file_name) == 0){
                
                if(temp_node->in_use){
                    //if current file that we want to evict is being transferred, don't evict this 
                    return false;
                }
                //found entry to be evicted
                int file_freed = temp_node->value->file_size;
                temp_node->access_lru_node = NULL;
                remove_lru_node(server->least_recently_used_list);  //remove from lru list 
                last_node->next = temp_node->next;
                file_data_free(temp_node->value);           //free data
                free(temp_node);                            //free node 

                file_size_removed = file_size_removed + file_freed; //update cache memory, then get out and see if we have to free another node
                server->server_cache->cache_memory_used = server->server_cache->cache_memory_used - file_freed;
                break;
            }else{
                last_node = temp_node;
                temp_node = temp_node->next;
            }
        }
    }
    return true;
}
/*
 after fetching from disk, we write file into cache, if exceeds limit, returns 0, means we did not write into it
 this function is called in the conditions of a cache miss*/
struct cache_entry* cache_insert(struct server* server, struct file_data* file){
    
    if(file->file_size > server->server_cache->cache_memory_limit)    //memory exceeded
        return NULL;
    else if(file->file_size > (server->server_cache->cache_memory_limit - server->server_cache->cache_memory_used)){   //have to evict
        if(!cache_evict(server, file->file_size))
            return NULL;
    }
    
    struct cache_entry* cache_node;
    int cache_index = pri_hash_function(file->file_name) % HASHTABLE_SIZE;
    if(server->server_cache->headptrs[cache_index] == NULL){
        
        insert_linked_list_head(server->server_cache, file, cache_index); //insert at the start of the list
        
        cache_node = server->server_cache->headptrs[cache_index];
        //creates new lru_node and it is inserted at the head of lru_list, this is the most recently used node 
        cache_node->access_lru_node = push_cache_miss(server->least_recently_used_list, file->file_name); //update lru
        cache_node->in_use++;
        
    }else if(server->server_cache->headptrs[cache_index] != NULL){     //insert at tail of the list 
        struct cache_entry* temp_node = server->server_cache->headptrs[cache_index];
        struct cache_entry* last_node = temp_node;
        
        while(temp_node != NULL){
            last_node = temp_node;
            temp_node = temp_node->next;
        }
        
        struct cache_entry* new_cache_entry = (struct cache_entry*)malloc(sizeof(struct cache_entry)); //new hashtable node 
        last_node->next = create_cache_entry(new_cache_entry, file);
        new_cache_entry->next = NULL;
        new_cache_entry->access_lru_node = push_cache_miss(server->least_recently_used_list, file->file_name);//update lru
        new_cache_entry->in_use++;
        
        cache_node = new_cache_entry;
    }
    server->server_cache->cache_memory_used = server->server_cache->cache_memory_used + file->file_size; //update cache memory 
    
    return cache_node;
}


static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
        //search it in the cache first
        pthread_mutex_lock(&sv->mutex); // 1 thread access cache at a time 
        struct cache_entry* temp = cache_lookup(sv, data->file_name);
        if(temp != NULL){   //cache hit
            //request_set_data(rq, temp->value);
            //request_sendfile(rq);
            temp->in_use++;
            data->file_buf = (char*)malloc(strlen(temp->value->file_buf) + 1);
            data->file_name = (char*)malloc(strlen(temp->value->file_name) + 1);
            strcpy(data->file_buf, temp->value->file_buf);
            strcpy(data->file_name, temp->value->file_name);
            data->file_size = temp->value->file_size;
            request_set_data(rq, data);
            push_cache_hit(sv->least_recently_used_list, temp->access_lru_node);
            //file_data_free(data);
            //return;
        }else{      //look into disk 
            //multiple threads can read into disk 
            pthread_mutex_unlock(&sv->mutex);
            /*
             * if we get here, means we are searching disk 
	     * fills data->file_buf with the file contents,
	     * data->file_size with file size. */
   
            ret = request_readfile(rq);
            if (ret == 0) { /* couldn't read file */
		goto out;
            }
            pthread_mutex_lock(&sv->mutex);
            //check if file has been inserted by another thread
            temp = cache_lookup(sv, data->file_name);
            if(temp == NULL){ //not inside cache, we have to insert
                temp = cache_insert(sv, data); //if not inserted in cache by another thread, this thread inserts it 
            }else{
                push_cache_hit(sv->least_recently_used_list, temp->access_lru_node); //update most recently used 
            }
        }
	pthread_mutex_unlock(&sv->mutex);
        /* send file to client */
        request_sendfile(rq);
        //inserted into cache, after sending file the file is no longer in use
        //pthread_mutex_lock(&sv->mutex);
        if(temp != NULL)
            temp->in_use--;
        //pthread_mutex_unlock(&sv->mutex);
out:
	request_destroy(rq);
	file_data_free(data);
}

static void *
do_server_thread(void *arg)
{
	struct server *sv = (struct server *)arg;
	int connfd;

	while (1) {
		pthread_mutex_lock(&sv->mutex);
		while (sv->request_head == sv->request_tail) {
			/* buffer is empty */
			if (sv->exiting) {
				pthread_mutex_unlock(&sv->mutex);
				goto out;
			}
			pthread_cond_wait(&sv->cons_cond, &sv->mutex);
		}
		/* get request from tail */
		connfd = sv->conn_buf[sv->request_tail];
		/* consume request */
		sv->conn_buf[sv->request_tail] = -1;
		sv->request_tail = (sv->request_tail + 1) % sv->max_requests;
		
		pthread_cond_signal(&sv->prod_cond);
		pthread_mutex_unlock(&sv->mutex);
		/* now serve request */
		do_server_request(sv, connfd);
	}
out:
	return NULL;
}

/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;
	int i;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	/* we add 1 because we queue at most max_request - 1 requests */
	sv->max_requests = max_requests + 1;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;

	/* Lab 4: create queue of max_request size when max_requests > 0 */
	sv->conn_buf = Malloc(sizeof(*sv->conn_buf) * sv->max_requests);
	for (i = 0; i < sv->max_requests; i++) {
		sv->conn_buf[i] = -1;
	}
	sv->request_head = 0;
	sv->request_tail = 0;

	/* Lab 5: init server cache and limit its size to max_cache_size */
        sv->max_cache_size = max_cache_size;
        sv->server_cache = create_cache(max_cache_size); //init server cache
        sv->least_recently_used_list = (struct lru_list*)malloc(sizeof(struct lru_list));
        
	/* Lab 4: create worker threads when nr_threads > 0 */
	pthread_mutex_init(&sv->mutex, NULL);
	pthread_cond_init(&sv->prod_cond, NULL);
	pthread_cond_init(&sv->cons_cond, NULL);	
	sv->threads = Malloc(sizeof(pthread_t) * nr_threads);
	for (i = 0; i < nr_threads; i++) {
		SYS(pthread_create(&(sv->threads[i]), NULL, do_server_thread,
				   (void *)sv));
	}
	return sv;
}

void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */

		pthread_mutex_lock(&sv->mutex);
		while (((sv->request_head - sv->request_tail + sv->max_requests)
			% sv->max_requests) == (sv->max_requests - 1)) {
			/* buffer is full */
			pthread_cond_wait(&sv->prod_cond, &sv->mutex);
		}
		/* fill conn_buf with this request */
		assert(sv->conn_buf[sv->request_head] == -1);
		sv->conn_buf[sv->request_head] = connfd;
		sv->request_head = (sv->request_head + 1) % sv->max_requests;
		pthread_cond_signal(&sv->cons_cond);
		pthread_mutex_unlock(&sv->mutex);
	}
}

/*
 this helper function frees all the nodes in cache*/
void free_cache(struct cache* cache){
    
    for(int i = 0; i < HASHTABLE_SIZE; i++){
        if(cache->headptrs[i] == NULL){
            free(cache->headptrs[i]);
            continue;
        }
        
        struct cache_entry* head = cache->headptrs[i];
        struct cache_entry* temp = cache->headptrs[i];
        
        if(head->next == NULL){
            head->access_lru_node = NULL;
            file_data_free(head->value);
            free(head);
        }else{
            while(head->next != NULL){
                temp = head;
                head = head->next;
                temp->access_lru_node = NULL;
                file_data_free(temp->value);
                free(temp);
            }
            free(head);
        }
        free(cache->headptrs[i]);
    }
    free(cache->headptrs);
    free(cache);
}

/*
 helper function to free lru*/

void free_lru(struct lru_list* lru_list){
    struct lru_node* temp_head = lru_list->head;
    struct lru_node* next;
    
    while(temp_head != NULL){
        next = temp_head->next;
        free(temp_head->file_name);
        free(temp_head);
        temp_head = next;
    }

    free(lru_list);
}

void
server_exit(struct server *sv)
{
	int i;
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	pthread_mutex_lock(&sv->mutex);
	sv->exiting = 1;
	pthread_cond_broadcast(&sv->cons_cond);
	pthread_mutex_unlock(&sv->mutex);
	for (i = 0; i < sv->nr_threads; i++) {
		pthread_join(sv->threads[i], NULL);
	}

	/* make sure to free any allocated resources */
	free(sv->conn_buf);
	free(sv->threads);
        /*
         free cache
         free lru */
        free_cache(sv->server_cache);
        free_lru(sv->least_recently_used_list);
        
	free(sv);
}
