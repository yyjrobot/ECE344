#include "common.h"
#include <stdio.h>

int factorial(int i){
    if (i==1) return i;
    
    return (i*factorial(i-1));
}

int main(int argc, char **argv)
{
    int valid=1;
    if (argc<2){//no further argument
        printf("Huh?\n");
    } 
    else {
        
        for (int i=0;argv[1][i]!='\0';i++){//check legalty of argument
            if (argv[1][i]>57||argv[1][i]<48)//unexpected char detected
                valid=0;
        }
        if (valid==0)
            printf("Huh?\n");
        else{
            char *pass_in_value=argv[1];
            int value;
            value = atoi(pass_in_value);
            if (value>12)
                printf("Overflow\n");
            if (value==0)
                printf("Huh?\n");
            else {
                int result=factorial(value);
                printf("%d\n",result);
            }
        }
    }

	return 0;
}
