#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include <string.h>
#include <ctype.h>
#include "wc.h"
#define TableMax 1000000


struct hash_element{
    char* word;
    int occupied;
    int count;//used to count same words
    long size;//count total size of the table, all same in every element
};

//following hash function comes from 
//https://stackoverflow.com/questions/10696223/reason-for-5381-number-in-djb-hash-function
unsigned long DJBHash(char* str, unsigned int len)
{
   unsigned long hash = 5381;
   unsigned int i    = 0;

   for(i = 0; i < len; str++, i++)
   {   
      hash = ((hash << 5) + hash) + (*str);
   }   

   return hash;
}

struct hash_element* hash_table_init(long size){
    struct hash_element* new_table;
    long table_size=size*2;//size of new table is about 2 times of the word size
    new_table = (struct hash_element*)malloc(table_size*sizeof(struct hash_element));
    for (long i=0;i<table_size;i++){
        new_table[i].occupied=0;
        new_table[i].count=0;
        new_table[i].size=table_size;
    }
    return new_table;
}

int hash_add(struct hash_element hash_table[], char* word){
    int length=strlen(word);
    long pos=DJBHash(word,length)%hash_table[0].size;//make sure not oversize
    if (hash_table[pos].occupied==0){//new word
        hash_table[pos].word=(char* ) malloc((1+length)*sizeof(char));//+1 is for the '\0'
        strcpy(hash_table[pos].word,word);
        hash_table[pos].word[length]='\0';//end this word
        hash_table[pos].occupied=1;
        hash_table[pos].count++;
    }
    else if (hash_table[pos].occupied!=0){
        //we have same word
        if (strcmp(hash_table[pos].word,word)==0){
            hash_table[pos].count++;
        }
        //new word with collision, find a new position
        else{
            while(1){
                if (pos==(hash_table[0].size-1))//reach the end, go back to the head
                    pos=0;
                else 
                    pos++;
                if (hash_table[pos].occupied==1){//new collision
                    if (strcmp(hash_table[pos].word,word)==0){//same word
                        hash_table[pos].count++;
                        break;
                    }
                }
                else if (hash_table[pos].occupied==0){//add the word here
                    hash_table[pos].word=(char* ) malloc((1+length)*sizeof(char));
                    strcpy(hash_table[pos].word,word);
                    hash_table[pos].word[length]='\0';
                    hash_table[pos].occupied=1;
                    hash_table[pos].count++;
                    break;
                }
            }
        }
    }
    return length;
}

struct wc {
	/* you can define this struct to have whatever fields you want. */
    struct hash_element* new_table;
};

struct wc *
wc_init(char *word_array, long size)
{
	struct wc *wc;

	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);
        
        long i;
        long word_total=0;
        
        //count the total word amounts in the array
        for (i=0;i<size-1;i++){
            if (isspace(word_array[i+1])&&(!isspace(word_array[i]))){
                word_total++;
            }
            else if(((i+1)==(size-1))&&(!isspace(word_array[i+1]))){//last char reach the end
                word_total++;
            }
        }
               
	wc->new_table=hash_table_init(word_total);
        
        for (i=0;i<size;i++){    
            if (!isspace(word_array[i])){//start to pick a word
                long end_idx=i;
                char* word_buf;
                
                while(!isspace(word_array[end_idx+1])){
                    if ((end_idx+1)<size)//make sure not reach eof
                        end_idx++;
                    else{ //reach eof
                        break;
                    }
                }
                
                word_buf=(char*)malloc((end_idx-i+2)*sizeof(char));
                              
                strncpy(word_buf,word_array+i,(end_idx-i+1));
                word_buf[end_idx-i+1]='\0';
                hash_add(wc->new_table,word_buf);
                free (word_buf);
                word_buf=NULL;
                i=end_idx;//last char of this word
            }
            
            
        }
	return wc;
}

void
wc_output(struct wc *wc)
{
	//TBD();
    long i;
    for (i=0;i<wc->new_table[0].size;i++){
        if (wc->new_table[i].occupied){
            printf("%s:%d\n",wc->new_table[i].word,wc->new_table[i].count);
        }
    }
}

void
wc_destroy(struct wc *wc)
{
	//TBD();
    long i;
    for (i=0;i<wc->new_table[0].size;i++){
            free(wc->new_table[i].word);
            wc->new_table[i].word=NULL;
        
    }
}
