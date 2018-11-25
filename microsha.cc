#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h> 
#include <sys/stat.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <iostream>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <dirent.h>
#include <algorithm>
#include <signal.h> 
#include <errno.h> 
#include <sys/resource.h>
#include <sys/time.h>

using namespace std;

void metacharacters(string pattern, string dir, vector <string> *arg);
void my_pwd();
void pars_argv(vector <string> s);

void show_vector_string( vector <string> s)
{
	for (vector<string>::iterator it = s.begin() ; it!=s.end() ; ++it){
		cout<<*it<< endl;
	}
	return;
}

int shell_prompt_show(){
	char buf[MAXPATHLEN + 1] = {0};
	
	if(getcwd(buf, MAXPATHLEN) == NULL){
		perror("getcwd");
		return 0;
	}
	int i = 0;
	string newbuf = "";
	while(buf[i] = '/' && buf[i] != '\0'){
		i++;
		newbuf.clear();
		while(buf[i] != '/' && buf[i] != '\0'){
			newbuf +=buf[i];
			i++;
		}
	}
	if(i==1) newbuf = "/";
	printf("[%s]", newbuf.c_str());
	//printf("[%s]", buf);
	uid_t uid=getuid();
	if(uid == 0){
		printf("! ");
	} else {
		printf("> ");
	}
	return 1;
}

string reduce_slash(string buf){
	string newbuf;
	int i = 0;
	while(i < buf.size()){
		newbuf +=buf[i];
		if(buf[i] == '/') while(buf[i] == '/' && i<buf.size())i++;
		else i++;
	}
	return newbuf;
}

int first_command(string s, string *buf){
	vector <string> placeholder;
	int i = 0;
	while(((s[i]==' ') || (s[i] == '\t')) && (s[i] != '\n') && i < s.size()){
		++i;
	}
	while((s[i] != ' ') && (s[i] != '\t') && (s[i] != '\n') && (s[i] != '>') && (s[i] != '<') && (s[i] != '|') && i < s.size()){
		*buf += s[i];
		++i;
	}
	return 0;
}	
int pars(string s, vector <string> *str){
	vector <string> placeholder;
	string buf;
	int size;
	int count_out = 0;
	int count_in = 0;
	int count_pipe = 0;
	for(int i = 0; i < s.size() && s[i] != '\n'; ++i){
		buf = "";
		while(((s[i]==' ') || (s[i] == '\t')) && (s[i] != '\n') && i < s.size()){
			++i;
		}
		while((s[i] != ' ') && (s[i] != '\t') && (s[i] != '\n') && (s[i] != '>') && (s[i] != '<') && (s[i] != '|') && (s[i] != '*') && (s[i] != '?') && i < s.size()){
			buf += s[i];
			++i;
		}
		switch(s[i]){
			case '>':
				if(!count_out && !count_in){
					if(!buf.empty()) placeholder.push_back(buf);
					placeholder.push_back(">");
					++count_out;
				} else {
					fprintf(stderr, "microsha: string parsing error: ");
					if(!count_pipe && !count_out && count_in) fprintf(stderr, "\"<\" can not be with \">\"\n");
					if(!count_pipe && count_out && !count_in) fprintf(stderr, "more than one \">\"\n");
					return 1;
				}
				continue;
				break;
			case '<':
				if(!count_in && !count_out){
					if(!buf.empty()) placeholder.push_back(buf);
					placeholder.push_back("<");
					++count_in;
				} else  {
					fprintf(stderr, "microsha: string parsing error: ");
					if(!count_pipe && count_out && !count_in) fprintf(stderr, "\"<\" can not be with \">\"\n");
					if(!count_pipe && !count_out && count_in) fprintf(stderr, "more than one \"<\"\n");
					return 1;
				}
				continue;
				break;
			case '|':
				if(!buf.empty()) placeholder.push_back(buf);
				placeholder.push_back("|");
				++count_pipe;
				continue;
				break;
			case '*':
				buf+='*';
				++i;
				while((s[i] != ' ') && (s[i] != '\t') && (s[i] != '\n') && (s[i] != '>') && (s[i] != '<') && (s[i] != '|') && i < s.size()){
					buf += s[i];
					++i;
				}
				size = placeholder.size();
				metacharacters(reduce_slash(buf), "", &placeholder);
				if(size == placeholder.size()){
					fprintf(stderr, "microsha: %s: no such file or directory\n", buf.c_str());
					return 1;
				}
				continue;
				break;
			case '?':
				buf+='?';
				++i;
				while((s[i] != ' ') && (s[i] != '\t') && (s[i] != '\n') && (s[i] != '>') && (s[i] != '<') && (s[i] != '|') && i < s.size()){
					buf += s[i];
					++i;
				}
				size = placeholder.size();
				metacharacters(reduce_slash(buf), "", &placeholder);
				if(size == placeholder.size()){
					fprintf(stderr, "microsha: %s: no such file or directory\n", buf.c_str());
					return 1;
				}
				continue;
				break;
		}
		if(buf == "time" && placeholder.size() == 0) continue;
		if(!buf.empty()) placeholder.push_back(buf);
	}
	if(!placeholder.empty()){
		for (vector<string>::iterator it = placeholder.begin() ; it!=placeholder.end() ; ++it){
			str->push_back(*it);
		}
		if((*str)[0] == "pwd" && str->size() == 1) return 2;
	}
	if(count_pipe) return 3;
	return 0;
}	

void my_out (vector <string>  s, string file){
	vector <char *> argv;
	for (vector<string>::iterator it = s.begin() ; it!=s.end() ; ++it){
		argv.push_back((char *)(*it).c_str());
	}
	argv.push_back(NULL);
	
	int fid = open((char *)file.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
	if(fid == -1){
		perror("open");
		return;
	}
	dup2(fid, 1);
	if(argv[0] == "pwd" && argv.size() == 1){
			my_pwd();
			return;
	}
	pid_t pid = fork();
	if(pid == 0){
		execvp(argv[0], &argv[0]);
		fprintf(stderr, "microsha: %s: command not found\n", argv[0]);
		exit(0);
	} 
	int status;
	wait(&status);
	close(fid);
	return;
}

void my_in (vector <string>  s, string file){
	vector <char *> argv;
	for (vector<string>::iterator it = s.begin() ; it!=s.end() ; ++it){
		argv.push_back((char *)(*it).c_str());
	}
	argv.push_back(NULL);
	
	int fid = open((char *)file.c_str(), O_RDWR , 0600);
	if(fid == -1){
		fprintf(stderr, "microsha: %s: no such file or directory\n", (char *)file.c_str());
		return;
	}
	dup2(fid, 0);
	if(argv[0] == "pwd" && argv.size() == 1){
			my_pwd();
			return;
	}
	pid_t pid = fork();
	if(pid == 0){
		execvp(argv[0], &argv[0]);
		fprintf(stderr, "microsha: %s: command not found\n", argv[0]);
		exit(0);
	}
	int status;
	wait(&status);
	close(fid);
	return;
}

void my_pipeline (vector <vector <string> > &arg){
	int i; 
	int count = arg.size();
	//close(2);
	for(i = 0; i < count - 1; i++){
		int fd[2];
		pipe(fd);
		pid_t pid = fork();
		if(pid == 0){
			dup2(fd[1], 1);
			close(fd[0]);
			if(arg[i][0] == "pwd" && arg[i][0].size() == 1){
					my_pwd();
					exit(0);
			}		
			pars_argv(arg[i]);
		}
		dup2(fd[0], 0);
		close(fd[1]);
	}
	if(arg[i][0] == "pwd" && arg[i][0].size() == 1){
			my_pwd();
			exit(0);
	}
	pars_argv(arg[i]);
}

void my_cd(vector <string> s){
	if(s.size() == 1){
		if(chdir(getenv("HOME")) == -1) perror("chdir");
	} else {
		if(chdir(s[1].c_str()) == -1) fprintf(stderr, "microsha: %s: no such directory\n", s[1].c_str());
	}
	return;
}

void my_pwd(){
	char buf[MAXPATHLEN] = {0};
	if(getcwd(buf, MAXPATHLEN) == NULL){
		perror("getcwd");
		return ;
	}	
	printf("%s\n", buf);
	return;
}

void metacharacters(string pattern, string dir, vector <string> *arg){
	vector <string> placeholder;
	int size = pattern.size();
	int i = 0;
	string buf1, buf2;
	buf1 = buf2 = "";
	while(i < size && pattern[i] =='/') i++;
	while(i < size && pattern[i] !='/'){
		buf1+=pattern[i];
		i++;
	}
	if(i >= size) i--;
	if(pattern[i]=='/'){
		while(i < size){
			buf2+=pattern[i];
			i++;
		}
		string tmp = "";
		if(pattern[0]=='/'){
			if(dir.empty()) tmp = '/';
			else tmp = dir;
		}
		else tmp = '.';
		if(buf1 == ".." || buf1 == "."){
			if(pattern[0]=='/') tmp = dir  + '/' + buf1;
			else if (buf1 == ".") tmp = ".";
			else tmp = "..";
			if(buf2.size() > 1)	metacharacters(buf2, tmp, arg);
			else arg->push_back(tmp);
		} else {
			DIR *d = opendir(tmp.c_str());
			if(d == NULL) return;
			errno = 0;
			for( dirent *de = readdir(d); de != NULL; de = readdir(d)){
				if(string(de->d_name) == "." || string(de->d_name) == "..") continue;
				if(!fnmatch(buf1.c_str(), de->d_name, 0)){
					if(pattern[0]=='/') tmp = dir  + '/' + (string)de->d_name;
					else tmp = (string)de->d_name;
					struct stat st;
					if(stat(tmp.c_str(), &st)<0){
						//perror(tmp.c_str());
						return;
					}
					if(S_ISDIR(st.st_mode)) {
						placeholder.push_back(string(de->d_name));
					}
				}
			}
			if(closedir(d) == -1) printf("closedir error \n");
			if(!placeholder.empty()){
				sort(placeholder.begin(), placeholder.end(), less<string>());
				for(vector <string>::iterator it = placeholder.begin(); it!=placeholder.end(); it++){
					if(pattern[0]=='/') tmp = dir  + '/' + *it;
					else tmp = *it;
					if(buf2.size() > 1)	metacharacters(buf2, tmp, arg);
					else arg->push_back(tmp);
				}
			}
		}
	}else{
		string tmp = "";
		if(pattern[0]=='/'){
			if(dir.empty()) tmp = '/';
			else tmp = dir;
		}
		else tmp = '.';
		if(buf1 == ".." || buf1 == "."){
			if(pattern[0]=='/') tmp = dir  + '/' + buf1;
			else if (buf1 == ".") tmp = ".";
			else tmp = "..";
			if(buf2.size() > 1)	metacharacters(buf2, tmp, arg);
			else arg->push_back(tmp);
		} else {
			DIR *d = opendir(tmp.c_str());
			if(d == NULL) return;

			for( dirent *de = readdir(d); de != NULL; de = readdir(d)){
				
				if(string(de->d_name) == "." || string(de->d_name) == ".." || de->d_name[0] == '.') continue;
				if(!fnmatch(buf1.c_str(), de->d_name, 0)){
					placeholder.push_back(string(de->d_name));	
				}
			}
			if(!placeholder.empty()){
				sort(placeholder.begin(), placeholder.end(), less<string>());
				if(pattern[0] == '/'){
					for(vector <string>::iterator it = placeholder.begin(); it!=placeholder.end(); it++){
						string tmp = dir + '/' + *it;
						arg->push_back(tmp);
					}
				}else{
					for(vector <string>::iterator it = placeholder.begin(); it!=placeholder.end(); it++){
						string tmp = *it;
						arg->push_back(tmp);
					}
				}
			}
			closedir(d);
		}
	}
	return;
}

void pars_pipeline(vector <string> s){
	vector <vector <string> > arg;
	vector <string> placeholder;
	for(vector <string>::iterator it = s.begin(); it!=s.end(); it++){
		if(*it == "|" && it!=s.begin() && it!=s.end()){
			it++;
			arg.push_back(placeholder);
			placeholder.clear();
		}
		placeholder.push_back(*it);
	}
	arg.push_back(placeholder);
	if(!fork()) my_pipeline(arg);
	int status;
	wait(&status);
	return;
}

void pars_argv(vector <string> s){
	vector <string> placeholder;
	for (vector<string>::iterator it = s.begin(); it!=s.end(); it++){
		if (*it == "\n") break;
		if(*it == ">"){
			if(it!=(s.end() - 1)){
				it++;
				my_out(placeholder, (*it));
				placeholder.clear();
				continue;
			} else {
				printf("microsha: syntax error near > \n");
				return;
			}
		} else if(*it == "<"){
			if(it!=(s.end() - 1)){
				it++;
				my_in(placeholder, (*it));
				placeholder.clear();
				continue;
			} else {
				printf("microsha: syntax error near < \n");
				return;
			}
		}
		placeholder.push_back(*it);
	}		
	if(!placeholder.empty()){
	vector <char *> argv;
	for(vector<string>::iterator it = placeholder.begin() ; it!=placeholder.end() ; ++it){
		argv.push_back((char *)(*it).c_str());
	}
	argv.push_back(NULL);
	execvp(argv[0], &argv[0]);
	fprintf(stderr, "microsha: %s: command not found\n", argv[0]);
	int status;
	exit(status);
	}
}


int main(){
	if(chdir(getenv("HOME")) == -1) perror("chdir");
	signal(SIGINT, SIG_IGN);
	for(;;){
		if(shell_prompt_show() == 0) return 0;
		string s;
		getline(cin, s);
		if (cin.eof() && (s.size() == 0)) {
			printf("\n");
			exit(0);
		}
		if(s.size() == 0) continue;
		if(s == "leave") exit(0);
		string command = "";
		first_command(s, &command);
		if(command.size() &&  command == "cd"){
			vector <string> argv;
			if(pars(s, &argv) != 1) my_cd(argv);
			if(argv.size()) argv.clear();
		}
		else {
			pid_t pid = fork();
			if(pid == 0){
				signal(SIGINT, SIG_DFL);
				vector <string> argv;
				int flag = pars(s, &argv);
				switch(flag){
						case 1:
							exit(0);
						case 0:
							if(argv.size())pars_argv(argv);
							break;
						case 2:
							my_pwd();
							break;
						case 3:
							pars_pipeline(argv);
							break;
				}
				if(argv.size()) argv.clear();
				exit(0);
			}
			int st;
			if(command == "time"){
				struct timeval start_time, end_time;
				struct rusage ch_start_utime, ch_end_utime;
				getrusage(RUSAGE_CHILDREN, &ch_start_utime);
				gettimeofday(&start_time, NULL);
				wait(&st);
				getrusage(RUSAGE_CHILDREN, &ch_end_utime);
				gettimeofday(&end_time, NULL);
				double u_time = ch_end_utime.ru_utime.tv_sec - ch_start_utime.ru_utime.tv_sec + (double)(ch_end_utime.ru_utime.tv_usec - ch_start_utime.ru_utime.tv_usec)/1000000;
				double s_time = ch_end_utime.ru_stime.tv_sec - ch_start_utime.ru_stime.tv_sec + (double)(ch_end_utime.ru_stime.tv_usec - ch_start_utime.ru_stime.tv_usec)/1000000;
				double r_time = end_time.tv_sec - start_time.tv_sec + (double)(end_time.tv_usec - start_time.tv_usec)/1000000;			
				printf("real  %.3fs\n", r_time);
				printf("user  %.3fs\n", u_time);
				printf("sys   %.3fs\n", s_time);
			} else 
				wait(&st);
		}
	}	
	return 0;
}









