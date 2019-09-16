#include "common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

/* make sure to use syserror() when a system call fails. see common.h */

void copy_file(const char* to,const char* from){
    int fd_from,fd_to,close_from,close_to;
    char buf[4096];
    ssize_t read_val,write_val;
    fd_from = open(from, O_RDONLY);//open the file with READ ONLY
    if (fd_from < 0){
        syserror(open,from);
    }
    fd_to = creat(to, O_WRONLY);//create a file with WRITE ONLY
    if (fd_to < 0){
        syserror(creat,to);
    }
    while(1){
        read_val = read(fd_from,buf,4096);
	if (read_val<0){
	    syserror(read,from);	
	}
        if (read_val==0){//all data are copied
            break;
        }

        write_val=write(fd_to,buf,read_val);
        
        if (write_val<0){
            syserror(write,to);
        }
    }
    
    //copy permission
    struct stat tmp;
    stat(from, &tmp);
    chmod(to, tmp.st_mode);
    
    //finish copying, close fd
    close_from=close(fd_from);
    if (close_from<0){
        syserror(close,from);
    }
    close_to=close(fd_to);
    if (close_to<0){
        syserror(to,from);
    }
}

void copy_dir(const char* to,const char* from){
    //if no dir exist, create one
    struct stat tmp;
    stat(from, &tmp);//read the source dir info
    if(opendir(to)==NULL){
        int mkdir_result=mkdir(to,0777);
        if (mkdir_result<0){
            syserror(mkdir,to);
        }
        chmod (to,tmp.st_mode);
    }
    
    DIR *pdir_from;
    pdir_from=opendir(from);
    if (pdir_from==NULL){
        //printf("error opening\n");
        syserror(opendir,from);
    }
    
    //used to make up new path names
    char *path_to;
    path_to=(char*)malloc(256*sizeof(char));
    strcat(path_to,to);
    
    //recursive copy contents

    struct dirent *new_dirent;
    while (1){
        new_dirent=readdir(pdir_from);
        if (new_dirent==NULL){
            //syserror(readdir,from);
            break;//nothing to copy, exit loop
        }
        else{
            if ((strcmp(".",new_dirent->d_name)==0||strcmp("..",new_dirent->d_name)==0)){
                continue;
            }
            char dirent_path_from[256];
            dirent_path_from[0]='\0';
            path_to[0]='\0';
            strcat(dirent_path_from,from);
            strcat(dirent_path_from,"/");
            strcat(dirent_path_from,new_dirent->d_name);
            stat(dirent_path_from, &tmp);
            strcat(path_to,to);
            strcat(path_to,"/");
            strcat(path_to,new_dirent->d_name);
            
            if (S_ISREG(tmp.st_mode)){
                printf("this is file\n");
                copy_file(path_to,dirent_path_from);
            }
            else {
                printf("this is dir\n");
                copy_dir(path_to,dirent_path_from);
            }
        }   

    }

    free(path_to);
    int close_source_result;
    close_source_result=closedir(pdir_from);
    if (close_source_result<0){
        syserror(closedir,from);
    }
}

void
usage()
{
	fprintf(stderr, "Usage: cpr srcdir dstdir\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		usage();
	}
	char *from;
    char *to;
    from = (char*) malloc(sizeof(char)*strlen(argv[1])+1);
    to = (char*) malloc(sizeof(char)*strlen(argv[2])+1);
	strcpy(from,argv[1]);
	strcpy(to,argv[2]);
    copy_dir(to,from);
    free(from);
    free(to);
	return 0;
}
