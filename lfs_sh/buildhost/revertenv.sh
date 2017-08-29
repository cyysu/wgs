#!/bin/sh

# .bash_profile
cat > /root/.bash_profile <<\EOF
if [ -f ~/.bashrc ]; then
  source ~/.bashrc
fi
EOF

# .bashrc
cat > /root/.bashrc <<\EOF
if [ "$PS1" ]; then

  # Colored output for 'ls'
  eval `dircolors -b`
  alias ls='ls --color=auto'

  # Some more aliases
  alias ll='ls -al'
  alias ..='cd ..'
  alias ...='cd ../..'
  alias cp='cp -i'
  alias rm='rm -i'
  alias mv='mv -i'
  alias vi='vim'
  alias uman='GROFF_ENCODING=utf8 man'

  # Command prompt for root
  WHITE='\[\033[1;37m\]'
  RED='\[\033[0;33m\]'
  NC='\[\033[0m\]'
  PS1="$RED[$WHITE\u$NC@$WHITE\h$NC:$WHITE\W$RED] #$NC "

  # Vim is our preferred editor
  EDITOR=vim
  VISUAL=$EDITOR
  export EDITOR VISUAL

fi
EOF

echo
echo "############################"
echo "Now log out and log back in."
echo "############################"
echo
