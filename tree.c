//R CMD SHLIB -lz tree.c
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "zlib.h"
#include <pthread.h>


#define forR 0
#if forR==1
	#include <R.h>
	#define errorMessage(A) error(A)
	#define warningMessage(A) warning(A)
#else
	#define errorMessage(A) printf(A)
	#define warningMessage(A) printf(A)
#endif

#define MIN(a,b) ((a)>(b)?(b):(a))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MAXLINELENGTH 100000
#define QUEUESIZE 100
#define MAXSTRINGIN 1000

struct node{
	//int minPos,maxPos;
	unsigned int nPos; //number of positions
	int *pos; //array of positions sorted from least to greatest
	struct node *children[4]; //pointer to 4 children
};
//return null pointer if bad
//assume single seqs/qual per line and no comments
char** getSeqFromFastq(gzFile *in,char **buffers){
	size_t length;
  int ii;
	
	if(gzgets(*in,buffers[0],MAXLINELENGTH)==(char*)0)return((char**)0);
	//peak ahead
	//ungetc(tmpChar,in);
	//name,seq,name again,qual
	for(ii=1;ii<4;ii++){
		if(gzgets(*in,buffers[ii],MAXLINELENGTH)==(char*)0){
			errorMessage("Incomplete fastq record");
			exit(-99);
		}
		//remove new lines
		//length=strlen(buffers[ii]);
		//if(buffers[ii][length-1]=='\n')buffers[ii][length-1]='\0';
	}
	return(buffers);
}


//arguments to pass into pthread_create
struct fsitArgs{
	struct node *tree;
	char *query;
	struct node *currentNode;
	int pos;
	unsigned int maxMismatch;
	int *out;
	int id;
	char *buffer;
};

/*
 * Don't need this now that we're using smarter sort
int compareInt(const void *x,const void *y){
	int *ix=(int*)x;
	int *iy=(int*)y;
	return(*ix-*iy);   //*ix>*iy?1:*ix<*iy?-1:0
}
*/

int findMinPos(struct node *node, int pos){
  unsigned int ii;
	for(ii=0;ii<node->nPos;ii++){
		if(node->pos[ii]>pos)return(node->pos[ii]); //careful: always >?
	}
	warningMessage("Couldn't find valid position");
	return(-99);
}

void addNodePos(struct node *node,int pos){
	int *tmp=malloc(sizeof(int)*node->nPos+1);
  unsigned int ii;
	unsigned int foundInsert=0;
	for(ii=0;ii<node->nPos;ii++){
		if(!foundInsert&&pos<node->pos[ii]){
			tmp[ii]=pos;
			foundInsert=1;
		}
		tmp[ii+foundInsert]=node->pos[ii];
	}
	//must be the biggest
	if(!foundInsert)tmp[node->nPos]=pos;
	//qsort(tmp,node->nPos+1,sizeof(int),compareInt); //could do this smarter since we actually know we're getting a sorted array
	free(node->pos);
	node->pos=tmp;
	node->nPos++;
}

struct node* createNode(unsigned int pos){
	struct node* thisNode;
  int ii;
	thisNode=(struct node *)malloc(sizeof(struct node));
	if(thisNode==(struct node*)0){
		errorMessage("No memory");
		exit(-99);
	}
	//store all positions
	//thisNode->minPos=pos;
	//thisNode->maxPos=pos;
	thisNode->pos=malloc(sizeof(int));
	thisNode->nPos=1;
	thisNode->pos[0]=pos;
	for(ii=0;ii<4;ii++)thisNode->children[ii]=NULL;
	return thisNode;
}

unsigned int convertCharToIndex(char base){//somesort of inline here?
	return base=='A'?0:
	base=='C'?1:
	base=='G'?2:
	base=='T'?3:99;
}
char convertIndexToChar(int index){//somesort of inline here?
	return index==0?'A':
	index==1?'C':
	index==2?'G':
	index==3?'T':'N';
}

char complementBase(char base){
	return base=='A'?'T':
	base=='C'?'G':
	base=='G'?'C':
	base=='T'?'A':'N';
}

int complementString(char *read,char *out){
	unsigned int counter=0;
	while(read[counter]!='\0'&&read[counter]!='\n'){
		out[counter]=complementBase(read[counter]);
		counter++;
	}
	out[counter]='\0';
	return(1);
}
struct node* buildTree(char *s1) {
	struct node *head, *currentNode;
	char thisChar;
	unsigned int charId;
	head=createNode(-1);
	size_t n=strlen(s1);
  int ii,jj;
	//head->hasChildren[1]=99;
	//printf("%d",head->hasChildren[1]);

	//could do this in linear instead of n^2 if necessary
	//also could be more efficient in memory
	for(ii=n-1;ii>=0;ii--){
		//printf("ii:%d\n",ii);
		currentNode=head;
		for(jj=ii;jj<n;jj++){
			//printf("jj:%d\n",jj);
			thisChar=s1[jj];
			charId=convertCharToIndex(thisChar);
			//check if currentNode's child is filled
			if(currentNode->children[charId]==(struct node*)0){
				//Add node
				//printf("Created node");
				currentNode->children[charId]=createNode(jj);
			}else{
				//Update node
				//printf("Updated node");
				addNodePos(currentNode->children[charId],jj);
				//currentNode->children[charId]->minPos=MIN(jj,currentNode->children[charId]->pos[0]);
				//currentNode->children[charId]->maxPos=MAX(jj,currentNode->children[charId]->maxPos);
			}
			currentNode=currentNode->children[charId];
			//printf("char:%c\n",thisChar);
		}
	}
	return(head);
}

unsigned int countNodes(struct node *tree){
	unsigned int count=0;
  unsigned int ii;
	if(tree==(struct node*)0)return(count);//catch empty tree
	for(ii=0;ii<4;ii++){
		if(tree->children[ii]!=(struct node*)0)count+=countNodes(tree->children[ii]);
	}
	count++;
	return(count);
}

int destroyTree(struct node *tree){
  unsigned int ii;
	for(ii=0;ii<4;ii++){
		if(tree->children[ii]!=(struct node*)0)destroyTree(tree->children[ii]);
	}
	free(tree->pos);
	free(tree);
	return(0);
}


//make recursive with minPos and maxMisses (and currentNode)? then call on each substring (and mismatch)?
//return -matches or 1 if we made it
int findStringInTree(struct node *tree,char *query,struct node *currentNode, int pos, unsigned int maxMismatch){ // unsigned int maxGap, unsigned int maxMismatch, unsigned int maxInsert){
	int match;
	//iterators
	unsigned int ii,k;
	int jj;

	unsigned int depth,charId;
	int tmp;
	//int *match=malloc(sizeof(int)*4); //matches, completed, mismatches, splices (should probably have insertions, deletions too)
	//for(ii=0;ii<5;ii++)match[ii]=0;
	size_t n=strlen(query);
	//printf("Pointer size: %lu\n",sizeof(int*));
	//store binary for nodes to check out if we need to check splicing (don't need to check if our min pos didn't change)
	int *pathPos=malloc(sizeof(int)*(n+1));//if we start storing multiples, e.g. down multiple paths, then we'll need more
	//unsigned int nJumpBack=0;
	//store pointers to nodes to check mismatch
	struct node **path=malloc(sizeof(struct node*)*(n+1));//if we start storing multiples, e.g. down multiple paths, then we'll need more
	unsigned int nPath=0;
	depth=0;
	int bestMatch;
	char base, tmpChar;

	//printf("Running on char %d string with %d mismatch.\n",n,maxMismatch);
	
	pathPos[0]=pos; 
	path[0]=currentNode; 
	bestMatch=1;//Really shouldn't default to success
	for(ii=0;ii<n;ii++){
		if(query[ii]=='\n')break;//Assuming new line means end of string
		charId=convertCharToIndex(query[ii]);
		//printf("ii:%d ",ii);
		//appropriate child is not null & we're not passed the position of that child
		if(currentNode->children[charId]!=(struct node*)0 && currentNode->children[charId]->pos[currentNode->children[charId]->nPos-1]>pos){
			//printf("Match\n");
			currentNode=currentNode->children[charId];
			tmp=findMinPos(currentNode,pos);
			pos=tmp;
			if(pos<0){errorMessage("Invalid position");exit(-2);}
			depth++;
			path[depth]=currentNode;
			pathPos[depth]=pos;
		}else{
			//printf("Break at depth %d",depth,ii);
			bestMatch=-depth; //kind of dangerous probably should do another way
			//printf("Break\n");
			//break in string
			if(maxMismatch>0){
				for(jj=depth;jj>=0;jj--){
					//printf("jj:%d ",jj);
					//splicing
					if(pathPos[jj]-pathPos[jj-1]>1&&jj>0||jj==depth){
						//jump to root and try to match the rest, respecting the position of this match
						tmp=findStringInTree(tree,&query[jj],tree,pathPos[jj],maxMismatch-1);
						//printf("Splice %d %s<----\n",tmp,&query[jj]);
						if(tmp>0){
							bestMatch=1;
							break; //depth loop
						}
						tmp=tmp-jj;
						if(tmp<bestMatch)bestMatch=tmp;
					}
					//mismatch
					tmpChar=query[jj];
					for(k=0;k<4;k++){
						base=convertIndexToChar(k);
						if(tmpChar==base)continue;
						//substitute char in string here
						query[jj]=base;
						//printf("%s",&query[jj]);
						tmp=findStringInTree(tree,&query[jj],path[jj],pathPos[jj],maxMismatch-1); //find best match for remainder of the string with base substituted at the next position
						if(tmp>0){
							bestMatch=1;
							break; //ACTG loop
						}
						tmp=tmp-jj;
						//printf("Mismatch %c->%c %d  Best:%d",tmpChar,base,tmp,bestMatch);
						if(tmp<bestMatch)bestMatch=tmp;
					}
					query[jj]=tmpChar;
					//printf("\n");
					if(bestMatch>0)break;//depth loop
				}
			}
			break; //could do an insertion here
		}
	}
	free(path);
	free(pathPos);
	return(bestMatch);
}


int writeSeqToFastq(gzFile *out, char **buffers){
	size_t length;
  int ii;
	for(ii=0;ii<4;ii++){
		if(gzputs(*out,buffers[ii])==-1)return(0);
	}
	return(1);
}

int onlyACTG(char *read){
	unsigned int counter=0;
	while(read[counter]!='\0'&&read[counter]!='\n'){
		if(convertCharToIndex(read[counter])>3){
			//printf("Bad: %c -> %u\n",read[counter],convertCharToIndex(read[counter]));
			return(0);
		}
		counter++;
	}
	return(1);
}

char *revString(const char *str,char *str2){
	size_t n=strlen(str);
  int ii;
	//char *buffer=(char *)malloc(sizeof(char)*(n+1));
	if(str[n-1]=='\n')n--;
	str2[n]='\0';
	for(ii=n-1;ii>=0;ii--){
		str2[n-1-ii]=str[ii];
	}
	return(str2);
}

int strCat(char *str1, const char *str2){
	int counter=0;
	while(str1[counter]!='\0'&&str1[counter]!='\n')counter++;//assuming new line is end of string
	int firstPos=counter;
	counter=0;
	while(str2[counter]!='\0'){
		str1[firstPos+counter]=str2[counter];
		counter++;
	}
	str1[firstPos+counter]='\0';
	return(1);
}

int switchBuffers(char **buffer1, char **buffer2){
	char *tmp; //for switching buffers
  int ii;
	for(ii=0;ii<4;ii++){
		tmp=buffer1[ii];
		buffer1[ii]=buffer2[ii];
		buffer2[ii]=tmp;
	}
	return(1);
}


void *findStringInTreePar(void *fsitArgs){
	struct fsitArgs *args=(struct fsitArgs *)fsitArgs;
	//printf("HERE %d ",args->id);
	*args->out=findStringInTree(args->tree,args->query,args->currentNode,args->pos,args->maxMismatch);
	//printf("HERE2 %d ",args->id);
}
void findReadsInFastq(char** ref, char **fileName, int *parameters,char **outNames){
	int ii,jj; //iterators
	int index; //for filling in answers appropriately
	int maxMismatch=parameters[0]; //at most x mismatch and splices
	int minPartial=parameters[1]; //require x length to call partial match, if left and right partial then call a match 
	int sumMatch=parameters[2]; //if left length + right length > x, call a match
	struct node *tree[2]; //big suffix tree for aligning
	gzFile in,outMatch,outPartial; //file pointer for fastqs
	//line buffers
	char* buffers[4];
	for(ii=0;ii<4;ii++)buffers[ii]=(char *)malloc(sizeof(char)*MAXLINELENGTH);
	char* buffers2[4];
	for(ii=0;ii<4;ii++)buffers2[ii]=(char *)malloc(sizeof(char)*MAXLINELENGTH);
	//seq conversions
	char* seqs[4];
	for(ii=0;ii<4;ii++)seqs[ii]=(char *)malloc(sizeof(char)*MAXLINELENGTH);
	
	char tmpStr[1000];//to append to things

	long int counter=0, matchCounter=0, partialCounter=0;
	struct fsitArgs *args[8];
	for(ii=0;ii<8;ii++)args[ii]=(struct fsitArgs *)malloc(sizeof(struct fsitArgs));
	int ans[8];//answer from findStringInTree
	int isMatch,isPartial;
	char **fastqCheck;//keep track of whether our fastq is empty

	//threads
	pthread_t threads[8];

	printf("Building tree\n");
	tree[0]=buildTree(ref[0]);
	printf("Building reverse tree\n");
	revString(ref[0],seqs[0]);
	tree[1]=buildTree(seqs[0]);

	printf("%d node trees ready\n",countNodes(tree[0]));


	printf("Opening file %s\n",fileName[0]);
	//I think this should work with uncompressed files too
	in=gzopen(fileName[0],"rt");
	printf("Opening outFile %s\n",outNames[0]);
	outMatch=gzopen(outNames[0],"w");
	printf("Opening outFile %s\n",outNames[1]);
	outPartial=gzopen(outNames[1],"w");

	printf("Scanning file (max mismatch %d, min partial %d, sum match %d)\n",maxMismatch,minPartial,sumMatch);
	fastqCheck=getSeqFromFastq(&in,buffers);
	while(fastqCheck != (char**)0){
		counter++;
		if(counter%10000==0)printf("Working on read %ld\n",counter);
		if(!onlyACTG(buffers[1])){
			//write somewhere else?
			//printf("Bad read %ld: %s\n",counter,buffers[1]);
			fastqCheck=getSeqFromFastq(&in,buffers);
			continue;
		}
		strcpy(seqs[0],buffers[1]);
		revString(seqs[0],seqs[1]);
		complementString(seqs[0],seqs[2]);
		revString(seqs[2],seqs[3]);
		for(ii=0;ii<2;ii++){
			for(jj=0;jj<4;jj++){
				if(ii==0)index=jj;
				else index=4+(jj/2*2)+1-(jj%2); //using integer division to floor. and switching 4-5 and 6-7 to make later comparisons easy 0&4,1&5,...
				args[ii*4+jj]->tree=tree[ii];
				args[ii*4+jj]->query=seqs[jj];
				args[ii*4+jj]->currentNode=tree[ii];
				args[ii*4+jj]->pos=-1;
				args[ii*4+jj]->maxMismatch=maxMismatch;
				args[ii*4+jj]->out=&ans[index];
				args[ii*4+jj]->id=ii*4+jj;
				if(pthread_create(&threads[ii*4+jj],NULL,findStringInTreePar,args[ii*4+jj])){errorMessage("Couldn't create thread");exit(-98);}
				//ans[index]=findStringInTree(tree[ii],seqs[jj],tree[ii],-1,maxMismatch);
			}
		}

		//read from disk while we're waiting
		fastqCheck=getSeqFromFastq(&in,buffers2);
		//printf("%p",fastqCheck);
		//stop waiting here? and throw it all into some sort of giant queue?
		for(ii=0;ii<2;ii++){
			for(jj=0;jj<4;jj++){
				if(pthread_join(threads[ii*4+jj],NULL)){errorMessage("Couldn't join thread");exit(-97);}
			}
		}
		isMatch=0;
		isPartial=0;
		for(ii=0;ii<4;ii++){
			if(ans[ii]>0||ans[ii+4]>0||(ans[ii]<-minPartial&&ans[ii+4]<-minPartial)||(ans[ii]+ans[ii+4])<-sumMatch)isMatch=1; //found a match, or both side have partial, or sum of sides enough
			if(ans[ii]<-minPartial||ans[ii+4]<-minPartial)isPartial=1;
			sprintf(tmpStr,":%d|%d",-ans[ii],-ans[ii+4]);
			strCat(buffers[0],tmpStr);
		}
		strCat(buffers[0],"\n");

		if(isMatch){ //also ans+ans2>X
			writeSeqToFastq(&outMatch,buffers);
			matchCounter++;
		}else if(isPartial){
			writeSeqToFastq(&outPartial,buffers);
			partialCounter++;
		}
		switchBuffers(buffers,buffers2);
		//ignore bad matches for now
	}
	printf("All done. Found %ld matches, %ld partials from %ld reads\n",matchCounter,partialCounter,counter);
	for(ii=0;ii<4;ii++)free(buffers[ii]);
	for(ii=0;ii<4;ii++)free(buffers2[ii]);
	for(ii=0;ii<4;ii++)free(seqs[ii]);
	for(ii=0;ii<8;ii++)free(args[ii]);
	printf("Closing file %s\n",fileName[0]);
	gzclose(in);
	printf("Closing outFiles\n");
	gzclose(outMatch);
	gzclose(outPartial);
	printf("Destroying trees\n");
	for(ii=0;ii<2;ii++) destroyTree(tree[ii]);
	return;
}

char* readFastq(char *fileName){
	FILE *in = fopen(fileName,"rt");
	char name[MAXLINELENGTH],seq[MAXLINELENGTH];
	size_t length;
	while(fgets(name,MAXLINELENGTH,in) != (char*)0){
		length=strlen(name);
		if(name[length-1]=='\n')name[length-1]='\0';
		if(fgets(seq,MAXLINELENGTH,in)==(char*)0){
			printf("Missing sequence");
		}
		length=strlen(seq);
		if(seq[length-1]=='\n')seq[length-1]='\0';
	}
	fclose(in);
}



int main (int argc, char *argv[]){
  int nMismatch=0,nThread=2;
  int c;
  int ii;
  char usage[500];
  char refFile[MAXSTRINGIN+1];
  char fastqFile[MAXSTRINGIN+1];
  sprintf(usage,"Usage: %s ref.fa reads.fastq [-m 2] [-t 4]\n  first argument: a reference sequence in a fasta file (if this is much more than 10kb then we could get memory problems)\n  second argument: a fastq file containing the reads to search\n  -t: (optional) specify how many threads to use (default: 2)\n  -m: (optional) specify how many mismatches to tolerate (default: 00)\n  -h: (optional) display this message and exit\n",argv[0]);
  while ((c = getopt (argc, argv, "hm:t:")) != -1){
    switch (c){
      case 'm':
        nMismatch = atoi(optarg);
        break;
      case 'y':
        nThread = atoi(optarg);
        break;
      case 'h':
        fprintf(stderr,"%s",usage);
        return(3);
      case '?':
        if (optopt == 'c')
          fprintf (stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
        return 1;
    }
  }


  if(optind!=argc-2){
    fprintf(stderr,"%s",usage);
    return(2);
  }

  for (ii = 0; ii < argc-optind; ii++){
    if(strlen(argv[optind+ii])>MAXSTRINGIN){
      fprintf(stderr,"Filenames must be less than %d characters long",MAXSTRINGIN);
      return(4);
    }
    switch(ii){
      case 0:
        strcpy(refFile,argv[optind+ii]);
        break;
      case 1:
        strcpy(fastqFile,argv[optind+ii]);
        break;
    }
  }
  fprintf (stderr,"nMismatch: %d\nnThread: %d\nrefFile: %s\nfastqFile: %s\n", nMismatch, nThread,refFile,fastqFile);

  //void findReadsInFastq(char** ref, char **fileName, int *parameters,char **outNames){

  return 0;
}
