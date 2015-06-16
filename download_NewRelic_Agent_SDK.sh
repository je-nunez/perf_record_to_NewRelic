#!/bin/bash

BUILD_DIR=$HOME/src/newrelic_agent_sdk_installation/

mkdir -p $BUILD_DIR
url=$( curl  http://download.newrelic.com/agent_sdk/ | egrep -i -e '\.tar\.gz' -e '.tgz' | sed 's/^.*href="//; s/".*//' )

echo -e "Downloading $url\n"

curl  "http://download.newrelic.com/agent_sdk/$url"  -o  "$BUILD_DIR/nr_agent_sdk.tar.gz"

echo  -e "\n------ Extracting $BUILD_DIR/nr_agent_sdk.tar.gz ...\n"

cd $BUILD_DIR && \
   tar  -xzf  nr_agent_sdk.tar.gz

nr_agent_base_directory=$( find $(pwd) -maxdepth 1 -mindepth 1 -type d )

echo $nr_agent_base_directory

ln  -sf  "$nr_agent_base_directory"   $(pwd)/nr_agent_sdk_base_dir

echo -e "\n------ $BUILD_DIR is ready.\n"

# Prepare the ~/.newrelic/log4plus.properties files

echo -e "Preparing NewRelic logging settings in $HOME/.newrelic/log4cplus.properties ...\n"
mkdir  $HOME/.newrelic/
cp   $BUILD_DIR/nr_agent_sdk-v0.16.2.0-beta.x86_64/config/log4cplus.properties  $HOME/.newrelic/
sed -i 's#log4cplus.logger.com.newrelic=info,#log4cplus.logger.com.newrelic=debug,#'   $HOME/.newrelic/log4cplus.properties

