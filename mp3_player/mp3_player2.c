/*
 * MP3播放器控制程序
 * 功能：
 * 		1.播放，暂停
 * 		2.停止
 * 		3.上一首
 * 		4.下一首
 * 附加：歌曲自动循环播放（可以改变播放模式）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/select.h>
#include <errno.h>
#include <time.h>

/*双向循环链表，存储歌曲名*/
typedef struct _node {
	char name[256];
	struct _node* prev;
	struct _node* next;
} Node;

pid_t grandchild_pid;/*孙子进程ID号*/
pid_t child_pid;/*子进程ID号*/
int shmid;/*共享内存描述标记*/
char *p_addr;
int first_key = 1;/*播放标记*/
int play_flag = 0;

void list_insert(Node** phead, char *filepath) {
	Node* p = malloc(sizeof(Node));
	strcpy(p->name, filepath);

	if (*phead == NULL) {
		p->prev = p;
		p->next = p;
		*phead = p;
		return;
	}

	p->prev = (*phead)->prev;
	p->next = *phead;
	(*phead)->prev->next = p;
	(*phead)->prev = p;
}

void list_travel(Node* head) {
	if (head == NULL)
		return;
	Node *p = head;
	Node *curr = NULL;
	memcpy(&curr, p_addr + 4, 4);
	do {
		if (curr == p)
			printf("[playing]");
		printf("%s\n", p->name);
		p = p->next;
	} while (p != head);
}

Node* create_song_list() {
	Node *head = NULL;
	char *path = "/mp3/song/";
	DIR* pdir = opendir(path);
	if (pdir != NULL) {
		struct dirent* pdirent = NULL;
		while ((pdirent = readdir(pdir)) != NULL) {
			if (strncmp(".", pdirent->d_name, 1) == 0)
				continue;
			//printf("%s\n",pdirent->d_name);
			char all_path[256] = { '\0' };
			sprintf(all_path, "%s%s", path, pdirent->d_name);
			list_insert(&head, all_path);
		}
		closedir(pdir);
	}
	return head;
}

Node* get_at(Node* head, int n) {
	Node* p = head;
	int i = 0;
	do {
		if (i == n)
			return p;
		p = p->next;
		++i;
	} while (p != head);
	return p;
}

void wp_play(Node* curr) {
	while (curr) {
		/*创建子进程,即孙子进程*/
		pid_t pid = fork();
		if (-1 == pid) {
			perror("fork");
			exit(1);
		} else if (0 == pid) {
			printf("this song is %s\n", curr->name);
			execlp("madplay", "madplay", curr->name, NULL);
		} else {
			/*内存映射*/
			char* c_addr = shmat(shmid, NULL, 0);
			/*把孙子进程的ID和当前播放歌曲的节点指针传入共享内存*/
			memcpy(c_addr, &pid, 4);
			memcpy(c_addr + 4, &curr, 4);
			int play_mode = 0;
			memcpy(&play_mode, c_addr + 8, 4);
			printf("play_mode=%d\n", play_mode);
			/*使用wait阻塞孙子进程，直到孙子进程播放完才能被唤醒；
			 当被唤醒时表示播放MP3期间没有按键按下，则继续顺序播放下一首MP3*/
			if (pid == wait(NULL)) {
				switch (play_mode) {
				case 1:
					//curr = curr;单曲循环
					break;
				case 2:
					curr = curr->next;//顺序循环播放
					break;
				case 3:
					srand(time(NULL));
					curr = get_at(curr, rand() % 5);//随机播放
					break;
				default:
					break;
				}
				printf("the next song is %s\n", curr->name);
			}
		}
	}
}

void wp_startplay(Node* head) {
	/*创建子进程*/
	pid_t pid = fork();
	if (pid > 0) {
		child_pid = pid;
		play_flag = 1;
		sleep(1);
		/*把孙子进程的pid传给父进程*/
		memcpy(&grandchild_pid, p_addr, 4);
	} else if (0 == pid) {
		/*子进程播放MP3函数*/
		wp_play(head);
	}
}

void wp_pause() {
	printf("pause!press 1 to continue\n");
	kill(grandchild_pid, SIGSTOP);
	play_flag = 0;
}

void wp_continue() {
	printf("continue!press 1 to pause\n");
	kill(grandchild_pid, SIGCONT);
	play_flag = 1;
}

void wp_stop() {
	printf("stop!press 1 to start\n");
	/*杀死当前歌曲播放的子进程，孙子进程*/
	kill(child_pid, SIGKILL);
	kill(grandchild_pid, SIGKILL);
	wait(NULL);
	first_key = 1;
}

void wp_next() {
	Node *nextsong;
	printf("next mp3\n");
	/*从共享内存获得孙子进程播放歌曲的节点指针*/
	memcpy(&nextsong, p_addr + 4, 4);
	/*指向下首歌曲的节点*/
	nextsong = nextsong->next;
	wp_stop();
	wp_startplay(nextsong);
}

void wp_prev() {
	Node *prevsong;
	printf("prev mp3\n");
	memcpy(&prevsong, p_addr + 4, 4);
	prevsong = prevsong->prev;
	wp_stop();
	wp_startplay(prevsong);
}

void wp_start(Node* head) {
	if (first_key) {
		wp_startplay(head);
		first_key = 0;
		play_flag = 1;
	} else {
		if (play_flag)
			wp_pause();
		else
			wp_continue();
	}
}

void wp_chmod() {
	printf("\tenter your choice(1~3):");
	int i;
	scanf("%d", &i);
	//play_mode = i;
	memcpy(p_addr + 8, &i, 4);

}

int showMenu() {
	printf("play/pause ------------->1\n");
	printf("stop       ------------->2\n");
	printf("prev       ------------->3\n");
	printf("next       ------------->4\n");
	printf("chmd       ------------->5\n");
	printf("exit       ------------->0\n");
	printf("\tenter your choice:");
	int c;
	while (scanf("%d", &c) != 1) {
		scanf("%*[^\n]");
		printf("\tenter your choice:");
	}
	return c;
}

void init_shm() {
	shmid = shmget(0x1234, 12, IPC_CREAT | 0600);
	if (shmid != -1) {
		printf("shmget shmid=%d\n", shmid);
	}
	p_addr = shmat(shmid, NULL, 0);
	memset(p_addr, '\0', 8);
	int i = 2;
	memcpy(p_addr + 8, &i, 4);
}

int main() {
	init_shm();
	Node *head = create_song_list();//create play list
	int i;
	do {
		printf("============================\n");
		list_travel(head);
		printf("============================\n");
		i = showMenu();
		switch (i) {
		case 1:
			wp_start(head);
			break;
		case 2:
			wp_stop();
			break;
		case 3:
			wp_prev();
			break;
		case 4:
			wp_next();
			break;
		case 5:
			wp_chmod();
		default:
			break;
		}
	} while (i != 0);
	return 0;
}
