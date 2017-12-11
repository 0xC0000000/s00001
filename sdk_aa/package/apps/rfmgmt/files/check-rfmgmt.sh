#!/bin/sh

APP_NAME="/usr/bin/rfmgmt"

ps | grep "$APP_NAME" | grep -v grep > /dev/null
if [ $? -ne 0 ]; then
    echo "rfmgmt not exist, start it"
    /etc/init.d/rfmgmt start
else
    echo "rfmgmt exist"
fi