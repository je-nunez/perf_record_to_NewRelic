
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


int
usage_and_exit(void);


void
newrelic_perf_counters_wrapper(int program_argc, char * program_argv[]);


int
execute_perf_record_and_program(int program_argc, char * program_argv[],
                                 struct timespec * duration);


int
upload_perf_report_to_NewRelic(long newrelic_transaction,
                               const struct timespec * prog_exec_duration);


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
    // newrelic_enable_instrumentation(1);  /* 1 is enable */

    newrelic_perf_counters_wrapper(argc-2, argv+2);

    return 0;
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
                                           "tx_start_time", start_time);

    newr_segm_external_perf_record =
           newrelic_segment_external_begin(newrelic_transxtion_id,
                                           NEWRELIC_ROOT_SEGMENT,
                                           "localhost", "perf record");
    if (newr_segm_external_perf_record < 0)
        fprintf(stderr, "ERROR: newrelic_segment_external_begin() "
                        "returned %ld\n",newr_segm_external_perf_record);


    struct timespec  program_exec_duration;
    int program_exit_code;
    program_exit_code = execute_perf_record_and_program(program_argc,
                                     program_argv, &program_exec_duration);

    /* end this NewRelic segment */
    if (newr_segm_external_perf_record >= 0) {
        int ret_code =  newrelic_segment_end(newrelic_transxtion_id,
                                             newr_segm_external_perf_record);
        if (ret_code < 0)
            fprintf(stderr, "ERROR: newrelic_segment_end() returned %d\n",
                    ret_code);
    }

    if (program_exit_code >= 0) {
        long newr_segm_external_perf_report =
                newrelic_segment_external_begin(newrelic_transxtion_id,
                                                NEWRELIC_ROOT_SEGMENT,
                                                "localhost", "perf report");
        if (newr_segm_external_perf_report < 0)
            fprintf(stderr, "ERROR: newrelic_segment_external_begin() "
                             "returned %ld\n", newr_segm_external_perf_report);

        upload_perf_report_to_NewRelic(newrelic_transxtion_id,
                                       &program_exec_duration);

        if (newr_segm_external_perf_report >= 0) {
            int ret_code = newrelic_segment_end(newrelic_transxtion_id,
                                                newr_segm_external_perf_report);
            if (ret_code < 0)
                fprintf(stderr, "ERROR: newrelic_segment_end() returned %d\n",
                        ret_code);
        }
    }

    /* Finnish the NewRelic transaction */
    fprintf(stderr, "DEBUG: about to call newrelic_transaction_end()\n");
    return_code = newrelic_transaction_end(newrelic_transxtion_id);
    if (return_code < 0)
    fprintf(stderr, "ERROR: newrelic_transaction_end() "
                    "returned %d\n", return_code);
}


int
execute_perf_record_and_program(int program_argc, char * program_argv[],
                                 struct timespec * duration)
{
    int status;
    char ** new_argv;
    new_argv = calloc(program_argc+3, sizeof (char *));
    if (!new_argv) {
        perror("calloc");
        return -1;
    }

    /* Build the new argv[] to call `perf record <program_argv[]> */
    new_argv[0] = "perf";
    new_argv[1] = "record";
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
    int src_idx=0, dest_idx=2, still_in_perf_record_options=1;
    while (src_idx < program_argc) {
        /* Sanitize */
        if (still_in_perf_record_options == 1 &&
            strncmp(program_argv[src_idx], "--output=", 9) == 0) {
            fprintf(stderr, "Ignoring option %s\n", program_argv[src_idx]);
            src_idx++;
        } else if (still_in_perf_record_options == 1 &&
                 strncmp(program_argv[src_idx], "-o", 2) == 0) {
             fprintf(stderr, "Ignoring option %s\n", program_argv[src_idx]);
             if (program_argv[src_idx][2] == '\0')
                src_idx += 2; /* fmt: "-o <file>" */
             else
                src_idx++; /* fmt: "-o<file>" without space */
        } else {
             new_argv[dest_idx] = program_argv[src_idx];
             if (program_argv[src_idx] && program_argv[src_idx][0] != '-')
                 still_in_perf_record_options = 0;
             dest_idx++;
             src_idx++;
        }
    }
    new_argv[dest_idx] = NULL;

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_REALTIME, &start_time);

    int forked_pid;
    forked_pid = fork();
    if (forked_pid == 0) {
        /* child process */
        execvp("perf", new_argv);
    } else if (forked_pid > 0) {
        /* parent process */
        free(new_argv);
        wait(&status);
    } else {
        perror("fork");
        free(new_argv);
        return -1;
    }

    clock_gettime(CLOCK_REALTIME, &end_time);

    duration->tv_sec = end_time.tv_sec - start_time.tv_sec;
    duration->tv_nsec = end_time.tv_nsec - start_time.tv_nsec;
    if (duration->tv_nsec < 0) {
        duration->tv_sec--;
        duration->tv_nsec += 1000000000;
    }

    return WEXITSTATUS(status);
}


int
upload_perf_report_to_NewRelic(long newrelic_transaction,
                               const struct timespec * prog_exec_duration)
{
    FILE * perf_report_pipe;

    perf_report_pipe = popen("perf report", "r");
    if (!perf_report_pipe) {
        perror("popen");
        return -1;
    }

    double total_progr_duration;
    total_progr_duration = prog_exec_duration->tv_sec +
                           (prog_exec_duration->tv_nsec/1000000000);
    fprintf(stderr, "DEBUG: Total duration %.06f\n", total_progr_duration);

    char * buff_line = NULL;
    size_t buff_len = 0;

    while (getline(&buff_line, &buff_len, perf_report_pipe) != -1) {
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

         char newrelic_attrib_from_perf_record[1024];
         snprintf(newrelic_attrib_from_perf_record,
                  sizeof newrelic_attrib_from_perf_record,
                  "%s@%s", symbol, so_object);

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
        perror("pclose");
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

