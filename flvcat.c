/*****************************************************************************/
/** flvcat ver. 0.01 (2008/Apr/17)
 *
 * flvファイルの結合。
 * まともにdurationを計算する小物ツールが見当たらなかったので。
 * あとlinuxで使える軽量で高速なやつも欲しかったから。
 * ライセンス考えるの面倒だったのでGPL。
 * METAタグのパースが不安。
 * vcでコンパイルする時は setargv.obj をリンクすると幸せになれます。
 *
 *  Copyright (C) 2008 MakKi
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
/*****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <fcntl.h>
#endif


typedef struct OPTIONS {
	int show_version;
	int show_help;
	const char *output;
	char **files;
	int filenum;
} OPTIONS;

typedef struct FLVFile {
	FILE *fp;
	int version;
	int flag;
	unsigned int duration;
} FLVFile;

enum {
	METATYPE_NUM        = 0x00,
	METATYPE_BOOL       = 0x01,
	METATYPE_STR        = 0x02,
	METATYPE_OBJ        = 0x03,
	METATYPE_NULL       = 0x05,
	METATYPE_UNDEF      = 0x06,
	METATYPE_REF        = 0x07,
	METATYPE_MIXEDARRAY = 0x08,
	METATYPE_TERMINATOR = 0x09,
	METATYPE_ARRAY      = 0x0a,
	METATYPE_DATE       = 0x0b,
	METATYPE_UNSUPPORT  = 0x0d,
};


enum {
	TAGTYPE_AUDIO = 0x08,
	TAGTYPE_VIDEO = 0x09,
	TAGTYPE_META  = 0x12,
};


/* ui, command options */
void print_version(void);
void print_usage(void);
OPTIONS *parse_options(int,char **);
void release_options(OPTIONS *);

/* input files */
FLVFile* open_files(OPTIONS *);
void close_files(FLVFile *);
int open_flvfile(const char *fname,FLVFile *flv);
unsigned int get_flv_duration(FILE*);
unsigned int get_flv_meta_duration(FILE*,int);
int skip_meta_element(int,FILE*,int);

/* output files */
FILE *open_output(OPTIONS *,FLVFile *);
void close_output(FILE *);

/* bit handling */
unsigned int be32_to_int(unsigned char *b);
int be24_to_int(unsigned char *b);
int be16_to_int(unsigned char *b);
int be_double_to_int_1000(unsigned char *n);
void double_to_be(double n,unsigned char *c);
void int32_to_be(unsigned int i,unsigned char *c);

int flvcat(FILE*,FLVFile*,OPTIONS*);

int flvtag_type(unsigned char *h);
int flvtag_bodysize(unsigned char *h);
int flvtag_timestamp(unsigned char *h);

void flvtag_set_timestamp(unsigned char *,unsigned int);
int copyfile(FILE*,FILE*,unsigned int);

/*===========================================================================*/
/** main
 */
int main(int argc,char *argv[])
{
	OPTIONS *opt;
	FLVFile *files;
	FILE *out;
	int ret;

	opt = parse_options(argc,argv);
	if(opt==NULL){
		return -1;
	}

	if(opt->show_version || opt->show_help){
		if(opt->show_version) print_version();
		if(opt->show_help) print_usage();
		release_options(opt);
		return 0;
	}

	if((files=open_files(opt))==NULL){
		release_options(opt);
		return -1;
	}
	if((out=open_output(opt,files))==NULL){
		release_options(opt);
		close_files(files);
		return -1;
	}


	ret = flvcat(out,files,opt);

	release_options(opt);
	close_files(files);
	close_output(out);

	return ret;
}


void print_version(void)
{
	puts("flvcat version 0.01. Copylight (c) 2008 MakKi");
}
void print_usage(void)
{
	puts("usage: flvcat [options] flvfiles ...");
	puts(" OPTIONS");
	puts("  -h, --help               show this help");
	puts("  -v, --version            show version");
	puts("  -o, --output <filename>  set output filename");
}

OPTIONS *parse_options(int argc,char **argv)
{
	int i;
	OPTIONS *opt;

	if(argc<2){
		return NULL;
	}

	opt = (OPTIONS*) malloc(sizeof(OPTIONS));
	if(opt==NULL){
		fputs("flvcat: error on memory allocation.",stderr);
		return NULL;
	}
	memset(opt,0,sizeof(OPTIONS));

	opt->files = (char **) malloc((argc-1)*sizeof(char **));
	if(opt->files==NULL){
		fputs("flvcat: error on memory allocation.",stderr);
		release_options(opt);
		return NULL;
	}

	for(i=1;i<argc;++i){
		if(argv[i][0]=='-'){
			if(strcmp(argv[i],"-v")==0 ||
			   strcmp(argv[i],"--version")==0){
				opt->show_version =1;
			}
			else if(strcmp(argv[i],"-h")==0 ||
					strcmp(argv[i],"--help")==0){
				opt->show_help = 1;
			}
			else if(strcmp(argv[i],"-o")==0 ||
					strcmp(argv[i],"--output")==0){
				if(++i == argc){
					fputs("flvcat: output file name is not specified.",stderr);
					release_options(opt);
					return NULL;
				}
				opt->output = argv[i];
			}
			else {
				opt->files[opt->filenum++] = argv[i];
			}
		}
		else{
			opt->files[opt->filenum++] = argv[i];
		}
	}

	return opt;
}

void release_options(OPTIONS *opt)
{
	free(opt->files);
	free(opt);
}

/*---------------------------------------------------------------------------*/

FLVFile* open_files(OPTIONS *opt)
{
	FLVFile *files;
	int i;
	files = (FLVFile*) malloc((opt->filenum+1)*sizeof(FLVFile));
	if(files==NULL){
		return NULL;
	}

	for(i=0;i<opt->filenum;++i){
		if(open_flvfile(opt->files[i],&files[i])<0){
			close_files(files);
			free(files);
			return NULL;
		}
	}
	files[i].fp = NULL;
	return files;
}

void close_files(FLVFile *files)
{
	while(files->fp!=NULL){
		if(files->fp!=stdin) fclose(files->fp);
		++files;
	}
}

int open_flvfile(const char *fname,FLVFile *flv)
{
	unsigned char head[9];
	unsigned int offset;
	unsigned int duration;
	FILE *fp;

	memset(flv,0,sizeof(FLVFile));

	if(strcmp(fname,"-")==0){
		fp = stdin;
	}
	else{
		if((fp=fopen(fname,"rb"))==NULL){
			fprintf(stderr,"flvcat: %s: No such file\n",fname);
			return -1;
		}
	}

	if(fread(head,sizeof(head),1,fp)!=1){
		fprintf(stderr,"flvcat: %s: Cannot read file header\n",fname);
		if(fp!=stdin) fclose(fp);
		return -2;
	}
	if(head[0]!='F' || head[1]!='L' || head[2]!='V'){
		fprintf(stderr,"flvcat: %s: file is not FLV file\n",fname);
		if(fp!=stdin) fclose(fp);
		return -3;
	}

	offset = be32_to_int(&head[5]);
	fseek(fp,offset+4,SEEK_SET);

	flv->fp = fp;
	flv->version = head[3];
	flv->flag = head[4];
	flv->duration = get_flv_duration(fp);
	return 0;
}

unsigned int get_flv_duration(FILE *fp)
{
	unsigned char tagh[11];
	int pos = ftell(fp);
	int last_time = 0;
	int penultimate_time = 0;

	while(fread(tagh,sizeof(tagh),1,fp)==1){
		if(flvtag_type(tagh)==TAGTYPE_META){
			int size = flvtag_bodysize(tagh);
			unsigned int duration = get_flv_meta_duration(fp,size);
			if(duration){
				fseek(fp,pos,SEEK_SET);
				return duration;
			}
		}
		penultimate_time = last_time;
		last_time = flvtag_timestamp(tagh);
		fseek(fp,flvtag_bodysize(tagh)+4,SEEK_CUR);
	}
	fseek(fp,pos,SEEK_SET);
	return last_time - penultimate_time + last_time;
}

unsigned int get_flv_meta_duration(FILE *fp,int size)
{
	while(size>0){
		int type;
		if((size-=1)<=0) return 0;
		type = getc(fp);

		if(type==METATYPE_MIXEDARRAY){
			char tmp[128];
			unsigned int i;
			if((size -= 4)<=0) return 0;
			fread(tmp,4,1,fp);
			for(i=be32_to_int(tmp);i;--i){
				int slen;
				if((size-=2)<=0) return 0;
				if(fread(tmp,2,1,fp)!=1) return 0;
				slen = be16_to_int(tmp);
				if((size-=slen+1)<=0) return 0;
				if(fread(tmp,slen,1,fp)!=1) return 0;
				tmp[slen] = '\0';
				type = getc(fp);
				if(strcmp(tmp,"duration")==0 && type==METATYPE_NUM){
					if((size-=8)<=0) return 0;
					if(fread(tmp,8,1,fp)!=1) return 0;
					return be_double_to_int_1000(tmp);
				}
				else{
					size -= skip_meta_element(type,fp,size);
				}
			}
			/* terminator */
			if((size-=3)<=0) return 0;
			fread(tmp,2,1,fp);
			if(be16_to_int(tmp)!=0
			   || getc(fp)!=METATYPE_TERMINATOR){
				return 0;
			}
		}
		else{
			size -= skip_meta_element(type,fp,size);
		}
	}
	return 0;
}

int skip_meta_element(int type,FILE *fp,int size)
{
	char tmp[4];
	int len;
	int s;
	int i;

	switch(type){

	case METATYPE_NUM:
		fseek(fp,8,SEEK_CUR);
		return 8;

	case METATYPE_BOOL:
		fseek(fp,1,SEEK_CUR);
		return 1;

	case METATYPE_STR:
		if(size-2<=0) return size;
		if(fread(tmp,2,1,fp)!=1) return size;
		len = be16_to_int(tmp);
		if(size-2-len<0) return size;
		fseek(fp,len,SEEK_CUR);
		return 2+len;

	case METATYPE_OBJ:
		s = 2;
		if(size-s<=0) return size;
		if(fread(tmp,2,1,fp)!=1) return size;
		len = be16_to_int(tmp);
		while(len){
			s += len + 1;
			if(size-s<=0) return size;
			fseek(fp,len,SEEK_CUR);
			type = getc(fp);
			s += skip_meta_element(type,fp,size-s);
			s += 2;
			if(size-s<=0) return size;
			if(fread(tmp,2,1,fp)!=1) return size;
			len = be16_to_int(tmp);
		}
		return s;

	case METATYPE_NULL:
	case METATYPE_UNDEF:
	case METATYPE_UNSUPPORT:
		return 0;

	case METATYPE_MIXEDARRAY:
		s = 4;
		if(size-s<=0) return size;
		if(fread(tmp,4,1,fp)!=1) return size;
		for(i=be32_to_int(tmp);i;--i){
			s += 2;
			if(size-s<=0) return size;
			if(fread(tmp,2,1,fp)!=1) return size;
			len = be16_to_int(tmp);
			s += len;
			if(size-s-1<=0) return size;
			fseek(fp,len,SEEK_CUR);
			type = getc(fp);
			s += skip_meta_element(type,fp,size-s);
		}
		s+=3;
		if(size-s<=0) return size;
		if(fread(tmp,2,1,fp)!=1) return size;
		if(be16_to_int(tmp)!=0) return size;
		if(getc(fp)!=METATYPE_TERMINATOR) return size;
		return s;

	case METATYPE_ARRAY:
		s = 4;
		if(size-s<=0) return size;
		if(fread(tmp,4,1,fp)!=1) return size;
		for(i=be32_to_int(tmp);i;--i){
			s += 1;
			if(size-s<=0) return size;
			type = getc(fp);
			s += skip_meta_element(type,fp,size-s);
		}
		return s;

	case METATYPE_DATE:
		fseek(fp,10,SEEK_CUR);
		return 10;

	default:
		return size;
	}
}

/*---------------------------------------------------------------------------*/
FILE *open_output(OPTIONS *opt,FLVFile *files)
{
	unsigned long duration = 0;
	int flag = 0;
	int version = 1;
	FILE *fp;

	/* File Header
	 * "FLV", version(1), flag(1), offset(4), prev tag size(4)
	 */
	unsigned char head[] = { 'F','L','V', 0, 0, 0,0,0,9, 0,0,0,0};

	/* Meta Tag (onMetaData)
	 * tag head: type(1), body len(3), timestamp(3+1), streamid(3)
	 * type=2:string(1), strlen=10(2), "onMetaData"(10)
	 * type=8:mixed array(1), arraynum=1(4)
	 *  strlen(2), "duration"(8), type=0:number(1), double(8)
	 *  len=0(2), ""(0), type=9:terminater(1)
	 * prev tag size(4)
	 */
	unsigned char metadata[] = {
		TAGTYPE_META, 0,0,40, 0,0,0,0, 0,0,0,
		METATYPE_STR, 0,10, 'o','n','M','e','t','a','D','a','t','a',
		METATYPE_MIXEDARRAY, 0,0,0,1,
		0,8, 'd','u','r','a','t','i','o','n', METATYPE_NUM, 0,0,0,0,0,0,0,0,
		0,0, METATYPE_TERMINATOR,
		0,0,0,51
		};

	if(opt->output!=NULL && strcmp(opt->output,"-")!=0){
		fp = fopen(opt->output,"wb");
	}
	else{
		fp = stdout;
#ifdef _WIN32
		_setmode(_fileno(fp),_O_BINARY);
#endif
	}

	while(files->fp){
		duration += files->duration;
		version = (version<files->version) ? files->version : version;
		flag |= files->flag;
		++files;
	}

	/* prepare file header */
	head[3] = version;
	head[4] = flag;
	if(fwrite(head,sizeof(head),1,fp)!=1){
		fputs("flvcat: output error (file header)",stderr);
		close_output(fp);
		return NULL;
	}

	/* prepare meta tag */
	double_to_be(((double)duration)/1000,&metadata[40]);
	if(fwrite(metadata,sizeof(metadata),1,fp)!=1){
		fputs("flvcat: output error (meta tag)",stderr);
		close_output(fp);
		return NULL;
	}

	return fp;
}

void close_output(FILE *fp)
{
	if(fp!=stdout) fclose(fp);
}

/*===========================================================================*/

unsigned int be32_to_int(unsigned char *b)
{
	return (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
}

int be24_to_int(unsigned char *b)
{
	return (b[0]<<16)|(b[1]<<8)|b[2];
}

int be16_to_int(unsigned char *b)
{
	return (b[0]<<8)|b[1];
}

void double_to_be(double n,unsigned char *c)
{
	unsigned char *p = (unsigned char *) &n;
	int i;
	for(i=0;i<8;++i){
		c[i] = p[7-i];
	}
}

void int32_to_be(unsigned int i,unsigned char *h)
{
	h[0] = (i & 0xff000000) >> 24;
	h[1] = (i & 0xff0000) >> 16;
	h[2] = (i & 0xff00) >> 8;
	h[3] =  i & 0xff;
}

int be_double_to_int_1000(unsigned char *n)
{
	double d;
	int i;
	unsigned char *c = (unsigned char *)&d;
	for(i=0;i<8;++i){
		c[i] = n[7-i];
	}
	return d * 1000;
}

/*===========================================================================*/

int flvcat(FILE *out,FLVFile *files,OPTIONS *opt)
{
	unsigned int duration = 0;

	while(files->fp){
		unsigned char tagh[11];

		while(fread(tagh,sizeof(tagh),1,files->fp)==1){
			int type = flvtag_type(tagh);
			if(type==TAGTYPE_VIDEO || type==TAGTYPE_AUDIO){
				unsigned char prevsize[4];
				unsigned int ts = duration + flvtag_timestamp(tagh);
				unsigned int bodysize = flvtag_bodysize(tagh);
				flvtag_set_timestamp(tagh,ts);

				if(fwrite(tagh,sizeof(tagh),1,out)!=1){
					fputs("flvcat: writing error",stderr);
					return -1;
				}

				if(copyfile(files->fp,out,bodysize)!=0){
					fputs("flvcat: file copy error",stderr);
					return -1;
				}

				int32_to_be(bodysize+11,prevsize);
				if(fwrite(prevsize,sizeof(prevsize),1,out)!=1){
					fputs("flvcat: writing error",stderr);
					return -1;
				}

				fseek(files->fp,4,SEEK_CUR);
			}
			else{
				fseek(files->fp,flvtag_bodysize(tagh)+4,SEEK_CUR);
			}
		}
		duration += files->duration;
		++files;
	}
	return 0;
}



int flvtag_type(unsigned char *h)
{
	return h[0];
}

int flvtag_bodysize(unsigned char *h)
{
	return be24_to_int(h+1);
}

int flvtag_timestamp(unsigned char *h)
{
	return be24_to_int(h+4) + (h[7]<<24);
}


void flvtag_set_timestamp(unsigned char *h,unsigned int t)
{
	h[4] = (t & 0xff0000) >> 16;
	h[5] = (t & 0xff00) >> 8;
	h[6] =  t & 0xff;
	h[7] = (t & 0xff000000) >> 24;
}



int copyfile(FILE *src,FILE *dst,unsigned int size)
{
#define BUFSIZE 1024
	static char buf[BUFSIZE];
	int readed;

	while(size){
		readed = fread(buf,1,(size<BUFSIZE)?size:BUFSIZE,src);
		if(readed==0){
			return size;
		}
		if(fwrite(buf,1,readed,dst)!=readed){
			return size;
		}
		size -= readed;
	}
	return size;
}
