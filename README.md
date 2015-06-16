# perf_record_NewRelic_wrapper

A Linux Performance Counters to NewRelic wrapper in C, using `perf record` and `perf report` to rely to the NewRelic via its Agent SDK (embedded mode).

( For an example of the overall structure instrumenting your C application with the NewRelic Agent SDK (embedded mode), please refer to the previous project, https://github.com/je-nunez/NewRelic_instrumentation_SDK )

# WIP

This project is a *work in progress*. The implementation is *incomplete* and subject to change. The documentation can be inaccurate.

This is the first version of this document.

# Description

This is an example of how to send Linux Performance Counters (more exactly `perf record` and `perf report`) to NewRelic, via its Agent SDK.

This is the first version of this program. The current way of calling it is:

    perf_record_newrelic  <NewRelic_license_key> \
                          [<options-to-perf-record>] \
                          <program> <prg-args> ...

The `<NewRelic_license_key>` is the NewRelic key associated to your NewRelic account.

Note that the arguments between the `<NewRelic_license_key>` and the `<program>`, are `<options-to-perf-record>`, ie., options that are passed as-is to `perf record`. In this sense, this program is merely a wrapper between many options to `perf record` and NewRelic.


# Create a free NewRelic account

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

        apt-get install  libss  libnss3

    and for RedHat-oriented systems:

        yum install  openssl-libs  nss

    Other shared-libraries may be as well necessary.)


