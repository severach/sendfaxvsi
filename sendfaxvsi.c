#define __POCC__OLDNAMES
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#ifdef __POCC__
#include <windows.h>
#include <io.h>
#include <process.h>
#define open _open
#define read _read
#define close _close
#define write _write
#else
#include <unistd.h>
#include <linux/limits.h>
#include <sys/stat.h>
#define O_BINARY (0)
#define O_SEQUENTIAL (0)
#define MAX_PATH (PATH_MAX)
#define __cdecl
#endif

#ifndef NELEM
#define NELEM(x) ((sizeof(x)/sizeof(x[0])))
#endif
/* Compiles in Pelles C for Windows, primary debugging environment
 * gcc for Linux: gcc -s -o sendfaxvsi sendfaxvsi.c
 */

static void usage(char *argv[]) {
  fprintf(stderr,
"sendfaxvsi (C)2015 Chris Severance/GPLv2 or later!\n"
"Usage:\n"
"sendfaxvsi < taggedprintfile sendfax <options>\n"
"VSI-FAX tag blocks will be extracted from stdin and recognized ones will added to the options line.\n"
"sendfax will be provided the file without the flags.\n"
);
}
// I could add switches like -i for input file, -o for output file, and -s for switch file.

/*

# In our application we print plain text. For text printed 
# to a SCO 5.x VSI-FAX printer we add a first line 
# with embedded tags. 
# See VSI-FAX Installation and Administration Guide: Using vfx Tags
# @+VFX[tfn="5175551212";tnm="John Doe"]\r
# This corresponds to the VSI-FAX tags and sendfax switches:
#cli client ID
#fa1-fa3 from address lines 1 thru 3
#fcn from country name (-Y location)
#fco from company name (-X company)
#fem from email address
#ffn from fax number   (-W fax-no)
#fnm from name         (-f from)
#fvn from voice number (-U voice-no)
#ntf note file
#sub subject line      (-r regarding)
#sig signature file
#tco to company name   (-x company)
#tfn to fax number     (-d "John Doe@+5175551212")
#tg1-tg4 user-defined tages 1 through 4
#tin custom to information
#tnm to name           (-d "John Doe@+5175551212")
#tvn to voice number   (-V voice-no)  

Unused Hylafax::sendfax switches
-c Cover page comments (ntf maybe)
-S tsi
-e name     Use name as the name value in the outbound call identification.
-f from
-i identifier
-n          Suppress the automatic generation of a cover page
-o login    Specify the fax owner login name

*/

#define TAGXLAT (4)
static struct _xlat {
  char sz_vsitag[5];
  char sz_switch[3];
  unsigned n_id; // non zero for special handling
  size_t cb_vsitag;
  int b_set;
  char *msz_val;
} g_tagxlat[] = {
  {"fcn","-Y"},
  {"fco","-X"},
  {"ffn","-W"},
  {"fnm","-f"},
  {"fvn","-U"},
  {"sub","-r"},
  {"tco","-x"},
#define XLAT_ID_NAME (1)
  {"tnm","-d",XLAT_ID_NAME}, // this simple implemenation does not support multiple to addresses
#define XLAT_ID_PHONE (2)
  {"tfn","-d",XLAT_ID_PHONE}, // tfn phone is purposely listed after tnm name for easier handling
  {"tvn","-V"},
};

static void init_tagxlat(void) {
  unsigned n;
  for(n=0; n<NELEM(g_tagxlat); n++) {
    g_tagxlat[n].cb_vsitag=strlen(g_tagxlat[n].sz_vsitag);
    g_tagxlat[n].b_set=0;
    g_tagxlat[n].msz_val=NULL;
  }
}

/* If the match starts with \n then the line can start with \n, \r, or \r\n */
static const char *memstr(const char *s_str,size_t cb_str,const char *s_match,size_t cb_match) {
  for(; cb_str>=cb_match; s_str++, cb_str--) {
    if (*s_str == *s_match /* || *s_match == '\n' && *s_str=='\r' */) {
      unsigned n;
      for(n=1; n<cb_match; n++) if (s_str[n] != s_match[n]) goto next; // continue won't work here.
      return s_str;
    }
next: ;   
  }
  return NULL;
}

#ifdef __POCC__
static void memstrtest(void) {
  printf("%s\n",memstr("1234567",6,"4567",4)); /* This should not be found */
  printf("%s\n",memstr("1234567",7,"4567",4));
  printf("%s\n",memstr("12\r\n1234abc",9,"\n1234",4));
  printf("%s\n",memstr("12\n1234abc",8,"\n1234",4));
  printf("%s\n",memstr("12\r1234abc",8,"\n1234",4));
}
#endif

// memchr-noquotes: scan forward through a string for chFind. Text inside of quotes "]" is skipped. No escaping.
static const char *memchrnq(const char *sBuf,size_t cbBuf,const char chFind) {
  int b_inquote=0;
  for(;cbBuf;sBuf++,cbBuf--) {
    if (*sBuf == '"') b_inquote=!b_inquote;
    if (b_inquote==0 && *sBuf==chFind) return sBuf;
  }
  return NULL;
}

#define S_FIND_VFX "@+VFX["
#define CB_FIND_VFX (6) // This length must match the text above.

#ifdef __POCC__
static void memchrnq_test1(char *szT1,char *szT2,char *szT3) {
  char sz_test1[256],sz_aout[256];
  char sz_ckout[256],sz_cktag[256]; // the calculated answers
  sprintf(sz_test1,"%s%s%s",szT1,szT2,szT3);
  sprintf(sz_ckout,"%s%s%s",szT1,szT2,szT3); sprintf(sz_cktag,""); /* the result if p1 or p2 fail */
  sprintf(sz_aout,"%s%s",szT1,szT3); // the correct answer for stdout. szT2 is the correct answer for tags
  const char *p1=memstr(sz_test1,strlen(sz_test1),S_FIND_VFX,CB_FIND_VFX);
  if (p1) { // this is an unbuffered version of the code in sendfaxvsi()
    const char *p2=memchrnq(p1,strlen(p1),']');
    if (p2) {
      p2++;
      sprintf(sz_ckout,"%.*s%s",p1-sz_test1,sz_test1,p2);
      sprintf(sz_cktag,"%.*s",p2-p1,p1);
    }
  }
  if (strcmp(sz_aout,sz_ckout) || strcmp(sz_cktag,szT2)) printf("Error!");
}    

static void memchrnq_test(void) {
  memchrnq_test1("","@+VFX[tfn=\"[0000000000]\"]","4567");
  memchrnq_test1("Hello","@+VFX[tfn=\"[0000000000]\"]","4567");
  memchrnq_test1("Hello","@+VFX[tfn=\"[0000000000]\"]","");
  memchrnq_test1("","@+VFX[tfn=\"[0000000000]\"]","");
  memchrnq_test1("@+VFX[tfn=\"[0000000000]\"","","");
}
#endif

static void **g_addargv;
static int   g_addargc;

static void init_argc(void) {
  g_addargv=NULL;
  g_addargc=0;
}

static char *strdupmem(const char *sz,size_t cb) {
  char *rv=malloc(cb+1);
  if (rv) {
    memcpy(rv,sz,cb);
    rv[cb]='\0';
  }
  return rv;
}

static void addtag(const char *sTag,size_t cbTag,const char *sValue,size_t cbValue) {
  //printf("%.*s=\"%.*s\"\n",cbTag,sTag,cbValue,sValue);
  unsigned n;
  for(n=0; n<NELEM(g_tagxlat); n++) if (g_tagxlat[n].cb_vsitag == cbTag && 0==memcmp(sTag,g_tagxlat[n].sz_vsitag,cbTag)) {
    g_tagxlat[n].b_set=1;
    g_tagxlat[n].msz_val=strdupmem(sValue,cbValue); // execvp makes this a memory leak which so far I can tell is unsolvable.
  }
}

// decode tags with delims @+VFX[] stripped: example tfn="1234567";tnm="John Doe"
static int decodetags(const char *szTags) {
  int rv=1; // not much use for rv=0 right now
  const char *sz_p1=szTags,*sz_pend=sz_p1+strlen(sz_p1);
  for(sz_p1=szTags; sz_p1<sz_pend; ) {
    const char *sz_p2=memchrnq(sz_p1,sz_pend-sz_p1,';');
    if (sz_p2 == NULL) sz_p2=sz_pend;
    const char *sz_peq=memchrnq(sz_p1,sz_p2-sz_p1,'=');
    if (sz_peq && sz_peq > sz_p1 && sz_peq < sz_p2) {
      const char *ps_tag1=sz_p1,*ps_tag2=sz_peq,*ps_val1=sz_peq+1,*ps_val2=sz_p2;
      while (ps_tag1<ps_tag2 && (*ps_tag1==' ' || *ps_tag1=='"')) ps_tag1++; // we support "tag"="value" even though there's no reason to do this.
      while (ps_tag2>ps_tag1 && (ps_tag2[-1]==' ' || ps_tag2[-1]=='"')) ps_tag2--;
      while (ps_val1<ps_val2 && (*ps_val1==' ' || *ps_val1=='"')) ps_val1++; // We trim "" from values because we're using _execvp()
      while (ps_val2>ps_val1 && (ps_val2[-1]==' ' || ps_val2[-1]=='"')) ps_val2--;
      addtag(ps_tag1,ps_tag2-ps_tag1,ps_val1,ps_val2-ps_val1);
    }
    sz_p1=sz_p2; if (*sz_p1) sz_p1++;
  }
  return rv;
}

// scan file extracting VSI-FAX tags. Write tagless data to output file.
// The tags are removed from the output whether recognized or not.
static void sendfaxvsi(int fdIn,int fdOut) {
  char s_buf[4096]; // the entire @+VFX[...] must fit in this buffer
  size_t cb_buf=0;
  size_t cb_read=1;
  while (1) {
    if (cb_read) {
      cb_read=read(fdIn,s_buf+cb_buf,sizeof(s_buf)-cb_buf);
      cb_buf += cb_read;
    }
    if (cb_buf==0) break;
    const char *p1=memstr(s_buf,cb_buf,S_FIND_VFX,CB_FIND_VFX);
    size_t cb_write=0,cb_skip=0;
    if (p1==NULL && (cb_read==0 || cb_buf<CB_FIND_VFX) ) {
      cb_write=cb_buf; // end of file or too little to make our search possible.
    } else if (p1==NULL) {
      cb_write=(cb_buf>CB_FIND_VFX)?cb_buf:(cb_buf-CB_FIND_VFX); // preserve end of buffer because our string might be partially there. this is what makes our search always work when other simple searches sometimes fail.
    } else if (p1!=s_buf) { // move @VFX+[ to beginning of buf
      cb_write=p1-s_buf;
    } else { // p1="@VFX+[" at beginning of buf
      char *p2=(char *)memchrnq(s_buf,cb_buf,']');
      if (p2) {
        *p2++='\0';
        if (decodetags(p1+CB_FIND_VFX) || 1) {
          cb_skip=p2-p1; 
        } else {
          p2[-1]=']';
          cb_write=p2-p1;
        }
      } else {
        cb_write=CB_FIND_VFX; // lots of values would work here.
      }
    }
    if (cb_write) {
      if (cb_write>cb_buf) cb_write=cb_buf;
      write(fdOut,s_buf,cb_write);
      cb_skip += cb_write;
    }
    if (cb_skip) {
      if (cb_skip>cb_buf) cb_skip=cb_buf;
      memmove(s_buf,s_buf+cb_skip,cb_buf-cb_skip);
      cb_buf -= cb_skip;
#ifdef __POCC__
      s_buf[cb_buf]='\0'; // easier to see in the debugger
#endif
    }
  }
}

// by definition argv is expected to end with a NULL at argv[argc]
static unsigned countargv(char **argv) {
  unsigned n;
  for(n=0; argv[n]; n++);
  return n;
}

// tack our new args on the end of the existing args
static char **buildargv(char **argvold) {
  int n_newargc=0;
  int n_oldargc;
  unsigned n;
  for(n=0; n<NELEM(g_tagxlat); n++) if (g_tagxlat[n].b_set) {
    n_newargc++;
    if (g_tagxlat[n].msz_val) n_newargc++; // switch with value
  }
  n_oldargc=countargv(argvold);
  n_newargc+=n_oldargc;
  size_t cb_newargv=sizeof(*argvold)*(n_newargc+1); // +1 for NULL which wasn't counted.
  char **argvnew=malloc(cb_newargv);
  if (argvnew) {
#ifdef __POCC__
    memset(argvnew,'\x90',cb_newargv);
#endif
    memcpy(argvnew,argvold,n_oldargc*sizeof(*argvnew));
    int argp2=n_oldargc;
    char *sz_name="";
    for(n=0; n<NELEM(g_tagxlat); n++) if (g_tagxlat[n].b_set) {
      argvnew[argp2]=g_tagxlat[n].sz_switch;
      argp2++;
      if (g_tagxlat[n].msz_val) {
        switch(g_tagxlat[n].n_id) {
        case XLAT_ID_NAME: 
          sz_name=g_tagxlat[n].msz_val; 
          argp2--;
          break;
        case XLAT_ID_PHONE: {
            char *sz_name_phone=malloc(strlen(sz_name)+1+strlen(g_tagxlat[n].msz_val)+1); // another memory leak made unsolvable by execvp
            if (sz_name_phone) {
              sprintf(sz_name_phone,"%s@%s",sz_name,g_tagxlat[n].msz_val); 
              argvnew[argp2]=sz_name_phone;
              argp2++; 
            }
          }
          break;
        default: 
          argvnew[argp2]=g_tagxlat[n].msz_val; 
          argp2++;
          break;
        }
      }
    }
    argvnew[argp2]=NULL;
  }
  return argvnew;
}

#ifndef STDIN_FILENO
#define STDIN_FILENO  (0)
#define STDOUT_FILENO (1)
#endif

// in Pelles C we read a test file, write the print file, and show the switches.
// in Linux we read from stdin, extract and filter tags, dup2 our filtered file back to stdin, then exec sendfax with the filtered file and the added switches.
int __cdecl main(int argc,char *argv[]) {
  init_argc();
  init_tagxlat();
  if (argc<=1) usage(argv); else {
    int fdin=STDIN_FILENO;
    int fdout; //=STDOUT_FILENO;
    char fnout[MAX_PATH];
#ifdef __POCC__
    sprintf(fnout,"sendfaxvsi.%u.txt",0);
#else
    sprintf(fnout,"/tmp/sendfaxvsi.%u.txt",getpid());
#endif
    fdout=open(fnout,O_RDWR|O_CREAT|O_TRUNC|O_BINARY|O_SEQUENTIAL,S_IREAD|S_IWRITE); /* see _creat() for defs */
    if (fdout == -1) return 1; // fdout=STDOUT_FILENO;
#ifdef __POCC__
    fdin=_open("test.vsi",O_RDONLY|O_BINARY|O_SEQUENTIAL,S_IREAD); /* see _creat() for defs */
    if (fdin == -1) fdin=STDIN_FILENO;
    memstrtest();
    memchrnq_test();
#endif
    sendfaxvsi(fdin,fdout);
    char **argv2=buildargv(argv+1);
#if defined(__POCC__)
    if (fdin != STDIN_FILENO) close(fdin);
    if (fdout != STDOUT_FILENO) close(fdout);
    printf("\n");
    printf("PATH=%s\n",getenv("PATH")); // This is short when launched from Pelles
    //execlp("CMD.EXE","CMD.EXE","/c","echo","EpicFail",NULL); // This doesn't work when launched from Pelles
    //execlp(argv[1],argv[1],"/c","echo","Fail",NULL);
    //execvp(argv[1],argv+2);
    free(argv2);
#else
    if (fdout != STDOUT_FILENO) {
      if (dup2(fdout,STDIN_FILENO) != -1) {
        close(fdout);
        unlink(fnout);
        lseek(STDIN_FILENO,0,0);
        //char z[8];
        //read(STDIN_FILENO
        //execlp("/bin/sh","/bin/sh","-c","echo mytest",NULL);
        //printf("execvpe %s %s\n",argv2[0],argv2[1]);
        //int n;
        //for(n=0; argv2[n]; n++) printf(" %s",argv2[n]); printf("\n");
        //fflush(stdout);
        //argv2[0]="/bin/sh"; argv2[1]="-c"; argv2[2]="echo mytest2"; argv2[3]=NULL;
        //argv2[0]="cat"; argv2[1]=NULL;
        if (execvp(argv2[0],argv2)<0) perror(argv2[0]);
      }
    }
#endif
  }
  return 254;
}
