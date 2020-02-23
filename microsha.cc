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
void execute_command(vector <string> s);

//remove escape character (e.g. '\') from string
//example: "\x\\\<" => "x\<"
string character_unescaping(string str){
	int i = 0;
	string ret_str = "";
	char prev_char = 0;
	while(i < str.size()){
		if(str[i] != '\\' || prev_char == '\\') ret_str += str[i];
		if(str[i] == '\\' && prev_char == '\\')	prev_char = 0;
		else prev_char = str[i];
		i++;
	}
	return ret_str;
}

int check_valid_file_name(string str){
	if(str == "<" || str == ">" || str == "\\" || str == "/" || str == "<") return 1;
	return 0;
}

//prompts for user input
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
	uid_t uid = getuid();//get user id
	if(uid == 0){//Super user
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

//put into buf first command
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

//user input parsing
int pars(string s, vector <string> *str){
	vector <string> placeholder;//contains each command and it's arguments and "<", ">", "|" symbols
	string buf;//temporary buffer for current command
	char prev_char = 0;//contains previous character in input
	int size;
	int count_out = 0;
	int count_in = 0;
	int count_pipe = 0;
	//scan all characters in user input
	for(int i = 0; i < s.size() && s[i] != '\n'; ++i){
		buf = "";
		//skip unsignificant characters before command
		while(((s[i]==' ') || (s[i] == '\t')) && (s[i] != '\n') && i < s.size()){
			++i;
		}
		//collect character by character commad and argument in the buf 
		while(
			(s[i] != ' ') && (s[i] != '\t') && (s[i] != '\n') && 
			!(s[i] == '>' && prev_char != '\\') && !(s[i] == '<' && prev_char != '\\') && (s[i] != '|') && 
			(s[i] != '*') && (s[i] != '?') && i < s.size()
		)
		{
			buf += s[i];		
			if(s[i] == '\\' && prev_char == '\\')	prev_char = 0;
			else prev_char = s[i];
			++i;
		}
		//analyze next character after buf
		switch(s[i]){
			case '>'://redirection to
				if(count_out < 1){//only one ">" symbol permitted
					if(!buf.empty()) placeholder.push_back(buf);
					placeholder.push_back(">");
					++count_out;
				} else {
					fprintf(stderr, "microsha: string parsing error: ");
					fprintf(stderr, "more than one \">\"\n");
					return 1;//indicate error
				}
				continue;
				break;
			case '<'://redirection from
				if(count_in < 1){//only one "<" symbol permitted
					if(!buf.empty()) placeholder.push_back(buf);
					placeholder.push_back("<");
					++count_in;
				} else {
					fprintf(stderr, "microsha: string parsing error: ");
					fprintf(stderr, "more than one \"<\"\n");
					return 1;//indicate error
				}
				continue;
				break;
			case '|'://pipeline
				if(!buf.empty()) placeholder.push_back(buf);
				placeholder.push_back("|");
				++count_pipe;
				continue;
				break;
			case '*'://metacharacter
				buf+='*';
				++i;
				//add character by character symbols in the buf after metacharacter
				while((s[i] != ' ') && (s[i] != '\t') && (s[i] != '\n') && (s[i] != '>') && (s[i] != '<') && (s[i] != '|') && i < s.size()){
					buf += s[i];
					++i;
				}
				size = placeholder.size();
				metacharacters(reduce_slash(buf), "", &placeholder);//replaces template symbols to real files's and directories's names
				if(size == placeholder.size()){//if size is not changed then no files or directories for this template
					fprintf(stderr, "microsha: %s: no such file or directory\n", buf.c_str());
					return 1;//indicate error
				}
				continue;
				break;
			case '?'://metacharacter
				buf+='?';
				++i;
				//add character by character symbols in the buf after metacharacter
				while((s[i] != ' ') && (s[i] != '\t') && (s[i] != '\n') && (s[i] != '>') && (s[i] != '<') && (s[i] != '|') && i < s.size()){
					buf += s[i];
					++i;
				}
				size = placeholder.size();
				metacharacters(reduce_slash(buf), "", &placeholder);//replaces template symbols to real files's and directories's names
				if(size == placeholder.size()){//if size is not changed then no files or directories for this template
					fprintf(stderr, "microsha: %s: no such file or directory\n", buf.c_str());
					return 1;//indicate error
				}
				continue;
				break;
		}
		if(buf == "time" && placeholder.size() == 0) continue;//if first command is "time" then it performed separately
		if(!buf.empty()) placeholder.push_back(buf);
	}
	if(!placeholder.empty()){
		for (vector<string>::iterator it = placeholder.begin() ; it!=placeholder.end() ; ++it){
			str->push_back(*it);
		}
		if((*str)[0] == "pwd" && str->size() == 1) return 2;//indicate "pwd" command
	}
	if(count_pipe) return 3;//indicate pipeline
	return 0;//other command
}

//performs pipeline
void my_pipeline (vector <vector <string> > &arg){
	int i; 
	int count = arg.size();
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
			execute_command(arg[i]);
		}
		dup2(fd[0], 0);
		close(fd[1]);
	}
	if(arg[i][0] == "pwd" && arg[i][0].size() == 1){
			my_pwd();
			exit(0);
	}
	execute_command(arg[i]);
}

//performs cd command
void my_cd(vector <string> s){
	if(s.size() == 1){//if no argumets then change directory to home directory
		if(chdir(getenv("HOME")) == -1) perror("chdir");
	} else {
		if(chdir(s[1].c_str()) == -1) fprintf(stderr, "microsha: %s: no such directory\n", s[1].c_str());
	}
	return;
}

//show full path
void my_pwd(){
	char buf[MAXPATHLEN] = {0};
	if(getcwd(buf, MAXPATHLEN) == NULL){
		perror("getcwd");
		return ;
	}	
	printf("%s\n", buf);
	return;
}

//replaces template symbols to real files's and directories's names
void metacharacters(string pattern, string dir, vector <string> *arg){
	vector <string> placeholder;
	int size = pattern.size();
	int i = 0;
	string buf1, buf2;
	buf1 = buf2 = "";
	//skip first unsignificant symbols "/"
	while(i < size && pattern[i] =='/') i++;
	//collect symbols before "/"
	while(i < size && pattern[i] !='/'){
		buf1+=pattern[i];
		i++;
	}
	if(i >= size) i--;//if no symbol "/" then reduce index
	if(pattern[i]=='/'){//
		while(i < size){
			buf2+=pattern[i];//collect symbols after "/"
			i++;
		}
		string tmp = "";//path
		if(pattern[0]=='/'){//for root dir
			if(dir.empty()) tmp = '/';
			else tmp = dir;
		}
		else tmp = '.';//current dir
		if(buf1 == ".." || buf1 == "."){
			if(pattern[0]=='/') tmp = dir  + '/' + buf1; //case when "/xxx/"
			else if (buf1 == ".") tmp = "."; //case when "xxx/"
			else tmp = "..";
			if(buf2.size() > 1)	metacharacters(buf2, tmp, arg); //if exists symbols after "/" then recursive call
			else arg->push_back(tmp);//else - store name 
		} else {
			//search for directory
			DIR *d = opendir(tmp.c_str());
			if(d == NULL) return;//not found
			errno = 0;
			//scan each directory and store suitable
			for( dirent *de = readdir(d); de != NULL; de = readdir(d)){
				if(string(de->d_name) == "." || string(de->d_name) == "..") continue;//skip "..", "."
				if(!fnmatch(buf1.c_str(), de->d_name, 0)){
					if(pattern[0]=='/') tmp = dir  + '/' + (string)de->d_name;
					else tmp = (string)de->d_name;
					struct stat st;
					if(stat(tmp.c_str(), &st)<0) return;
					if(S_ISDIR(st.st_mode)) {
						placeholder.push_back(string(de->d_name));
					}
				}
			}
			if(closedir(d) == -1) printf("closedir error \n");
			if(!placeholder.empty()){//there are matches
				sort(placeholder.begin(), placeholder.end(), less<string>());//sort by name
				for(vector <string>::iterator it = placeholder.begin(); it!=placeholder.end(); it++){
					if(pattern[0]=='/') tmp = dir  + '/' + *it;
					else tmp = *it;
					if(buf2.size() > 1) metacharacters(buf2, tmp, arg);//recursively call the function for the remaining string
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

//for each pipleline's component parses commands and their arguments 
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
	for(int i = 1; i < arg.size() - 1; ++i){
		for(vector <string>::iterator it = arg[i].begin(); it!=arg[i].end(); it++){
			if(*it == "<" || *it == ">"){
				fprintf(stderr, "microsha: error: \"<\" or \">\" in the pipeline's component\n");
				return;
			}
		}
	}
	if(!fork()) my_pipeline(arg);
	int status;
	wait(&status);
	return;
}

//parses commad's arguments and then execute the command
//also performs input/output redirection
void execute_command(vector <string> s){
	vector <string> placeholder;
	int flag = 0;//check the number of files after "<", ">"
	for (vector<string>::iterator it = s.begin(); it!=s.end(); it++){
		if (*it == "\n") break;
		if(*it == ">"){
			if(it!=(s.end() - 1)){
				flag = 0;
				it++;
				string tmp = *it;
				if(check_valid_file_name(tmp)){
					fprintf(stderr,"microsha: invalid file name: %s\n", tmp.c_str());
					return;
				}
				tmp = character_unescaping(*it);
				int fid = open((char *)tmp.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
				if(fid == -1){
					perror("open");
					return;
				}
				flag = 1;
				dup2(fid, 1);
				continue;
			} else {
				fprintf(stderr,"microsha: syntax error near > \n");
				return;
			}
		} else if(*it == "<"){
			if(it!=(s.end() - 1)){
				flag = 0;
				it++;string tmp = *it;
				if(check_valid_file_name(tmp)){
					fprintf(stderr,"microsha: invalid file name: %s\n", tmp.c_str());
					return;
				}
				tmp = character_unescaping(*it);
				int fid = open((char *)tmp.c_str(), O_RDWR , 0600);
				if(fid == -1){
					fprintf(stderr, "microsha: %s: no such file or directory\n", (char *)tmp.c_str());
					return;
				}
				flag = 1;
				dup2(fid, 0);
				continue;
			} else {
				fprintf(stderr,"microsha: syntax error near < \n");
				return;
			}
		}
		if(flag) {
			fprintf(stderr,"microsha: more then one file\n");
			return;
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
	return;
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
		if(command == "cd"){
			vector <string> argv;
			if(pars(s, &argv) != 1) my_cd(argv);
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
							if(argv.size())execute_command(argv);
							break;
						case 2:
							my_pwd();
							break;
						case 3:
							pars_pipeline(argv);
							break;
				}
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