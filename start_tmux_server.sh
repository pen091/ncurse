#!/bin/bash
# Usage: ./start_tmux_server.sh <port>
SESSION="chat-server-$1"
tmux new-session -d -s $SESSION
tmux send-keys -t $SESSION "./server $1" C-m
tmux attach -t $SESSION
