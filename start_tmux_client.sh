#!/bin/bash
# Usage: ./start_tmux_client.sh <server-ip> <port> <username>
SESSION="chat-client-$3"
tmux new-session -d -s $SESSION
tmux send-keys -t $SESSION "./client $1 $2 $3" C-m
tmux attach -t $SESSION
