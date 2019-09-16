#include "common.h"
#include <stdio.h>


int
main(int argc, char **argv)
{

	int arg_counter;
	for (arg_counter=1;arg_counter<argc;arg_counter++){//arg_counter starts from the second arg
		int char_counter;
		for (char_counter=0;argv[arg_counter][char_counter]!='\0';char_counter++){
			printf("%c",argv[arg_counter][char_counter]);		
		}
		printf("\n");
	}
	return 0;
}
