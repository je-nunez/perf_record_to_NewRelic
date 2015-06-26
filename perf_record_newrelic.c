
/* An example of how to send Linux Performance Counters (more exactly
 * "perf record" and "perf report") to NewRelic, via its Agent SDK in
 * C.
 *
 * This is the first version of this program.
 *
 * The current way of calling this program is:
 *
 *     perf_record_newrelic  NewRelic_license_key \
 *                           [options-to-perf-record] \
 *                           <program> <prg-args> ...
 *
 * Note that the arguments between the NewRelic_license_key and
 * the <program>, are "options-to-perf-record", ie., options that
 * are passed as-is to "perf record". In this sense, this program
 * is merely a wrapper between many options to "perf record" and
 * NewRelic.
 *
 * (The current version does remove the "-o" and "--output=" options
 * to "perf record", so that "perf report" will take its perf.data
 * file from the current directory. Other "options-to-perf-record" can
 * also make difficult to parse results from "perf report" to NewRelic:
 * such options will need to be sanitized too.)
 *
 * Todo: not to invoke two sub-processes "perf record" and "perf report" via a
 *       perf.data file, but the system-call perf_counter_open() directly to
 *       NewRelic.
 *
 * Bugs: many.
 *
 * Jose E. Nunez, 2015
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/wait.h>


#include "newrelic_common.h"
#include "newrelic_transaction.h"
#include "newrelic_collector_client.h"


/* The maximum length of a New Relic identifier.
 *
 * In:
 *
 *  https://docs.newrelic.com/docs/agents/php-agent/configuration/php-agent-api
 *
 * it is mentioned:
 *
 *  newrelic_record_custom_event:
 *   ...
 *   The attributes parameter is expected to be an associative array: the keys
 *   should be the attribute names (which may be up to 255 characters in length)
 *   ...
 *
 * so we limit our custom attributes also to be at most 255 chars in length
 */

const int MAX_LENGTH_NEW_RELIC_IDENT = 255;

/*
 * On this issue of the custom attributes, that New Relic documentation above:
 *
 *  https://docs.newrelic.com/docs/agents/php-agent/configuration/php-agent-api
 *
 * also mentions on the NAMING CONVENTIONS for your custom attributes:
 *
 *  function newrelic_custom_metric (...)
 *
 *    ... Your custom metrics can then be used in custom dashboards and custom
 *    views in the New Relic user interface. Name your custom metrics with a
 *    Custom/ prefix (for example, Custom/MyMetric). This will make them easily
 *    usable in custom dashboards...
 *
 *    Note: Avoid creating too many unique custom metric names. New Relic limits
 *    the total number of custom metrics you can use (not the total you can
 *    report for each of these custom metrics). Exceeding more than 2000 unique
 *    custom metric names can cause automatic clamps that will affect other
 *    data.
 *
 * We follow the first paragraph on prefixing with "Custom/" our attributes, but
 * not the second of the "Note:", of not submitting more that 2000 unique custom
 * metric names, because we don't use the API call:
 *
 *           newrelic_record_metric()
 * but:
 *           newrelic_transaction_add_attribute()
 *
 * Also, on the NAMING CONVENTIONS for your custom attributes (URL below exceeds
 * 80 columns):
 *
 *  https://docs.newrelic.com/docs/insights/new-relic-insights/decorating-events/insights-custom-attributes#keywords
 *
 * it lists some reserved words that New Relic understands, which should not be
 * used as attribute names:
 *
 *     The following words are used by NRQL and by Insights. Avoid using
 *     them as names for attributes. Otherwise the results may not be
 *     what was expected.
 *      .... [List of reserved words by New Relic] ....
 *
 * we add the preffix "ct_" to our attribute names to avouid any accidental
 * conflict with New Relic of some identifier returned by the Linux Performance
 * Counters.
 */

int
usage_and_exit(void);


void
newrelic_perf_counters_wrapper(int program_argc, char * program_argv[]);


int
execute_perf_record_and_program(int in_program_argc, char * in_program_argv[],
                                struct timespec * out_duration,
                                char * out_perf_data_file);


int
upload_perf_report_to_NewRelic(char * in_perf_data_fname,
                               const struct timespec * prog_exec_duration,
                               long newrelic_transaction);


int
main(int argc, char** argv)
{
    if (argc < 3 ||
          strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
        usage_and_exit();

    /* the first argument in the command-line is the NewRelic license key of
     * the NewRelic account to send information to */
    char newrelic_license_key[256];
    strncpy(newrelic_license_key, argv[1], sizeof newrelic_license_key);

    newrelic_register_message_handler(newrelic_message_handler);

    newrelic_init(newrelic_license_key,
                  "Linux Performance Counters to NewRelic", "C", "4.8");
    // newrelic_enable_instrumentation(0);  /* 0 is enable */

    newrelic_perf_counters_wrapper(argc-2, argv+2);

    return 0;
}


void     (*newrelic_agent_sighandler)(int) = NULL;

volatile sig_atomic_t interrupt_execution = 0;

void signal_handler(int sig)
{
    if (interrupt_execution == 0) {
        /* This is the first time that the signal was caught */
        /* It is very probable that the embedded-mode NewRelic installed a
         * signal-handler for INT, because although NR is closed-source so
         * it is very difficult to say, it uses log4cplus for logging, that
         * is with signals: see log4cplus's warning in "README.md" and also
         * log4cplus's "src/threads.cxx" file
         */
        /* be careful with the fake signal-handlers in bits/signum.h,
         * because we don't know what handler existed previously */
        switch ((long)newrelic_agent_sighandler) {
           case (long)SIG_ERR:   /* fake signal-handlers in bits/signum.h */
           case (long)SIG_DFL:
           case (long)SIG_IGN:
              break;
           default:
              ;
              raise(sig);   /* let New Relic see this signal, if it does */
        }
    }
    interrupt_execution = 1;
}


void
send_error_notice_to_NewRelic(long transaction_id, const char *exception_type,
                              const char *error_message)
{
    fprintf(stderr, "ERROR: %s: %s\n", exception_type, error_message);

    int err_code;
    err_code = newrelic_transaction_notice_error(transaction_id, exception_type,
                                                 error_message, "", "");
    if (err_code != 0)
        fprintf(stderr, "ERROR: Couldn't send error message to New Relic: "
                        "returned error %d\n", err_code);
}



void
newrelic_perf_counters_wrapper(int program_argc, char * program_argv[])
{
    int return_code;

    fprintf(stderr, "DEBUG: about to call newrelic_transaction_begin()\n");
    long newrelic_transxtion_id = newrelic_transaction_begin();
    if (newrelic_transxtion_id < 0) {
        fprintf(stderr, "ERROR: newrelic_transaction_begin() returned %ld\n"
                        "Aborting.\n", newrelic_transxtion_id);
        return;
    }

    long newr_segm_external_perf_record = 0;

    /* Naming transactions is optional.
     * Use caution when naming transactions. Issues come about when the
     * _ranularity of transaction names is too fine, resulting in hundreds if
     * not thousands of different transactions. If you name transactions
     * poorly, your application may get blacklisted due to too many metrics
     * being sent to New Relic.
     */
    return_code = newrelic_transaction_set_type_other(newrelic_transxtion_id);
    if (return_code < 0)
       fprintf(stderr, "ERROR: newrelic_transaction_set_type_other() "
                       "returned %d\n", return_code);

    return_code = newrelic_transaction_set_name(newrelic_transxtion_id,
                                               "Linux Perf Counters");
    if (return_code < 0)
       fprintf(stderr, "ERROR: newrelic_transaction_set_name() "
                       "returned %d\n", return_code);

    return_code = newrelic_transaction_set_category(newrelic_transxtion_id,
                                                "BackendTrans/Perf/counters");
    if (return_code < 0)
       fprintf(stderr, "ERROR: newrelic_transaction_set_category() "
                       "returned %d\n", return_code);

    /* Record an attribute for this transaction: the start time */
    char start_time[16];
    snprintf(start_time, sizeof start_time, "%u",
               (unsigned)time(NULL));

    return_code = newrelic_transaction_add_attribute(newrelic_transxtion_id,
                                           "ct_tx_start_time", start_time);

    newr_segm_external_perf_record =
           newrelic_segment_external_begin(newrelic_transxtion_id,
                                           NEWRELIC_ROOT_SEGMENT,
                                           "localhost", "perf record");
    if (newr_segm_external_perf_record < 0)
        fprintf(stderr, "ERROR: newrelic_segment_external_begin() "
                        "returned %ld\n",newr_segm_external_perf_record);

    /* Set signal handler */
    /* see if NewRelic installed a signal-handler for INT in our thread */
    struct sigaction newrelic_signal_handler;

    sigaction(SIGINT, NULL, &newrelic_signal_handler);
    newrelic_agent_sighandler = newrelic_signal_handler.sa_handler;

    struct sigaction our_signal_handler;
    memset(&our_signal_handler, 0, sizeof(our_signal_handler));
    our_signal_handler.sa_handler = signal_handler;
    sigemptyset(&our_signal_handler.sa_mask);
    sigaction(SIGINT, &our_signal_handler, NULL);

    /* Run "perf record" */
    char temp_perf_data_file[PATH_MAX];
    memset(temp_perf_data_file, 0, sizeof temp_perf_data_file);
    struct timespec  program_exec_duration;
    int program_exit_code;
    struct stat buf;
    if (interrupt_execution == 0)
        program_exit_code = execute_perf_record_and_program(program_argc,
                                                        program_argv,
                                                        &program_exec_duration,
                                                        temp_perf_data_file);

    /* Try to respect calling newrelic_segment_end() before interruption */
    /*
    if (interrupt_execution != 0)
        goto goto_point_delete_temp_perf_data_file;
    */

    /* end this NewRelic segment */
    if (newr_segm_external_perf_record >= 0) {
        int ret_code =  newrelic_segment_end(newrelic_transxtion_id,
                                             newr_segm_external_perf_record);
        if (ret_code < 0)
            fprintf(stderr, "ERROR: newrelic_segment_end() returned %d\n",
                    ret_code);
    }

    /* We respected calling newrelic_segment_end(), so we can interrupt now */
    if (interrupt_execution != 0)
        goto goto_point_delete_temp_perf_data_file;

    if (program_exit_code < 0 && program_exit_code >= -4) {
        /* An error was caught in execute_perf_record_and_program()
         *
         * Note that program_exit_code is between -4 and -1 only
         * that are the error codes we pre-check.
         */
        const char *errors_in_execute_perf_record[] = {
                            "0",
                            "Couldn't find a temp filename for perf.data file",
                            "calloc() failed",
                            "Interrupted by a signal",
                            "fork() failed"
             };
        send_error_notice_to_NewRelic(newrelic_transxtion_id,
                                      "execute_perf_record_and_program",
                                      errors_in_execute_perf_record[
                                                         abs(program_exit_code)
                                                      ]);
        goto goto_point_delete_temp_perf_data_file;
    }

    if (interrupt_execution == 0 && program_exit_code >= 0) {
        long newr_segm_external_perf_report =
                newrelic_segment_external_begin(newrelic_transxtion_id,
                                                NEWRELIC_ROOT_SEGMENT,
                                                "localhost", "perf report");
        if (newr_segm_external_perf_report < 0)
            fprintf(stderr, "ERROR: newrelic_segment_external_begin() "
                             "returned %ld\n", newr_segm_external_perf_report);

        upload_perf_report_to_NewRelic(temp_perf_data_file,
                                       &program_exec_duration,
                                       newrelic_transxtion_id);

        if (newr_segm_external_perf_report >= 0) {
            int ret_code = newrelic_segment_end(newrelic_transxtion_id,
                                                newr_segm_external_perf_report);
            if (ret_code < 0)
                fprintf(stderr, "ERROR: newrelic_segment_end() returned %d\n",
                        ret_code);
        }
    }

goto_point_delete_temp_perf_data_file:
    if (stat(temp_perf_data_file, &buf) == 0) {
       // There could be a race condition with another program that does
       // a `perf` command on this same "perf.data" temp file, or a newer one.
       // Even looking at the "perf.data" header to see if it is about the
       // command we executed can't give us security to avoid the race
       // condition, because another process can be doing a `perf record`
       // with the same command line writing to this same "perf.data" file

       time_t current_time = time(NULL);
       if (current_time - buf.st_mtime < 30) {
           // "perf.data" was modified less than 30 seconds ago: it's ours
           return_code = unlink(temp_perf_data_file);
           if (return_code != 0) {
               char err_msg[256];
               strerror_r(errno, err_msg, sizeof err_msg);
               send_error_notice_to_NewRelic(newrelic_transxtion_id,
                                             "unlink_temp_perf_data_file",
                                             err_msg);
           }
       }
    }

    /* Finnish the NewRelic transaction */
    fprintf(stderr, "DEBUG: about to call newrelic_transaction_end()\n");
    return_code = newrelic_transaction_end(newrelic_transxtion_id);
    if (return_code < 0)
    fprintf(stderr, "ERROR: newrelic_transaction_end() "
                    "returned %d\n", return_code);
}


static int
create_a_temp_filename(char * out_new_temp_fname)
{
    pid_t my_pid = getpid();
    time_t curr_time = time(NULL);

    /* We need to use a random number generator that be
     * re-entrant, because the embedded New Relic collector
     * agent in running in another thread concurrent to
     * this main thread, and we don't know if that thread
     * -which relies on SSL- uses a random number generator
     * or not. It is very probable.
     */

     struct drand48_data random_state;
     memset(&random_state, 0, sizeof(struct drand48_data));

     long rand_seed = (long)(17*my_pid + curr_time);
     srand48_r(rand_seed, &random_state);

     /* Try up to ten times to generate a random file name.
      * This is similar to the tempnam() re-entrant function
      */
     int i;
     for (i=0; i<10; i++) {
         if (interrupt_execution != 0) return 0;

         long a_rand_number;
         lrand48_r(&random_state, &a_rand_number);
         snprintf(out_new_temp_fname, PATH_MAX,
                 "/tmp/perf_%ld_%ld_%ld.dat", (long)my_pid,
                 (long)curr_time, a_rand_number);
          struct stat buf;
          if (stat(out_new_temp_fname, &buf) != 0)
              return 1;
    }

    return 0;
}

int
execute_perf_record_and_program(int in_program_argc, char * in_program_argv[],
                                struct timespec * out_duration,
                                char * out_perf_data_file)
{
    int status;
    status = create_a_temp_filename(out_perf_data_file);
    if (status == 0)
        return -1;

    /* Prepare the new options and arguments to call "perf record ..." */
    char ** new_argv;
    new_argv = calloc(in_program_argc+4, sizeof (char *));
    if (!new_argv)
        return -2;

    /* Build the new argv[] to call `perf record <in_program_argv[]> */
    new_argv[0] = "perf";
    new_argv[1] = "record";
    char output_arg[PATH_MAX+16];
    snprintf(output_arg, sizeof output_arg, "--output=%s", out_perf_data_file);
    new_argv[2] = output_arg;
    /* Note that in the following argv copy, since "perf record" was
     * already inserted in the new_argv[], then the first argvs in
     * this copy are arguments that are passed directly as-is to
     * "perf record".
     * TODO: Some arguments to "perf record" can make difficult to
     * parse a "perf report" later into NewRelic, so a sanitizing
     * of these arguments is necessary in "perf record" for NewRelic
     * transmission be factible. Ie., in this first version we
     * sanitize the "-o" or "--output" option to "perf record", so
     * NewRelic always see the perf.data file in the current directory
     * -ie., the "perf report" feed to NewRelic
     */
    int src_idx=0, dest_idx=3, still_in_perf_record_options=1;
    while (src_idx < in_program_argc) {
        /* Sanitize */
        if (still_in_perf_record_options == 1 &&
            strncmp(in_program_argv[src_idx], "--output=", 9) == 0) {
            fprintf(stderr, "Ignoring option %s\n", in_program_argv[src_idx]);
            src_idx++;
        } else if (still_in_perf_record_options == 1 &&
                 strncmp(in_program_argv[src_idx], "-o", 2) == 0) {
             fprintf(stderr, "Ignoring option %s\n", in_program_argv[src_idx]);
             if (in_program_argv[src_idx][2] == '\0')
                src_idx += 2; /* fmt: "-o <file>" */
             else
                src_idx++; /* fmt: "-o<file>" without space */
        } else {
             new_argv[dest_idx] = in_program_argv[src_idx];
             if (in_program_argv[src_idx] && in_program_argv[src_idx][0] != '-')
                 still_in_perf_record_options = 0;
             dest_idx++;
             src_idx++;
        }
    }
    new_argv[dest_idx] = NULL;

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_REALTIME, &start_time);

    if (interrupt_execution != 0) {
        free(new_argv);
        return -3;
    }

    int forked_pid;
    forked_pid = fork();
    if (forked_pid == 0) {
        /* child process */
        execvp("perf", new_argv);
    } else if (forked_pid > 0) {
        /* parent process */
        wait(&status);
    } else {
        free(new_argv);
        return -4;
    }

    free(new_argv);
    if (interrupt_execution != 0)
        return -3;

    clock_gettime(CLOCK_REALTIME, &end_time);

    out_duration->tv_sec = end_time.tv_sec - start_time.tv_sec;
    out_duration->tv_nsec = end_time.tv_nsec - start_time.tv_nsec;
    if (out_duration->tv_nsec < 0) {
        out_duration->tv_sec--;
        out_duration->tv_nsec += 1000000000;
    }

    return WEXITSTATUS(status);
}


int
upload_perf_report_to_NewRelic(char * in_perf_data_fname,
                               const struct timespec * prog_exec_duration,
                               long newrelic_transaction)
{
    FILE * perf_report_pipe;

    char perf_report_cmd[PATH_MAX+64];
    snprintf(perf_report_cmd, sizeof perf_report_cmd,
             "perf report --input=%s", in_perf_data_fname);

    if (interrupt_execution != 0) return -1;

    perf_report_pipe = popen(perf_report_cmd, "r");
    if (!perf_report_pipe) {
        char err_msg[256];
        strerror_r(errno, err_msg, sizeof err_msg);
        send_error_notice_to_NewRelic(newrelic_transaction,
                                      "popen_perf_report", err_msg);
        return -1;
    }

    double total_progr_duration;
    total_progr_duration = prog_exec_duration->tv_sec +
                           (prog_exec_duration->tv_nsec/1000000000);
    fprintf(stderr, "DEBUG: Total duration %.06f\n", total_progr_duration);

    char * buff_line = NULL;
    size_t buff_len = 0;

    while (interrupt_execution == 0 &&
           getline(&buff_line, &buff_len, perf_report_pipe) != -1) {
         if (!buff_line || buff_line[0] == '\0' ||
             buff_line[0] == '\n' || buff_line[0] == '#') /* '#' is a comment */
             continue;
         /* buff_line is in the format:
               16.67%       <prog>  [kernel.kallsyms]  [k] vm_normal_page
               16.67%       <prog>  libc-2.17.so       [.] __fxstat64
         */
         /* fprintf(stderr, "DEBUG: %s", buff_line); */
         float percent;
         char *so_object, *symbol;
         sscanf(buff_line, " %f%% %*s %ms %*s %ms", &percent,
                           &so_object, &symbol);
         // fprintf(stderr, "DEBUG: %f %s %s\n", percent, symbol, so_object);

         char newrelic_attrib_from_perf_record[MAX_LENGTH_NEW_RELIC_IDENT+1];
         snprintf(newrelic_attrib_from_perf_record,
                  sizeof newrelic_attrib_from_perf_record,
                  "Custom/ct_%s@%s", symbol, so_object);

         double relative_duration;
         relative_duration = (percent/100) * total_progr_duration;
         char relative_duration_str[32];
         snprintf(relative_duration_str, sizeof relative_duration_str,
                  "%.06f", relative_duration);

         fprintf(stderr, "DEBUG: %s: %s\n", newrelic_attrib_from_perf_record,
                                            relative_duration_str);

         if (strcmp(relative_duration_str, "0.000000") == 0)
             /* This symbol didn't have a weight (relative-duration) in the
              * execution of the program: ignore it */
             goto free_dynamic_buffers_in_this_while_loop;

         /* Send the "perf record" to NewRelic for this symbol */
         /* TODO: how to send the "perf report" to NewRelic:
          *        newrelic_record_metric()
          * or
          *        newrelic_transaction_add_attribute()
          * The current newrelic_record_metric() for this version 0.16.2.0 of
          * the NewRelic Agent SDK doesn't allow to specify a transaction
          * argument, and newrelic_transaction_add_attribute() allows it,
          * but sends the value as a string, and not as  floating-point
          * number. The latter option is chosen here, although the change to
          * newrelic_record_metric() is below and is very small, just that
          *  line */
         int ret_code;
         ret_code = newrelic_transaction_add_attribute(newrelic_transaction,
                                            newrelic_attrib_from_perf_record,
                                            relative_duration_str);

         if (ret_code < 0)
             fprintf(stderr, "ERROR: newrelic_transaction_add_attribute() "
                             "returned %d\n", ret_code);

       free_dynamic_buffers_in_this_while_loop:
         if (so_object) free(so_object);
         if (symbol) free(symbol);
    }

    if (buff_line)
        free(buff_line);

    int ret = pclose(perf_report_pipe);
    if (ret < 0) {
        char err_msg[256];
        strerror_r(errno, err_msg, sizeof err_msg);
        send_error_notice_to_NewRelic(newrelic_transaction,
                                      "pclose_perf_report", err_msg);
        return -2;
    }

    return 0;
}



int
usage_and_exit(void)
{
    printf("Usage:\n"
           "\n"
           "  perf_record_newrelic  newrelic_license_key "
                             " [options-to-perf-record] <program> <args> ...\n"
           "                           Run and record performance of <program>"
                                     " under this NewRelic license key\n"
           "  perf_record_newrelic  [-h|--help]\n"
           "                           Show this usage help\n");
    exit(1);
}

