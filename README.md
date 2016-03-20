# A-Shell-Project-in-C

################
Computer Systerms Project : Shell Lab
################

Files in this Repository for the use:

Makefile	# Compiles this shell program and runs the tests
README		# This file
tsh.c		# The shell program contains the main function and that matters the most
tshref		# The reference shell binary.

# The remaining files are used to test your shell
sdriver.pl	# The trace-driven shell driver
trace*.txt	# The 15 trace files that control the shell driver
tshref.out 	# Example output of the reference shell on all 15 traces to check if your code gives the right output

# Little C programs that are called by the trace files
myspin.c	# Takes argument <n> and spins for <n> seconds
mysplit.c	# Forks a child that spins for <n> seconds
mystop.c        # Spins for <n> seconds and sends SIGTSTP to itself
myint.c         # Spins for <n> seconds and sends SIGINT to itself
