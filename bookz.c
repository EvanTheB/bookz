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

#include "libircclient.h"
#include "archive.h"


/*
 * We store data in IRC session context.
 */
typedef struct
{
    char    * channel;
    char    * nick;

} irc_ctx_t;


struct thread_args
{
    irc_session_t *session;
    char *channel;
};
static void * thread_function(void * vargs)
{
    struct thread_args *args = vargs;
    while ( 1 )
    {
        char buffer[200];
        char *buf = buffer;
        size_t buffer_size = 200;
        getline(&buf, &buffer_size,stdin);
        irc_cmd_msg (args->session, args->channel, buf);
    }
    free(args->channel);
    free(vargs);
    return 0;
}

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

static void
extract(const char *filename)
{
  struct archive *a;
  struct archive *ext;
  struct archive_entry *entry;
  int flags;
  int r;

  /* Select which attributes we want to restore. */
  flags = ARCHIVE_EXTRACT_TIME;
  flags |= ARCHIVE_EXTRACT_PERM;
  flags |= ARCHIVE_EXTRACT_ACL;
  flags |= ARCHIVE_EXTRACT_FFLAGS;

  a = archive_read_new();
  archive_read_support_format_all(a);
  archive_read_support_compression_all(a);
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
  exit(0);
}



void event_join (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
    printf("joined channel\n");
    irc_cmd_user_mode (session, "+i");

    pthread_t tid;
    struct thread_args* ta = malloc(sizeof(ta));
    ta->session = session;
    ta->channel = malloc(strlen(params[0]) + 1);
    strcpy(ta->channel, params[0]);

    if (pthread_create (&tid, 0, thread_function, ta) != 0)
        printf ("CREATE_THREAD failed: %s\n", strerror(errno));
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

struct dcc_data
{
    FILE* fp;
    char* fname;
};
void dcc_file_recv_callback (irc_session_t * session, irc_dcc_t id, int status, void * ctx, const char * data, unsigned int length)
{
    struct dcc_data* fdata = ctx;
    if ( status == 0 && length == 0 )
    {
        printf ("File sent successfully\n");

        if ( fdata->fp )
            fclose (fdata->fp);
        extract(fdata->fname);
        free(fdata);
    }
    else if ( status )
    {
        printf ("File sent error: %d\n", status);

        if ( fdata->fp )
            fclose (fdata->fp);
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
    FILE * fp;
    printf ("DCC send [%d] requested from '%s' (%s): %s (%lu bytes)\n", dccid, nick, addr, filename, size);

    if ( (fp = fopen (filename, "wb")) == 0 )
        abort();

    struct dcc_data* ctx = malloc(sizeof(ctx));
    ctx->fp = fp;
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
