/* Copyright (C) 2003 Nadav Har'El and Dan Kenigsberg */

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#include "dict_radix.h"
#include "gimatria.h"
#include "corlist.h"
#include "hash.h"

#define HSPELL_VERSION "0.6"

int debug=0;

/* Load the data files */
static void
load_data(struct dict_radix **dictp)
{
	clock_t t1, t2;
	if(debug){
		fprintf(stderr,"Loading data files... ");
		t1=clock();
	}

	*dictp = new_dict_radix();
#ifndef DICTIONARY_BASE
#define DICTIONARY_BASE "./hebrew.wgz"
#endif
	if(!read_dict(*dictp, DICTIONARY_BASE)){
		fprintf(stderr,"Sorry, could not read dictionary. Hspell "
			"was probably installed improperly.\n");
		exit(1);
	}

	if(debug){
		t2=clock();
		fprintf(stderr,"done (%d ms).\n",
				(int)((t2-t1)/(CLOCKS_PER_SEC/1000)));
	}
}

/*
 * The prefix tree "prefix_tree" is built by build_prefix_tree, from a list of
 * known combinations of prefixes. Each prefix also has a mask that determines
 * to what kind of words it can be applied.
 *
 * The list of known prefixes and masks were defined in the prefixes[] and
 * masks[] arrays in prefixes.c. That file is automatically generated by the
 * genprefixes.pl program.
 */

#include "prefixes.c"

struct prefix_node {
	/* if a prefix has a certain 'mask', and lookup on a word returns
	 * 'val' (a bitmask of prefixes allowed for it), our prefix is
	 * allowed on this word if and only if (mask & val)!=0.
	 *
	 * This means that 'mask' defines the bits that this prefix "supplies"
	 * and he 'val' defined for a word is the bits this words insists on
	 * getting at least one of (i.e., val is the list of types of
	 * prefixes that are allowed for this word).
	 */
	int mask;
	struct prefix_node *next['�'-'�'+1];
};
static struct prefix_node *prefix_tree = 0;

static void
build_prefix_tree(void){
	int i;
	const char *p;
	struct prefix_node **n;
	for(i=0; prefixes[i]; i++){
		p=prefixes[i];
		n=&prefix_tree;
		if(debug)
			fprintf(stderr,"prefix %s ",p);
		while(*p){
			if(!(*n))
				*n=(struct prefix_node *)
					calloc(1,sizeof(struct prefix_node));
			n=& ((*n)->next[*p-'�']);
			p++;
		}
		/* define the mask (making sure the node exists). */
		if(!*n)
			*n=(struct prefix_node *)
				calloc(1,sizeof(struct prefix_node));
		(*n)->mask=masks[i];

		if(debug)
			fprintf(stderr,"mask=%d\n",(*n)->mask);
	}
}


#define ishebrew(c) ((c)>=(int)(unsigned char)'�' && (c)<=(int)(unsigned char)'�')

static int
check_word(struct dict_radix *dict, const char *word, int *preflen)
{
	int hashebrew;
	const char *w=word;
	struct prefix_node *n;
	*preflen = 0;

	/* ignore empty words: */
	hashebrew=0;
	while(*w){
		if(*w>='�' && *w<='�'){
			hashebrew=1;
			break;
		}
		(*preflen)++;
		w++;
	}
	if(!hashebrew)
		return 1; /* ignore (accept) empty words */


	n=prefix_tree;
	if(debug)
		fprintf(stderr,"looking %s\n",w);
	while(*w && n){
		/* eat up the " if necessary, to recognize words like
		 * �"����".  or ������ �"�����...".
		 * See the Academy's punctuation rules (see ������ ���, ���,
		 * ���"�) for an explanation of this rule (we're probably don't
		 * support here everything they suggested; in particular I
		 * don't recognize a single quote as valid form of merchaot).
		 */
		if(*w=='"'){
			(*preflen)++;
			w++;
			continue;
		}
		/* The first case here is the Academia's "ha-ktiv hasar
		 * ha-niqqud" rule of doubling a consonant waw in the middle
		 * a word, unless it's already next to a waw. When adding a
		 * prefix, any initial waw in a word will nececessarily
		 * become a consonant waw in the middle of the word.
		 * The "else if" below is the normal check.
		 */
		if(n!=prefix_tree && *w=='�' && w[-1]!='�'){
			if(w[1]=='�'){
				if(w[2]!='�' && (lookup(dict,w+1) & n->mask)){
					/* for example: ����� */
					if(debug)
						fprintf(stderr,"found %s: double waw.\n",w);
					return 1;
				} else if(lookup(dict,w) & n->mask){
					/* for example: ����� */
					if(debug)
						fprintf(stderr,"found %s: nondouble waw.\n",w);
					return 1;
				}
			}
		} else {
			if (debug) fprintf (stderr, "tried %s mask %d prefmask %d\n",w,lookup(dict,w), n->mask);
			if(lookup(dict,w) & n->mask) return 1; /* found word! */
		}

		/* try the next prefix... */
		if(*w>='�' && *w<='�'){
			n=n->next[*w-'�'];
			(*preflen)++;
			w++;
		} else {
			break;
		}
	}
	if(n && !*w){
		/* allow prefix followed by nothing (or a non-word like
		 * number, maqaf, etc.) */
		if(debug) fprintf(stderr,"Accepting empty word\n");
		return 1;
	} else
		return 0; /* unrecognized (mis-spelled) word */
}

/* try to find corrections for word */
void
trycorrect(struct dict_radix *dict, const char *w, struct corlist *cl)
{
	char buf[30];
	int i;
	int len=strlen(w), preflen;
	static char *similar[] = {"���", "��", "��", "��", "��", "��",
				  "��", "��", "��"};

#define TRYBUF if(check_word(dict, buf, &preflen)) corlist_add(cl, buf)
	/* try to add a missing em kri'a - yud or vav */
	for(i=1;i<len;i++){
		snprintf(buf,sizeof(buf),"%.*s�%s",i,w,w+i);
		TRYBUF;
		snprintf(buf,sizeof(buf),"%.*s�%s",i,w,w+i);
		TRYBUF;
	}
	/* try to remove an em kri'a - yud or vav */
	/* NOTE: in hspell.pl the loop was from i=0 to i<len... */
	for(i=1;i<len-1;i++){
		if(w[i]=='�' || w[i]=='�'){
			snprintf(buf,sizeof(buf),"%.*s%s",i,w,w+i+1);
			TRYBUF;
		}
	}
	/* try to add or remove an aleph (is that useful?) */
	/* TODO: don't add an aleph next to yud or non-double vav,
	 * as it can't be an em kria there? */
	for(i=1;i<len;i++){
		snprintf(buf,sizeof(buf),"%.*s�%s",i,w,w+i);
		TRYBUF;
	}
	for(i=1;i<len-1;i++){
		if(w[i]=='�'){
			snprintf(buf,sizeof(buf),"%.*s%s",i,w,w+i+1);
			TRYBUF;
		}
	}
	/* try to replace similarly sounding (for certain people) letters:
	 */
	for(i=0;i<len;i++){
		int group;
		char *g;
		for(group=0; group< (sizeof(similar)/sizeof(similar[0]));
				group++){
			for(g=similar[group];*g && *g!=w[i];g++);
				;
			if(*g){
				/* character in group - try the other ones
				 * in this group! */
				for(g=similar[group];*g;g++){
					if(*g==w[i]) continue;
					if(i>0 && w[i]=='�' && w[i+1]=='�')
						snprintf(buf,sizeof(buf),
						    "%.*s%c%s",i,w,*g,w+i+2);
					else if(*g=='�')
						snprintf(buf,sizeof(buf),
						    "%.*s��%s",i,w,w+i+1);
					else
						snprintf(buf,sizeof(buf),
						   "%.*s%c%s",i,w,*g,w+i+1);
					TRYBUF;
				}
			}
		}
	}
	/* try to replace a non-final letter at the end of the word by its
	 * final form and vice versa (useful check for abbreviations) */
	strncpy(buf,w,sizeof(buf));
	switch(w[len-1]){
		case '�': buf[len-1]='�'; break;
		case '�': buf[len-1]='�'; break;
		case '�': buf[len-1]='�'; break;
		case '�': buf[len-1]='�'; break;
		case '�': buf[len-1]='�'; break;
		case '�': buf[len-1]='�'; break;
		case '�': buf[len-1]='�'; break;
		case '�': buf[len-1]='�'; break;
		case '�': buf[len-1]='�'; break;
		case '�': buf[len-1]='�'; break;
	}
	if(buf[len-1]!=w[len-1]){ TRYBUF; }
	/* try to make the word into an acronym (add " before last character */
	if(len>=2){
		snprintf(buf,sizeof(buf), "%.*s\"%c",len-1,w,w[len-1]);
		TRYBUF;
	}
	/* try to make the word into an abbreviation (add ' at the end) */
	snprintf(buf,sizeof(buf), "%s'",w);
	TRYBUF;
}

/* load_personal_dict tries to load ~/.hspell_words and ./hspell_words.
   Currently, they are read into a hash table, where each word in the
   file gets a non-zero value.
   Empty lines starting with # are ignored. Lines containing non-Hebrew
   characters aren't ignored, but they won't be tried as questioned words
   anyway.
*/
static void
load_personal_dict(hspell_hash *personaldict)
{
	int i;
	hspell_hash_init(personaldict);
	for(i=0; i<=1; i++){
		char buf[512];
		FILE *fp;
		if(i==0)
			snprintf(buf, sizeof(buf),
				 "%s/.hspell_words", getenv("HOME"));
		else
			snprintf(buf, sizeof(buf), "./hspell_words");
		fp=fopen(buf, "r");
		if(!fp) continue;
		while(fgets(buf, sizeof(buf), fp)){
			int l=strlen(buf);
			if(buf[l-1]=='\n')
				buf[l-1]='\0';
			if(buf[0]!='#' && buf[0]!='\0')
				hspell_hash_incr_int(personaldict, buf);
		}
		fclose(fp);
	}
}

/* used for sorting later: */
static int 
compare_key(const void *a, const void *b){
	register hspell_hash_keyvalue *aa = (hspell_hash_keyvalue *)a;
	register hspell_hash_keyvalue *bb = (hspell_hash_keyvalue *)b;
	return strcmp(aa->key, bb->key);
}
static int 
compare_value_reverse(const void *a, const void *b){
	register hspell_hash_keyvalue *aa = (hspell_hash_keyvalue *)a;
	register hspell_hash_keyvalue *bb = (hspell_hash_keyvalue *)b;
	if(aa->value < bb->value)
		return 1;
	else if(aa->value > bb->value)
		return -1;
	else return 0;
}

static FILE *
next_file(int *argcp, char ***argvp)
{
	FILE *ret=0;
	if(*argcp<=0)
		return 0;
	while(*argcp && !ret){
		ret=fopen((*argvp)[0],"r");
		if(!ret)
			perror((*argvp)[0]);
		(*argvp)++;
		(*argcp)--;
	}
	return ret;
}


#define VERSION_IDENTIFICATION ("@(#) International Ispell Version 3.1.20 " \
			       "(but really Hspell/C " HSPELL_VERSION ")\n")

int
main(int argc, char *argv[])
{
	struct dict_radix *dict;
#define MAXWORD 30
	char word[MAXWORD+1], *w;
	int wordlen=0, offset=0, wordstart;
	int c;
	int res;
	FILE *slavefp;
	int terse_mode=0;
	hspell_hash wrongwords;
	int preflen; /* used by -l */
	hspell_hash personaldict;

	/* command line options */
	char *progname=argv[0];
	int interpipe=0; /* pipe interface (ispell -a like) */
	int slave=0;  /* there's a slave ispell process (-i option) */
	int opt_s=0; /* -s option */
	int opt_c=0; /* -c option */
	int opt_l=0; /* -l option */
	int opt_v=0; /* -v option (show version and quit) */

	/* TODO: when -a is not given, allow filename parameters, like
	   the "spell" command does. */
	FILE *in=stdin;

	/* Parse command-line options */
	while((c=getopt(argc, argv, "clnsviad:BmVhT:CSPp:w:W:"))!=EOF){
		switch(c){
		case 'a':
			interpipe=1;
			break;
		case 'i':
			slave=1;
			break;
		/* The following options do something on ispell or aspell,
		   and some confused programs call hspell with them. We just
		   ignore them silently, hoping that all's going to be well...
		*/
		case 'd': case 'B': case 'm': case 'T': case 'C': case 'S':
		case 'P': case 'p': case 'w': case 'W':
			/*fprintf(stderr, "Warning: ispell options -d, -B and "
			  "-m are ignored by hspell.\n");*/
			break;
		case 's':
			opt_s=1;
			break;
		case 'c':
			opt_c=1;
			break;
		case 'l':
			opt_l=1;
			break;
		case 'n':
			/* TODO: do the real thing */
			fprintf(stderr, "Option %c is not yet supported in "
				"this new version.\n",c);
			return 1;
		case 'v':
			opt_v++;
			break;
		case 'V':
			printf("Hspell " HSPELL_VERSION "\nWritten by Nadav "
			       "Har'El and Dan Kenigsberg\n");
			return 0;
		case 'h': case '?':
			fprintf(stderr,"hspell - Hebrew spellchecker\n"
				"Usage: %s [-acinslV] [file ...]\n\n"
				"See hspell(1) manual for a description of "
				"hspell and its options.\n", progname);
			return 1;
		}
	}
	argc -= optind;
	argv += optind;

	/* The -v option causes ispell to print its current version
	   identification on the standard output and exit. If the switch is
	   doubled, ispell will also print the options that it was compiled
	   with.
	*/
	if(opt_v){
		printf("%s", VERSION_IDENTIFICATION);
		return 0;
	}

	/* If the program name ends with "-i", we enable the -i option.
	   This ugly hack is useful when a certain application can be given
	   a different spell-checker, but not extra options to pass to it */
	if(strlen(progname)>=2 && progname[strlen(progname)-2] == '-' &&
	   progname[strlen(progname)-1] == 'i'){
		slave=interpipe=1;
	}
	
	if(interpipe){
		/* for ispell -a like behavior, we want to flush every line: */
		setlinebuf(stdout);
	} else {
		/* No "-a" option: UNIX spell-like mode: */

		/* Set up hash-table for remembering the wrong words seen */
		hspell_hash_init(&wrongwords);

		/* If we have any more arguments, treat them as files to
		   spellcheck. Otherwise, just use stdin as set above.
		*/
		if(argc){
			in=next_file(&argc, &argv);
			if(!in)
				exit(1); /* nothing to do, really... */
		}
	}

	load_data(&dict);
	load_personal_dict(&personaldict);
	build_prefix_tree();

	if(interpipe){
		if(slave){
			/* We open a pipe to an "ispell -a" process, letting
			   it output directly to the user. We also let it
			   output its own version string instead of ours. Is
			   this wise? I don't know. Does anyone care?
			   Note that we also don't make any attempts to catch
			   broken pipes.
			*/
			slavefp=popen("ispell -a", "w");
			if(!slavefp){
				fprintf(stderr, "Warning: Cannot create slave "
				    "ispell process. Disabling -i option.\n");
				slave=0;
			} else {
				setlinebuf(slavefp);
			}
		}
		if(!slave)
			printf("%s",VERSION_IDENTIFICATION);
	}

	for(;;){
		c=getc(in);
		if(c==EOF) {
			/* in UNIX spell mode (!interpipe) we should read
			   all the files given in the command line...
			   Otherwise, an EOF is the end of this loop.
			*/
			if(!interpipe && argc>0){
				in=next_file(&argc, &argv);
				if(!in)
					break;
			} else
				break;
		}
		if(ishebrew(c) || c=='\'' || c=='"'){
			/* swallow up another letter into the word (if the word
			 * is too long, lose the last letters) */
			if(wordlen<MAXWORD)
				word[wordlen++]=c;
		} else if(wordlen){
			/* found word seperator, after a non-empty word */
			word[wordlen]='\0';
			wordstart=offset-wordlen;
			/* TODO: convert two single quotes ('') into one
			 * double quote ("). For TeX junkies. */

			/* remove quotes from end or beginning of the word
			 * (we do leave, however, single quotes in the middle
			 * of the word - used to signify "j" sound in Hebrew,
			 * for example, and double quotes used to signify
			 * acronyms. A single quote at the end of the word is
			 * used to signify an abbreviate - or can be an actual
			 * quote (there is no difference in ASCII...), so we
			 * must check both possibilities. */
			w=word;
			if(*w=='"' || *w=='\''){
				w++; wordlen--; wordstart++;
			}
			if(w[wordlen-1]=='"'){
				w[wordlen-1]='\0'; wordlen--;
			}
			res=check_word(dict,w,&preflen);
			if(res!=1 && (res=is_canonic_gimatria(w))){
				if(debug)
					fprintf(stderr,"found canonic gimatria\n");
				if(opt_l){
					printf("�������: %s=%d\n",w,res);
					preflen = -1; /* yes, I know it is bad programming, but I need to tell later printf not to print anything, and I hate to add a flag just for that. */
				}
				res=1;
			}
			if(res!=1 && w[wordlen-1]=='\''){
				/* try again, without the quote */
				w[wordlen-1]='\0'; wordlen--;
				res=check_word(dict,w,&preflen);
			}
			/* as last resort, try the user's personal word list */
			if(res!=1)
				res=hspell_hash_exists(&personaldict, w);

			if(res){
				if(debug)
					fprintf(stderr,"correct: %s\n",w);
				if(interpipe && !terse_mode)
					if(wordlen)
						printf("*\n");
				if(opt_l){
					if(preflen>0){
						printf("����� ����: %.*s+%s\n",
						       preflen, w, w+preflen);
					} else if (!preflen){
						printf("���� �����: %s\n",w);
					}
				}
			} else if(interpipe){
				/* Mispelling in -a mode: show suggested
				   corrections */
				struct corlist cl;
				int i;
				if(debug)
					fprintf(stderr,"misspelling: %s\n",w);
				corlist_init(&cl);
				trycorrect(dict, w, &cl);
				if(corlist_n(&cl))
					printf("& %s %d %d: ", w,
					       corlist_n(&cl), wordstart);
				else
					printf("# %s %d", w, wordstart);
				for(i=0;i<corlist_n(&cl);i++){
					printf("%s%s",
					       i ? ", " : "",
					       corlist_str(&cl,i));
				}
				printf("\n");
				corlist_free(&cl);
			} else {
				/* Mispelling in "spell" mode: remember this
				   mispelling for later */

				if(debug)
					fprintf(stderr,"mispelling: %s\n",w);
				hspell_hash_incr_int(&wrongwords, w);
			}
			/* we're done with this word: */
			wordlen=0;
		} else if(interpipe && 
			  offset==0 && (c=='#' || c=='!' || c=='~' || c=='@' ||
					c=='%' || c=='-' || c=='+' || c=='&' ||
					c=='*')){
			if(c=='!')
				terse_mode=1;
			else if(c=='%')
				terse_mode=0;
			/* In the future we should do something about the
			   other ispell commands (see the ispell manual
			   for their description).
			   For now, ignore this line, or send it to a slave
			   ispell. Does this make sense? Probably not... */
			if(slave){
				putc(c,slavefp);
				while((c=getc(in))!=EOF && c!='\n')
					putc(c,slavefp);
				if(c!=EOF) putc(c,slavefp);
			} else {
				while((c=getc(in))!=EOF && c!='\n')
					;
			}
			/* offset=0 remains but we don't want to output
			   a newline */
			continue;
		}
		if(c=='\n'){
			offset=0;
			if(interpipe && !slave)  /*slave already outputs a newline...*/
			printf("\n");
		} else {
			offset++;
		}
		/* pass the character also to the slave, replacing Hebrew
		   characters by spaces */
		if(interpipe && slave)
			putc(ishebrew(c) ? ' ' : c, slavefp);
	}
	/* TODO: check the last word in case of no newline at end of file? */

	/* in spell-like mode (!interpipe) - list the wrong words */
	if(!interpipe){
		hspell_hash_keyvalue *wrongwords_array;
		int wrongwords_number;
		wrongwords_array = hspell_hash_build_keyvalue_array(
			&wrongwords, &wrongwords_number);

		if(wrongwords_number){
			int i;
			if(opt_c)
				printf("������ ���� ������, ��������� "
				       "��������:\n\n");
			else
				printf("������ ���� ������:\n\n");

			/* sort word list by key or value (depending on -s
			   option) */
			qsort(wrongwords_array, wrongwords_number,
			      sizeof(hspell_hash_keyvalue),
			      opt_s ? compare_value_reverse : compare_key);

			for(i=0; i<wrongwords_number; i++){
				if(opt_c){
					struct corlist cl;
					int j;
					printf("%d %s -> ",
					       wrongwords_array[i].value,
					       wrongwords_array[i].key);
					corlist_init(&cl);
					trycorrect(dict,
					       wrongwords_array[i].key, &cl);
					for(j=0;j<corlist_n(&cl);j++){
						printf("%s%s",
						       j ? ", " : "",
						       corlist_str(&cl,j));
					}
					corlist_free(&cl);
					printf("\n");
				} else if(opt_s){
					printf("%d %s\n",
					       wrongwords_array[i].value,
					       wrongwords_array[i].key);
				} else {
					printf("%s\n",wrongwords_array[i].key);
				}
			}
		}
#if 0
		hspell_hash_free_keyvalue_array(&wrongwords, wrongwords_number,
						wrongwords_array);
#endif
	}
	
	return 0;
}
