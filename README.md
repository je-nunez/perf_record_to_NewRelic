# perf_record_to_NewRelic

A Linux Performance Counters to NewRelic wrapper in C, using `perf record` and `perf report` to rely to the NewRelic via its Agent SDK (embedded mode).

( For an example of the overall structure instrumenting your C application with the NewRelic Agent SDK (embedded mode), please refer to the previous project, https://github.com/je-nunez/NewRelic_instrumentation_SDK )

# WIP

This project is a *work in progress*. The implementation is *incomplete* and subject to change. The documentation can be inaccurate.

# Description

This is an example of how to send Linux Performance Counters (more exactly `perf record` and `perf report`) to NewRelic, via its Agent SDK. 

The `Linux Performance Counters` is the main-line way to instrument the Linux kernel with very little overhead, offering CPU performance counters, tracepoints, kernel-probes, userland-probes, and eBPF. `New Relic` allows to collect, summarize, and display massive amounts of real-time performance data on your systems and applications. To know more about both:

    https://perf.wiki.kernel.org/index.php/Main_Page
    http://newrelic.com/resources/tutorials
 
The program in this repository allows to collect Linux Performance Counters and send them to New Relic, via its Agent SDK. The current way of calling this program is:

    perf_record_newrelic  <NewRelic_license_key> \
                          [<options-to-perf-record>] \
                          <program> <prg-args> ...

The `<NewRelic_license_key>` is the NewRelic key (associated to your New Relic account) under which to record and consult your performance metrics (See section below "Create a free New Relic account" if you don't have an open account in New Relic yet).

The arguments between the `<NewRelic_license_key>` and the `<program>`, are `<options-to-perf-record>`, ie., options that are passed as-is to the `perf record` program, for `perf record` supports many options:

    http://man7.org/linux/man-pages/man1/perf-record.1.html
    
so you can pass them in the `<options-to-perf-record>` to this program. The last command-line arguments `<program> <prg-args> ...` is the program and its arguments which you would like to collect performance statistics about in `perf record` and `New Relic`.

This repository also contains a script, `download_NewRelic_Agent_SDK.sh`, which is useful to download and install the New Relic Agent SDK. This contains the shared-libraries necessary in Linux to communicate with New Relic, and the header files in `C`. The `Makefile` in this project has one target which calls this script, to prepare the build-environment. In reference to its shared-libraries, they need to be in the `LD_LIBRARY_PATH` (or `ldconfig`) path to call this program, so a common use of the program is:

    # optional to find NewRelic shared-libraries for the Agent embedded mode
    
    NEW_RELIC_SDK_LIBS=~/src/newrelic_agent_sdk_installation/nr_agent_sdk_base_dir/lib/
    
    export  LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$NEW_RELIC_SDK_LIBS"


    perf_record_newrelic  <NewRelic_license_key> \
                          [<options-to-perf-record>] \
                          <program> <prg-args> ...

The `Makefile` has a `run_a_test` target which runs this program to collect performance statistics on a `ls` invocation. Another possible and usual test target for `perf record` is an invocation to `sleep 60`, which allows to see what happens in the system in the next 60 seconds. (But for this test target `sleep 60` the parameters `<options-to-perf-record>` need to be provided, to indicate what perf-counters of the system you would like to see, since there are many; see:

     http://man7.org/linux/man-pages/man1/perf-list.1.html
     
# Create a free New Relic account

To run this test application, you need to create (or have already) a NewRelic account.
If you don't have one already, you may create a free NewRelic account:

    http://newrelic.com/application-monitoring/pricing

Choose the 'Lite' option.

# How to install the NewRelic Agent SDK and compile this application

To compile this test application you need to download and install first the `NewRelic Agent SDK` for C/C++:

    $ make install_newrelic_agent_sdk

This will download and install the NewRelic Agent SDK under `$HOME/src/newrelic_agent_sdk_installation/`, and create
also a new subdirectory `$HOME/.newrelic/` and file `$HOME/.newrelic/log4cplus.properties` with the debug log settings
(which write to `standard-error` and to `/tmp/newrelic-*.log` files). Note: the download-link to the NewRelic Agent
SDK (beta) changes, as newer beta versions are released and NewRelic deletes the old versions. This change will
make the rule

    $ make install_newrelic_agent_sdk

to fail. E.g., three days ago the tar-ball was at:

    http://download.newrelic.com/agent_sdk/nr_agent_sdk-v0.16.1.0-beta.x86_64.tar.gz

and this one no longer exists, instead this new version:

    http://download.newrelic.com/agent_sdk/nr_agent_sdk-v0.16.2.0-beta.x86_64.tar.gz

    (Current versions of the NewRelic Agent SDK link
    against these shared-libraries:

        /lib64/libssl3.so
        /lib64/libssl.so.10  [symlink to -> libssl.so.1.0.<n>]

    so you will need to install them in your system.
    E.g., for Debian-oriented systems:

        apt-get install  libssl  libnss3

    and for RedHat-oriented systems:

        yum install  openssl  openssl-libs  nss

    Other shared-libraries may be as well necessary.)

# How to test this application

There is a `make` target ready to test this application, just that it needs to know under which New Relic account to submit the statistics. So, if you are using the `bash` shell:


     export NEW_RELIC_LICENSE_KEY=<the-key-of-my-New-Relic-account>
     make  run_a_test
     
Note that the environment variable `NEW_RELIC_LICENSE_KEY` is used by the `Makefile`, but not by the program in `C`, which simply expects the New Relic License Key as the first argument in its command-line.

This target `run_a_test` collects statistics on a `ls` invocation. Other programs, with corresponding arguments, can be substituted in its place, as well as `<options-to-perf-record>` before the programs.

# Current Issues

It seems that the current NewRelic Agent SDK,

      http://download.newrelic.com/agent_sdk/nr_agent_sdk-v0.16.2.0-beta.x86_64.tar.gz

has its library

      libnewrelic-collector-client.so

linked to

      $ ldd libnewrelic-collector-client.so

           libssl.so.1.0.0 => /lib64/libssl.so.1.0.0
           libcrypto.so.1.0.0 => /lib64/libcrypto.so.1.0.0

whereas the previous version, `v0.16.1.0-beta`, active till four days ago, linked to

           libssl.so.10 => /lib64/libssl.so.10 (0x00007f100ac5c000)
           libcrypto.so.10 => /lib64/libcrypto.so.10 (0x00007f100a875000)

In some Linux systems (RedHat-derived, Fedora, etc), `/lib64/libssl.so.1.0.0` and `/lib64/libcrypto.so.1.0.0` don't need to exist (although the old `/lib64/libssl.so.10` and `/lib64/libcrypto.so.10` may). This causes that a shared library is not found at runtime, so the program fails. If you experience this issue that either of `libssl.so.1.0.0` or `libcrypto.so.1.0.0` shared-library is not found, then to solve this issue for the time being,

           # cd /lib64/
           # ln -s  libssl.so.1.0.[0-9][a-z]  libssl.so.1.0.0
           # ln -s  libcrypto.so.1.0.[0-9][a-z] libcrypto.so.1.0.0

