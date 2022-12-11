#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

#define SHM_NAME "OS"
#define FULL_NAME "sem_full"
#define EMPTY_NAME "sem_empty"
#define SRVMTX_NAME "sem_srvmtx"
#define POS_NAME "sem_pos"

void *ptr;
sem_t *pos;
sem_t *full;
sem_t *empty;
sem_t *server_mutex;

struct request {
	char file_path[1025];
	char keyword[257];
};

struct request *create_request(char *file_path, char *keyword) {
	struct request *req = malloc(sizeof(struct request));
	strncpy(&req->file_path,file_path,sizeof(req->file_path)-1);
	strncpy(&req->keyword,keyword,sizeof(req->keyword)-1);
	return req;
}

void add_request(struct request *req, int queueSize) {	
	sem_wait(empty);
	sem_wait(server_mutex);
	
	int posVal;
	sem_getvalue(pos,&posVal);
	
	sprintf(ptr+(posVal*sizeof(struct request)),"%s",&req->file_path);
	sprintf(ptr+(posVal*sizeof(struct request))+sizeof(req->file_path),"%s",&req->keyword);

	sem_post(pos);
	if (posVal+1 == queueSize) {
		for (int i=0; i<posVal+1; i++) {
			sem_wait(pos);
		}
	}
	
	sem_post(server_mutex);
	sem_post(full);
}

int main(int argc, char *argv[]) {
	const int queueSize = atoi(argv[1]);
	const int SIZE = sizeof(struct request)*queueSize;
	int fd;
	
	full = sem_open(FULL_NAME, O_CREAT, 0660, 0);
	empty = sem_open(EMPTY_NAME, O_CREAT, 0660, queueSize);
	server_mutex = sem_open(SRVMTX_NAME, O_CREAT, 0660, 1);
	pos = sem_open(POS_NAME, O_CREAT, 0660, 0);
	
	FILE *file;
	file = fopen(argv[2], "r");
	
	fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
	ftruncate(fd, SIZE);
	ptr = mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	char currline[1024];
	char *token;
	char *directoryPath;
	char *keyword;
	while(fgets(currline, 1024, file)) {
		char *the_rest = NULL;
		char *templine = strdup(currline);
		int varNum = 1;
		for (token = strtok_r(currline, " \t\n", &the_rest); token != NULL; token = strtok_r(NULL, " \t\n", &the_rest)) {
			if (varNum == 1) {
				directoryPath = strdup(token);
				varNum++;
				if (strcmp(directoryPath,"exit") == 0) {
					varNum = 1;
					keyword = strdup(directoryPath);
					break;
				}
			} else {
				keyword = strdup(token);
				varNum = 1;
			}
		}
		struct request *req = create_request(directoryPath, keyword);
		add_request(req, queueSize);
		free(req);
		free(directoryPath);
		free(keyword);
		free(templine);
	}

	fclose(file);
	
	sem_unlink(POS_NAME);
	sem_unlink(FULL_NAME);
	sem_unlink(EMPTY_NAME);
	sem_unlink(SRVMTX_NAME);
	
	return 0;
}
