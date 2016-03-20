/* 
 * tsh - A tiny shell program with job control
 * 
 * <
Name: Dhaval Panjwani
 * 	 Id: 201401162
 * >
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
	pid_t pid;              /* job PID */
	int jid;                /* job ID [1, 2, ...] */
	int state;              /* UNDEF, BG, FG, or ST */
	char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_fgbg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
	char c;
	char cmdline[MAXLINE];
	int emit_prompt = 1; /* emit prompt (default) */

	/* Redirect stderr to stdout (so that driver will get all output
	 * on the pipe connected to stdout) */
	dup2(1, 2);

	/* Parse the command line */
	while ((c = getopt(argc, argv, "hvp")) != EOF) {
		switch (c) {
			case 'h':             /* print help message */
				usage();
				break;
			case 'v':             /* emit additional diagnostic info */
				verbose = 1;
				break;
			case 'p':             /* don't print a prompt */
				emit_prompt = 0;  /* handy for automatic testing */
				break;
			default:
				usage();
		}
	}

	/* Install the signal handlers */

	/* These are the ones you will need to implement */
	Signal(SIGINT,  sigint_handler);   /* ctrl-c */
	Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
	Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

	/* This one provides a clean way to kill the shell */
	Signal(SIGQUIT, sigquit_handler); 

	/* Initialize the job list */
	initjobs(jobs);

	/* Execute the shell's read/eval loop */
	while (1) {

		/* Read command line */
		if (emit_prompt) {
			printf("%s", prompt);
			fflush(stdout);
		}
		if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
			app_error("fgets error");
		if (feof(stdin)) { /* End of file (ctrl-d) */
			fflush(stdout);
			exit(0);
		}

		/* Evaluate the command line */
		eval(cmdline);
		fflush(stdout);
		fflush(stdout);
	} 

	exit(0); /* control never reaches here */
}

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 */
void eval(char *cmdline) 
{
	char **argv=(char **) malloc(2048);
	struct job_t *jbid;
	sigset_t cone;
	int ret=parseline(cmdline,argv),cpid;
	if(ret==1){    
		return;
	}
	else{
		int retcmd=builtin_cmd(argv);
		if(retcmd==1){
			return;
		}
		else
		{
			sigemptyset(&cone);               // initializing signal set mask
			sigaddset(&cone, SIGCHLD);        // adding SIGCHLD signal to the set mask

			sigprocmask(SIG_BLOCK,&cone,NULL); /* blocking to prevent parent from receive SIGCHLD signal before crearing this job process is initiated in child process  */
			
			if((cpid=fork())==0){			//	  child process to execute non-builtin command
				int fail=0;
				setpgrp();				// set the group
				sigprocmask(SIG_UNBLOCK,&cone,NULL);    // unblocking for child process 
				fail=execv(*argv,argv);			// execing the non-builltin command
				printf("%s: Command not found \n",argv[0]);   
				exit(0);

			}
		
		}
// below is for foreground jobs
		if(ret==0)
		{
			addjob(jobs,cpid,FG,cmdline);    // add the foreground jobs
			sigprocmask(SIG_UNBLOCK,&cone,NULL); // unblock signals for  parent as child is now added in jobs
			waitfg(cpid);			// waiting for signal as it is still in foreground
		}
		else
		{
			addjob(jobs,cpid,BG,cmdline);   // add the background jobs
			sigprocmask(SIG_UNBLOCK,&cone,NULL);    // unblock signals for  parent as child is now added in jobs

			jbid=getjobpid(jobs, cpid);                   // getting the job
                                printf("[%d] (%d) %s", jbid->jid, jbid->pid, jbid->cmdline);
		}
	}

	return;

}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
	static char array[MAXLINE]; /* holds local copy of command line */
	char *buf = array;          /* ptr that traverses command line */
	char *delim;                /* points to first space delimiter */
	int argc;                   /* number of args */
	int bg;                     /* background job? */

	strcpy(buf, cmdline);
	buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
	while (*buf && (*buf == ' ')) /* ignore leading spaces */
		buf++;

	/* Build the argv list */
	argc = 0;
	if (*buf == '\'') {
		buf++;
		delim = strchr(buf, '\'');
	}
	else {
		delim = strchr(buf, ' ');
	}

	while (delim) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* ignore spaces */
			buf++;

		if (*buf == '\'') {
			buf++;
			delim = strchr(buf, '\'');
		}
		else {
			delim = strchr(buf, ' ');
		}
	}
	argv[argc] = NULL;

	if (argc == 0)  /* ignore blank line */
		return 1;

	/* should the job run in the background? */
	if ((bg = (*argv[argc-1] == '&')) != 0) {
		argv[--argc] = NULL;
		return 3;
	}
	return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
	if(strcmp("quit",argv[0])==0)
	{	int i=0;
		for(;i<MAXJOBS;i++)
		{
			if(jobs[i].state==ST)
			{
				printf("[%d] (%d) Stopped", jobs[i].jid, jobs[i].pid);         // Printing the stopped jobs
                       		return 1;                                  
			}
		}
		exit(0);
	}
	if(strcmp("jobs",argv[0])==0)
	{
		listjobs(jobs);                          // Printing Jobs
		return 1;
	}
	if(strcmp("fg",argv[0])==0)
	{
		do_fgbg(argv);				// Running inbuilt FG command
		return 1;
	}
	if(strcmp("bg",argv[0])==0)
	{
		do_fgbg(argv);                          // Running inbuilt BG command
		return 1;
	}

	return 0;     // Returns 0 if it is user based command
}

/* 
 * do_fgbg - Execute the builtin bg and fg commands
 */
void do_fgbg(char **argv) 
{   
	if(argv[1]=='\0')
	{ 
		if(strcmp(argv[0],"fg")==0){
			printf("PID or %% job id argument is prerequisite for fg command \n");
			return;
		}										// check if fg and bg command are followed by PID or JID
		if(strcmp(argv[0],"bg")==0){
			printf("PID or %% job id argument is prerequisite for bg command \n");
			return;
		}
	}
	else
	{
		if(argv[1][0]=='%' && argv[2]=='\0')
		{										// Fetching the JID from the command
			int count=1,flag=0,jobid=0,units=1;
			for(;;)
			{
				if(argv[1][count]=='\0')
					break;	
				if(argv[1][count]>='0' && argv[1][count]<='9')
					count++;
				else
				{
					flag=1;
					break;
				}    
			}
			if(flag==1){
				if(strcmp(argv[0],"fg")==0){        				// Check if JID is not a number
					printf("Argument should be a PID or %% job ID in fg \n");
					return;}
				else if(strcmp(argv[0],"bg")==0){
					printf("Argument should be a PID or %% job ID in bg \n");
					return;} 
			}
			else{
				while(count!=1){
					count--;
					jobid=jobid+(argv[1][count]-'0')*units;
					units*=10;
				}
printf("   %d   \n ",jobid);
				struct job_t *entry=getjobjid(jobs,jobid);
				if(entry==NULL)			// getting the job from the given jobid and checking if such job exists in the job table
					{
						if(strcmp(argv[0],"fg")==0){	
							printf("No job with %%d jobid found in fg\n",jobid);
							return;     
						}
						if(strcmp(argv[0],"bg")==0){	
							printf("No job with %%d jobid found in bg\n",jobid);
							return;    
							} 
					}
					else
					{
						if((entry->state==ST) && (strcmp(argv[0],"bg")==0))
						{
							entry->state=BG;
							kill(-(entry->pid),SIGCONT);
							printf("Changed to BG from ST");
						}
						if((entry->state==ST) && (strcmp(argv[0],"fg")==0))
						{
							entry->state=FG;
							kill(-(entry->pid),SIGCONT);
							waitfg(entry->pid);
							printf("Changed to FG from ST");
						}
						if((entry->state==BG) && (strcmp(argv[0],"fg")==0))
						{
							entry->state=FG;
							waitfg(entry->pid);
							printf("Changed to FG from BG");
						}
					}


			}

		}
		else{
			int j=0,bit=0,PID=0,units=1;
			while(argv[1][j]!='\0'){
				if(argv[1][j]>='0' && argv[1][j]<='9')       		//getting the PID of the job given in the command
					j++;
				else{
					bit=1;
					break;
				}
			}
			if(bit==1){
				if(strcmp(argv[0],"fg")==0){				//  if PID is not a number
					printf("Argument should be a PID or %% job ID in fg \n");
					return;}
				else if(strcmp(argv[0],"bg")==0){
					printf("Argument should be a PID or %% job ID in bg \n");
					return;}
			}
			else{
				while(j!=0){
					j--;
					PID=PID+(argv[1][j]-'0')*units;
					units*=10;
				}
				struct job_t *entry=getjobpid(jobs,PID);
				if(entry==NULL)				// getting the job from the given PID and checking if such job exists
				{
					printf("No process with PID : %d\n",PID);
					return;     
				}
				else
				{
					if((entry->state==ST) && (strcmp(argv[0],"bg")==0))
					{
						entry->state=BG;
						kill(-(entry->pid),SIGCONT);      //    for bg, set the state to BG and wait for background jobs to terminate
						printf("Changed to BG from ST");
					}
					if((entry->state==ST) && (strcmp(argv[0],"fg")==0))
					{
						entry->state=FG;
						kill(-(entry->pid),SIGCONT);   // for fg argument, set the state to FG and wait for background jobs to terminate
						waitfg(entry->pid);
						printf("Changed to FG from ST");
					}
					if((entry->state==BG) && (strcmp(argv[0],"fg")==0))
					{
						entry->state=FG;
						waitfg(entry->pid);
						printf("Changed to FG from BG");
					}
				}
				//printf("pid is %d\n",pid);
			}
		}
	}

	return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
	struct job_t *temp=getjobpid(jobs,pid);         // getting JOB from PID
	while((*temp).state==FG)                        // check if it is still foreground
	{
		sleep(1);
	}
	return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
	int stat,cpid;
	while((cpid=waitpid(fgpid(jobs),&stat,WNOHANG|WUNTRACED))>0)				// If child terminated normally
	{
			if(WIFEXITED(stat))
			{
				deletejob(jobs,cpid);
			}
			else if(WIFSIGNALED(stat))			// If child terminated due to sigint
			{
				psignal(WTERMSIG(stat)," Terminated due to : ");
				deletejob(jobs,cpid);
			}
			else if(WIFSTOPPED(stat))                         // If child terminated due to sigtstp 
			{
				getjobpid(jobs,cpid)->state=ST;
				psignal(WSTOPSIG(stat)," Stopped due to : ");
			}       
	} 
	return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
	pid_t fg=fgpid(jobs);
	if(fg!=0)
	{
			kill(-fg,SIGINT);
 			deletejob(jobs,fg);
                  

	}
	return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
	pid_t fg=fgpid(jobs);
	if(fg!=0)
	{
			kill(-fg,SIGTSTP);
			getjobpid(jobs,fg)->state=ST;
	}
	return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
	job->pid = 0;
	job->jid = 0;
	job->state = UNDEF;
	job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
	int i, max=0;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid > max)
			max = jobs[i].jid;
	return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
	int i;

	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == 0) {
			jobs[i].pid = pid;
			jobs[i].state = state;
			jobs[i].jid = nextjid++;
			if (nextjid > MAXJOBS)
				nextjid = 1;
			strcpy(jobs[i].cmdline, cmdline);
			if(verbose){
				printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
			}
			return 1;
		}
	}
	printf("Tried to create too many jobs\n");
	return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
	int i;

	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == pid) {
			clearjob(&jobs[i]);
			nextjid = maxjid(jobs)+1;
			return 1;
		}
	}
	return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].state == FG)
			return jobs[i].pid;
	return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
	int i;

	if (pid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid)
			return &jobs[i];
	return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
	int i;

	if (jid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].jid == jid)
			return &jobs[i];
	return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
	int i;

	if (pid < 1)
		return 0;
	for (i = 0; i < MAXJOBS; i++)
		if (jobs[i].pid == pid) {
			return jobs[i].jid;
		}
	return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
	int i;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid != 0) {
			printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
			switch (jobs[i].state) {
				case BG: 
					printf("Running ");
					break;
				case FG: 
					printf("Foreground ");
					break;
				case ST: 
					printf("Stopped ");
					break;
				default:
					printf("listjobs: Internal error: job[%d].state=%d ", 
							i, jobs[i].state);
			}
			printf("%s", jobs[i].cmdline);
		}
	}
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
	printf("Usage: shell [-hvp]\n");
	printf("   -h   print this message\n");
	printf("   -v   print additional diagnostic information\n");
	printf("   -p   do not emit a command prompt\n");
	exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
	fprintf(stdout, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
	fprintf(stdout, "%s\n", msg);
	exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
	struct sigaction action, old_action;

	action.sa_handler = handler;  
	sigemptyset(&action.sa_mask); /* block sigs of type being handled */
	action.sa_flags = SA_RESTART; /* restart syscalls if possible */

	if (sigaction(signum, &action, &old_action) < 0)
		unix_error("Signal error");
	return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
	printf("Terminating after receipt of SIGQUIT signal\n");
	exit(1);
}



