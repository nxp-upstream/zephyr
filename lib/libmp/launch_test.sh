#!/bin/bash

[ -d twister-out ] && rm -rf twister-out

#Automatically activate the environment
if [ -z "$VIRTUAL_ENV" ]; then
    echo "No Python virtual environment is currently activated."
    echo "Activating environment..."
    source ~/zephyrproject/.venv/bin/activate
else
    echo "Python virtual environment is already active: $VIRTUAL_ENV"
fi

#Launch libmp integration tests
west twister -T ./tests --disable-warnings-as-errors