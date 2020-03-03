#!/bin/bash

# include this in ~/.bashrc

# detect if ssh-agent is already running
# if not then start a new one
[ -f ~/.ssh-agent-socket ] && source ~/.ssh-agent-socket
ssh-add -l &>/dev/null
if [ $? = 2 ]; then
        echo "Starting SSH agent"
        killall ssh-agent 2>/dev/null
        rm -rf $SSH_AUTH_SOCK 2>/dev/null
        ssh-agent > ~/.ssh-agent-socket
        source ~/.ssh-agent-socket
fi

# print message about ssh-agent and list keys
ssh-add -l

# if not using ssh and list is empty then add aliases to automatically add keys when first running ssh or scp
if [ -z "$SSH_TTY" ]; then
  ssh-add -l > /dev/null
  if [ $? -ne 0 ] ; then
    alias ssh='ssh-add -l > /dev/null || ssh-add && unalias ssh scp ; ssh'
    alias scp='ssh-add -l > /dev/null || ssh-add && unalias scp ssh ; scp'
  fi
fi
