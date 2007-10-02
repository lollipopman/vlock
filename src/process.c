/* process.c -- child process routines for vlock,
 *              the VT locking program for linux
 *
 * This program is copyright (C) 2007 Frank Benkstein, and is free
 * software which is freely distributable under the terms of the
 * GNU General Public License version 2, included as the file COPYING in this
 * distribution.  It is NOT public domain software, and any
 * redistribution not permitted by the GNU General Public License is
 * expressly forbidden without prior written permission from
 * the author.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <fcntl.h>

#include "process.h"

/* Do nothing. */
static void ignore_sigalarm(int __attribute__((unused)) signum)
{
}

bool wait_for_death(pid_t pid, long sec, long usec)
{
  int status;
  struct sigaction act;
  struct sigaction oldact;
  struct itimerval timer;
  struct itimerval otimer;
  bool result;

  /* Ignore SIGALRM.  The handler must be a real function instead of SIG_IGN
   * otherwise waitpid() would not get interrupted.
   *
   * There is a small window here where a previously set alarm might be
   * ignored.  */
  sigemptyset(&act.sa_mask);
  act.sa_handler = ignore_sigalarm;
  act.sa_flags = 0;
  sigaction(SIGALRM, &act, &oldact);

  /* Initialize the timer. */
  timer.it_value.tv_sec = sec;
  timer.it_value.tv_usec = usec;
  /* No repetition. */
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 0;

  /* Set the timer. */
  setitimer(ITIMER_REAL, &timer, &otimer);

  /* Wait until the child exits or the timer fires. */
  result = (waitpid(pid, &status, 0) == pid);

  /* Possible race condition.  If an alarm was set before it may get ignored.
   * This is probably better than getting killed by our own alarm. */

  /* Restore the timer. */
  setitimer(ITIMER_REAL, &otimer, NULL);

  /* Restore signal handler for SIGALRM. */
  sigaction(SIGALRM, &oldact, NULL);

  return result;
}

/* Try hard to kill the given child process. */
void ensure_death(pid_t pid)
{
  int status;

  switch (waitpid(pid, &status, WNOHANG)) {
    case -1:
      /* Not your child? */
      return;
    case 0:
      /* Not dead yet.  Continue. */
      break;
    default:
      /* Already dead.  Nothing to do. */
      return;
  }

  /* Send SIGTERM. */
  (void) kill(pid, SIGTERM);

  /* SIGTERM handler (if any) has 500ms to finish. */
  if (wait_for_death(pid, 0, 500000L))
    return;

  // Send SIGKILL. */
  (void) kill(pid, SIGKILL);
  /* Child may be stopped.  Send SIGCONT just to be sure. */
  (void) kill(pid, SIGCONT);

  /* Wait until dead.  Shouldn't take long. */
  (void) waitpid(pid, &status, 0);
}

/* Close all possibly open file descriptors except STDIN_FILENO, STDOUT_FILENO
 * and STDERR_FILENO. */
void close_all_fds(void)
{
  struct rlimit r;
  int maxfd;

  /* Get the maximum number of file descriptors. */
  if (getrlimit(RLIMIT_NOFILE, &r) == 0)
    maxfd = r.rlim_cur;
  else
    /* Hopefully safe default. */
    maxfd = 1024;

  /* Close all possibly open file descriptors except STDIN_FILENO,
   * STDOUT_FILENO and STDERR_FILENO. */
  for (int i = 0; i < maxfd; i++) {
    switch (i) {
      case STDIN_FILENO:
      case STDOUT_FILENO:
      case STDERR_FILENO:
        break;
      default:
        (void) close(i);
        break;
    }
  }
}

static int open_devnull(void)
{
  static int devnull_fd = -1;

  if (devnull_fd < 0)
    devnull_fd = open("/dev/null", O_RDWR);

  return devnull_fd;
}

bool create_child(struct child_process *child)
{
  int stdin_pipe[2];
  int stdout_pipe[2];
  int stderr_pipe[3];

  if (child->stdin_fd == REDIRECT_PIPE) {
    if (pipe(stdin_pipe) < 0)
      return false;
  } 

  if (child->stdout_fd == REDIRECT_PIPE) {
    if (pipe(stdout_pipe) < 0)
      goto stdout_pipe_failed;
  }

  if (child->stderr_fd == REDIRECT_PIPE) {
    if (pipe(stderr_pipe) < 0)
      goto stderr_pipe_failed;
  }

  child->pid = fork();

  if (child->pid == 0) {
    /* Child. */
    if (child->stdin_fd == REDIRECT_PIPE)
      (void) dup2(STDIN_FILENO, stdin_pipe[0]);
    else if (child->stdin_fd == REDIRECT_DEV_NULL)
      (void) dup2(STDIN_FILENO, open_devnull());
    else if (child->stdin_fd != NO_REDIRECT)
      (void) dup2(STDIN_FILENO, child->stdin_fd);

    if (child->stdout_fd == REDIRECT_PIPE)
      (void) dup2(STDIN_FILENO, stdout_pipe[1]);
    else if (child->stdout_fd == REDIRECT_DEV_NULL)
      (void) dup2(STDIN_FILENO, open_devnull());
    else if (child->stdout_fd != NO_REDIRECT)
      (void) dup2(STDIN_FILENO, child->stdout_fd);

    if (child->stderr_fd == REDIRECT_PIPE)
      (void) dup2(STDIN_FILENO, stderr_pipe[1]);
    else if (child->stderr_fd == REDIRECT_DEV_NULL)
      (void) dup2(STDIN_FILENO, open_devnull());
    else if (child->stderr_fd != NO_REDIRECT)
      (void) dup2(STDIN_FILENO, child->stderr_fd);

    (void) close_all_fds();

    (void) setgid(getgid());
    (void) setuid(getuid());

    if (child->function != NULL) {
      _exit(child->function(child->argument));
    } else {
      execv(child->path, (char *const*)child->argv);
    }

    _exit(1);
  }

  if (child->pid < 0)
    goto fork_failed;

  if (child->stdin_fd == REDIRECT_PIPE) {
    /* Write end. */
    child->stdin_fd = stdin_pipe[1];
    /* Read end. */
    (void) close(stdin_pipe[0]);
  }

  if (child->stdout_fd == REDIRECT_PIPE) {
    /* Read end. */
    child->stdout_fd = stdout_pipe[0];
    /* Write end. */
    (void) close(stdout_pipe[1]);
  }

  if (child->stderr_fd == REDIRECT_PIPE) {
    /* Read end. */
    child->stderr_fd = stderr_pipe[0];
    /* Write end. */
    (void) close(stderr_pipe[1]);
  }

  return true;

fork_failed:
  if (child->stderr_fd == REDIRECT_PIPE) {
    (void) close(stderr_pipe[0]);
    (void) close(stderr_pipe[1]);
  }

stderr_pipe_failed:
  if (child->stdout_fd == REDIRECT_PIPE) {
    (void) close(stdout_pipe[0]);
    (void) close(stdout_pipe[1]);
  }

stdout_pipe_failed:
  if (child->stdin_fd == REDIRECT_PIPE) {
    (void) close(stdin_pipe[0]);
    (void) close(stdin_pipe[1]);
  }

  return false;
}
