/*
 * Copyright (C) 2004-2009 Georgy Yunaev gyunaev@ulduzsoft.com
 *
 * This example is free, and not covered by LGPL license. There is no
 * restriction applied to their modification, redistribution, using and so on.
 * You can study them, modify them, use them in your own program - either
 * completely or partially. By using it you may give me some credits in your
 * program, but you don't have to.
 *
 *
 * This example tests most features of libirc. It can join the specific
 * channel, welcoming all the people there, and react on some messages -
 * 'help', 'quit', 'dcc chat', 'dcc send', 'ctcp'. Also it can reply to
 * CTCP requests, receive DCC files and accept DCC chats.
 *
 * Features used:
 * - nickname parsing;
 * - handling 'channel' event to track the messages;
 * - handling dcc and ctcp events;
 * - using internal ctcp rely procedure;
 * - generating channel messages;
 * - handling dcc send and dcc chat events;
 * - initiating dcc send and dcc chat.
 *
 * $Id: irctest.c 94 2012-01-18 08:04:49Z gyunaev $
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/eventfd.h>


#include "libircclient.h"
#include "archive.h"
#include "archive_entry.h"


/*
 * We store data in IRC session context.
 */
typedef struct
{
    char    * channel;
    char    * nick;
    struct file_thread_args* file_thread;
} irc_ctx_t;

static int
copy_data(struct archive *ar, struct archive *aw)
{
  int r;
  const void *buff;
  size_t size;
  la_int64_t offset;

  for (;;) {
    r = archive_read_data_block(ar, &buff, &size, &offset);
    if (r == ARCHIVE_EOF)
      return (ARCHIVE_OK);
    if (r < ARCHIVE_OK)
      return (r);
    r = archive_write_data_block(aw, buff, size, offset);
    if (r < ARCHIVE_OK) {
      fprintf(stderr, "%s\n", archive_error_string(aw));
      return (r);
    }
  }
}

static char*
extract(const char *filename)
{
    struct archive *a;
    struct archive *ext;
    struct archive_entry *entry;
    int flags;
    int r;

    char *search_result_fname = NULL;

    /* Select which attributes we want to restore. */
    flags = ARCHIVE_EXTRACT_TIME;
    flags |= ARCHIVE_EXTRACT_PERM;
    flags |= ARCHIVE_EXTRACT_ACL;
    flags |= ARCHIVE_EXTRACT_FFLAGS;

    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);
    if ((r = archive_read_open_filename(a, filename, 10240)))
        exit(1);
    for (;;) {
            r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF)
          break;

        if (r < ARCHIVE_OK)
          fprintf(stderr, "%s\n", archive_error_string(a));
        if (r < ARCHIVE_WARN)
          exit(1);

        const char *fname = archive_entry_pathname(entry);
        if (strncasecmp(fname, "search", 6) == 0)
        {
            search_result_fname = realloc(search_result_fname, strlen(fname) + 1);
            strcpy(search_result_fname, fname);
        }

        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK)
          fprintf(stderr, "%s\n", archive_error_string(ext));
        else if (archive_entry_size(entry) > 0) {
          r = copy_data(a, ext);
          if (r < ARCHIVE_OK)
            fprintf(stderr, "%s\n", archive_error_string(ext));
          if (r < ARCHIVE_WARN)
            exit(1);
        }

        r = archive_write_finish_entry(ext);
        if (r < ARCHIVE_OK)
          fprintf(stderr, "%s\n", archive_error_string(ext));
        if (r < ARCHIVE_WARN)
          exit(1);
    }
    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return search_result_fname;
}

static void
search_result_display(const char* fname)
{
    FILE* fp = fopen(fname, "r");
    char *buf = NULL;
    size_t buffer_size = 0;
    int counter = 0;
    while (-1 != getline(&buf, &buffer_size, fp))
    {
        printf("%d: %s", counter++, buf);
    }
    fclose(fp);
    free(buf);
}

struct control_thread_args
{
    irc_session_t *session;
    char *channel;
    int extract_efd;
    char *extract_fname;
};
static void * control_thread(void * vargs)
{
    struct control_thread_args *args = vargs;
    fd_set rfds;
    int retval;
    char *buf = NULL;
    size_t buffer_size = 0;

    while ( 1 )
    {
        /* Watch stdin (fd 0) to see when it has input. */
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
        FD_SET(args->extract_efd, &rfds);

        retval = select(args->extract_efd + 1, &rfds, NULL, NULL, NULL);
        if (retval < 0)
        {
            printf("select error\n");
        }

        if (FD_ISSET(0, &rfds))
        {
            getline(&buf, &buffer_size, stdin);
            irc_cmd_msg (args->session, args->channel, buf);
        }
        if (FD_ISSET(args->extract_efd, &rfds))
        {
            uint64_t tmp;
            read(args->extract_efd, &tmp, sizeof(uint64_t));
            search_result_display(args->extract_fname);
        }
    }

    free(buf);
    free(vargs);
    return 0;
}

struct file_thread_args
{
    struct control_thread_args* control_thread;
    pthread_mutex_t mtx;
    char *fname;
};
static void * file_thread(void * vargs)
{
    struct file_thread_args *args = vargs;
    while(1)
    {
        // block
        pthread_mutex_lock(&args->mtx);
        // extract
        printf("Extracting %s\n", args->fname);
        char *search = extract(args->fname);
        // notify
        if (search)
        {
            printf("Search result found %s\n", search);
            free(args->control_thread->extract_fname);
            args->control_thread->extract_fname = search;
            uint64_t tmp = 1;
            write(args->control_thread->extract_efd, &tmp, sizeof(uint64_t));
        }
    }
    free(vargs);
    return 0;
}





void event_join (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
    printf("joined channel\n");
    irc_cmd_user_mode (session, "+i");

    pthread_t tid;
    struct control_thread_args* ct = malloc(sizeof(struct control_thread_args));
    ct->session = session;
    ct->channel = ((irc_ctx_t *)irc_get_ctx(session))->channel;
    ct->extract_efd = eventfd(0, 0);
    ct->extract_fname = NULL;

    if (pthread_create (&tid, NULL, control_thread, ct) != 0)
        printf ("CREATE_THREAD failed: %s\n", strerror(errno));

    struct file_thread_args* ft = malloc(sizeof(struct file_thread_args));
    ft->control_thread = ct;
    pthread_mutex_init(&ft->mtx, NULL);
    pthread_mutex_lock(&ft->mtx);
    ft->fname = NULL;
    if (pthread_create (&tid, NULL, file_thread, ft) != 0)
        printf ("CREATE_THREAD failed: %s\n", strerror(errno));

    ((irc_ctx_t *)irc_get_ctx(session))->file_thread = ft;
}


void event_connect (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
    printf("connected\n");
    irc_ctx_t * ctx = (irc_ctx_t *) irc_get_ctx (session);
    // dump_event (session, event, origin, params, count);

    irc_cmd_join (session, ctx->channel, 0);
}


void event_privmsg (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
    printf ("'%s' said me (%s): %s\n",
        origin ? origin : "someone",
        params[0], params[1] );
}

struct dcc_ctx
{
    FILE* fp;
    char* fname;
};
void dcc_file_recv_callback (irc_session_t * session, irc_dcc_t id, int status, void * ctx, const char * data, unsigned int length)
{
    struct dcc_ctx* fdata = ctx;
    irc_ctx_t* session_ctx = irc_get_ctx(session);

    if ( status == 0 && length == 0 )
    {
        printf ("File sent successfully\n");

        if ( fdata->fp )
            fclose (fdata->fp);

        free(session_ctx->file_thread->fname);
        session_ctx->file_thread->fname = fdata->fname;
        pthread_mutex_unlock(&session_ctx->file_thread->mtx);

        free(fdata);
    }
    else if ( status )
    {
        printf ("File sent error: %d\n", status);

        if ( fdata->fp )
            fclose (fdata->fp);
        free(fdata);
    }
    else
    {
        if ( fdata->fp )
            fwrite (data, 1, length, fdata->fp);
        printf ("File sent progress: %d\n", length);
    }
}

void irc_event_dcc_send (irc_session_t * session, const char * nick, const char * addr, const char * filename, unsigned long size, irc_dcc_t dccid)
{
    struct dcc_ctx* ctx = malloc(sizeof(struct dcc_ctx));

    printf ("DCC send [%d] requested from '%s' (%s): %s (%lu bytes)\n", dccid, nick, addr, filename, size);

    if ( (ctx->fp = fopen (filename, "wb")) == 0 )
        abort();

    ctx->fname = malloc(strlen(filename) + 1);
    strcpy(ctx->fname, filename);
    irc_dcc_accept (session, dccid, ctx, dcc_file_recv_callback);
}

int main (int argc, char **argv)
{
    irc_callbacks_t callbacks;
    irc_ctx_t ctx;
    irc_session_t * s;
    unsigned short port;

    if ( argc != 4 )
    {
        printf ("Usage: %s <server> <nick> <channel>\n", argv[0]);
        return 1;
    }

    memset (&callbacks, 0, sizeof(callbacks));

    callbacks.event_connect = event_connect;
    callbacks.event_join = event_join;
    callbacks.event_privmsg = event_privmsg;
    callbacks.event_dcc_send_req = irc_event_dcc_send;

    s = irc_create_session (&callbacks);

    if ( !s )
    {
        printf ("Could not create session\n");
        return 1;
    }

    ctx.channel = argv[3];
    ctx.nick = argv[2];

    irc_set_ctx (s, &ctx);

    // If the port number is specified in the server string, use the port 0 so it gets parsed
    if ( strchr( argv[1], ':' ) != 0 )
        port = 0;

    // To handle the "SSL certificate verify failed" from command line we allow passing ## in front
    // of the server name, and in this case tell libircclient not to verify the cert
    if ( argv[1][0] == '#' && argv[1][1] == '#' )
    {
        // Skip the first character as libircclient needs only one # for SSL support, i.e. #irc.freenode.net
        argv[1]++;

        irc_option_set( s, LIBIRC_OPTION_SSL_NO_VERIFY );
    }

    // Initiate the IRC server connection
    if ( irc_connect (s, argv[1], port, 0, argv[2], 0, 0) )
    {
        printf ("Could not connect: %s\n", irc_strerror (irc_errno(s)));
        return 1;
    }

    // and run into forever loop, generating events
    if ( irc_run (s) )
    {
        printf ("Could not connect or I/O error: %s\n", irc_strerror (irc_errno(s)));
        return 1;
    }

    return 1;
}
