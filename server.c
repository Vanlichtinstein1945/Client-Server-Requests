/*

	Threads are not joining correctly for some reason.
	They will join the first thread and then stop.
	It also doesn't like directories with more than one file.
	If the directory contains more than one file,
	it won't grab any data from any of the files.
	Hence why inputFile2.dat only grabs keyword from file2.txt.

	change request to arrays instead of pointers
	sizeof req->file_path instead of strlen+1

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#define SHM_NAME "OS"
#define FULL_NAME "sem_full"
#define EMPTY_NAME "sem_empty"
#define SRVMTX_NAME "sem_srvmtx"
#define POS_NAME "sem_pos"

void *ptrVal;
int posVal = 0;
int children = 0;
sem_t *pos;
sem_t *full;
sem_t *empty;
sem_t *server_mutex;

struct request {
	char file_path[1025];
	char keyword[257];
};

struct searchT_arg {
	char *file;
	char *full_file;
	char *keyword;
	sem_t *empty_bounded;
	sem_t *full_bounded;
	sem_t *bounded_mutex;
	struct List *list;
};

struct writeT_arg {
	struct List *list;
	sem_t *full_bounded;
	sem_t *empty_bounded;
	sem_t *bounded_mutex;
	int *threads_completed;
};

struct Node {
	struct Node *left;
	struct Node *right;
	char *file;
	char *line;
	int linenum;
};

struct nodeT {
	struct nodeT *right;
	struct nodeT *left;
	pthread_t thread_id;
};

struct List {
	struct Node *head;
	struct Node *tail;
	int items;
};

struct ListT {
	struct nodeT *head;
	struct nodeT *tail;
};

struct Node *create_node(char *file, char *line, int linenum) {
	struct Node *tmp = malloc(sizeof(struct Node));
	tmp->file = strdup(file);
	tmp->line = strdup(line);
	tmp->linenum = linenum;
	return tmp;
}

struct nodeT *create_nodeT(pthread_t thread_id) {
	struct nodeT *tmp = malloc(sizeof(struct nodeT));
	tmp->left = NULL;
	tmp->right = NULL;
	tmp->thread_id = thread_id;
	return tmp;
}

struct List *create_list() {
	struct List *tmp = malloc(sizeof(struct List));
	tmp->head = NULL;
	tmp->tail = NULL;
	tmp->items = 0;
	return tmp;
}

struct ListT *create_listT() {
	struct ListT *tmp = malloc(sizeof(struct ListT));
	tmp->head = NULL;
	tmp->tail = NULL;
	return tmp;
}

void insert_tail(struct List *list, struct Node *tmp) {
	if (list->head == NULL && list->tail == NULL) {
		list->head = tmp;
		list->tail = tmp;
	} else {
		list->tail->right = tmp;
		tmp->left = list->tail;
		list->tail = tmp;
	}
	list->items++;
}

void insert_headT(struct ListT *list, struct nodeT *tmp) {
	if (list->head == NULL && list->tail == NULL) {
		list->head = tmp;
		list->tail = tmp;
	} else {
		tmp->right = list->head;
		list->head->left = tmp;
		list->head = tmp;
	}
}

struct Node *remove_head(struct List *list) {
	struct Node *tmp;
	if (list->head == list->tail) {
		tmp = list->head;
		list->head = NULL;
		list->tail = NULL;
	} else {
		tmp = list->head;
		tmp->right->left = NULL;
		list->head = tmp->right;
		tmp->right = NULL;
	}
	list->items--;
	return tmp;
}

void *searchT_func(void *arg) {
	struct searchT_arg *tmp = arg;
	FILE *file;
	file = fopen(tmp->full_file, "r");
	char currline[1024];
	int linenum = 1;
	char *token;
	while(fgets(currline, 1024, file)) {
		char *the_rest = NULL;
		char *templine = strdup(currline);
		for (token = strtok_r(currline, " \t\n", &the_rest); token != NULL; token = strtok_r(NULL, " \t\n", &the_rest)) {
			if (strcmp(token, tmp->keyword) == 0) {
				sem_wait(tmp->empty_bounded);
				sem_wait(tmp->bounded_mutex);
				struct Node *foundKW = create_node(tmp->file, templine, linenum);
				insert_tail(tmp->list, foundKW);
				sem_post(tmp->bounded_mutex);
				sem_post(tmp->full_bounded);
				break;
			}
		}
		free(templine);
		linenum++;
	}
	fclose(file);
	free(tmp->file);
	free(tmp->full_file);
	free(tmp->keyword);
	free(tmp);
	pthread_exit(NULL);
}

void *writerT_func(void *arg) {
	struct writeT_arg *tmp = arg;
	int semVal;
	int *semValPtr;
	semValPtr = &semVal;
	int running = 1;
	while (running == 1) {
		sem_getvalue(tmp->full_bounded, semValPtr);
		if (*tmp->threads_completed == 0) {
			sem_getvalue(tmp->full_bounded, semValPtr);
			if (*semValPtr == 0) {
				break;
			}
		}
		if (*semValPtr != 0) {
			sem_wait(tmp->full_bounded);
			sem_wait(tmp->bounded_mutex);
		
			struct Node *currnode = remove_head(tmp->list);
			int fd;
			struct flock fl = {F_WRLCK, SEEK_END, 0, 0, 0};
			fl.l_pid = getpid();
			fd = open("output.txt", O_CREAT | O_APPEND | O_RDWR, 0666);
			while (fl.l_type != F_UNLCK) {
				fcntl(fd, F_GETLK, &fl);
			}
			fl.l_type = F_WRLCK;
			fcntl(fd, F_SETLKW, &fl);
			char *output = malloc(sizeof(char[1024]));
			sprintf(output,"%s:%d:%s",currnode->file,currnode->linenum,currnode->line);
			write(fd,output,strlen(output));
			fl.l_type = F_UNLCK;
			fcntl(fd, F_SETLK, &fl);
		
			free(output);
			free(currnode->file);
			free(currnode->line);
			free(currnode);
			close(fd);
		
			sem_post(tmp->bounded_mutex);
			sem_post(tmp->empty_bounded);
		}
	}
	free(tmp);
	pthread_exit(NULL);
}

void child_process(struct request *req, int boundedSize) {
	int threadNum = 0;
	sem_t full_bounded;
	sem_t empty_bounded;
	sem_t bounded_mutex;
	sem_init(&bounded_mutex, 0, 1);
	sem_init(&full_bounded, 0, 0);
	sem_init(&empty_bounded, 0, boundedSize);
	DIR *directory = opendir(&req->file_path);
	struct dirent *dir;
	char *temp = strcat(&req->file_path, "/");
	struct List *bounded_buffer = create_list();
	struct ListT *thread_list = create_listT();
	while ((dir = readdir(directory)) != NULL) {
		if (dir->d_type != DT_DIR) {
			pthread_t thread_id;
			struct searchT_arg *tmp = malloc(sizeof(struct searchT_arg));
			tmp->file = strdup(dir->d_name);
			tmp->full_file = strdup(temp);
			realloc(tmp->full_file,strlen(tmp->full_file)+strlen(tmp->file)+2);
			strcat(tmp->full_file,tmp->file);
			tmp->keyword = strdup(&req->keyword);
			tmp->list = bounded_buffer;
			tmp->full_bounded = &full_bounded;
			tmp->empty_bounded = &empty_bounded;
			tmp->bounded_mutex = &bounded_mutex;
			threadNum++;
			pthread_create(&thread_id, NULL, &searchT_func, tmp);
			struct nodeT *tmp_nodeT = create_nodeT(thread_id);
			insert_headT(thread_list, tmp_nodeT);
		}
	}
	struct writeT_arg *writer_args = malloc(sizeof(struct writeT_arg));
	writer_args->list = bounded_buffer;
	writer_args->full_bounded = &full_bounded;
	writer_args->empty_bounded = &empty_bounded;
	writer_args->bounded_mutex = &bounded_mutex;
	int threads_completed = 1;
	writer_args->threads_completed = &threads_completed;
	pthread_t writerT;
	pthread_create(&writerT, NULL, &writerT_func, writer_args);
	struct nodeT *tmp_currNodeT;
	struct nodeT *tmp_ptrNodeT = thread_list->head;
	while (tmp_ptrNodeT != NULL) {
		tmp_currNodeT = tmp_ptrNodeT;
		tmp_ptrNodeT = tmp_currNodeT->right;
		pthread_join(tmp_currNodeT->thread_id, NULL);
		free(tmp_currNodeT);
	}
	free(thread_list);
	threads_completed = 0;
	pthread_join(writerT, NULL);
	sem_destroy(&full_bounded);
	sem_destroy(&empty_bounded);
	sem_destroy(&bounded_mutex);
	free(req);
}

int grab_request(int queueSize, int boundedSize) {
	struct request *req = malloc(sizeof(struct request));
	
	sem_wait(full);
	sem_wait(server_mutex);
	
	char *file_pathPtr = &req->file_path;
	char *keywordPtr = &req->keyword;
	strcpy(file_pathPtr,ptrVal+(posVal*sizeof(struct request)));
	strcpy(keywordPtr,ptrVal+(posVal*sizeof(struct request))+sizeof(req->file_path));
	posVal++;
	if (posVal == queueSize) {
		posVal = 0;
	}
	
	sem_post(server_mutex);
	sem_post(empty);
	
	if (strcmp(req->file_path,"exit") == 0) {
		free(req);
		return 1;
	}
	
	pid_t x;
	x = fork();
	children++;
	if (x == 0) {
		child_process(req, boundedSize);
		children--;
		exit(0);
	}

	return 0;
}

int main(int argc, char *argv[]) {
	const int queueSize = atoi(argv[1]);
	const int boundedSize = atoi(argv[2]);
	const int SIZE = sizeof(struct request)*queueSize;
	int fd;
	
	full = sem_open(FULL_NAME, O_CREAT, 0660, 0);
	empty = sem_open(EMPTY_NAME, O_CREAT, 0660, queueSize);
	server_mutex = sem_open(SRVMTX_NAME, O_CREAT, 0660, 1);
	pos = sem_open(POS_NAME, O_CREAT, 0660, 0);
	
	fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
	ftruncate(fd, SIZE);
	ptrVal = mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	
	int running = 0;
	while (running == 0)	{
		running = grab_request(queueSize, boundedSize);
	}
	
	while (children != 0) {
		wait(NULL);
	}

	shm_unlink(SHM_NAME);
	sem_unlink(POS_NAME);
	sem_unlink(FULL_NAME);
	sem_unlink(EMPTY_NAME);
	sem_unlink(SRVMTX_NAME);
	
	return 0;
}
