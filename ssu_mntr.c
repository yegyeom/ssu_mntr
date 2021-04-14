#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <syslog.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>

#define SECOND_TO_MICRO 1000000
#define BUFFER_SIZE 1024

struct fileinfo{ 
	char f_name[BUFFER_SIZE];
	time_t m_time;
};

typedef struct fileinfo finfo;

struct timeval begin_t, end_t;
int daemon_init(void);
void ssu_runtime();
void promptmake(char* dirname);
void do_delete(char* filename);
void do_help();
int check_time(char* input_date, char* input_time);
void check_info_size();
void check_info_time();
void check_files_dir(char* filename);
void remove_dir_files(char *wd);
void do_recover(char* filename);
void do_tree(char* wd, int level);
void ssu_checkfile(finfo *file_info1, int num1, time_t* beforemtime);
int daemon_dir(char* wd, finfo *file_info, long* recenttime);
char cur_dir[BUFFER_SIZE];
char new_dir[BUFFER_SIZE];
char trash_dir[BUFFER_SIZE];
char files_dir[BUFFER_SIZE];
char info_dir[BUFFER_SIZE];
int log_fd;
int daemon_num;
long file_size;

int main(){
	pid_t daemonpid;
	char temp_path[BUFFER_SIZE] = { 0 };
	
	gettimeofday(&begin_t, NULL); //시간 측정 시작

	openlog("ssu_mntr", LOG_PID, LOG_LPR);

	getcwd(cur_dir, BUFFER_SIZE); //현재경로 확인
	
	sprintf(new_dir, "%s/%s", cur_dir, "check"); 
	if(access(new_dir,F_OK) < 0) 
		mkdir(new_dir, 0755); //모니터링 할 디렉토리 생성
	
	sprintf(trash_dir, "%s/%s", cur_dir, "trash"); //trash 디렉토리 생성
	if(access(trash_dir,F_OK) < 0)
		mkdir(trash_dir, 0755);

	sprintf(files_dir, "%s/%s/%s", cur_dir, "trash", "files"); //trash하위에 존재하는 files 디렉토리 생성
	if(access(files_dir, F_OK) < 0)
		mkdir(files_dir, 0755);

	sprintf(info_dir, "%s/%s/%s", cur_dir, "trash", "info"); //trash하위에 존재하는 info 디렉토리 생성
	if(access(info_dir, F_OK) < 0)
		mkdir(info_dir, 0755);

	if(log_fd < 0)
		syslog(LOG_ERR, "open error for %s\n", temp_path);
	
	if((daemonpid = fork()) < 0){
		fprintf(stderr, "fork error\n");
		exit(1);
	}
	else if(daemonpid == 0){
		if(daemon_init() < 0){ //데몬 프로세스 실행
			fprintf(stderr, "daemon_init failed\n");
			exit(1);
		}
	}

	promptmake(new_dir); //프롬프트 띄우고 입력 받음

	//gettimeofday(&end_t, NULL); //시작 측정 끝
	//ssu_runtime();
}

void ssu_runtime() //tv_sec는 초 tv_usec는 마이크로초를 저장
{
	end_t.tv_sec -= begin_t.tv_sec; 

	if(end_t.tv_usec < begin_t.tv_usec){ //시작 시간의 수가 더 클 때
		end_t.tv_sec--; 
		end_t.tv_usec += SECOND_TO_MICRO; 
	}
	//실행 시간 계산
	end_t.tv_usec -= begin_t.tv_usec; 
	printf("Runtime: %ld:%06ld(sec:usec)\n", end_t.tv_sec, end_t.tv_usec); //runtime 초단위: 마이크로초 단위로 출력
}

int daemon_init(void){ //디몬 프로세스 만들기
	pid_t pid;
	DIR* dp;
	time_t intertime;
	int maxfd;
	struct stat stat1;
	struct stat stat2;
	int nitems1, nitems2;
	finfo file_info1[BUFFER_SIZE];
	finfo file_info2[BUFFER_SIZE];

	
	if((pid = fork()) < 0){
		fprintf(stderr, "fork error\n");
		exit(0);
	}
	else if(pid != 0) //부모 프로세스 죽음
		exit(0);

	pid = getpid(); 
	setsid(); //새로운 세션 id
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTSTP, SIG_IGN); 
	//터미널 입출력 시그널 무시
	maxfd = getdtablesize(); 

	chdir(new_dir);

	if((dp = opendir("./")) == NULL){ //모니터링 할 디렉토리 open
		fprintf(stderr, "opendir error\n");
		exit(1);
	}

	rewinddir(dp); 

	while(1){
		chdir(cur_dir);
		
		stat("check", &stat1); //check 디렉토리의 정보 stat1 구조체로 받음
		
		daemon_num = 0; //파일 갯수를 셀 변수 초기화
		nitems1=0;
		nitems1 = daemon_dir(new_dir, file_info1, &stat1.st_mtime);
		
		sleep(1);

		chdir(cur_dir);		
		
		stat("check", &stat2); //check 디렉토리의 정보 stat2 구조체로 받음
		
		daemon_num = 0; //파일 갯수를 셀 변수 초기화
		nitems2=0;
		nitems2 = daemon_dir(new_dir, file_info2, &stat2.st_mtime); //지정 디렉토리의 총 파일 개수 리턴하고 구조체에 정보 저장
		
		if(stat1.st_mtime != stat2.st_mtime){ //파일이 생성, 삭제, 수정되었을 경우
			ssu_checkfile(file_info1, nitems1, &stat1.st_mtime); //변경 사항 확인하고 log.txt에 기록
		} 
	}
	
	return 0;
}

int daemon_dir(char *wd, finfo *file_info, long* recenttime){
	struct dirent **items;
	struct stat stat2;
	int num=0;
	char *buf;

	chdir(wd); //check 디렉토리로 이동
	
	num = scandir(".", &items, NULL, alphasort); //check 디렉토리에 존재하는 파일 및 디렉토리 전체 목록 조회
	
	for(int i = 0 ; i < num ; i++){ 
		if(!strcmp(items[i]->d_name, ".") || !strcmp(items[i]->d_name, "..")){ //현재 디렉토리, 이전 디렉토리 무시
			continue;
		}
		stat(items[i]->d_name, &stat2); //각 파일마다 stat2 구조체에 정보 저장
		strcpy(file_info[daemon_num].f_name, items[i]->d_name); //각 파일 이름을 file_info 구조체에 저장
		file_info[daemon_num].m_time = stat2.st_mtime; //각 파일 mtime을 file_info 구조체에 저장
		
		daemon_num++;

		if(S_ISDIR(stat2.st_mode)){ //디렉토리가 존재한다면 함수 재귀호출
			daemon_dir(items[i]->d_name, file_info,recenttime);
		}
		if(*recenttime < stat2.st_mtime) //mtime이 가장 큰 파일이 가장 최근에 수정된 파일
			*recenttime = stat2.st_mtime;

	}
	chdir(".."); //이전 디렉토리로 이등
	return daemon_num; //총 파일 갯수 리턴
}

void ssu_checkfile(finfo *file_info1, int num1, time_t* beforetime){ //log.txt에 변경사항 추가
	FILE *fp;
	char timebuf[80];
	struct tm *t;
	int num2;
	time_t timer;
	struct stat stat2, stat3;
	finfo file_info2[BUFFER_SIZE] = {"",0};
	
	chdir(cur_dir); 

	if((fp = fopen("log.txt", "a+")) == NULL){ //log.txt 오픈, 없다면 생성
		fprintf(stderr, "fopen error\n");
		return;
	}

	chdir(new_dir); //지정 디렉토리로 이동

	daemon_num = 0; //파일 갯수 변수 초기화

	num2 = daemon_dir(new_dir, file_info2, &stat2.st_mtime); //지정 디렉토리의 총 파일 개수 리턴하고 구조체에 정보 저장

	if(num1 > num2){ //파일이 삭제 되었을 경우
		for(int i = 0 ; i < num1 ; i++){
			if(i==num1-1){ //마지막 순서의 파일이 삭제 된 경우
				time(&timer);
				t=localtime(&timer);
				strftime(timebuf, 80, "%F %H:%M:%S", t);
				fprintf(fp, "[%s][delete_%s]\n", timebuf, file_info1[i].f_name); //파일이름과 삭제시간 포함하여 파일에 기록
				break;
			}

			if((!strcmp(file_info2[i].f_name, ".")) || (!strcmp(file_info2[i].f_name, ".."))){ //현재 디렉토리와 이전 디렉토리는 무시
				continue;
			}

			if(!strcmp(file_info1[i].f_name, file_info2[i].f_name)) //각 구조체에서 파일들의 순서가 같으면 삭제 파일 아님
				continue;
			else{ //순서가 달라졌을 때 file_info1의 파일이 삭제 된 것
				time(&timer);
				t=localtime(&timer);
				strftime(timebuf, 80, "%F %H:%M:%S", t);
				fprintf(fp, "[%s][delete_%s]\n", timebuf, file_info1[i].f_name); //파일이름과 삭제시간 포함하여 파일에 기록
				
				break;
			}
		}
	}
	else if(num1 < num2){ //파일이 생성 되었을 경우
		for(int i = 0 ; i < num2 ; i++){
			if(i==num2-1){ //마지막 순서의 파일이 삭제 된 경우
				time(&timer);
				t=localtime(&timer);
				strftime(timebuf, 80, "%F %H:%M:%S", t);
				fprintf(fp, "[%s][create_%s]\n", timebuf, file_info2[i].f_name); //파일이름과 삭제시간 포함하여 파일에 기록

				break;
			}

			if((!strcmp(file_info2[i].f_name, ".")) || (!strcmp(file_info2[i].f_name, ".."))){ //현재 디렉토리와 이전 디렉토리는 무시
				continue;
			}

			if(!strcmp(file_info1[i].f_name, file_info2[i].f_name)) //각 구조체에서 파일들의 순서가 같으면 삭제 파일 아님
				continue;
			else{ //순서가 달라졌을 때 file_info2의 파일이 삭제 된 것
				stat(file_info2[i].f_name, &stat3); //파일의 정보를 stat3 구조체에 저장
				t=localtime(&file_info2[i].m_time);
				strftime(timebuf, 80, "%F %H:%M:%S", t);
				fprintf(fp, "[%s][create_%s]\n", timebuf, file_info2[i].f_name); //파일이름과 생성시간 포함하여 파일에 기록
				
				break;
			}
		}
	}
	else if(num1==num2){ //파일이 수정 되었을 경우
		for(int i = 0 ; i < num2 ; i++){
			stat(file_info2[i].f_name, &stat3); //각 파일의 정보를 stat3 구조체에 저장

			if(file_info2[i].m_time != file_info1[i].m_time){ //각 구조체에서 파일들의 mtime이 다른 경우가 있으면 파일이 수정된 것
				t=localtime(&file_info2[i].m_time);
				strftime(timebuf,80,"%F %H:%M:%S", t);
				fprintf(fp, "[%s][modify_%s]\n", timebuf, file_info2[i].f_name); //파일이름과 수정시간 포함하여 파일에 기록
				
				break;
			}
		}
	}

	fclose(fp);
}

void promptmake(char* dirname){ //프롬프트 띄우고 입력받음
	char path[BUFFER_SIZE];
	char* separate;
	char* command[5]; //입력 문장 자른거
	char* logfile;
	char* ptr;
	char *buf;
	int count=0, sec;
	pid_t pid;

	buf = (char*)malloc(sizeof(char)*500);
	logfile = (char*)malloc(sizeof(char)*255);

	char order[BUFFER_SIZE];
	
	while(1){ //프롬프트 출력
		memset(order, 0, 500);
		count = 0;
		printf("20182595>"); 
		fgets(order, 500, stdin); //표준입력으로 명령 받음
		order[strlen(order)-1] = 0; //개행문자 제거

		if(!strncmp(order, "\0", 1)){
			continue;
		}
		if(!strcmp(order, "exit")){ //exit을 입력 받았을 때
			printf("모니터링 종료\n");
			gettimeofday(&end_t, NULL);
			ssu_runtime();
			exit(1);
		}

		memcpy(buf, order, sizeof(order)); //명령으로 받은 문자열 복사
		ptr = strtok(buf, " "); //공백 기준으로 문자열 자름
		
		for(int i = 0 ; ptr != NULL ; i++){ //문자열의 끝까지 공백을 기준으로 명령어 모두 나눔
			command[i] = ptr;
			ptr = strtok(NULL, " ");
			count++;
		}

		if((strcmp(command[0], "delete")) && (strcmp(command[0], "size")) && (strcmp(command[0], "recover")) && (strcmp(command[0], "tree")) && (strcmp(command[0], "exit")) && (strcmp(command[0], "help"))){ //주어진 명령어 아닐 경우 help()
			do_help();
			continue;
		} 

		if(!strcmp(command[0], "delete")){ //delete 명령어
			if(!strcmp(command[1], "\0")){
				fprintf(stderr, "파일 이름을 입력해주세요\n");
				continue;
			}
			
			pid = fork();

			if(pid == 0){ //자식 프로세스
				if(count == 2){ //endtime 없이 filename만 입력한 경우
					do_delete(command[1]);
				}
				else{ //filename과 endtime 함께 입력한 경우
					sec = check_time(command[2], command[3]);
					sleep(sec); //주어진 예정 시간까지의 시간
					do_delete(command[1]);
				}
				exit(0);
			}
			else if(pid > 0){ //부모 프로세스
				continue;
			}
		}

		if(!strcmp(command[0], "recover")){ //recover 명령어
			do_recover(command[1]);
		}

		if(!strcmp(command[0], "tree")){ //tree 명령어
			printf("check");
			do_tree(new_dir,1);
		}

		if(!strcmp(command[0], "help")){ //help 명령어
			do_help();
			continue;
		}

	}
}

int check_time(char* input_date, char* input_time){ //delete 명령어에서 endtime이 입력되었을 경우 예약시간까지의 시간 계산
	time_t deltime, nowtime;
	struct tm* dtime;
	struct tm* ntime;
	char* ptr_date;
	char* sdate[3];
	char* ptr_time;
	char* stime[2];
	
	ptr_date = strtok(input_date, "-"); 
	for(int i = 0 ; ptr_date != NULL ; i++){ 
		sdate[i] = ptr_date;
		ptr_date = strtok(NULL, "-");
	}
//입력받은 날짜 - 기준으로 자름
	ptr_time = strtok(input_time, ":");
	for(int j = 0 ; ptr_time != NULL ; j++){
		stime[j] = ptr_time;
		ptr_time = strtok(NULL, ":");
	}		
//입력받은 시간 : 기준으로 자름
	time(&deltime);
	dtime = localtime(&deltime);
	dtime->tm_year = atoi(sdate[0]) - 1900;
	dtime->tm_mon = atoi(sdate[1]) - 1;
	dtime->tm_mday = atoi(sdate[2]);
	dtime->tm_hour = atoi(stime[0]);
	dtime->tm_min = atoi(stime[1]);
//tm 구조체 설정하여 채움
	mktime(dtime);

	int dsecond = (dtime->tm_mday)*24*60*60 + (dtime->tm_hour)*60*60 + (dtime->tm_min)*60 + (dtime->tm_sec);
	//채워진 값으로 계산해서 현재까지 몇초인지 리턴받음

	time(&nowtime);
	ntime = localtime(&nowtime);
	mktime(ntime);
	
	int nsecond = (ntime->tm_mday)*24*60*60 + (ntime->tm_hour)*60*60 + (ntime->tm_min)*60 + (ntime->tm_sec);
	//현재 시각을 기준으로 총 몇초인지 리턴받음
	int sec = dsecond - nsecond; //삭제 예정 시간까지 남은 시간
	
	if(sec < 0){ //음수일 경우 과거시간이므로 에러처리
		fprintf(stderr, "삭제 예정 시간이 올바르지 않습니다\n");
	}

	return sec;
}

void do_delete(char* filename){ //지정한 삭제 시간에 자동으로 파일을 삭제해주는 명령어
	char a_path[BUFFER_SIZE]; //절대 경로
	char d_path[BUFFER_SIZE];
	char new_d_path[BUFFER_SIZE];
	char info_path[BUFFER_SIZE]; //절대 경로
	char info_name[BUFFER_SIZE]; //파일 이름
	char new_info_name[BUFFER_SIZE], new_info_name2[BUFFER_SIZE];
	char* ptr;
	char* ptr_date;
	char* sdate[3];
	char* ptr_time;
	char* stime[3];
	FILE* fp;
	struct stat buf;
	char timebuf1[80], timebuf2[80];
	struct tm *t1;
	struct tm *t2;
	time_t timer;
	int num=0;
	
	chdir(new_dir); //지정 디렉토리로 이동

	stat(filename, &buf); //입력받은 파일의 정보를 buf 구조체에 저장
	t1 = localtime(&buf.st_mtime); //최종 수정 시간을 스트링으로 변환
	strftime(timebuf1, 80, "%F %H:%M:%S", t1);
	
	time(&timer);
	t2 = localtime(&timer);
	strftime(timebuf2, 80, "%F %H:%M:%S", t2); //현재 시간을 스트링으로 변환


	if(filename[0] == '/') { //입력받은 파일명이 절대경로일 때
		ptr = strrchr(filename, '/'); //파일명에서 가장 마지막으로 나오는 /를 가르킴 => ptr+1은 파일명만 존재
		sprintf(d_path, "%s/%s", files_dir, ptr+1); //files 경로로 변환
		strcpy(a_path, filename);
		strcpy(info_path, filename); //절대경로
		strcpy(info_name, ptr+1); //파일이름
	}
	else if(filename[0] == '.'){ //입력받은 파일명이 상대 경로일 때
		if(realpath(filename, a_path) == NULL){ //상대 경로로 입력 받은 파일을 절대 경로로 변환
			fprintf(stderr, "존재하지 않는 파일입니다.\n");
			return;
		}
		ptr = strrchr(a_path, '/');
		sprintf(d_path, "%s/%s", files_dir, ptr+1); //files 경로로 변환
		strcpy(info_path, a_path); //절대경로
		strcpy(info_name, ptr+1); //파일이름
	}
	else{ //파일명만 입력받았을 때
		strcpy(info_name, filename); //파일이름
		if(realpath(filename, a_path) == NULL){ //입력받았던 파일명을 절대 경로로 변환
			fprintf(stderr, "존재하지 않는 파일입니다.\n");
			return;
		}
		strcpy(info_path, a_path); //절대경로
		sprintf(d_path, "%s/%s", files_dir, filename); //files 경로로 변환
	}
	
	chdir(files_dir); //files 디렉토리로 이동
	if(access(info_name, F_OK) == 0){ //입력한 이름의 파일이 이미 존재하면
		strcpy(new_info_name, info_name);
		num = 0;
		while(access(new_info_name, F_OK) == 0){
			num++;
			sprintf(new_info_name, "%d%s%s", num, "_", info_name); //파일명 앞에 숫자_ 를 붙여서 구분함
		}
		sprintf(new_d_path, "%s/%s", files_dir, new_info_name); //숫자를 붙였을 때 새로운 files 경로
		if(rename(a_path, new_d_path) < 0){ //files 디렉토리로 이동 완료 (delete)
			fprintf(stderr, "존재하지 않는 파일입니다.\n");
			return;
		}
	}
	else //입력한 이름의 파일이 files에 존재하지 않을 때
		if(rename(a_path, d_path) < 0){ //files 디렉토리로 이동 완료 (delete)
			fprintf(stderr, "존재하지 않는 파일입니다.\n");
			return;
		}

	//*****info파일 작성*****
	chdir(info_dir); //info 디렉토리로 이동
	char str[] = "[Trash info]\n";
	
	if(access(info_name, F_OK) == 0){ //입력한 이름의 파일이 info 디렉토리에 이미 존재하면
		strcpy(new_info_name2, info_name);
		num = 0;
		while(access(new_info_name2, F_OK) == 0){
			num++;
			sprintf(new_info_name2, "%d%s%s", num, "_", info_name); //파일명 앞에 숫자_를 붙여서 구분함
		}
		fp = fopen(new_info_name2, "a+"); //info파일 오픈, 없으면 생성
	}
	else //입력한 이름의 파일이 info에 존재하지 않을 때
		fp = fopen(info_name, "a+");
	
	fputs(str, fp); 
	fputs(info_path, fp);
	//파일에 [Trash info] 와 절대 경로 씀
	fprintf(fp, "\nD : %s\n", timebuf2); //삭제 시간 씀
	fprintf(fp, "M : %s\n", timebuf1); //최종 수정 씀
	fclose(fp);
	
	check_info_size(); //info디렉토리의 크기 검사
	
	if(file_size > 2000){ //info 디렉토리의 크기가 2KB를 초과할 경우
		while(file_size > 2000){ //디렉토리 크기가 2KB를 초과할 동안 가장 오래된 파일부터 삭제
			check_info_time();
		}
	}
}

void check_info_size(){ //info 디렉토리의 크기를 검사
	struct dirent **filelist;
	int num;
	char buf[BUFFER_SIZE];
	struct stat fstatbuf;
	
	file_size = 0;
	num = scandir(".", &filelist, NULL, alphasort); //info 디렉토리의 모든 파일 조회하고 갯수 리턴

	for (int i = 0 ; i < num ; i++){

		if((!strcmp(filelist[i]->d_name, ".")) || (!strcmp(filelist[i]->d_name, ".."))){ //현재디렉토리, 이전 디렉토리는 무시
			continue;
		}
		
		realpath(filelist[i]->d_name, buf); //각 파일마다 절대경로로 변환
		stat(buf, &fstatbuf); //buf의 정보를 fstatbuf 구조체에 저장

	//	printf("%s\t%ld\n", filelist[i]->d_name, fstatbuf.st_size); 
		file_size += fstatbuf.st_size; //파일의 사이즈를 모두 저장 => info 디렉토리의 크기
	}

}

void check_info_time(){
	struct dirent **filelist;
	int num,count=0;
	long check;
	int delnum;
	char *del_filename = (char*)malloc(sizeof(char)*500);

	num = scandir(".", &filelist, NULL, alphasort); //info 디렉토리의 모든 파일 조회하고 갯수 리턴

	for (int i = 0 ; i < num ; i++){
		struct stat fstatbuf;

		if((!strcmp(filelist[i]->d_name, ".")) || (!strcmp(filelist[i]->d_name, ".."))){ //현재 디렉토리, 이전 디렉토리는 무시
			continue;
		}
		
		count++;

		lstat(filelist[i]->d_name, &fstatbuf); //각 파일마다 절대경로로 변환
		
	//	printf("%s\t%ld\n", filelist[i]->d_name, fstatbuf.st_mtime);

		if(count==1){ //첫번째 파일일 경우
			check = fstatbuf.st_mtime; //구조체에 저장되어 있는 최종 수정 시간을 check 변수에 저장
			delnum = i;
		}
		else if(count>1){ //첫번째 파일이 아닐 경우
			if(check > fstatbuf.st_mtime){ //모든 파일마다 구조체에 저장된 최종 수정 시간을 비교함
				check = fstatbuf.st_mtime;
				delnum = i;
			}//최종 수정 시간이 가장 작은 파일을 check, delnum 변수에 기록 => 가장 오래 된 파일
		}
	}		
	
	strcpy(del_filename, filelist[delnum]->d_name); //삭제할 파일명
	remove(filelist[delnum]->d_name); //info 디렉토리에서 해당 파일 삭제
	check_info_size(info_dir); //info 디렉토리 크기 확인

	chdir(files_dir); //files 디렉토리로 이동
	remove(filelist[delnum]->d_name); //해당 파일 원본 파일도 삭제
	
	//* 이 때 해당파일이 하위 파일이 존재하는 디렉토리라면 윗 코드에서 remove 안 될 것 *
	
	check_files_dir(del_filename);
	chdir(info_dir);
}

void check_files_dir(char *filename){
	//files 디렉토리
	struct dirent **filelist;
	struct stat fstatbuf;
	int num;

	num = scandir(".", &filelist, NULL, alphasort); //해당 디렉토리에 존재하는 파일 및 디렉토리 전체 목록 조회하고 갯수 리턴

	for(int k = 0 ; k < num ; k++){

		if(!strcmp(filelist[k]->d_name, filename)){ //파일 있는 디렉토리 발견

			lstat(filelist[k]->d_name, &fstatbuf); //정보를 fstatbuf 구조체에 저장

			if(S_ISDIR(fstatbuf.st_mode)){ //디렉토리일때
				remove_dir_files(filelist[k]->d_name); //디렉토리의 내부 파일 삭제 후 디렉토리 삭제
			}
			break;
		}
	}
}

void remove_dir_files(char *wd){ //디렉토리 내부의 파일 삭제하는 함수
	struct dirent **filelist;
	struct stat fstatbuf;
	int num;

	if(chdir(wd) < 0){ //삭제할 파일이 있는 디렉토리로 이동
		fprintf(stderr, "chdir error\n");
		return;
	}

	num = scandir(".", &filelist, NULL, alphasort); //해당 디렉토리에 존재하는 파일 및 디렉토리 전체 목록 조회하고 갯수 리턴

	for(int k = 0 ; k < num ; k++){
		remove(filelist[k]->d_name); //파일 삭제

		if(S_ISDIR(fstatbuf.st_mode)){ //디렉토리일때 함수 재귀호출
			remove_dir_files(filelist[k]->d_name);
		}
	}
	chdir("..");
	rmdir(wd); //디렉토리도 삭제
}

void do_recover(char* filename){ //trash 디렉토리 안에 있는 파일을 원래 경로로 복구하는 명령어
	FILE *fp;
	int num=0, number=0, input;
	char* ptr;
	char* p;  
	char str[] = "[Trash info]\n"; 
	char sfilename[BUFFER_SIZE]; 
	char path[BUFFER_SIZE], d_path[BUFFER_SIZE], a_path[BUFFER_SIZE], s_path[BUFFER_SIZE];
	char new_d_path[BUFFER_SIZE], new_s_path[BUFFER_SIZE], new_sfilename[BUFFER_SIZE];
	char namebuf[BUFFER_SIZE]; 
	char check[BUFFER_SIZE]; 
	char slicepath[BUFFER_SIZE]; 
	char** buffer = (char**)malloc(100*BUFFER_SIZE);
	char** filelist = (char**)malloc(100*BUFFER_SIZE);
	char** s_filelist = (char**)malloc(100*BUFFER_SIZE);
	char** infolist1 = (char**)malloc(100*BUFFER_SIZE); 
	char** infolist2 = (char**)malloc(100*BUFFER_SIZE);
	char** pathlist = (char**)malloc(100*BUFFER_SIZE); 
	char pathbuf[BUFFER_SIZE]; 

	for(int i=0 ; i<100;i++){
		buffer[i]=(char*)malloc(BUFFER_SIZE*sizeof(char));
		filelist[i]=(char*)malloc(BUFFER_SIZE*sizeof(char)); 
		s_filelist[i] = (char*)malloc(BUFFER_SIZE*sizeof(char)); 
		infolist1[i]=(char*)malloc(BUFFER_SIZE*sizeof(char));
		infolist2[i]=(char*)malloc(BUFFER_SIZE*sizeof(char));
		pathlist[i]=(char*)malloc(BUFFER_SIZE*sizeof(char)); 
	}
	
	chdir(files_dir); //files 디렉토리로 이동
	
	struct dirent **items;
	int fnum; //info디렉토리의 파일 갯수
	struct stat fstat;

	getcwd(path, BUFFER_SIZE); //현재경로
	sprintf(d_path, "%s/%s", path, filename); //현재경로에 파일이름
	
	chdir(info_dir); //info 디렉토리로 이동
	fnum = scandir(".", &items, NULL, alphasort); //info디렉토리에 존재하는 파일 전체 목록 조회하고 갯수 리턴
	
	for(int i = 0 ; i < fnum ; i++){ //info 디렉토리의 모든 파일 돌면서
		struct stat fstat; //각 파일의 정보를 fstat에 저장

		if((!strcmp(items[i]->d_name, ".")) || (!strcmp(items[i]->d_name, ".."))){ //현재 디렉토리와 이전 디렉토리 무시
			continue;
		}

		strcpy(filelist[i], items[i]->d_name); //info 디렉토리의 모든 파일은 filelist배열에 복사
		
		fp = fopen(filelist[i], "r"); //각 파일들 읽기 전용으로 오픈
		fseek(fp, 0, SEEK_SET);
		fseek(fp, strlen(str), SEEK_SET); 

		memset(pathbuf,0,BUFFER_SIZE);
		memset(namebuf,0,BUFFER_SIZE);

		fgets(pathbuf, BUFFER_SIZE, fp); //info 파일의 경로 pathbuf로 얻어옴
		pathbuf[strlen(pathbuf) - 1] = '\0'; //개행제거
		p = strrchr(pathbuf, '/'); //파일의 절대 경로에서 마지막 / 를 p가 가르킴 
		sprintf(namebuf, "%s", p+1); //파일이름
		strcpy(s_filelist[i],namebuf); //원래 경로의 파일 이름만 s_filelist에 저장
	}

	for(int i = 0 ; i < fnum ; i++){
		if((!strcmp(items[i]->d_name, ".")) || (!strcmp(items[i]->d_name, ".."))){ //현재 디렉토리와 이전 디렉토리 무시
			continue;
		}
		if(!strcmp(filename, s_filelist[i])){ //같은 이름의 파일이 있으면
			buffer[num] = filelist[i]; //buffer에 해당 파일 이름 입력
			num++;
		}
	}

	if(num == 0){ //trash 디렉토리에 존재하지 않는 파일 입력했을 때
		fprintf(stderr, "존재하지 않는 파일입니다.\n");
		return;
	}

	fclose(fp);

	chdir(info_dir); //info 디렉토리로 이동

	if(num == 1){ //중복 이름이 없을 때
		fp = fopen(buffer[0], "r"); //해당 파일 읽기 전용으로 오픈
		
		fseek(fp, strlen(str), SEEK_SET);
		fgets(a_path, BUFFER_SIZE, fp); //info 파일에 써있는 절대 경로 얻어옴
		a_path[strlen(a_path) - 1] = '\0'; //개행 제거
		
		ptr = strrchr(a_path, '/'); //절대 경로에서 마지막 /를 ptr이 가르킴
		sprintf(sfilename, "%s", ptr+1); //sfilename은 파일이름 (숫자 없이)
		memcpy(slicepath, a_path, strlen(a_path) -strlen(ptr+1)); //절대 경로에서 파일이름을 제외한 경로
		sprintf(new_d_path, "%s/%s", files_dir, buffer[0]); //files 경로에 파일이름
		strcpy(check,buffer[0]); //파일 이름을 check에 복사
		fclose(fp);
	}
	else{ //중복 되는 이름 있을 때
		for(int i = 0 ; i < num ; i++){
			fp = fopen(buffer[i], "r"); //해당파일 읽기 전용으로 오픈
			
			fseek(fp, 0, SEEK_SET);
			fseek(fp, strlen(str), SEEK_SET);
			memset(pathbuf, 0, BUFFER_SIZE);

			fgets(pathbuf, BUFFER_SIZE, fp); //절대 경로 pathbuf 얻어옴
			pathbuf[strlen(pathbuf)-1] = '\0'; //개행제거
			ptr = strrchr(pathbuf, '/'); //절대 경로에서 마지막 /를 ptr이 가르킴
			sprintf(sfilename, "%s", ptr+1); //sfilename은 파일이름 (숫자 없이)
			
			memcpy(slicepath, pathbuf, strlen(pathbuf) - strlen(ptr+1)); //절대 경로에서 파일이름을 제외한 경로
			strcpy(pathlist[i], pathbuf); // pathlist는 원래 경로

			fscanf(fp, "%[^\n]\n", infolist1[i]); //D time
			fscanf(fp, "%[^\n]", infolist2[i]); //M time

			fclose(fp);
		}
	
	for(int j = 0 ; j < num ; j++){
		printf("%d. %s\t%s %s\n", j+1, sfilename, infolist1[j], infolist2[j]); //선택문 출력
	}

	while(1){
		printf("Choose : ");
		scanf("%d", &input); //파일 선택
		getchar();

		if((input < 1)||(input>num)){
			printf("올바른 번호를 입력하세요\n");
		}
		else
			break;
		}

		strcpy(check, buffer[input-1]); //파일 이름 check에 복사
		sprintf(new_d_path, "%s/%s", files_dir, buffer[input-1]); //해당파일 현재 경로
	}

	chdir(new_dir); //지정 디렉토리로 이동

	if(access(sfilename, F_OK) == 0){ //check 디렉토리에 이미 동일한 이름이 있으면
		strcpy(new_sfilename, sfilename);
		number=0;
		while(access(new_sfilename, F_OK) == 0){
			number++;
			sprintf(new_sfilename, "%d%s%s", number, "_", sfilename); //파일 이름 앞에 숫자_ 붙임
		}
		sprintf(new_s_path, "%s/%s", slicepath, new_sfilename); //새로운 이름으로 만든 복구할 경로

		if(rename(new_d_path,new_s_path) < 0){ //파일에 숫자 붙혀서 check디렉토리로 복구
			fprintf(stderr, "경로가 올바르지 않습니다.\n");
			return;
		}
	}
	else{ //check디렉토리에 동일한 이름 없으면
		sprintf(s_path, "%s%s", slicepath, sfilename); //복구할 최종 경로
		if(rename(new_d_path, s_path) < 0){ //동일한 이름 없으므로 파일에 숫자 안붙이고 check디렉토리로 복구
			fprintf(stderr, "경로가 올바르지 않습니다.\n");
			return;
		}
	}

	chdir(info_dir); //info 디렉토리로 이동
	remove(check); //info 파일도 제거
}

void do_tree(char* wd,int level){ //check 디렉토리의 구조를 tree 형태로 보여주는 명령어
	struct dirent **items;
	char* ptr;
	char dirname[BUFFER_SIZE];
	int num;
	int count=0;
	
	chdir(wd); //check 디렉토리로 이동
	
	num = scandir(".", &items, NULL, alphasort); //디렉토리에 존재하는 파일 및 디렉토리 전체 목록을 조회
	for(int i = 0 ; i < num ; i ++){
		struct stat fstat; //각 파일의 정보를 fstat 구조체에 저장

		if((!strcmp(items[i]->d_name, ".")) || (!strcmp(items[i]->d_name, ".."))){ //현재 디렉토리와 이전 디렉토리는 무시
			continue;
		}
		count++;
		
		if(count == 1){ //첫번째 파일일 때
			printf("----%s", items[i]->d_name);
		}
		else{
			for(int j = 0 ; j < level ; j++){
				printf("\t");
			}
			printf("|\n");

			for(int j=0; j<level; j++)
				printf("\t");

			printf("--%s", items[i]->d_name);
		}
		
		lstat(items[i]->d_name,&fstat);

		if(S_ISDIR(fstat.st_mode)){ //디렉토리일 경우 레벨을 올려서 재귀 호출
			do_tree(items[i]->d_name,level+1);
			chdir(wd);
		}
		else
			printf("\n");

	}
}

void do_help(){ //명령어 사용법을 출력하는 명령어
	printf("<Usage : ssu_mntr>\n");
	printf("command : \n");
	printf("DELETE		delete files at the specified time\n");
	printf("SIZE		print the file path and file size\n");
	printf("RECOVER		recover deleted files to original path\n");
	printf("TREE		show structure of ""check"" directory in tree form\n");
	printf("EXIT		exit the program\n");
	printf("HELP		printf usage\n");
}

