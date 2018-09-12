#ifndef LLVM_DATARANDO_RUNTIME_WRAPPER_H
#define LLVM_DATARANDO_RUNTIME_WRAPPER_H

/* Here is some macro magic to make defining wrapper functions easier. Users of
   this macro should first define an appropriate DR_WR macro for the desired
   use. The parameters to DR_WR are the original name, the wrapper name, the
   type of the return value, and the type of the parameter list. */
#define DRRT_WRAPPERS                                                   \
  DR_WR(__not_main, drrt_main, int, (int, char**, mask_t, mask_t))      \
  DR_WR(strtol, drrt_strtol, long int , (const char *, char **, int, mask_t, mask_t, mask_t)) \
  DR_WR(strrchr, drrt_strrchr, char *, (const char *, int , mask_t , mask_t )) \
  DR_WR(openlog, drrt_openlog, void , (const char *, int , int , mask_t )) \
  DR_WR(syslog, drrt_syslog, void , (int , const char *, mask_t, ...))  \
  DR_WR(strcmp, drrt_strcmp, int , (const char *, const char *, mask_t , mask_t )) \
  DR_WR(sprintf, drrt_sprintf, int , (char *, const char *, mask_t , mask_t, ...)) \
  DR_WR(printf, drrt_printf, int , (const char *, mask_t, ...))         \
  DR_WR(puts, drrt_puts, int , (const char *, mask_t ))                 \
  DR_WR(fputs, drrt_fputs, int , (const char *, FILE *, mask_t))        \
  DR_WR(atoi, drrt_atoi, int , (const char *, mask_t ))                 \
  DR_WR(fprintf, drrt_fprintf, int , (FILE *, const char *, mask_t, ...)) \
  DR_WR(snprintf, drrt_snprintf, int , (char *, size_t , const char *, mask_t , mask_t, ...)) \
  DR_WR(getaddrinfo, drrt_getaddrinfo, int , (const char *, const char *, const struct addrinfo *, struct addrinfo **, mask_t , mask_t , mask_t , mask_t, mask_t , mask_t , mask_t , mask_t, mask_t )) \
  DR_WR(freeaddrinfo, drrt_freeaddrinfo, void , (struct addrinfo *, mask_t, mask_t, mask_t )) \
  DR_WR(chdir, drrt_chdir, int , (const char *, mask_t ))               \
  DR_WR(strlen, drrt_strlen, size_t, (const char*, mask_t))             \
  DR_WR(getcwd, drrt_getcwd, char *, (char *, size_t , mask_t , mask_t )) \
  DR_WR(strcat, drrt_strcat, char *, (char *, const char *, mask_t , mask_t , mask_t )) \
  DR_WR(getrlimit, drrt_getrlimit, int , (int , struct rlimit *, mask_t )) \
  DR_WR(gethostname, drrt_gethostname, int , (char *, size_t , mask_t )) \
  DR_WR(strdup, drrt_strdup, char *, (const char *, mask_t, mask_t ))   \
  DR_WR(setsockopt, drrt_setsockopt, int , (int , int , int , const void *, socklen_t , mask_t )) \
  DR_WR(getsockopt, drrt_getsockopt, int , (int , int , int , void *, socklen_t *, mask_t , mask_t )) \
  DR_WR(bind, drrt_bind, int , (int , const struct sockaddr *, socklen_t , mask_t )) \
  DR_WR(gettimeofday, drrt_gettimeofday, int , (struct timeval *, struct timezone *, mask_t , mask_t )) \
  DR_WR(poll, drrt_poll, int , (struct pollfd *, nfds_t , int , mask_t )) \
  DR_WR(accept, drrt_accept, int , (int , struct sockaddr *, socklen_t *, mask_t , mask_t )) \
  DR_WR(read, drrt_read, ssize_t , (int , void *, size_t , mask_t ))    \
  DR_WR(write, drrt_write, ssize_t , (int , void *, size_t , mask_t )) \
  DR_WR(writev, drrt_writev, ssize_t , (int , const struct iovec *, int , mask_t, mask_t )) \
  DR_WR(strpbrk, drrt_strpbrk, char *, (const char *, const char *, mask_t , mask_t , mask_t )) \
  DR_WR(strspn, drrt_strspn, size_t , (const char *, const char *, mask_t , mask_t )) \
  DR_WR(strcasecmp, drrt_strcasecmp, int , (const char *, const char *, mask_t , mask_t )) \
  DR_WR(strncasecmp, drrt_strncasecmp, int , (const char *, const char *, size_t , mask_t , mask_t )) \
  DR_WR(strchr, drrt_strchr, char *, (const char *, int , mask_t , mask_t )) \
  DR_WR(strcpy, drrt_strcpy, char *, (char *, const char *, mask_t , mask_t , mask_t )) \
  DR_WR(strncpy, drrt_strncpy, char *, (char *, const char *, size_t , mask_t , mask_t , mask_t )) \
  DR_WR(readlink, drrt_readlink, ssize_t , (const char *, char *, size_t , mask_t , mask_t )) \
  DR_WR(strftime, drrt_strftime, size_t , (char *, size_t , const char *, const struct tm *, mask_t , mask_t , mask_t, mask_t )) \
  DR_WR(gmtime, drrt_gmtime, struct tm *, (const time_t *, mask_t , mask_t, mask_t )) \
  DR_WR(strstr, drrt_strstr, char *, (const char *, const char *, mask_t , mask_t, mask_t )) \
  DR_WR(stat, drrt_stat, int , (const char *, struct stat *, mask_t , mask_t )) \
  DR_WR(open, drrt_open, int, (const char *, int , mask_t, mask_t, ...)) \
  DR_WR(__xstat, drrt__xstat, int , (int , const char * , struct stat * , mask_t , mask_t )) \
  DR_WR(fileno, fileno, int, (FILE*))                                   \
  DR_WR(strtoll, drrt_strtoll, long long int, (const char*, char **, int, mask_t, mask_t, mask_t)) \
  DR_WR(fopen, drrt_fopen, FILE*, (const char*, const char*, mask_t, mask_t)) \
  DR_WR(memchr, drrt_memchr, void*, (const void*,int,size_t,mask_t,mask_t)) \
  DR_WR(fdopen, drrt_fdopen, FILE*, (int,const char*,mask_t))           \
  DR_WR(initgroups, drrt_initgroups, int, (const char*, gid_t, mask_t)) \
  DR_WR(fread, drrt_fread, size_t, (void*,size_t,size_t,FILE*,mask_t))  \
  DR_WR(perror, drrt_perror, void, (const char *, mask_t))              \
  DR_WR(__strdup, drrt_strdup, char *, (const char *, mask_t, mask_t )) \
  DR_WR(fgets, drrt_fgets, char*, (char*,int,FILE*,mask_t,mask_t))      \
  DR_WR(getsockname, drrt_getsockname, int, (int,struct sockaddr*,socklen_t*,mask_t,mask_t)) \
  DR_WR(setrlimit, drrt_setrlimit, int, (int,const struct rlimit*,mask_t)) \
  DR_WR(ctime, drrt_ctime, char*, (const time_t *, mask_t, mask_t))     \
  DR_WR(chroot, drrt_chroot, int, (const char*, mask_t))                \
  DR_WR(localtime, drrt_localtime, struct tm*, (const time_t*, mask_t,mask_t,mask_t)) \
  DR_WR(fclose, fclose, int, (FILE*))                                   \
  DR_WR(fflush, fflush, int, (FILE*))                                   \
  DR_WR(pipe, drrt_pipe, int, (int*,mask_t))                            \
  DR_WR(strncmp, drrt_strncmp, int, (const char*,const char*,size_t, mask_t, mask_t)) \
  DR_WR(waitpid, drrt_waitpid, pid_t, (pid_t,int*,int, mask_t))         \
  DR_WR(fwrite, drrt_fwrite, size_t, (const void *, size_t, size_t, FILE*, mask_t)) \
  DR_WR(strcspn, drrt_strcspn, size_t, (const char*, const char*, mask_t, mask_t)) \
  DR_WR(getnameinfo, drrt_getnameinfo, int, (const struct sockaddr*, socklen_t, char*, socklen_t, char*, socklen_t, int, mask_t, mask_t, mask_t)) \
  DR_WR(getpwnam, drrt_getpwnam, struct passwd *, (const char *, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t)) \
  DR_WR(execve, drrt_execve, int, (const char *, char *const[], char *const[], mask_t,mask_t, mask_t, mask_t, mask_t)) \
  DR_WR(__lxstat, drrt___lxstat, int, (int, const char *, struct stat *, mask_t, mask_t)) \
  DR_WR(setenv, drrt_setenv, int, (const char *, const char *, int, mask_t, mask_t)) \
  DR_WR(unsetenv, drrt_unsetenv, int, (const char *, mask_t))           \
  DR_WR(memcmp, drrt_memcmp, int, (const void *, const void *, size_t, mask_t, mask_t)) \
  DR_WR(memmove, drrt_memmove, void*, (void *, const void *, size_t, mask_t, mask_t, mask_t)) \
  DR_WR(localtime_r, drrt_localtime_r, struct tm*, (const time_t *, struct tm *result, mask_t, mask_t, mask_t, mask_t, mask_t)) \
  DR_WR(gmtime_r, drrt_gmtime_r, struct tm*, (const time_t *, struct tm *result, mask_t, mask_t, mask_t, mask_t, mask_t)) \
  DR_WR(sigemptyset, drrt_sigemptyset, int, (sigset_t *, mask_t ))      \
  DR_WR(getpwnam_r, drrt_getpwnam_r, int, (const char *, struct passwd *, char *, size_t , struct passwd **, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t)) \
  DR_WR(select, drrt_select, int, (int, fd_set *, fd_set *, fd_set *, struct timeval *, mask_t, mask_t, mask_t, mask_t)) \
  DR_WR(sigaction, drrt_sigaction, int, (int, const struct sigaction*, struct sigaction*, mask_t, mask_t)) \
  DR_WR(fcntl, drrt_fcntl, int, (int, int, mask_t, ...))                \
  DR_WR(pcre_compile2, drrt_pcre_compile2, pcre*, (const char *, int, int *, const char **, int *, const unsigned char *, mask_t, mask_t, mask_t, mask_t, mask_t, mask_t)) \
  DR_WR(calloc, drrt_calloc, void*, (size_t, size_t, mask_t))           \
  DR_WR(realloc, drrt_realloc, void*, (void * , size_t, mask_t, mask_t)) \
  DR_WR(getrusage, drrt_getrusage, int, (int, struct rusage*, mask_t))  \
  DR_WR(times, drrt_times, clock_t, (struct tms*, mask_t))              \
  DR_WR(strncat, drrt_strncat, char *, (char *, const char*, size_t, mask_t, mask_t, mask_t))\
  DR_WR(time, drrt_time, time_t, (time_t*, mask_t))\
  DR_WR(ttyname, drrt_ttyname, char*, (int, mask_t))\
  DR_WR(strtok, drrt_strtok, char*, (char*,const char*, mask_t, mask_t, mask_t))\
  DR_WR(memset, drrt_memset, void*, (void*,int,size_t, mask_t, mask_t))\
  DR_WR(posix_memalign, drrt_posix_memalign, int, (void **, size_t , size_t , mask_t , mask_t ))\
  DR_WR(sscanf, drrt_sscanf, int, (const char *, const char *, mask_t , mask_t , mask_t , ...)) \
  DR_WR(__isoc99_sscanf, drrt_sscanf, int, (const char *, const char *, mask_t , mask_t , mask_t , ...)) \
  DR_WR(fscanf, drrt_fscanf, int, (FILE *, const char *, mask_t , mask_t , ...))\
  DR_WR(__isoc99_fscanf, drrt_fscanf, int, (FILE *, const char *, mask_t , mask_t , ...))\
  DR_WR(scanf, drrt_scanf, int, (const char *, mask_t , mask_t , ...))\
  DR_WR(getenv, drrt_getenv, const char *, (const char*, mask_t, mask_t))\
  DR_WR(sleep, sleep, unsigned int, (unsigned int))\
  DR_WR(exit, exit, void, (int))\
  DR_WR(rand, rand, int, (void))\
  DR_WR(recv, drrt_recv, ssize_t, (int, void *, size_t , int, mask_t))\
  DR_WR(strtod, drrt_strtod, double, (const char *, char **, mask_t , mask_t , mask_t))

#endif /* LLVM_DATARANDO_RUNTIME_WRAPPER_H */
