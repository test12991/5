#!/usr/bin/env bash
# use testnet settings,  if you need mainnet,  use ~/.biblepaycore/biblepayd.pid file instead
export LC_ALL=C

biblepay_pid=$(<~/.biblepaycore/testnet3/biblepayd.pid)
sudo gdb -batch -ex "source debug.gdb" biblepayd ${biblepay_pid}
